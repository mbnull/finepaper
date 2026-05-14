// IpCoreCommandRunner resolves selected IP-core command descriptors.
#include "ipcore/ipcorecommandrunner.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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

IpCoreResolvedCommand failure(const QString& message,
                              const QString& inputFormat = QString(),
                              const QString& inputSchema = QString()) {
    IpCoreResolvedCommand command;
    command.errorMessage = message;
    if (!inputFormat.isEmpty()) {
        command.inputFormat = inputFormat;
    }
    command.inputSchema = inputSchema;
    return command;
}

QString ipcraftNocProjectSchema() {
    return QStringLiteral("ipcraft.noc.project.v1");
}

QString legacyIpcoreGraphSchema() {
    return QStringLiteral("finepaper-ipcore-graph-v1");
}

IpCoreCommandDescriptor manifestCommandDescriptor(const IpcraftCommandDescriptor& command) {
    IpCoreCommandDescriptor descriptor;
    descriptor.command = command.resolvedExecutablePath.trimmed().isEmpty()
        ? command.executablePath
        : command.resolvedExecutablePath;
    descriptor.inputFormat = command.inputSchema;
    descriptor.args = command.args;
    return descriptor;
}

IpCoreCommandDescriptor descriptorForCommand(
    const IpCatalogEntry& entry,
    const QString& manifestCommandName,
    const IpCoreCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.packageManifest.commands.contains(manifestCommandName)) {
        return manifestCommandDescriptor(entry.packageManifest.commands.value(manifestCommandName));
    }

    return entry.*commandMember;
}

QString commandInputSchema(const IpCoreCommandDescriptor& descriptor) {
    if (descriptor.usesIpcoreGraphInput()) {
        return legacyIpcoreGraphSchema();
    }
    if (descriptor.usesIpcraftNocProjectInput()) {
        return ipcraftNocProjectSchema();
    }
    return {};
}

bool supportsCommandInputFormat(const IpCatalogEntry& entry,
                                const IpCoreCommandDescriptor& descriptor,
                                const QString& inputSchema) {
    if (descriptor.usesIpcoreGraphInput()) {
        return true;
    }

    return descriptor.usesIpcraftNocProjectInput() &&
           !entry.packageManifest.id.trimmed().isEmpty() &&
           inputSchema == ipcraftNocProjectSchema();
}

struct ExportedSchemaRead {
    bool checked = false;
    QString schema;
    QString error;
};

ExportedSchemaRead exportedSchemaFromInput(const QString& inputPath) {
    ExportedSchemaRead result;
    QFile file(inputPath);
    if (!file.exists()) {
        return result;
    }
    result.checked = true;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Could not read command input JSON '%1': %2")
            .arg(inputPath, file.errorString());
        return result;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        result.error = QStringLiteral("Command input JSON '%1' must contain an object.")
            .arg(inputPath);
        return result;
    }

    result.schema = document.object().value(QStringLiteral("schema")).toString();
    return result;
}

IpCoreResolvedCommand resolveIpcoreCommand(const IpCatalogEntry& entry,
                                           const QString& inputPath,
                                           const QString& outputDirectory,
                                           const QString& commandLabel,
                                           const QString& manifestCommandName,
                                           const QString& missingCommandMessage,
                                           const IpCoreCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.id.trimmed().isEmpty()) {
        return failure(QStringLiteral("Select an IP core instance before running this action."));
    }

    const IpCoreCommandDescriptor descriptor =
        descriptorForCommand(entry, manifestCommandName, commandMember);
    if (!descriptor.hasCommand()) {
        return failure(missingCommandMessage.arg(entry.id));
    }

    const QString inputSchema = commandInputSchema(descriptor);
    if (!supportsCommandInputFormat(entry, descriptor, inputSchema)) {
        return failure(QStringLiteral("IP core '%1' declares unsupported %2 input_format '%3'.")
                           .arg(entry.id, commandLabel, descriptor.inputFormat),
                       descriptor.inputFormat,
                       inputSchema);
    }

    const ExportedSchemaRead exportedSchema = exportedSchemaFromInput(inputPath);
    if (!exportedSchema.error.isEmpty()) {
        return failure(exportedSchema.error, descriptor.inputFormat, inputSchema);
    }
    if (exportedSchema.checked && exportedSchema.schema != inputSchema) {
        const QString actualSchema = exportedSchema.schema.trimmed().isEmpty()
            ? QStringLiteral("<missing>")
            : exportedSchema.schema;
        return failure(QStringLiteral("IP core '%1' %2 command expects input schema '%3' but exported project uses '%4'.")
                           .arg(entry.id, commandLabel, inputSchema, actualSchema),
                       descriptor.inputFormat,
                       inputSchema);
    }

    IpCoreResolvedCommand command;
    command.valid = true;
    command.ipcoreId = entry.id;
    command.workingDirectory = entry.sourceRootPath;
    command.command = descriptor.command;
    command.inputFormat = descriptor.inputFormat;
    command.inputSchema = inputSchema;
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
                                QStringLiteral("generator"),
                                QStringLiteral("generate"),
                                QStringLiteral("IP core '%1' does not declare a generator."),
                                &IpCatalogEntry::generator);
}

IpCoreResolvedCommand IpCoreCommandRunner::resolveDrc(const IpCatalogEntry& entry,
                                                      const QString& inputPath,
                                                      const QString& outputDirectory) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                QStringLiteral("DRC"),
                                QStringLiteral("validate"),
                                QStringLiteral("IP core '%1' does not declare a DRC command."),
                                &IpCatalogEntry::drc);
}
