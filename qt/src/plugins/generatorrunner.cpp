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

GeneratorCommand resolvePluginCommandForGraph(const Graph* graph,
                                              const QList<PluginDescriptor>& plugins,
                                              const QString& inputPath,
                                              const QString& outputDirectory,
                                              const QString& emptyGraphMessage,
                                              const QString& missingCommandMessage,
                                              const PluginCommandDescriptor PluginDescriptor::* commandMember) {
    if (!graph || graph->modules().empty()) {
        return failure(emptyGraphMessage);
    }

    // Every module must be owned by the same loaded plugin because command
    // invocation currently has one working directory and one input schema.
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

    const PluginCommandDescriptor& descriptor = plugin->*commandMember;
    if (!descriptor.hasCommand()) {
        return failure(missingCommandMessage.arg(pluginId));
    }

    // Argument templating is manifest-owned. The caller only supplies the
    // already-written input path and selected output directory.
    GeneratorCommand command;
    command.valid = true;
    command.pluginId = plugin->id;
    command.workingDirectory = plugin->rootPath;
    command.command = descriptor.command;
    command.inputFormat = descriptor.inputFormat;
    command.arguments = descriptor.arguments(inputPath, outputDirectory);
    return command;
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
    return resolvePluginCommandForGraph(graph,
                                        plugins,
                                        inputPath,
                                        outputDirectory,
                                        QStringLiteral("No modules in graph. Add a plugin-owned IP before generating."),
                                        QStringLiteral("Plugin '%1' does not declare a generator."),
                                        &PluginDescriptor::generator);
}

GeneratorCommand GeneratorRunner::resolveDrcForGraph(const Graph* graph,
                                                     const QString& inputPath,
                                                     const QString& outputDirectory) {
    return resolveDrcForGraph(graph,
                              PluginRegistry::instance().plugins(),
                              inputPath,
                              outputDirectory);
}

GeneratorCommand GeneratorRunner::resolveDrcForGraph(const Graph* graph,
                                                     const QList<PluginDescriptor>& plugins,
                                                     const QString& inputPath,
                                                     const QString& outputDirectory) {
    return resolvePluginCommandForGraph(graph,
                                        plugins,
                                        inputPath,
                                        outputDirectory,
                                        QStringLiteral("No modules in graph. Add a plugin-owned IP before running DRC."),
                                        QStringLiteral("Plugin '%1' does not declare a DRC command."),
                                        &PluginDescriptor::drc);
}
