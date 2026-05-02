// GeneratorRunner maps graph module ownership to a plugin generator command.
#include "plugins/generatorrunner.h"

#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "plugins/pluginregistry.h"

#include <QSet>

namespace {

GeneratorCommand failure(const QString& message) {
    GeneratorCommand command;
    command.errorMessage = message;
    return command;
}

const PluginDescriptor* findPlugin(const QList<PluginDescriptor>& plugins, const QString& pluginId) {
    for (const PluginDescriptor& plugin : plugins) {
        if (plugin.id == pluginId) {
            return &plugin;
        }
    }
    return nullptr;
}

} // namespace

GeneratorCommand GeneratorRunner::resolveForGraph(const Graph* graph,
                                                  const QString& inputPath,
                                                  const QString& outputDirectory) {
    return resolveForGraph(graph,
                           PluginRegistry::instance().plugins(),
                           inputPath,
                           outputDirectory);
}

GeneratorCommand GeneratorRunner::resolveForGraph(const Graph* graph,
                                                  const QList<PluginDescriptor>& plugins,
                                                  const QString& inputPath,
                                                  const QString& outputDirectory) {
    if (!graph || graph->modules().empty()) {
        return failure(QStringLiteral("No modules in graph. Add a plugin-owned IP before generating."));
    }

    QSet<QString> pluginIds;
    for (const auto& module : graph->modules()) {
        const ModuleType* type = ModuleRegistry::instance().getType(module->type());
        if (!type || type->pluginId.isEmpty()) {
            return failure(QStringLiteral("Module type '%1' is not owned by a loaded plugin.")
                               .arg(module->type()));
        }
        pluginIds.insert(type->pluginId);
    }

    if (pluginIds.size() > 1) {
        return failure(QStringLiteral("Multi-plugin generation is not enabled yet."));
    }

    const QString pluginId = *pluginIds.constBegin();
    const PluginDescriptor* plugin = findPlugin(plugins, pluginId);
    if (!plugin) {
        return failure(QStringLiteral("Plugin '%1' is not loaded.").arg(pluginId));
    }
    if (!plugin->generator.hasGenerator()) {
        return failure(QStringLiteral("Plugin '%1' does not declare a generator.").arg(pluginId));
    }

    GeneratorCommand command;
    command.valid = true;
    command.pluginId = plugin->id;
    command.workingDirectory = plugin->rootPath;
    command.command = plugin->generator.command;
    command.inputFormat = plugin->generator.inputFormat;
    command.arguments = plugin->generator.arguments(inputPath, outputDirectory);
    return command;
}
