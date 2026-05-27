// GraphProjectSerializer converts Graph state to/from Finepaper project records.
#include "project/graphprojectserializer.h"

#include "connection/connectionruleservice.h"
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "ipcraft/schemaids.h"
#include "modules/moduleregistry.h"

#include <QHash>
#include <QJsonValue>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

namespace {

QJsonValue parameterToJson(const Parameter::Value& value) {
    if (const auto* stringValue = std::get_if<QString>(&value)) return *stringValue;
    if (const auto* intValue = std::get_if<int>(&value)) return *intValue;
    if (const auto* doubleValue = std::get_if<double>(&value)) return *doubleValue;
    if (const auto* boolValue = std::get_if<bool>(&value)) return *boolValue;
    return {};
}

Parameter::Value valueFromJson(const QJsonValue& value, const Parameter::Value& defaultValue) {
    if (std::holds_alternative<QString>(defaultValue)) return value.toString();
    if (std::holds_alternative<int>(defaultValue)) return value.toInt();
    if (std::holds_alternative<double>(defaultValue)) return value.toDouble();
    if (std::holds_alternative<bool>(defaultValue)) return value.toBool();
    return value.toString();
}

bool jsonValueMatchesParameterType(const QJsonValue& value, const Parameter::Value& defaultValue) {
    if (std::holds_alternative<QString>(defaultValue)) return value.isString();
    if (std::holds_alternative<int>(defaultValue)) {
        if (!value.isDouble()) return false;
        const double number = value.toDouble();
        return std::floor(number) == number;
    }
    if (std::holds_alternative<double>(defaultValue)) return value.isDouble();
    if (std::holds_alternative<bool>(defaultValue)) return value.isBool();
    return false;
}

std::unique_ptr<Module> instantiateModule(const ModuleType& type, const QString& moduleId) {
    auto module = std::make_unique<Module>(moduleId, type.name);
    // Recreate runtime modules from type defaults first, then overlay persisted
    // parameters in populateGraph so missing fields still receive current defaults.
    for (const Port& port : type.defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type.defaultParameters.constBegin(); it != type.defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    return module;
}

bool hasPort(const Module* module, const QString& portId) {
    if (!module) {
        return false;
    }
    return std::any_of(module->ports().begin(), module->ports().end(), [&](const Port& port) {
        return port.id() == portId;
    });
}

bool hasPort(const ModuleType& type, const QString& portId) {
    return std::any_of(type.defaultPorts.begin(), type.defaultPorts.end(), [&](const Port& port) {
        return port.id() == portId;
    });
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) {
        return nullptr;
    }
    const auto it = std::find_if(module->ports().begin(), module->ports().end(), [&](const Port& port) {
        return port.id() == portId;
    });
    return it != module->ports().end() ? &(*it) : nullptr;
}

const Port* findPortByInterface(const ModuleType& type, const QString& interfaceId) {
    auto it = std::find_if(type.defaultPorts.begin(), type.defaultPorts.end(), [&](const Port& port) {
        return port.interfaceId() == interfaceId;
    });
    if (it != type.defaultPorts.end()) {
        return &(*it);
    }

    it = std::find_if(type.defaultPorts.begin(), type.defaultPorts.end(), [&](const Port& port) {
        return port.id() == interfaceId;
    });
    return it != type.defaultPorts.end() ? &(*it) : nullptr;
}

QString interfaceIdForPort(const Port& port) {
    return port.interfaceId().isEmpty() ? port.id() : port.interfaceId();
}

ProjectConnectionInterfaceRef projectInterfaceRef(const Module* module, const PortRef& portRef) {
    const Port* port = findPort(module, portRef.portId);
    return ProjectConnectionInterfaceRef{
        module ? module->id() : portRef.moduleId,
        port ? interfaceIdForPort(*port) : portRef.portId
    };
}

ConnectionInterfaceRef graphInterfaceRef(const ProjectConnectionInterfaceRef& interfaceRef) {
    return ConnectionInterfaceRef{interfaceRef.instanceId, interfaceRef.interfaceId};
}

const ModuleInterfaceMetadata* interfaceMetadata(const Module* module, const PortRef& portRef) {
    if (!module) {
        return nullptr;
    }
    const ModuleType* type = ModuleRegistry::instance().getType(module->type());
    const Port* port = findPort(module, portRef.portId);
    if (!type || !port) {
        return nullptr;
    }
    const QString interfaceId = interfaceIdForPort(*port);
    const auto metadataIt = type->interfaceMetadata.find(interfaceId);
    return metadataIt != type->interfaceMetadata.end() ? &metadataIt.value() : nullptr;
}

QString acceptRoleForClass(const ModuleInterfaceMetadata* metadata,
                           const QString& connectionClassId) {
    if (!metadata) {
        return {};
    }
    for (const IpcraftInterfaceAcceptRule& rule : metadata->acceptRules) {
        if (rule.connectionClassId == connectionClassId) {
            return rule.role;
        }
    }
    return {};
}

bool acceptsClass(const ModuleInterfaceMetadata* metadata,
                  const QString& connectionClassId) {
    if (!metadata) {
        return false;
    }
    return std::any_of(metadata->acceptRules.cbegin(), metadata->acceptRules.cend(),
        [&](const IpcraftInterfaceAcceptRule& rule) {
            return rule.connectionClassId == connectionClassId;
        });
}

QString deriveConnectionClassId(const QString& explicitConnectionClassId,
                                const PortRef& source,
                                const PortRef& target,
                                const Module* sourceModule,
                                const Module* targetModule) {
    if (!explicitConnectionClassId.isEmpty()) {
        return explicitConnectionClassId;
    }

    const ModuleInterfaceMetadata* sourceMetadata = interfaceMetadata(sourceModule, source);
    const ModuleInterfaceMetadata* targetMetadata = interfaceMetadata(targetModule, target);
    if (sourceMetadata && targetMetadata) {
        for (const IpcraftInterfaceAcceptRule& rule : sourceMetadata->acceptRules) {
            if (acceptsClass(targetMetadata, rule.connectionClassId)) {
                return rule.connectionClassId;
            }
        }
        if (!sourceMetadata->bus.isEmpty() && sourceMetadata->bus == targetMetadata->bus) {
            return sourceMetadata->bus;
        }
    }

    const Port* sourcePort = findPort(sourceModule, source.portId);
    const Port* targetPort = findPort(targetModule, target.portId);
    if (sourcePort && targetPort &&
        !sourcePort->busType().isEmpty() &&
        sourcePort->busType() == targetPort->busType()) {
        return sourcePort->busType();
    }
    return {};
}

QString deriveConnectionClassId(const Connection& connection,
                                const Module* sourceModule,
                                const Module* targetModule) {
    return deriveConnectionClassId(connection.connectionClassId(),
                                   connection.source(),
                                   connection.target(),
                                   sourceModule,
                                   targetModule);
}

bool symmetricConnectionClass(const QString& connectionClassId,
                              const ModuleInterfaceMetadata* sourceMetadata,
                              const ModuleInterfaceMetadata* targetMetadata) {
    const QString sourceRole = acceptRoleForClass(sourceMetadata, connectionClassId);
    const QString targetRole = acceptRoleForClass(targetMetadata, connectionClassId);
    return !connectionClassId.isEmpty() &&
           !sourceRole.isEmpty() &&
           sourceRole == targetRole;
}

QVector<ProjectConnectionInterfaceRef> normalizedInterfaces(
    QVector<ProjectConnectionInterfaceRef> interfaces,
    bool symmetricClass) {
    if (!symmetricClass) {
        return interfaces;
    }
    std::sort(interfaces.begin(), interfaces.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.instanceId != rhs.instanceId) {
            return lhs.instanceId < rhs.instanceId;
        }
        return lhs.interfaceId < rhs.interfaceId;
    });
    return interfaces;
}

GraphProjectLoadResult failure(const QString& error) {
    return {false, error};
}

QString instanceScopeKey(const QString& ipcoreId, const QString& instanceId) {
    return ipcoreId + QLatin1Char('/') + instanceId;
}

std::optional<PortRef> resolveInterfaceParticipant(const ProjectConnectionRecord& record,
                                                   const ProjectConnectionInterfaceRef& participant,
                                                   const QSet<QString>& moduleIds,
                                                   const QHash<QString, const ModuleType*>& moduleTypesById,
                                                   QString* error) {
    if (!moduleIds.contains(participant.instanceId)) {
        if (error) {
            *error = QStringLiteral("Connection %1 references missing instance %2")
                         .arg(record.id, participant.instanceId);
        }
        return std::nullopt;
    }

    const ModuleType* type = moduleTypesById.value(participant.instanceId);
    const Port* port = type ? findPortByInterface(*type, participant.interfaceId) : nullptr;
    if (!port) {
        if (error) {
            *error = QStringLiteral("Connection %1 references missing interface %2 on instance %3")
                         .arg(record.id, participant.interfaceId, participant.instanceId);
        }
        return std::nullopt;
    }

    return PortRef{participant.instanceId, port->id()};
}

std::optional<std::pair<PortRef, PortRef>> connectionPortRefs(
    const ProjectConnectionRecord& record,
    const QSet<QString>& moduleIds,
    const QHash<QString, const ModuleType*>& moduleTypesById,
    QString* error) {
    if (!record.interfaces.isEmpty()) {
        if (record.interfaces.size() != 2) {
            if (error) {
                *error = QStringLiteral("Connection %1 requires exactly two interface participants")
                             .arg(record.id);
            }
            return std::nullopt;
        }

        const std::optional<PortRef> first =
            resolveInterfaceParticipant(record, record.interfaces.at(0), moduleIds, moduleTypesById, error);
        if (!first.has_value()) {
            return std::nullopt;
        }
        const std::optional<PortRef> second =
            resolveInterfaceParticipant(record, record.interfaces.at(1), moduleIds, moduleTypesById, error);
        if (!second.has_value()) {
            return std::nullopt;
        }
        return std::make_pair(*first, *second);
    }

    if (!moduleIds.contains(record.source.moduleId) || !moduleIds.contains(record.target.moduleId)) {
        if (error) {
            *error = QStringLiteral("Connection %1 references missing module").arg(record.id);
        }
        return std::nullopt;
    }

    const ModuleType* sourceType = moduleTypesById.value(record.source.moduleId);
    const ModuleType* targetType = moduleTypesById.value(record.target.moduleId);
    if (!sourceType || !targetType ||
        !hasPort(*sourceType, record.source.portId) ||
        !hasPort(*targetType, record.target.portId)) {
        if (error) {
            *error = QStringLiteral("Connection %1 references missing port").arg(record.id);
        }
        return std::nullopt;
    }

    return std::make_pair(PortRef{record.source.moduleId, record.source.portId},
                          PortRef{record.target.moduleId, record.target.portId});
}

GraphProjectLoadResult populateGraph(const ProjectDocument& document,
                                     const QHash<QString, const ModuleType*>& moduleTypesById,
                                     Graph& graph) {
    for (const ProjectModuleRecord& record : document.modules) {
        const ModuleType* type = moduleTypesById.value(record.id);

        auto module = instantiateModule(*type, record.id);
        module->setIpcoreId(record.ipcoreId);
        module->setInstanceId(record.instanceId);
        // Values were type-checked before this function; conversion can use the
        // module type default as the target variant shape.
        for (auto it = record.parameters.begin(); it != record.parameters.end(); ++it) {
            const auto defaultIt = type->defaultParameters.find(it.key());
            module->setParameter(it.key(), valueFromJson(it.value(), defaultIt.value().value()));
        }

        if (!graph.addModule(std::move(module))) {
            return failure(QStringLiteral("Could not add module: %1").arg(record.id));
        }
    }

    QSet<QString> moduleIds;
    for (auto it = moduleTypesById.constBegin(); it != moduleTypesById.constEnd(); ++it) {
        moduleIds.insert(it.key());
    }

    const ConnectionRuleService ruleService(&graph,
                                            document.ipcoreState,
                                            ModuleRegistry::instance().packageManifests());
    for (const ProjectConnectionRecord& record : document.connections) {
        QString connectionError;
        const std::optional<std::pair<PortRef, PortRef>> refs =
            connectionPortRefs(record, moduleIds, moduleTypesById, &connectionError);
        if (!refs.has_value()) {
            return failure(connectionError);
        }

        PortRef source = refs->first;
        PortRef target = refs->second;
        const auto checkConnection = [&](const PortRef& candidateSource,
                                         const PortRef& candidateTarget) {
            ConnectionRequest request =
                ConnectionRequest::portToPort(candidateSource,
                                              candidateTarget,
                                              ConnectionRequestKind::ProjectLoad);
            request.connectionClassId = record.connectionClassId;
            return ruleService.check(request);
        };

        // Validate against the concrete Graph again because module defaults and
        // interface metadata may reject edges that pass basic project shape checks.
        const Module* sourceModule = graph.getModule(source.moduleId);
        const Module* targetModule = graph.getModule(target.moduleId);
        if (!sourceModule || !targetModule) {
            return failure(QStringLiteral("Connection %1 references missing module").arg(record.id));
        }
        if (!hasPort(sourceModule, source.portId) || !hasPort(targetModule, target.portId)) {
            return failure(QStringLiteral("Connection %1 references missing port").arg(record.id));
        }

        ConnectionCheckResult check = checkConnection(source, target);
        if (!check.hasSingleOption() && !record.interfaces.isEmpty()) {
            const ConnectionCheckResult reverseCheck = checkConnection(target, source);
            if (reverseCheck.hasSingleOption()) {
                std::swap(source, target);
                std::swap(sourceModule, targetModule);
                check = reverseCheck;
            }
        }
        if (!check.hasSingleOption()) {
            return failure(QStringLiteral("Invalid connection %1: %2")
                               .arg(record.id,
                                    check.reasonCode.isEmpty() ? check.message : check.reasonCode));
        }

        QVector<ProjectConnectionInterfaceRef> projectInterfaces = record.interfaces;
        if (projectInterfaces.isEmpty()) {
            projectInterfaces.push_back(projectInterfaceRef(sourceModule, source));
            projectInterfaces.push_back(projectInterfaceRef(targetModule, target));
        }

        const QString connectionClassId = record.interfaces.isEmpty()
            ? deriveConnectionClassId(record.connectionClassId, source, target, sourceModule, targetModule)
            : record.connectionClassId;
        const bool symmetricClass =
            symmetricConnectionClass(connectionClassId,
                                     interfaceMetadata(sourceModule, source),
                                     interfaceMetadata(targetModule, target));
        QVector<ConnectionInterfaceRef> graphInterfaces;
        for (const ProjectConnectionInterfaceRef& interfaceRef :
             normalizedInterfaces(std::move(projectInterfaces), symmetricClass)) {
            graphInterfaces.push_back(graphInterfaceRef(interfaceRef));
        }

        graph.addConnection(std::make_unique<Connection>(record.id,
                                                         source,
                                                         target,
                                                         connectionClassId,
                                                         graphInterfaces,
                                                         record.status,
                                                         record.alternatives));
    }

    return {true, {}};
}

} // namespace

ProjectDocument GraphProjectSerializer::toProject(const Graph& graph, const QString& projectName) {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    const QString resolvedName = projectName.isEmpty() ? QStringLiteral("Untitled") : projectName;
    document.projectName = resolvedName;
    document.name = resolvedName;

    QSet<QString> ipcoreIds;
    for (const auto& module : graph.modules()) {
        const ModuleType* type = ModuleRegistry::instance().getType(module->type());
        const QString ipcoreId = module->ipcoreId().isEmpty()
            ? (type ? type->ipcoreId : QString())
            : module->ipcoreId();
        if (!ipcoreId.isEmpty()) {
            ipcoreIds.insert(ipcoreId);
        }

        ProjectModuleRecord record;
        // Project records keep stable editor module IDs. External/generated IDs
        // are parameters and may differ per IP core.
        record.id = module->id();
        record.ipcoreId = ipcoreId;
        record.instanceId = module->instanceId();
        record.type = module->type();
        for (auto it = module->parameters().constBegin(); it != module->parameters().constEnd(); ++it) {
            record.parameters.insert(it.key(), parameterToJson(it.value().value()));
        }
        document.modules.push_back(record);
    }

    QStringList sortedIpcoreIds(ipcoreIds.begin(), ipcoreIds.end());
    sortedIpcoreIds.sort();
    for (const QString& ipcoreId : sortedIpcoreIds) {
        document.ipcores.push_back(ProjectIpcoreRecord{ipcoreId, QStringLiteral("1.0")});
    }

    for (const auto& connection : graph.connections()) {
        ProjectConnectionRecord record;
        record.id = connection->id();
        record.source = ProjectConnectionEndpoint{connection->source().moduleId,
                                                  connection->source().portId};
        record.target = ProjectConnectionEndpoint{connection->target().moduleId,
                                                  connection->target().portId};
        const Module* sourceModule = graph.getModule(connection->source().moduleId);
        const Module* targetModule = graph.getModule(connection->target().moduleId);
        record.connectionClassId = deriveConnectionClassId(*connection, sourceModule, targetModule);
        record.status = connection->status().isEmpty() ? QStringLiteral("valid") : connection->status();
        record.alternatives = connection->alternatives();
        for (const ConnectionInterfaceRef& interfaceRef : connection->interfaces()) {
            record.interfaces.push_back(ProjectConnectionInterfaceRef{
                interfaceRef.instanceId,
                interfaceRef.interfaceId
            });
        }
        if (record.interfaces.isEmpty()) {
            record.interfaces.push_back(projectInterfaceRef(sourceModule, connection->source()));
            record.interfaces.push_back(projectInterfaceRef(targetModule, connection->target()));
        }
        const bool symmetricClass =
            symmetricConnectionClass(record.connectionClassId,
                                     interfaceMetadata(sourceModule, connection->source()),
                                     interfaceMetadata(targetModule, connection->target()));
        record.interfaces = normalizedInterfaces(std::move(record.interfaces), symmetricClass);
        document.connections.push_back(record);
    }

    return document;
}

GraphProjectLoadResult GraphProjectSerializer::loadProject(const ProjectDocument& document, Graph& graph) {
    QSet<QString> moduleIds;
    QHash<QString, const ModuleType*> moduleTypesById;
    QSet<QString> validInstanceScopes;

    for (const ProjectIpInstanceRecord& state : document.ipcoreState) {
        if (state.ipcoreId.trimmed().isEmpty()) {
            return failure(QStringLiteral("Project ipcore_state entry is missing ipcore"));
        }
        if (state.instanceId.trimmed().isEmpty()) {
            return failure(QStringLiteral("Project ipcore_state entry is missing instance"));
        }

        const QString scopeKey = instanceScopeKey(state.ipcoreId, state.instanceId);
        if (validInstanceScopes.contains(scopeKey)) {
            return failure(QStringLiteral("Duplicate ipcore_state scope for ipcore %1 instance %2")
                               .arg(state.ipcoreId, state.instanceId));
        }
        validInstanceScopes.insert(scopeKey);
    }

    // First pass validates identifiers, IP core ownership, and parameter types
    // without mutating the live graph.
    for (const ProjectModuleRecord& record : document.modules) {
        if (record.id.isEmpty()) {
            return failure(QStringLiteral("Project module is missing id"));
        }
        if (moduleIds.contains(record.id)) {
            return failure(QStringLiteral("Duplicate module id: %1").arg(record.id));
        }
        moduleIds.insert(record.id);

        if (record.ipcoreId.isEmpty()) {
            return failure(QStringLiteral("Module %1 is missing ipcore").arg(record.id));
        }
        if (record.instanceId.trimmed().isEmpty()) {
            return failure(QStringLiteral("Module %1 is missing instance").arg(record.id));
        }
        if (record.type.isEmpty()) {
            return failure(QStringLiteral("Module %1 is missing type").arg(record.id));
        }
        if (!validInstanceScopes.contains(instanceScopeKey(record.ipcoreId, record.instanceId))) {
            return failure(QStringLiteral("Module %1 references missing instance %2 for ipcore %3")
                               .arg(record.id, record.instanceId, record.ipcoreId));
        }

        const ModuleType* type = ModuleRegistry::instance().getType(record.ipcoreId, record.type);
        if (!type) {
            // Missing type means the required IP core bundle was not loaded at
            // startup, so continuing would lose graph semantics.
            return failure(QStringLiteral("Missing module type: %1").arg(record.type));
        }
        if (type->ipcoreId != record.ipcoreId) {
            // Type names are currently globally unique, but the project still
            // records IP core ownership to catch accidental cross-IP reuse.
            return failure(QStringLiteral("Module %1 requires ipcore %2").arg(record.id, record.ipcoreId));
        }
        moduleTypesById.insert(record.id, type);

        for (auto it = record.parameters.begin(); it != record.parameters.end(); ++it) {
            const auto defaultIt = type->defaultParameters.find(it.key());
            if (defaultIt == type->defaultParameters.end()) {
                // Unknown parameters usually indicate a stale project/schema
                // mismatch; reject instead of silently dropping data.
                return failure(QStringLiteral("Unknown parameter %1 on module %2")
                                   .arg(it.key(), record.id));
            }
            if (!jsonValueMatchesParameterType(it.value(), defaultIt.value().value())) {
                // Preserve typed Parameter variants so property panels and
                // generators receive the expected value shape after load.
                return failure(QStringLiteral("Invalid type for parameter %1 on module %2")
                                   .arg(it.key(), record.id));
            }
        }
    }

    QSet<QString> connectionIds;
    for (const ProjectConnectionRecord& record : document.connections) {
        // Connection shape can be checked against module type defaults before
        // instantiating a candidate Graph.
        if (record.id.trimmed().isEmpty()) {
            return failure(QStringLiteral("Connection is missing id"));
        }
        if (connectionIds.contains(record.id)) {
            return failure(QStringLiteral("Duplicate connection id: %1").arg(record.id));
        }
        connectionIds.insert(record.id);

        if (!record.interfaces.isEmpty() && record.connectionClassId.trimmed().isEmpty()) {
            return failure(QStringLiteral("Connection %1 is missing class").arg(record.id));
        }

        QString connectionError;
        if (!connectionPortRefs(record, moduleIds, moduleTypesById, &connectionError).has_value()) {
            return failure(connectionError);
        }
    }

    Graph candidate;
    // Candidate load catches semantic Graph validation failures while keeping
    // the user's current design intact on error.
    GraphProjectLoadResult validationResult =
        populateGraph(document, moduleTypesById, candidate);
    if (!validationResult.success) {
        return validationResult;
    }

    // Only after all validation succeeds do we replace the live graph.
    graph.clear();
    return populateGraph(document, moduleTypesById, graph);
}
