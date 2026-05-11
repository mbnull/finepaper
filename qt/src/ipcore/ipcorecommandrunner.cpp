// IpCoreCommandRunner resolves selected IP-core command descriptors.
#include "ipcore/ipcorecommandrunner.h"

QStringList IpCoreCommandDescriptor::arguments(const QString& inputPath,
                                               const QString& outputDirectory) const {
    QStringList resolved;
    resolved.reserve(args.size());
    for (QString arg : args) {
        arg.replace(QStringLiteral("{input}"), inputPath);
        arg.replace(QStringLiteral("{output}"), outputDirectory);
        resolved.append(arg);
    }
    return resolved;
}

namespace {

IpCoreResolvedCommand failure(const QString& message) {
    IpCoreResolvedCommand command;
    command.errorMessage = message;
    return command;
}

IpCoreResolvedCommand resolveIpcoreCommand(const IpCatalogEntry& entry,
                                           const QString& inputPath,
                                           const QString& outputDirectory,
                                           const QString& missingCommandMessage,
                                           const IpCoreCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.id.trimmed().isEmpty()) {
        return failure(QStringLiteral("Select an IP core instance before running this action."));
    }

    const IpCoreCommandDescriptor& descriptor = entry.*commandMember;
    if (!descriptor.hasCommand()) {
        return failure(missingCommandMessage.arg(entry.id));
    }

    IpCoreResolvedCommand command;
    command.valid = true;
    command.ipcoreId = entry.id;
    command.workingDirectory = entry.sourceRootPath;
    command.command = descriptor.command;
    command.inputFormat = descriptor.inputFormat;
    command.arguments = descriptor.arguments(inputPath, outputDirectory);
    return command;
}

} // namespace

IpCoreResolvedCommand IpCoreCommandRunner::resolveGenerator(const IpCatalogEntry& entry,
                                                            const QString& inputPath,
                                                            const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("IP core '%1' does not declare a generator."),
                                &IpCatalogEntry::generator);
}

IpCoreResolvedCommand IpCoreCommandRunner::resolveDrc(const IpCatalogEntry& entry,
                                                      const QString& inputPath,
                                                      const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("IP core '%1' does not declare a DRC command."),
                                &IpCatalogEntry::drc);
}
