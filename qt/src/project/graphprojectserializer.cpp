// GraphProjectSerializer converts Graph state to/from Finepaper project records.
#include "project/graphprojectserializer.h"

#include "connection/connectionruleservice.h"
#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduleregistry.h"

#include <QHash>
#include <QJsonValue>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <memory>

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
    if (!module) return false;
    return std::any_of(module->ports().begin(), module->ports().end(), [&](const Port& port) {
        return port.id() == portId;
    });
}

bool hasPort(const ModuleType& type, const QString& portId) {
    return std::any_of(type.defaultPorts.begin(), type.defaultPorts.end(), [&](const Port& port) {
        return port.id() == portId;
    });
}

GraphProjectLoadResult failure(const QString& error) {
    return {false, error};
}

GraphProjectLoadResult populateGraph(const ProjectDocument& document,
                                     const QHash<QString, const ModuleType*>& moduleTypesById,
                                     Graph& graph) {
    for (const ProjectModuleRecord& record : document.modules) {
        const ModuleType* type = moduleTypesById.value(record.id);

        auto module = instantiateModule(*type, record.id);
        module->setIpcoreId(record.ipcoreId);
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

    const ConnectionRuleService ruleService(&graph, document.ipcoreState);
    for (const ProjectConnectionRecord& record : document.connections) {
        // Validate against the concrete Graph again because module defaults and
        // interface metadata may reject edges that pass basic project shape checks.
        const Module* sourceModule = graph.getModule(record.source.moduleId);
        const Module* targetModule = graph.getModule(record.target.moduleId);
        if (!sourceModule || !targetModule) {
            return failure(QStringLiteral("Connection %1 references missing module").arg(record.id));
        }
        if (!hasPort(sourceModule, record.source.portId) || !hasPort(targetModule, record.target.portId)) {
            return failure(QStringLiteral("Connection %1 references missing port").arg(record.id));
        }

        const PortRef source{record.source.moduleId, record.source.portId};
        const PortRef target{record.target.moduleId, record.target.portId};
        const ConnectionCheckResult check = ruleService.check(
            ConnectionRequest::portToPort(source, target, ConnectionRequestKind::ProjectLoad));
        if (!check.hasSingleOption()) {
            return failure(QStringLiteral("Invalid connection %1: %2")
                               .arg(record.id,
                                    check.reasonCode.isEmpty() ? check.message : check.reasonCode));
        }
        graph.addConnection(std::make_unique<Connection>(record.id, source, target));
    }

    return {true, {}};
}

} // namespace

ProjectDocument GraphProjectSerializer::toProject(const Graph& graph, const QString& projectName) {
    ProjectDocument document;
    document.name = projectName.isEmpty() ? QStringLiteral("Untitled") : projectName;

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
        document.connections.push_back(record);
    }

    return document;
}

GraphProjectLoadResult GraphProjectSerializer::loadProject(const ProjectDocument& document, Graph& graph) {
    QSet<QString> moduleIds;
    QHash<QString, const ModuleType*> moduleTypesById;

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
        if (record.type.isEmpty()) {
            return failure(QStringLiteral("Module %1 is missing type").arg(record.id));
        }

        const ModuleType* type = ModuleRegistry::instance().getType(record.type);
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

    for (const ProjectConnectionRecord& record : document.connections) {
        // Connection shape can be checked against module type defaults before
        // instantiating a candidate Graph.
        if (!moduleIds.contains(record.source.moduleId) || !moduleIds.contains(record.target.moduleId)) {
            return failure(QStringLiteral("Connection %1 references missing module").arg(record.id));
        }

        const ModuleType* sourceType = moduleTypesById.value(record.source.moduleId);
        const ModuleType* targetType = moduleTypesById.value(record.target.moduleId);
        if (!sourceType || !targetType ||
            !hasPort(*sourceType, record.source.portId) ||
            !hasPort(*targetType, record.target.portId)) {
            return failure(QStringLiteral("Connection %1 references missing port").arg(record.id));
        }
    }

    Graph candidate;
    // Candidate load catches semantic Graph validation failures while keeping
    // the user's current design intact on error.
    const GraphProjectLoadResult validationResult =
        populateGraph(document, moduleTypesById, candidate);
    if (!validationResult.success) {
        return validationResult;
    }

    // Only after all validation succeeds do we replace the live graph.
    graph.clear();
    return populateGraph(document, moduleTypesById, graph);
}
