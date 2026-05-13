#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct IpcraftDiagnostic {
    QString severity = QStringLiteral("error");
    QString packageRootPath;
    QString path;
    QString message;
};

struct IpcraftDynamicPluginMetadata {
    QString id;
    QString libraryPath;
    QString resolvedLibraryPath;
    QString entrypoint;
};

struct IpcraftExtensionDescriptor {
    QString id;
    bool enabled = false;
    QJsonObject configuration;
};

struct IpcraftConnectionClass {
    QString id;
    QStringList roles;
    bool symmetric = false;
};

struct IpcraftInterfaceAcceptRule {
    QString connectionClassId;
    QString role;
};

struct IpcraftInterfaceDescriptor {
    QString id;
    QString label;
    QStringList modes;
    QVector<IpcraftInterfaceAcceptRule> accepts;
    bool multiConnection = false;
    QJsonObject ipxact;
    QString ipxactBusInterface;
};

struct IpcraftModuleDescriptor {
    QString id;
    QString name;
    QString description;
    QString graphRole;
    QJsonObject parameters;
    QVector<IpcraftInterfaceDescriptor> interfaces;

    const IpcraftInterfaceDescriptor* interfaceDescriptor(const QString& interfaceId) const;
};

struct IpcraftViewDescriptor {
    QString moduleId;
    QString filePath;
    QString resolvedFilePath;
    QStringList requiredShapeFields;
};

struct IpcraftCommandDescriptor {
    QString name;
    QString executablePath;
    QString resolvedExecutablePath;
    QString inputSchema;
    QStringList args;
};

struct IpcraftIpxactDescriptor {
    QString rootPath;
    QString resolvedRootPath;
    bool generated = false;
};

struct IpcraftPackageManifest {
    QString schema;
    QString id;
    QString name;
    QString version;
    QString packageRootPath;
    std::optional<IpcraftDynamicPluginMetadata> plugin;
    QHash<QString, IpcraftExtensionDescriptor> extensions;
    QVector<IpcraftConnectionClass> connectionClasses;
    QVector<IpcraftModuleDescriptor> modules;
    QVector<IpcraftViewDescriptor> views;
    QHash<QString, IpcraftCommandDescriptor> commands;
    std::optional<IpcraftIpxactDescriptor> ipxact;
    QJsonObject parameters;
    QVector<QJsonObject> topologies;

    const IpcraftConnectionClass* connectionClass(const QString& connectionClassId) const;
    const IpcraftModuleDescriptor* module(const QString& moduleId) const;
    const IpcraftInterfaceDescriptor* interfaceDescriptor(const QString& moduleId,
                                                          const QString& interfaceId) const;
    const IpcraftViewDescriptor* viewForModule(const QString& moduleId) const;
};
