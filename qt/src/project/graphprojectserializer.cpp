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

std::unique_ptr<Module> instantiateModule(const ModuleType& type, const QString& moduleId) {
    auto module = std::make_unique<Module>(moduleId, type.name);
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

GraphProjectLoadResult failure(const QString& error) {
    return {false, error};
}

} // namespace

ProjectDocument GraphProjectSerializer::toProject(const Graph& graph, const QString& projectName) {
    ProjectDocument document;
    document.name = projectName.isEmpty() ? QStringLiteral("Untitled") : projectName;

    QSet<QString> pluginIds;
    for (const auto& module : graph.modules()) {
        const ModuleType* type = ModuleRegistry::instance().getType(module->type());
        const QString pluginId = type ? type->pluginId : QString();
        if (!pluginId.isEmpty()) {
            pluginIds.insert(pluginId);
        }

        ProjectModuleRecord record;
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
    while (!graph.modules().empty()) {
        graph.removeModule(graph.modules().front()->id());
    }

    for (const ProjectModuleRecord& record : document.modules) {
        const ModuleType* type = ModuleRegistry::instance().getType(record.type);
        if (!type) {
            return failure(QStringLiteral("Missing module type: %1").arg(record.type));
        }
        if (type->pluginId != record.pluginId) {
            return failure(QStringLiteral("Module %1 requires plugin %2").arg(record.id, record.pluginId));
        }

        auto module = instantiateModule(*type, record.id);
        for (auto it = record.parameters.begin(); it != record.parameters.end(); ++it) {
            const auto defaultIt = type->defaultParameters.find(it.key());
            if (defaultIt != type->defaultParameters.end()) {
                module->setParameter(it.key(), valueFromJson(it.value(), defaultIt.value().value()));
            } else {
                module->setParameter(it.key(), it.value().toVariant().toString());
            }
        }

        if (!graph.addModule(std::move(module))) {
            return failure(QStringLiteral("Could not add module: %1").arg(record.id));
        }
    }

    for (const ProjectConnectionRecord& record : document.connections) {
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
