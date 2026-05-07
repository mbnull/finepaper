// GraphProjectSerializer converts Graph state to/from Finepaper project records.
#include "project/graphprojectserializer.h"

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

QJsonObject parametersToJson(const QHash<QString, Parameter>& source) {
    QJsonObject parameters;
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        parameters.insert(it.key(), parameterToJson(it.value().value()));
    }
    return parameters;
}

Parameter::Value valueFromJson(const QJsonValue& value, const Parameter::Value& defaultValue) {
    if (std::holds_alternative<QString>(defaultValue)) return value.toString();
    if (std::holds_alternative<int>(defaultValue)) return value.toInt();
    if (std::holds_alternative<double>(defaultValue)) return value.toDouble();
    if (std::holds_alternative<bool>(defaultValue)) return value.toBool();
    return value.toString();
}

Parameter::Value instanceValueFromJson(const QJsonValue& value) {
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::floor(number) == number) {
            return value.toInt();
        }
        return number;
    }
    return QString();
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
    if (!document.ipInstances.empty()) {
        // The project schema supports a list for future extension, but the
        // current editor accepts one active generated IP instance.
        const ProjectIpInstanceRecord& record = document.ipInstances.first();
        QHash<QString, Parameter> parameters;
        for (auto it = record.parameters.begin(); it != record.parameters.end(); ++it) {
            parameters.insert(it.key(), Parameter(it.key(), instanceValueFromJson(it.value())));
        }
        graph.configureIpInstance(record.id, record.pluginId, record.kind, record.type, parameters);
    }

    for (const ProjectModuleRecord& record : document.modules) {
        const ModuleType* type = moduleTypesById.value(record.id);

        auto module = instantiateModule(*type, record.id);
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
        if (!graph.isValidConnection(source, target)) {
            return failure(QStringLiteral("Invalid connection: %1").arg(record.id));
        }
        graph.addConnection(std::make_unique<Connection>(record.id, source, target));
    }

    return {true, {}};
}

} // namespace

ProjectDocument GraphProjectSerializer::toProject(const Graph& graph, const QString& projectName) {
    ProjectDocument document;
    document.name = projectName.isEmpty() ? QStringLiteral("Untitled") : projectName;

    QSet<QString> pluginIds;
    if (graph.ipInstance().has_value()) {
        // Persist IP instance metadata separately from graph modules so plugin
        // generators can receive package-level parameters on reload.
        const GraphIpInstance& ipInstance = *graph.ipInstance();
        document.ipInstances.push_back(ProjectIpInstanceRecord{
            ipInstance.id,
            ipInstance.pluginId,
            ipInstance.kind,
            ipInstance.type,
            parametersToJson(ipInstance.parameters)
        });
        if (!ipInstance.pluginId.isEmpty()) {
            pluginIds.insert(ipInstance.pluginId);
        }
    }

    for (const auto& module : graph.modules()) {
        const ModuleType* type = ModuleRegistry::instance().getType(module->type());
        const QString pluginId = type ? type->pluginId : QString();
        if (!pluginId.isEmpty()) {
            pluginIds.insert(pluginId);
        }

        ProjectModuleRecord record;
        // Project records keep stable editor module IDs. External/generated IDs
        // are parameters and may differ per plugin.
        record.id = module->id();
        record.pluginId = pluginId;
        record.type = module->type();
        for (auto it = module->parameters().constBegin(); it != module->parameters().constEnd(); ++it) {
            record.parameters.insert(it.key(), parameterToJson(it.value().value()));
        }
        document.modules.push_back(record);
    }

    QStringList sortedPluginIds(pluginIds.begin(), pluginIds.end());
    sortedPluginIds.sort();
    for (const QString& pluginId : sortedPluginIds) {
        document.plugins.push_back(ProjectPluginRecord{pluginId, QStringLiteral("1.0")});
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
    if (document.ipInstances.size() > 1) {
        return failure(QStringLiteral("Project may contain at most one IP instance"));
    }

    // Validate package-level instance records before module records because the
    // active IP instance drives plugin-level generation behavior.
    int nocInstanceCount = 0;
    QSet<QString> ipInstanceIds;
    for (const ProjectIpInstanceRecord& record : document.ipInstances) {
        if (record.id.isEmpty()) {
            return failure(QStringLiteral("Project IP instance is missing id"));
        }
        if (ipInstanceIds.contains(record.id)) {
            // Duplicate instance IDs would make generated artifact ownership
            // ambiguous, even though only one instance is supported today.
            return failure(QStringLiteral("Duplicate IP instance id: %1").arg(record.id));
        }
        ipInstanceIds.insert(record.id);

        if (record.kind == QStringLiteral("noc")) {
            ++nocInstanceCount;
        }
    }
    if (nocInstanceCount > 1) {
        // Reserve multi-NoC projects for a later orchestration design.
        return failure(QStringLiteral("Project may contain at most one IP instance with kind: noc"));
    }

    QSet<QString> moduleIds;
    QHash<QString, const ModuleType*> moduleTypesById;

    // First pass validates identifiers, plugin ownership, and parameter types
    // without mutating the live graph.
    for (const ProjectModuleRecord& record : document.modules) {
        if (record.id.isEmpty()) {
            return failure(QStringLiteral("Project module is missing id"));
        }
        if (moduleIds.contains(record.id)) {
            return failure(QStringLiteral("Duplicate module id: %1").arg(record.id));
        }
        moduleIds.insert(record.id);

        if (record.pluginId.isEmpty()) {
            return failure(QStringLiteral("Module %1 is missing plugin").arg(record.id));
        }
        if (record.type.isEmpty()) {
            return failure(QStringLiteral("Module %1 is missing type").arg(record.id));
        }

        const ModuleType* type = ModuleRegistry::instance().getType(record.type);
        if (!type) {
            // Missing type means the required plugin bundle was not loaded at
            // startup, so continuing would lose graph semantics.
            return failure(QStringLiteral("Missing module type: %1").arg(record.type));
        }
        if (type->pluginId != record.pluginId) {
            // Type names are currently globally unique, but the project still
            // records plugin ownership to catch accidental cross-plugin reuse.
            return failure(QStringLiteral("Module %1 requires plugin %2").arg(record.id, record.pluginId));
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
