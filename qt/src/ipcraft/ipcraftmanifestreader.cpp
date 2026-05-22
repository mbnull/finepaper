#include "ipcraft/ipcraftmanifestreader.h"

#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>

namespace {

constexpr auto kPackageFileName = "ipcraft.json";

QString locationPath(const ipcraft::Diagnostic& diagnostic) {
    if (diagnostic.locations.isEmpty()) {
        return {};
    }

    const ipcraft::DiagnosticLocation& location = diagnostic.locations.first();
    if (!location.path.isEmpty()) {
        return location.path;
    }
    if (!location.file.isEmpty()) {
        return location.file;
    }
    if (!location.instanceId.isEmpty()) {
        return location.instanceId;
    }
    return location.kind;
}

IpcraftDiagnostic toLegacyDiagnostic(const ipcraft::Diagnostic& diagnostic,
                                     const QString& packageRootPath) {
    IpcraftDiagnostic converted;
    converted.severity = diagnostic.severity;
    converted.source = diagnostic.source;
    converted.ruleId = diagnostic.ruleId;
    converted.category = diagnostic.category;
    converted.packageRootPath = packageRootPath;
    converted.path = locationPath(diagnostic);
    converted.message = diagnostic.message;
    return converted;
}

QVector<IpcraftDiagnostic> toLegacyDiagnostics(const ipcraft::DiagnosticStore& diagnostics,
                                               const QString& packageRootPath) {
    QVector<IpcraftDiagnostic> converted;
    converted.reserve(diagnostics.records.size());
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        converted.append(toLegacyDiagnostic(diagnostic, packageRootPath));
    }
    return converted;
}

QString packageLocalAbsolutePath(const QString& packageRootPath, const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QFileInfo(QDir(packageRootPath).filePath(trimmed)).absoluteFilePath();
}

QStringList stringList(const QJsonValue& value) {
    QStringList result;
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                result.append(text);
            }
        }
    }
    return result;
}

QVector<QJsonObject> objectVector(const QJsonValue& value) {
    QVector<QJsonObject> result;
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue& item : array) {
        if (item.isObject()) {
            result.append(item.toObject());
        }
    }
    return result;
}

QJsonObject editorMetadata(const ipcraft::PackageSpec& spec) {
    return spec.native.value(QStringLiteral("ipcraft")).toObject()
        .value(QStringLiteral("editor")).toObject();
}

std::optional<IpcraftDynamicPluginMetadata>
pluginFromPackageSpec(const ipcraft::PackageSpec& spec) {
    if (!spec.hasPlugin) {
        return std::nullopt;
    }

    IpcraftDynamicPluginMetadata plugin;
    plugin.id = spec.plugin.value(QStringLiteral("id")).toString();
    plugin.libraryPath = spec.plugin.value(QStringLiteral("library")).toString();
    if (plugin.libraryPath.isEmpty()) {
        plugin.libraryPath = spec.plugin.value(QStringLiteral("library_path")).toString();
    }
    plugin.resolvedLibraryPath = packageLocalAbsolutePath(spec.packageRootPath,
                                                          plugin.libraryPath);
    plugin.entrypoint = spec.plugin.value(QStringLiteral("entrypoint")).toString();
    if (plugin.entrypoint.isEmpty()) {
        plugin.entrypoint = spec.plugin.value(QStringLiteral("entry")).toString();
    }
    return plugin;
}

IpcraftCommandDescriptor commandFromEditor(const QString& name,
                                           const QJsonObject& object,
                                           const QString& packageRootPath) {
    IpcraftCommandDescriptor command;
    command.name = name;
    command.executablePath = object.value(QStringLiteral("executable")).toString().trimmed();
    command.resolvedExecutablePath = packageLocalAbsolutePath(packageRootPath,
                                                              command.executablePath);
    command.frameworkTool = object.value(QStringLiteral("framework_tool")).toString().trimmed();
    command.inputSchema = object.value(QStringLiteral("input_schema")).toString().trimmed();
    command.args = stringList(object.value(QStringLiteral("args")));
    return command;
}

QHash<QString, IpcraftCommandDescriptor> commandsFromEditor(const QJsonObject& editor,
                                                            const QString& packageRootPath) {
    QHash<QString, IpcraftCommandDescriptor> commands;
    const QJsonObject commandObject = editor.value(QStringLiteral("commands")).toObject();
    for (auto it = commandObject.constBegin(); it != commandObject.constEnd(); ++it) {
        if (it.value().isObject()) {
            commands.insert(it.key(),
                            commandFromEditor(it.key(), it.value().toObject(), packageRootPath));
        }
    }
    return commands;
}

QHash<QString, IpcraftExtensionDescriptor> extensionsFromPackageSpec(
    const ipcraft::PackageSpec& spec,
    const QJsonObject& editor) {
    QHash<QString, IpcraftExtensionDescriptor> extensions;
    for (const QString& extensionId : spec.extensions) {
        IpcraftExtensionDescriptor extension;
        extension.id = extensionId;
        extension.enabled = true;
        extensions.insert(extensionId, extension);
    }

    const QJsonObject editorExtensions = editor.value(QStringLiteral("extensions")).toObject();
    for (auto it = editorExtensions.constBegin(); it != editorExtensions.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject object = it.value().toObject();
        IpcraftExtensionDescriptor extension = extensions.value(it.key());
        extension.id = it.key();
        extension.enabled = object.value(QStringLiteral("enabled")).isBool()
            ? object.value(QStringLiteral("enabled")).toBool()
            : true;
        extension.configuration = object;
        extensions.insert(it.key(), extension);
    }
    return extensions;
}

IpcraftConnectionClass connectionClassFromEditor(const QJsonObject& object) {
    IpcraftConnectionClass connectionClass;
    connectionClass.id = object.value(QStringLiteral("id")).toString().trimmed();
    connectionClass.roles = stringList(object.value(QStringLiteral("roles")));
    connectionClass.symmetric = object.value(QStringLiteral("symmetric")).toBool(false);
    connectionClass.ipxact = object.value(QStringLiteral("ipxact")).toObject();
    return connectionClass;
}

IpcraftInterfaceAcceptRule acceptRuleFromEditor(const QJsonObject& object) {
    IpcraftInterfaceAcceptRule accept;
    accept.connectionClassId = object.value(QStringLiteral("class")).toString().trimmed();
    accept.role = object.value(QStringLiteral("role")).toString().trimmed();
    return accept;
}

IpcraftInterfaceDescriptor interfaceFromEditor(const QJsonObject& object) {
    IpcraftInterfaceDescriptor interfaceDescriptor;
    interfaceDescriptor.id = object.value(QStringLiteral("id")).toString().trimmed();
    interfaceDescriptor.label = object.value(QStringLiteral("label")).toString().trimmed();
    interfaceDescriptor.modes = stringList(object.value(QStringLiteral("modes")));
    const QJsonArray accepts = object.value(QStringLiteral("accepts")).toArray();
    for (const QJsonValue& acceptValue : accepts) {
        if (acceptValue.isObject()) {
            interfaceDescriptor.accepts.append(acceptRuleFromEditor(acceptValue.toObject()));
        }
    }
    interfaceDescriptor.multiConnection =
        object.value(QStringLiteral("multi_connection")).toBool(false);
    interfaceDescriptor.ipxact = object.value(QStringLiteral("ipxact")).toObject();
    interfaceDescriptor.ipxactBusInterface =
        interfaceDescriptor.ipxact.value(QStringLiteral("bus_interface")).toString().trimmed();
    const QJsonObject topology = object.value(QStringLiteral("topology")).toObject();
    interfaceDescriptor.topology.side = topology.value(QStringLiteral("side")).toString().trimmed();
    interfaceDescriptor.topology.oppositeInterfaceId =
        topology.value(QStringLiteral("opposite")).toString().trimmed();
    interfaceDescriptor.topology.role = topology.value(QStringLiteral("role")).toString().trimmed();
    return interfaceDescriptor;
}

IpcraftModuleDescriptor moduleFromEditor(const QJsonObject& object) {
    IpcraftModuleDescriptor module;
    module.id = object.value(QStringLiteral("id")).toString().trimmed();
    module.name = object.value(QStringLiteral("name")).toString().trimmed();
    module.description = object.value(QStringLiteral("description")).toString().trimmed();
    module.graphRole = object.value(QStringLiteral("graph_role")).toString().trimmed();
    module.attach = object.value(QStringLiteral("attach")).toObject();
    module.parameters = object.value(QStringLiteral("parameters")).toObject();
    const QJsonObject display = object.value(QStringLiteral("display")).toObject();
    module.displayLabelParameter =
        display.value(QStringLiteral("label_parameter")).toString().trimmed();
    module.shortLabelParameter =
        display.value(QStringLiteral("short_label_parameter")).toString().trimmed();
    const QJsonArray interfaces = object.value(QStringLiteral("interfaces")).toArray();
    for (const QJsonValue& interfaceValue : interfaces) {
        if (interfaceValue.isObject()) {
            module.interfaces.append(interfaceFromEditor(interfaceValue.toObject()));
        }
    }
    return module;
}

IpcraftViewDescriptor viewFromSpec(const QJsonObject& object, const QString& packageRootPath) {
    IpcraftViewDescriptor view;
    view.moduleId = object.value(QStringLiteral("module")).toString().trimmed();
    view.filePath = object.value(QStringLiteral("file")).toString().trimmed();
    view.resolvedFilePath = packageLocalAbsolutePath(packageRootPath, view.filePath);
    view.requiredShapeFields = stringList(object.value(QStringLiteral("required_shape_fields")));
    return view;
}

IpcraftIpxactDescriptor ipxactFromEditor(const QJsonObject& editor,
                                         const QString& packageRootPath) {
    const QJsonObject object = editor.value(QStringLiteral("ipxact")).toObject();
    IpcraftIpxactDescriptor ipxact;
    ipxact.rootPath = object.value(QStringLiteral("root")).toString().trimmed();
    ipxact.resolvedRootPath = packageLocalAbsolutePath(packageRootPath, ipxact.rootPath);
    ipxact.generated = object.value(QStringLiteral("generated")).toBool(false);
    return ipxact;
}

IpcraftPackageManifest manifestFromPackageSpec(const ipcraft::PackageSpec& spec) {
    const QJsonObject editor = editorMetadata(spec);
    IpcraftPackageManifest manifest;
    manifest.schema = ipcraft::schemaids::packageV1;
    manifest.id = spec.id;
    manifest.name = spec.name;
    manifest.version = spec.version;
    manifest.packageRootPath = spec.packageRootPath;
    manifest.plugin = pluginFromPackageSpec(spec);
    manifest.extensions = extensionsFromPackageSpec(spec, editor);
    manifest.commands = commandsFromEditor(editor, spec.packageRootPath);
    manifest.parameters = editor.value(QStringLiteral("parameters")).toObject();

    const QJsonObject instances = editor.value(QStringLiteral("instances")).toObject();
    if (instances.value(QStringLiteral("max")).isDouble()) {
        manifest.instances.max = instances.value(QStringLiteral("max")).toInt();
    }
    const QJsonObject ipxactObject = editor.value(QStringLiteral("ipxact")).toObject();
    if (!ipxactObject.isEmpty()) {
        manifest.ipxact = ipxactFromEditor(editor, spec.packageRootPath);
    }
    for (const QJsonValue& value : editor.value(QStringLiteral("connection_classes")).toArray()) {
        if (value.isObject()) {
            manifest.connectionClasses.append(connectionClassFromEditor(value.toObject()));
        }
    }
    for (const QJsonValue& value : editor.value(QStringLiteral("modules")).toArray()) {
        if (value.isObject()) {
            manifest.modules.append(moduleFromEditor(value.toObject()));
        }
    }
    for (const QJsonObject& view : objectVector(spec.views)) {
        manifest.views.append(viewFromSpec(view, spec.packageRootPath));
    }
    if (manifest.views.isEmpty()) {
        for (const QJsonObject& view : objectVector(editor.value(QStringLiteral("views")))) {
            manifest.views.append(viewFromSpec(view, spec.packageRootPath));
        }
    }
    manifest.topologies = objectVector(editor.value(QStringLiteral("topologies")));
    manifest.generation.engine =
        editor.value(QStringLiteral("generation")).toObject()
            .value(QStringLiteral("engine")).toString().trimmed();
    manifest.generation.metadata = editor.value(QStringLiteral("generation")).toObject();

    return manifest;
}

} // namespace

IpcraftManifestReadResult
IpcraftManifestReader::readPackage(const QString& packageRootPath) const {
    return readManifestFile(QDir(packageRootPath).filePath(QString::fromLatin1(kPackageFileName)));
}

IpcraftManifestReadResult
IpcraftManifestReader::readManifestFile(const QString& manifestPath) const {
    IpcraftManifestReadResult result;
    const ipcraft::PackageSpecReadResult packageResult =
        ipcraft::PackageSpecReader().readSpecFile(manifestPath);

    result.manifest = manifestFromPackageSpec(packageResult.spec);
    result.diagnostics = toLegacyDiagnostics(packageResult.diagnostics,
                                             packageResult.spec.packageRootPath);
    result.ok = packageResult.ok;
    return result;
}
