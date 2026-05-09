// GeneratorRunner resolves selected IP-core command descriptors.
#include "plugins/generatorrunner.h"

namespace {

GeneratorCommand failure(const QString& message) {
    GeneratorCommand command;
    command.errorMessage = message;
    return command;
}

GeneratorCommand resolveIpcoreCommand(const IpCatalogEntry& entry,
                                      const QString& inputPath,
                                      const QString& outputDirectory,
                                      const QString& missingCommandMessage,
                                      const PluginCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.id.trimmed().isEmpty()) {
        return failure(QStringLiteral("Select an IP core instance before running this action."));
    }

    const PluginCommandDescriptor& descriptor = entry.*commandMember;
    if (!descriptor.hasCommand()) {
        return failure(missingCommandMessage.arg(entry.id));
    }

    GeneratorCommand command;
    command.valid = true;
    command.ipcoreId = entry.id;
    command.workingDirectory = entry.sourceRootPath;
    command.command = descriptor.command;
    command.inputFormat = descriptor.inputFormat;
    command.arguments = descriptor.arguments(inputPath, outputDirectory);
    return command;
}

} // namespace

GeneratorCommand GeneratorRunner::resolveForIpcore(const IpCatalogEntry& entry,
                                                   const QString& inputPath,
                                                   const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("IP core '%1' does not declare a generator."),
                                &IpCatalogEntry::generator);
}

GeneratorCommand GeneratorRunner::resolveDrcForIpcore(const IpCatalogEntry& entry,
                                                      const QString& inputPath,
                                                      const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("IP core '%1' does not declare a DRC command."),
                                &IpCatalogEntry::drc);
}
