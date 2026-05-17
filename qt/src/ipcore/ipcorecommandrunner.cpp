// IpCoreCommandRunner resolves selected IP-core command descriptors.
#include "ipcore/ipcorecommandrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QString packageManifestPath(const IpcraftPackageManifest& manifest) {
    const QString packageRootPath = manifest.packageRootPath.trimmed();
    if (packageRootPath.isEmpty()) {
        return {};
    }
    return QFileInfo(QDir(packageRootPath).filePath(QStringLiteral("ipcraft.json")))
        .absoluteFilePath();
}

struct RuntimeCommandDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList args;
    QString manifestPath;
    QString frameworkTool;

    bool hasCommand() const {
        return !command.trimmed().isEmpty() || !frameworkTool.trimmed().isEmpty();
    }

    bool usesIpcoreGraphInput() const {
        return inputFormat == QStringLiteral("ipcore_graph_v1");
    }

    bool usesIpcraftNocProjectInput() const {
        return inputFormat == QStringLiteral("ipcraft.noc.project.v1");
    }

    QStringList arguments(const QString& inputPath, const QString& outputDirectory) const {
        QStringList resolved;
        resolved.reserve(args.size());
        for (QString arg : args) {
            arg.replace(QStringLiteral("{manifest}"), manifestPath);
            arg.replace(QStringLiteral("{input}"), inputPath);
            arg.replace(QStringLiteral("{output}"), outputDirectory);
            resolved.append(arg);
        }
        return resolved;
    }
};

RuntimeCommandDescriptor legacyCommandDescriptor(const IpCoreCommandDescriptor& command) {
    RuntimeCommandDescriptor descriptor;
    descriptor.command = command.command;
    descriptor.inputFormat = command.inputFormat;
    descriptor.args = command.args;
    return descriptor;
}

RuntimeCommandDescriptor manifestCommandDescriptor(const IpcraftPackageManifest& manifest,
                                                   const IpcraftCommandDescriptor& command) {
    RuntimeCommandDescriptor descriptor;
    descriptor.command = command.resolvedExecutablePath.trimmed().isEmpty()
        ? command.executablePath
        : command.resolvedExecutablePath;
    descriptor.inputFormat = command.inputSchema;
    descriptor.args = command.args;
    descriptor.manifestPath = packageManifestPath(manifest);
    descriptor.frameworkTool = command.frameworkTool;
    return descriptor;
}

RuntimeCommandDescriptor descriptorForCommand(
    const IpCatalogEntry& entry,
    const QString& manifestCommandName,
    const IpCoreCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.packageManifest.commands.contains(manifestCommandName)) {
        return manifestCommandDescriptor(entry.packageManifest,
                                         entry.packageManifest.commands.value(manifestCommandName));
    }

    return legacyCommandDescriptor(entry.*commandMember);
}

QString commandInputSchema(const RuntimeCommandDescriptor& descriptor) {
    if (descriptor.usesIpcoreGraphInput()) {
        return legacyIpcoreGraphSchema();
    }
    if (descriptor.usesIpcraftNocProjectInput()) {
        return ipcraftNocProjectSchema();
    }
    return {};
}

bool supportsCommandInputFormat(const IpCatalogEntry& entry,
                                const RuntimeCommandDescriptor& descriptor,
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

QStringList normalizedSearchPaths(const QStringList& searchPaths) {
    QStringList normalized;
    for (const QString& path : searchPaths) {
        const QString trimmedPath = path.trimmed();
        if (trimmedPath.isEmpty()) {
            continue;
        }
        const QString absolutePath = QFileInfo(trimmedPath).absoluteFilePath();
        if (!normalized.contains(absolutePath)) {
            normalized.append(absolutePath);
        }
    }
    return normalized;
}

struct FrameworkToolResolution {
    QString path;
    QStringList searchedPaths;
};

FrameworkToolResolution resolveFrameworkTool(const QString& frameworkTool,
                                             const QStringList& searchPaths) {
    FrameworkToolResolution resolution;
    resolution.searchedPaths = normalizedSearchPaths(searchPaths);
    for (const QString& searchPath : resolution.searchedPaths) {
        const QString candidatePath = QDir(searchPath).filePath(frameworkTool);
        const QFileInfo candidate(candidatePath);
        if (candidate.exists() && candidate.isFile() && candidate.isExecutable()) {
            resolution.path = candidate.absoluteFilePath();
            break;
        }
    }
    return resolution;
}

QString searchedPathsMessage(const QStringList& searchedPaths) {
    return searchedPaths.isEmpty()
        ? QStringLiteral("<none>")
        : searchedPaths.join(QStringLiteral(", "));
}

IpCoreResolvedCommand resolveIpcoreCommand(const IpCatalogEntry& entry,
                                           const QString& inputPath,
                                           const QString& outputDirectory,
                                           const QStringList& frameworkToolSearchPaths,
                                           const QString& commandLabel,
                                           const QString& manifestCommandName,
                                           const QString& missingCommandMessage,
                                           const IpCoreCommandDescriptor IpCatalogEntry::* commandMember) {
    if (entry.id.trimmed().isEmpty()) {
        return failure(QStringLiteral("Select an IP core instance before running this action."));
    }

    const RuntimeCommandDescriptor descriptor =
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

    QString executableCommand = descriptor.command;
    if (!descriptor.frameworkTool.trimmed().isEmpty()) {
        if (descriptor.frameworkTool != QStringLiteral("ipcraft-generate")) {
            return failure(QStringLiteral("IP core '%1' declares unsupported %2 framework_tool '%3'.")
                               .arg(entry.id, commandLabel, descriptor.frameworkTool),
                           descriptor.inputFormat,
                           inputSchema);
        }

        const FrameworkToolResolution resolution =
            resolveFrameworkTool(descriptor.frameworkTool, frameworkToolSearchPaths);
        if (resolution.path.isEmpty()) {
            return failure(QStringLiteral("Framework tool '%1' for IP core '%2' was not found. Searched paths: %3")
                               .arg(descriptor.frameworkTool,
                                    entry.id,
                                    searchedPathsMessage(resolution.searchedPaths)),
                           descriptor.inputFormat,
                           inputSchema);
        }
        executableCommand = resolution.path;
    }

    IpCoreResolvedCommand command;
    command.valid = true;
    command.ipcoreId = entry.id;
    command.workingDirectory = entry.sourceRootPath;
    command.command = executableCommand;
    command.inputFormat = descriptor.inputFormat;
    command.inputSchema = inputSchema;
    command.arguments = descriptor.arguments(inputPath, outputDirectory);
    return command;
}

} // namespace

IpCoreResolvedCommand IpCoreCommandRunner::resolveGenerator(const IpCatalogEntry& entry,
                                                            const QString& inputPath,
                                                            const QString& outputDirectory) {
    return resolveGenerator(entry, inputPath, outputDirectory, {});
}

IpCoreResolvedCommand IpCoreCommandRunner::resolveGenerator(
    const IpCatalogEntry& entry,
    const QString& inputPath,
    const QString& outputDirectory,
    const QStringList& frameworkToolSearchPaths) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                frameworkToolSearchPaths,
                                QStringLiteral("generator"),
                                QStringLiteral("generate"),
                                QStringLiteral("IP core '%1' does not declare a generator."),
                                &IpCatalogEntry::generator);
}

IpCoreResolvedCommand IpCoreCommandRunner::resolveDrc(const IpCatalogEntry& entry,
                                                      const QString& inputPath,
                                                      const QString& outputDirectory) {
    return resolveDrc(entry, inputPath, outputDirectory, {});
}

IpCoreResolvedCommand IpCoreCommandRunner::resolveDrc(
    const IpCatalogEntry& entry,
    const QString& inputPath,
    const QString& outputDirectory,
    const QStringList& frameworkToolSearchPaths) {
    return resolveIpcoreCommand(entry,
                                inputPath,
                                outputDirectory,
                                frameworkToolSearchPaths,
                                QStringLiteral("DRC"),
                                QStringLiteral("validate"),
                                QStringLiteral("IP core '%1' does not declare a DRC command."),
                                &IpCatalogEntry::drc);
}
