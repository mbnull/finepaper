// IpcraftBuiltInValidator implementation.
#include "ipcraft/ipcraftbuiltinvalidator.h"

#include "graph/connection.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "graph/port.h"
#include "ipcraft/ipcraftconnectionvalidator.h"
#include "ipcraft/ipxactconnectionchecker.h"
#include "modules/moduletypemetadata.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <algorithm>
#include <optional>

namespace {

QString instanceKey(const QString& ipcoreId, const QString& instanceId) {
    return ipcoreId + QLatin1Char('\n') + instanceId;
}

bool entryMatchesIpcore(const IpCatalogEntry& entry, const QString& ipcoreId) {
    return entry.id == ipcoreId || entry.packageId == ipcoreId;
}

const IpCatalogEntry* findEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId) {
    for (const IpCatalogEntry& entry : entries) {
        if (entryMatchesIpcore(entry, ipcoreId)) {
            return &entry;
        }
    }
    return nullptr;
}

QSet<QString> collectReferencedPackageIds(const QVector<ProjectIpInstanceRecord>& instances) {
    QSet<QString> packageIds;
    for (const ProjectIpInstanceRecord& instance : instances) {
        packageIds.insert(instance.ipcoreId);
    }
    return packageIds;
}

bool entryMatchesReferencedPackage(const IpCatalogEntry& entry,
                                   const QSet<QString>& packageIds) {
    return packageIds.contains(entry.id) || packageIds.contains(entry.packageId);
}

const Module* findModule(const Graph* graph, const QString& moduleId) {
    if (!graph) {
        return nullptr;
    }
    for (const std::unique_ptr<Module>& module : graph->modules()) {
        if (module->id() == moduleId) {
            return module.get();
        }
    }
    return nullptr;
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) {
        return nullptr;
    }
    for (const Port& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }
    return nullptr;
}

const Connection* findConnection(const Graph* graph, const QString& connectionId) {
    if (!graph) {
        return nullptr;
    }
    for (const std::unique_ptr<Connection>& connection : graph->connections()) {
        if (connection->id() == connectionId) {
            return connection.get();
        }
    }
    return nullptr;
}

QString interfaceIdForPort(const Port& port) {
    return port.interfaceId().isEmpty() ? port.id() : port.interfaceId();
}

QString manifestPackageId(const IpCatalogEntry& entry) {
    if (!entry.packageManifest.id.trimmed().isEmpty()) {
        return entry.packageManifest.id;
    }
    if (!entry.packageId.trimmed().isEmpty()) {
        return entry.packageId;
    }
    return entry.id;
}

QString unscopedModuleType(const IpCatalogEntry& entry, const QString& moduleType) {
    const QString packageId = manifestPackageId(entry);
    const QString prefix = packageId + QStringLiteral("::");
    return moduleType.startsWith(prefix) ? moduleType.mid(prefix.size()) : moduleType;
}

QString moduleManifestId(const IpCatalogEntry& entry, const Module& module) {
    const QString metadataModuleId = ModuleTypeMetadata::moduleId(&module);
    if (entry.packageManifest.module(metadataModuleId) != nullptr) {
        return metadataModuleId;
    }
    if (entry.packageManifest.module(module.type()) != nullptr) {
        return module.type();
    }
    const QString unscopedType = unscopedModuleType(entry, module.type());
    if (entry.packageManifest.module(unscopedType) != nullptr) {
        return unscopedType;
    }
    return {};
}

QSet<QString> interfaceIdsForModule(const IpcraftModuleDescriptor& module) {
    QSet<QString> interfaceIds;
    for (const IpcraftInterfaceDescriptor& descriptor : module.interfaces) {
        interfaceIds.insert(descriptor.id);
    }
    return interfaceIds;
}

bool isNativeIpxactMode(const QString& mode) {
    static const QSet<QString> kNativeModes{
        QStringLiteral("initiator"),
        QStringLiteral("target"),
        QStringLiteral("system"),
        QStringLiteral("mirroredInitiator"),
        QStringLiteral("mirroredTarget"),
        QStringLiteral("mirroredSystem"),
        QStringLiteral("monitor")
    };
    return kNativeModes.contains(mode.trimmed());
}

bool hasEnabledExtensionModeMapping(const IpcraftPackageManifest& manifest,
                                    const QString& mode) {
    const QString normalizedMode = mode.trimmed();
    for (auto it = manifest.extensions.constBegin(); it != manifest.extensions.constEnd(); ++it) {
        const IpcraftExtensionDescriptor& extension = it.value();
        if (!extension.enabled) {
            continue;
        }

        const QJsonObject modes = extension.configuration.value(QStringLiteral("modes")).toObject();
        const QJsonObject modeConfig = modes.value(normalizedMode).toObject();
        const QJsonObject ipxact = modeConfig.value(QStringLiteral("ipxact")).toObject();
        const QString mappedMode = ipxact.value(QStringLiteral("mode")).toString().trimmed();
        if (isNativeIpxactMode(mappedMode)) {
            return true;
        }
    }

    return false;
}

bool hasExplicitInterfaceModeMapping(const IpcraftInterfaceDescriptor& interfaceDescriptor,
                                     const QString& mode) {
    const QString directMode =
        interfaceDescriptor.ipxact.value(QStringLiteral("mode")).toString().trimmed();
    if (isNativeIpxactMode(directMode)) {
        return true;
    }

    const QJsonObject modes = interfaceDescriptor.ipxact.value(QStringLiteral("modes")).toObject();
    const QJsonObject modeConfig = modes.value(mode.trimmed()).toObject();
    const QString mappedMode = modeConfig.value(QStringLiteral("mode")).toString().trimmed();
    return isNativeIpxactMode(mappedMode);
}

bool hasEnabledNocExtension(const IpcraftPackageManifest& manifest) {
    const auto it = manifest.extensions.constFind(QStringLiteral("noc.v1"));
    return it != manifest.extensions.constEnd() && it.value().enabled;
}

bool isNocChiConnectionRole(const QString& role) {
    static const QSet<QString> kNocChiRoles{
        QStringLiteral("node"),
        QStringLiteral("interconnect"),
        QStringLiteral("peer")
    };
    return kNocChiRoles.contains(role.trimmed());
}

QSet<QString> connectionClassRoles(const IpcraftConnectionClass& connectionClass) {
    QSet<QString> roles;
    for (const QString& role : connectionClass.roles) {
        const QString trimmedRole = role.trimmed();
        if (!trimmedRole.isEmpty()) {
            roles.insert(trimmedRole);
        }
    }
    return roles;
}

QString connectionClassMappingError(const IpcraftPackageManifest& manifest,
                                    const IpcraftConnectionClass& connectionClass) {
    const QSet<QString> roles = connectionClassRoles(connectionClass);
    if (roles.isEmpty()) {
        return {};
    }

    if (!connectionClass.ipxact.isEmpty()) {
        for (auto it = connectionClass.ipxact.constBegin();
             it != connectionClass.ipxact.constEnd();
             ++it) {
            if (!roles.contains(it.key())) {
                return QStringLiteral("Connection class '%1' ipxact field '%2' is not declared by its roles.")
                    .arg(connectionClass.id, it.key());
            }
            if (!it.value().isString() || !isNativeIpxactMode(it.value().toString())) {
                return QStringLiteral("Connection class '%1' ipxact.%2 is not mappable to IP-XACT connection semantics.")
                    .arg(connectionClass.id, it.key());
            }
        }

        for (const QString& role : roles) {
            if (!connectionClass.ipxact.contains(role)) {
                return QStringLiteral("Connection class '%1' ipxact mapping is missing role '%2'.")
                    .arg(connectionClass.id, role);
            }
        }
        return {};
    }

    bool allRolesNative = true;
    for (const QString& role : roles) {
        if (!isNativeIpxactMode(role)) {
            allRolesNative = false;
            break;
        }
    }
    if (allRolesNative) {
        return {};
    }

    if (hasEnabledNocExtension(manifest)
        && connectionClass.id.startsWith(QStringLiteral("chi_"))) {
        bool allRolesChi = true;
        for (const QString& role : roles) {
            if (!isNocChiConnectionRole(role)) {
                allRolesChi = false;
                break;
            }
        }
        if (allRolesChi) {
            return {};
        }
    }

    return QStringLiteral("Connection class '%1' is not mappable to IP-XACT connection semantics.")
        .arg(connectionClass.id);
}

bool isAttachmentZoneElement(const QString& elementName) {
    return elementName == QStringLiteral("zone")
           || elementName == QStringLiteral("attachment-zone");
}

bool isInterfaceReferenceAttribute(const QString& elementName, const QString& attributeName) {
    if (attributeName == QStringLiteral("ref")) {
        return elementName == QStringLiteral("anchor")
               || elementName == QStringLiteral("interface");
    }
    static const QSet<QString> kInterfaceReferenceAttributes{
        QStringLiteral("interface"),
        QStringLiteral("interface_id"),
        QStringLiteral("interface_ref")
    };
    return kInterfaceReferenceAttributes.contains(attributeName);
}

bool isAttachmentZoneReferenceAttribute(const QString& elementName, const QString& attributeName) {
    if (attributeName == QStringLiteral("ref")) {
        return isAttachmentZoneElement(elementName);
    }
    static const QSet<QString> kAttachmentZoneReferenceAttributes{
        QStringLiteral("zone"),
        QStringLiteral("attach_zone"),
        QStringLiteral("attachment_zone")
    };
    return kAttachmentZoneReferenceAttributes.contains(attributeName);
}

bool isTopologyConnectionClassField(const QString& key) {
    return key == QStringLiteral("connection_class")
           || key == QStringLiteral("class")
           || key == QStringLiteral("connection_classes");
}

bool isRouterTopologyKind(const QString& kind) {
    return kind == QStringLiteral("mesh") || kind == QStringLiteral("ring");
}

bool isRouterCapableGraphRole(const QString& graphRole) {
    return graphRole == QStringLiteral("host") || graphRole == QStringLiteral("router");
}

QSet<QString> connectionClassIdsForManifest(const IpcraftPackageManifest& manifest) {
    QSet<QString> classIds;
    for (const IpcraftConnectionClass& connectionClass : manifest.connectionClasses) {
        const QString classId = connectionClass.id.trimmed();
        if (!classId.isEmpty()) {
            classIds.insert(classId);
        }
    }
    return classIds;
}

bool interfaceExistsOnAnyModule(const IpcraftPackageManifest& manifest,
                                const QString& interfaceId) {
    for (const IpcraftModuleDescriptor& module : manifest.modules) {
        if (module.interfaceDescriptor(interfaceId) != nullptr) {
            return true;
        }
    }
    return false;
}

QString attachZone(const IpcraftModuleDescriptor& module) {
    return module.attach.value(QStringLiteral("zone")).toString().trimmed();
}

QStringList attachHosts(const IpcraftModuleDescriptor& module) {
    QStringList hosts;
    const QJsonValue hostsValue = module.attach.value(QStringLiteral("hosts"));
    if (hostsValue.isArray()) {
        const QJsonArray hostArray = hostsValue.toArray();
        for (const QJsonValue& hostValue : hostArray) {
            const QString host = hostValue.toString().trimmed();
            if (!host.isEmpty() && !hosts.contains(host)) {
                hosts.append(host);
            }
        }
    }

    const QString host = module.attach.value(QStringLiteral("host")).toString().trimmed();
    if (!host.isEmpty() && !hosts.contains(host)) {
        hosts.append(host);
    }
    return hosts;
}

QSet<QString> attachmentZonesForModule(const IpcraftPackageManifest& manifest,
                                       const QString& moduleId) {
    QSet<QString> zones;
    const IpcraftModuleDescriptor* module = manifest.module(moduleId);
    if (module != nullptr) {
        const QString selfZone = attachZone(*module);
        if (!selfZone.isEmpty()) {
            zones.insert(selfZone);
        }
    }

    for (const IpcraftModuleDescriptor& candidate : manifest.modules) {
        const QString zone = attachZone(candidate);
        if (zone.isEmpty()) {
            continue;
        }
        if (attachHosts(candidate).contains(moduleId)) {
            zones.insert(zone);
        }
    }
    return zones;
}

QVector<IpcraftPackageManifest> packageManifests(const QList<IpCatalogEntry>& entries) {
    QVector<IpcraftPackageManifest> manifests;
    for (const IpCatalogEntry& entry : entries) {
        if (!entry.packageManifest.id.trimmed().isEmpty()) {
            manifests.push_back(entry.packageManifest);
        }
    }
    return manifests;
}

const IpCoreCommandDescriptor& legacyCommandDescriptor(
    const IpCatalogEntry& entry,
    IpcraftBuiltInValidator::CommandPurpose purpose) {
    return purpose == IpcraftBuiltInValidator::CommandPurpose::Generate
        ? entry.generator
        : entry.drc;
}

QString commandName(IpcraftBuiltInValidator::CommandPurpose purpose) {
    return purpose == IpcraftBuiltInValidator::CommandPurpose::Generate
        ? QStringLiteral("generate")
        : QStringLiteral("validate");
}

struct ValidationAccumulator {
    IpcraftBuiltInValidator::Result result;
    QVector<ProjectIpInstanceRecord> instances;

    void addDiagnostic(ValidationSeverity severity,
                       const QString& message,
                       const QString& elementId,
                       const QString& ruleName) {
        result.diagnostics.append(ValidationResult(severity, message, elementId, ruleName));
    }

    void addGlobalError(const QString& message,
                        const QString& elementId,
                        const QString& ruleName) {
        addDiagnostic(ValidationSeverity::Error, message, elementId, ruleName);
        for (const ProjectIpInstanceRecord& instance : instances) {
            result.blockingInstanceIds.insert(instance.instanceId);
        }
    }

    void addInstanceError(const QString& instanceId,
                          const QString& message,
                          const QString& elementId,
                          const QString& ruleName) {
        addDiagnostic(ValidationSeverity::Error, message, elementId, ruleName);
        if (!instanceId.trimmed().isEmpty()) {
            result.blockingInstanceIds.insert(instanceId);
        }
    }

    void addPackageError(const IpCatalogEntry& entry,
                         const QString& message,
                         const QString& elementId,
                         const QString& ruleName) {
        addDiagnostic(ValidationSeverity::Error, message, elementId, ruleName);
        for (const ProjectIpInstanceRecord& instance : instances) {
            if (entryMatchesIpcore(entry, instance.ipcoreId)) {
                result.blockingInstanceIds.insert(instance.instanceId);
            }
        }
    }

    void addModuleError(const Module& module,
                        const QString& message,
                        const QString& ruleName) {
        addInstanceError(module.instanceId(), message, module.id(), ruleName);
    }

    void addConnectionError(const Graph* graph,
                            const Connection& connection,
                            const QString& message) {
        addDiagnostic(ValidationSeverity::Error,
                      message,
                      connection.id(),
                      QStringLiteral("built_in_connection"));
        blockConnectionInstances(graph, connection);
    }

    void addIpxactConnectionError(const Graph* graph,
                                  const QString& connectionId,
                                  const QString& message) {
        addDiagnostic(ValidationSeverity::Error,
                      message,
                      connectionId,
                      QStringLiteral("built_in_ipxact_connection"));

        const Connection* connection = findConnection(graph, connectionId);
        if (connection != nullptr) {
            blockConnectionInstances(graph, *connection);
        }
    }

    void blockConnectionInstances(const Graph* graph,
                                  const Connection& connection) {
        const Module* sourceModule = findModule(graph, connection.source().moduleId);
        const Module* targetModule = findModule(graph, connection.target().moduleId);
        if (sourceModule && !sourceModule->instanceId().trimmed().isEmpty()) {
            result.blockingInstanceIds.insert(sourceModule->instanceId());
        }
        if (targetModule && !targetModule->instanceId().trimmed().isEmpty()) {
            result.blockingInstanceIds.insert(targetModule->instanceId());
        }
        for (const ConnectionInterfaceRef& interfaceRef : connection.interfaces()) {
            const Module* module = findModule(graph, interfaceRef.instanceId);
            if (module && !module->instanceId().trimmed().isEmpty()) {
                result.blockingInstanceIds.insert(module->instanceId());
            }
        }
    }
};

QHash<QString, const ProjectIpInstanceRecord*> instanceRecordsByKey(
    const QVector<ProjectIpInstanceRecord>& instances) {
    QHash<QString, const ProjectIpInstanceRecord*> records;
    for (const ProjectIpInstanceRecord& instance : instances) {
        records.insert(instanceKey(instance.ipcoreId, instance.instanceId), &instance);
    }
    return records;
}

QVector<ConnectionInterfaceRef> effectiveConnectionInterfaces(const Graph* graph,
                                                              const Connection& connection) {
    if (!connection.interfaces().isEmpty()) {
        return connection.interfaces();
    }

    QVector<ConnectionInterfaceRef> interfaces;
    const Module* sourceModule = findModule(graph, connection.source().moduleId);
    const Module* targetModule = findModule(graph, connection.target().moduleId);
    const Port* sourcePort = findPort(sourceModule, connection.source().portId);
    const Port* targetPort = findPort(targetModule, connection.target().portId);
    interfaces.push_back(ConnectionInterfaceRef{
        connection.source().moduleId,
        sourcePort ? interfaceIdForPort(*sourcePort) : connection.source().portId
    });
    interfaces.push_back(ConnectionInterfaceRef{
        connection.target().moduleId,
        targetPort ? interfaceIdForPort(*targetPort) : connection.target().portId
    });
    return interfaces;
}

QVector<ProjectConnectionRecord> currentProjectConnections(const Graph* graph) {
    QVector<ProjectConnectionRecord> records;
    if (!graph) {
        return records;
    }

    for (const std::unique_ptr<Connection>& connection : graph->connections()) {
        ProjectConnectionRecord record;
        record.id = connection->id();
        record.source = ProjectConnectionEndpoint{connection->source().moduleId,
                                                  connection->source().portId};
        record.target = ProjectConnectionEndpoint{connection->target().moduleId,
                                                  connection->target().portId};
        record.connectionClassId = connection->connectionClassId();
        record.status = connection->status();
        record.alternatives = connection->alternatives();

        for (const ConnectionInterfaceRef& interfaceRef : connection->interfaces()) {
            record.interfaces.push_back(ProjectConnectionInterfaceRef{
                interfaceRef.instanceId,
                interfaceRef.interfaceId
            });
        }

        if (record.interfaces.isEmpty()) {
            const Module* sourceModule = findModule(graph, connection->source().moduleId);
            const Module* targetModule = findModule(graph, connection->target().moduleId);
            const Port* sourcePort = findPort(sourceModule, connection->source().portId);
            const Port* targetPort = findPort(targetModule, connection->target().portId);
            record.interfaces.push_back(ProjectConnectionInterfaceRef{
                connection->source().moduleId,
                sourcePort ? interfaceIdForPort(*sourcePort) : connection->source().portId
            });
            record.interfaces.push_back(ProjectConnectionInterfaceRef{
                connection->target().moduleId,
                targetPort ? interfaceIdForPort(*targetPort) : connection->target().portId
            });
        }

        records.push_back(record);
    }

    return records;
}

void validateCommand(const IpCatalogEntry& entry,
                     IpcraftBuiltInValidator::CommandPurpose purpose,
                     ValidationAccumulator& accumulator) {
    const QString name = commandName(purpose);
    const bool hasManifestCommand = entry.packageManifest.commands.contains(name);
    if (hasManifestCommand) {
        const IpcraftCommandDescriptor command = entry.packageManifest.commands.value(name);
        if (command.executablePath.trimmed().isEmpty()
            && command.resolvedExecutablePath.trimmed().isEmpty()
            && command.frameworkTool.trimmed().isEmpty()) {
            accumulator.addPackageError(
                entry,
                QStringLiteral("Package '%1' %2 command does not declare an executable or framework_tool.")
                    .arg(entry.id, name),
                entry.id,
                QStringLiteral("built_in_command"));
        }
        if (command.inputSchema.trimmed().isEmpty()) {
            accumulator.addPackageError(
                entry,
                QStringLiteral("Package '%1' %2 command does not declare an input schema.")
                    .arg(entry.id, name),
                entry.id,
                QStringLiteral("built_in_command"));
        }
        return;
    }

    const IpCoreCommandDescriptor& legacyCommand = legacyCommandDescriptor(entry, purpose);
    if (legacyCommand.hasCommand() && legacyCommand.inputFormat.trimmed().isEmpty()) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Package '%1' %2 command does not declare an input schema.")
                .arg(entry.id, name),
            entry.id,
            QStringLiteral("built_in_command"));
    }
}

void validateManifestReferences(const IpCatalogEntry& entry,
                                ValidationAccumulator& accumulator) {
    const IpcraftPackageManifest& manifest = entry.packageManifest;
    if (manifest.id.trimmed().isEmpty()) {
        return;
    }

    for (const IpcraftConnectionClass& connectionClass : manifest.connectionClasses) {
        if (connectionClassRoles(connectionClass).isEmpty()) {
            accumulator.addPackageError(
                entry,
                QStringLiteral("Connection class '%1' must declare at least one role.")
                    .arg(connectionClass.id),
                connectionClass.id,
                QStringLiteral("built_in_manifest"));
        }

        const QString mappingError = connectionClassMappingError(manifest, connectionClass);
        if (!mappingError.isEmpty()) {
            accumulator.addPackageError(entry,
                                        mappingError,
                                        connectionClass.id,
                                        QStringLiteral("built_in_manifest"));
        }
    }

    for (const IpcraftModuleDescriptor& module : manifest.modules) {
        for (const IpcraftInterfaceDescriptor& interfaceDescriptor : module.interfaces) {
            if (interfaceDescriptor.modes.isEmpty()) {
                accumulator.addPackageError(
                    entry,
                    QStringLiteral("Module '%1' interface '%2' must declare at least one mode.")
                        .arg(module.id, interfaceDescriptor.id),
                    module.id,
                    QStringLiteral("built_in_manifest"));
            }

            if (interfaceDescriptor.ipxactBusInterface.trimmed().isEmpty()) {
                accumulator.addPackageError(
                    entry,
                    QStringLiteral("Module '%1' interface '%2' must declare ipxact.bus_interface.")
                        .arg(module.id, interfaceDescriptor.id),
                    module.id,
                    QStringLiteral("built_in_manifest"));
            }

            for (const QString& mode : interfaceDescriptor.modes) {
                if (isNativeIpxactMode(mode)
                    || hasEnabledExtensionModeMapping(manifest, mode)
                    || hasExplicitInterfaceModeMapping(interfaceDescriptor, mode)) {
                    continue;
                }

                accumulator.addPackageError(
                    entry,
                    QStringLiteral("Module '%1' interface '%2' mode '%3' is not mappable to IP-XACT connection semantics.")
                        .arg(module.id, interfaceDescriptor.id, mode.trimmed()),
                    module.id,
                    QStringLiteral("built_in_manifest"));
            }

            for (const IpcraftInterfaceAcceptRule& rule : interfaceDescriptor.accepts) {
                const IpcraftConnectionClass* connectionClass =
                    manifest.connectionClass(rule.connectionClassId);
                if (connectionClass == nullptr) {
                    accumulator.addPackageError(
                        entry,
                        QStringLiteral("Module '%1' interface '%2' references missing connection class '%3'.")
                            .arg(module.id, interfaceDescriptor.id, rule.connectionClassId),
                        module.id,
                        QStringLiteral("built_in_manifest"));
                    continue;
                }

                const QString role = rule.role.trimmed();
                if (!connectionClassRoles(*connectionClass).contains(role)) {
                    accumulator.addPackageError(
                        entry,
                        QStringLiteral("Module '%1' interface '%2' accept role '%3' is not declared by connection class '%4'.")
                            .arg(module.id,
                                 interfaceDescriptor.id,
                                 role,
                                 rule.connectionClassId),
                        module.id,
                        QStringLiteral("built_in_manifest"));
                }
            }
        }
    }
}

void validateTopologyConnectionClassValue(const IpCatalogEntry& entry,
                                          const QString& topologyId,
                                          const QString& key,
                                          const QJsonValue& value,
                                          const QSet<QString>& classIds,
                                          ValidationAccumulator& accumulator) {
    auto validateClassId = [&](const QString& rawClassId) {
        const QString classId = rawClassId.trimmed();
        if (!classIds.contains(classId)) {
            accumulator.addPackageError(
                entry,
                QStringLiteral("Topology '%1' %2 references missing connection class '%3'.")
                    .arg(topologyId, key, classId),
                topologyId,
                QStringLiteral("built_in_topology"));
        }
    };

    if (value.isString()) {
        validateClassId(value.toString());
        return;
    }

    if (!value.isArray()) {
        return;
    }

    const QJsonArray values = value.toArray();
    for (const QJsonValue& classValue : values) {
        if (classValue.isString()) {
            validateClassId(classValue.toString());
        }
    }
}

void validateTopologyConnectionClassReferences(const IpCatalogEntry& entry,
                                               const QJsonObject& topology,
                                               const QString& topologyId,
                                               const QSet<QString>& classIds,
                                               ValidationAccumulator& accumulator) {
    for (auto it = topology.constBegin(); it != topology.constEnd(); ++it) {
        if (!isTopologyConnectionClassField(it.key())) {
            continue;
        }

        validateTopologyConnectionClassValue(entry,
                                             topologyId,
                                             it.key(),
                                             it.value(),
                                             classIds,
                                             accumulator);
    }
}

QString topologyStringValue(const QJsonObject& object, const QString& key) {
    return object.value(key).toString().trimmed();
}

void validateTopologyModuleReference(const IpCatalogEntry& entry,
                                     const IpcraftPackageManifest& manifest,
                                     const QString& topologyId,
                                     const QString& key,
                                     const QString& moduleId,
                                     ValidationAccumulator& accumulator) {
    if (moduleId.trimmed().isEmpty()) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Topology '%1' %2 module reference must not be empty.")
                .arg(topologyId, key),
            topologyId,
            QStringLiteral("built_in_topology"));
        return;
    }

    if (manifest.module(moduleId) == nullptr) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Topology '%1' %2 references missing module '%3'.")
                .arg(topologyId, key, moduleId),
            topologyId,
            QStringLiteral("built_in_topology"));
    }
}

void validateTopologyInterfaceReference(const IpCatalogEntry& entry,
                                        const IpcraftPackageManifest& manifest,
                                        const QString& topologyId,
                                        const QString& key,
                                        const QString& interfaceId,
                                        const QString& moduleId,
                                        ValidationAccumulator& accumulator) {
    if (interfaceId.trimmed().isEmpty()) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Topology '%1' %2 interface reference must not be empty.")
                .arg(topologyId, key),
            topologyId,
            QStringLiteral("built_in_topology"));
        return;
    }

    if (!moduleId.isEmpty()) {
        if (manifest.interfaceDescriptor(moduleId, interfaceId) == nullptr) {
            accumulator.addPackageError(
                entry,
                QStringLiteral("Topology '%1' %2 references missing interface '%3' on module '%4'.")
                    .arg(topologyId, key, interfaceId, moduleId),
                topologyId,
                QStringLiteral("built_in_topology"));
        }
        return;
    }

    if (!interfaceExistsOnAnyModule(manifest, interfaceId)) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Topology '%1' %2 references missing interface '%3'.")
                .arg(topologyId, key, interfaceId),
            topologyId,
            QStringLiteral("built_in_topology"));
    }
}

void validateTopologyParticipantObject(const IpCatalogEntry& entry,
                                       const IpcraftPackageManifest& manifest,
                                       const QString& topologyId,
                                       const QString& participantKey,
                                       const QJsonObject& participant,
                                       const QString& defaultModuleId,
                                       ValidationAccumulator& accumulator) {
    QString moduleId = topologyStringValue(participant, QStringLiteral("module"));
    if (moduleId.isEmpty()) {
        moduleId = topologyStringValue(participant, QStringLiteral("module_id"));
    }

    if (participant.contains(QStringLiteral("module"))) {
        validateTopologyModuleReference(entry,
                                        manifest,
                                        topologyId,
                                        participantKey + QStringLiteral(".module"),
                                        moduleId,
                                        accumulator);
    } else if (participant.contains(QStringLiteral("module_id"))) {
        validateTopologyModuleReference(entry,
                                        manifest,
                                        topologyId,
                                        participantKey + QStringLiteral(".module_id"),
                                        moduleId,
                                        accumulator);
    }

    const QString moduleContext = moduleId.isEmpty() ? defaultModuleId : moduleId;
    const QStringList interfaceKeys{
        QStringLiteral("interface"),
        QStringLiteral("interface_id"),
        QStringLiteral("interface_ref")
    };
    for (const QString& interfaceKey : interfaceKeys) {
        if (!participant.contains(interfaceKey)) {
            continue;
        }

        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           participantKey + QLatin1Char('.') + interfaceKey,
                                           topologyStringValue(participant, interfaceKey),
                                           moduleContext,
                                           accumulator);
    }
}

void validateTopologyParticipantValue(const IpCatalogEntry& entry,
                                      const IpcraftPackageManifest& manifest,
                                      const QString& topologyId,
                                      const QString& participantKey,
                                      const QJsonValue& value,
                                      const QString& defaultModuleId,
                                      ValidationAccumulator& accumulator) {
    if (value.isObject()) {
        validateTopologyParticipantObject(entry,
                                          manifest,
                                          topologyId,
                                          participantKey,
                                          value.toObject(),
                                          defaultModuleId,
                                          accumulator);
        return;
    }

    if (!value.isArray()) {
        return;
    }

    const QJsonArray participants = value.toArray();
    for (qsizetype i = 0; i < participants.size(); ++i) {
        const QJsonValue participant = participants.at(i);
        if (!participant.isObject()) {
            continue;
        }

        validateTopologyParticipantObject(entry,
                                          manifest,
                                          topologyId,
                                          QStringLiteral("%1[%2]").arg(participantKey).arg(i),
                                          participant.toObject(),
                                          defaultModuleId,
                                          accumulator);
    }
}

void validateTopologyEndpointReferences(const IpCatalogEntry& entry,
                                        const IpcraftPackageManifest& manifest,
                                        const QJsonObject& topology,
                                        const QString& topologyId,
                                        const QString& defaultModuleId,
                                        ValidationAccumulator& accumulator) {
    const QString attachedModule =
        topologyStringValue(topology, QStringLiteral("attached_module"));
    const QString attachmentModule =
        topologyStringValue(topology, QStringLiteral("attachment_module"));
    const QString endpointModule =
        topologyStringValue(topology, QStringLiteral("endpoint_module"));

    if (topology.contains(QStringLiteral("attached_module"))) {
        validateTopologyModuleReference(entry,
                                        manifest,
                                        topologyId,
                                        QStringLiteral("attached_module"),
                                        attachedModule,
                                        accumulator);
    }
    if (topology.contains(QStringLiteral("attachment_module"))) {
        validateTopologyModuleReference(entry,
                                        manifest,
                                        topologyId,
                                        QStringLiteral("attachment_module"),
                                        attachmentModule,
                                        accumulator);
    }
    if (topology.contains(QStringLiteral("endpoint_module"))) {
        validateTopologyModuleReference(entry,
                                        manifest,
                                        topologyId,
                                        QStringLiteral("endpoint_module"),
                                        endpointModule,
                                        accumulator);
    }

    const QString attachedContext =
        !attachedModule.isEmpty() ? attachedModule : attachmentModule;
    const QString endpointContext =
        !endpointModule.isEmpty() ? endpointModule : attachedContext;

    if (topology.contains(QStringLiteral("attached_interface"))) {
        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           QStringLiteral("attached_interface"),
                                           topologyStringValue(topology, QStringLiteral("attached_interface")),
                                           attachedContext.isEmpty() ? defaultModuleId : attachedContext,
                                           accumulator);
    }
    if (topology.contains(QStringLiteral("attachment_interface"))) {
        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           QStringLiteral("attachment_interface"),
                                           topologyStringValue(topology, QStringLiteral("attachment_interface")),
                                           attachedContext.isEmpty() ? defaultModuleId : attachedContext,
                                           accumulator);
    }
    if (topology.contains(QStringLiteral("endpoint_interface"))) {
        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           QStringLiteral("endpoint_interface"),
                                           topologyStringValue(topology, QStringLiteral("endpoint_interface")),
                                           endpointContext.isEmpty() ? defaultModuleId : endpointContext,
                                           accumulator);
    }
    if (topology.contains(QStringLiteral("interface"))) {
        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           QStringLiteral("interface"),
                                           topologyStringValue(topology, QStringLiteral("interface")),
                                           defaultModuleId,
                                           accumulator);
    }
    if (topology.contains(QStringLiteral("interface_id"))) {
        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           QStringLiteral("interface_id"),
                                           topologyStringValue(topology, QStringLiteral("interface_id")),
                                           defaultModuleId,
                                           accumulator);
    }
    if (topology.contains(QStringLiteral("interface_ref"))) {
        validateTopologyInterfaceReference(entry,
                                           manifest,
                                           topologyId,
                                           QStringLiteral("interface_ref"),
                                           topologyStringValue(topology, QStringLiteral("interface_ref")),
                                           defaultModuleId,
                                           accumulator);
    }

    const QStringList participantKeys{
        QStringLiteral("attached"),
        QStringLiteral("attachment"),
        QStringLiteral("attachments"),
        QStringLiteral("endpoint"),
        QStringLiteral("endpoints")
    };
    for (const QString& participantKey : participantKeys) {
        if (!topology.contains(participantKey)) {
            continue;
        }

        validateTopologyParticipantValue(entry,
                                         manifest,
                                         topologyId,
                                         participantKey,
                                         topology.value(participantKey),
                                         defaultModuleId,
                                         accumulator);
    }
}

void validateTopologyReferences(const IpCatalogEntry& entry,
                                ValidationAccumulator& accumulator) {
    const IpcraftPackageManifest& manifest = entry.packageManifest;
    if (manifest.id.trimmed().isEmpty()) {
        return;
    }

    const QSet<QString> classIds = connectionClassIdsForManifest(manifest);
    for (const QJsonObject& topology : manifest.topologies) {
        const QString topologyId =
            topology.value(QStringLiteral("id")).toString(QStringLiteral("topology"));
        const QString moduleId = topology.value(QStringLiteral("module")).toString().trimmed();
        const IpcraftModuleDescriptor* moduleDescriptor = nullptr;
        if (!moduleId.isEmpty() && manifest.module(moduleId) == nullptr) {
            accumulator.addPackageError(
                entry,
                QStringLiteral("Topology '%1' references missing module '%2'.")
                    .arg(topologyId, moduleId),
                topologyId,
                QStringLiteral("built_in_topology"));
            continue;
        }
        if (!moduleId.isEmpty()) {
            moduleDescriptor = manifest.module(moduleId);
        }

        const QString kind = topology.value(QStringLiteral("kind")).toString().trimmed();
        if (moduleDescriptor != nullptr && isRouterTopologyKind(kind)) {
            const QString graphRole = moduleDescriptor->graphRole.trimmed();
            if (!graphRole.isEmpty() && !isRouterCapableGraphRole(graphRole)) {
                accumulator.addPackageError(
                    entry,
                    QStringLiteral("Topology '%1' kind '%2' module '%3' graph_role '%4' is not host/router-capable.")
                        .arg(topologyId, kind, moduleId, graphRole),
                    topologyId,
                    QStringLiteral("built_in_topology"));
            }
        }

        validateTopologyConnectionClassReferences(entry,
                                                  topology,
                                                  topologyId,
                                                  classIds,
                                                  accumulator);
        validateTopologyEndpointReferences(entry,
                                           manifest,
                                           topology,
                                           topologyId,
                                           moduleId,
                                           accumulator);

        const QJsonObject ports = topology.value(QStringLiteral("ports")).toObject();
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            const QString interfaceId = it.value().toString().trimmed();
            if (moduleId.isEmpty() || interfaceId.isEmpty()) {
                continue;
            }
            if (manifest.interfaceDescriptor(moduleId, interfaceId) == nullptr) {
                accumulator.addPackageError(
                    entry,
                    QStringLiteral("Topology '%1' references missing interface '%2' on module '%3'.")
                        .arg(topologyId, interfaceId, moduleId),
                    topologyId,
                    QStringLiteral("built_in_topology"));
            }
        }
    }
}

void validateViewXml(const IpCatalogEntry& entry,
                     const IpcraftViewDescriptor& view,
                     ValidationAccumulator& accumulator) {
    const IpcraftPackageManifest& manifest = entry.packageManifest;
    const IpcraftModuleDescriptor* module = manifest.module(view.moduleId);
    if (module == nullptr) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("View XML references missing module '%1'.").arg(view.moduleId),
            view.filePath,
            QStringLiteral("built_in_view"));
        return;
    }

    QFile file(view.resolvedFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Could not open view XML '%1'.").arg(view.filePath),
            view.filePath,
            QStringLiteral("built_in_view"));
        return;
    }

    const QSet<QString> interfaceIds = interfaceIdsForModule(*module);
    const QSet<QString> attachmentZones = attachmentZonesForModule(manifest, view.moduleId);
    QXmlStreamReader xml(&file);
    bool sawRoot = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        if (!sawRoot) {
            sawRoot = true;
            if (xml.name() != QStringLiteral("module-view")) {
                accumulator.addPackageError(entry,
                                            QStringLiteral("View XML root must be module-view."),
                                            view.filePath,
                                            QStringLiteral("built_in_view"));
                return;
            }

            const QString xmlModule =
                xml.attributes().value(QStringLiteral("module")).toString().trimmed();
            if (xmlModule != view.moduleId) {
                accumulator.addPackageError(
                    entry,
                    QStringLiteral("View XML module '%1' does not match manifest module '%2'.")
                        .arg(xmlModule, view.moduleId),
                    view.filePath,
                    QStringLiteral("built_in_view"));
                return;
            }
            continue;
        }

        const QString elementName = xml.name().toString();
        const QXmlStreamAttributes attributes = xml.attributes();
        for (const QXmlStreamAttribute& attribute : attributes) {
            const QString attributeName = attribute.name().toString();
            const QString attributeValue = attribute.value().toString().trimmed();

            if (isInterfaceReferenceAttribute(elementName, attributeName)
                && !interfaceIds.contains(attributeValue)) {
                const bool isAnchorRef =
                    elementName == QStringLiteral("anchor")
                    && attributeName == QStringLiteral("ref");
                const QString message = isAnchorRef
                    ? QStringLiteral("View XML anchor references missing interface '%1' on module '%2'.")
                        .arg(attributeValue, view.moduleId)
                    : QStringLiteral("View XML interface reference attribute '%1' references missing interface '%2' on module '%3'.")
                        .arg(attributeName, attributeValue, view.moduleId);
                accumulator.addPackageError(
                    entry,
                    message,
                    view.filePath,
                    QStringLiteral("built_in_view"));
                return;
            }

            if (isAttachmentZoneReferenceAttribute(elementName, attributeName)) {
                if (attributeValue.isEmpty()) {
                    accumulator.addPackageError(
                        entry,
                        QStringLiteral("View XML attachment zone attribute '%1' must not be empty on module '%2'.")
                            .arg(attributeName, view.moduleId),
                        view.filePath,
                        QStringLiteral("built_in_view"));
                    return;
                }
                if (!attachmentZones.contains(attributeValue)) {
                    accumulator.addPackageError(
                        entry,
                        QStringLiteral("View XML attachment zone attribute '%1' references missing attachment zone '%2' on module '%3'.")
                            .arg(attributeName, attributeValue, view.moduleId),
                        view.filePath,
                        QStringLiteral("built_in_view"));
                    return;
                }
            }
        }
    }

    if (xml.hasError()) {
        accumulator.addPackageError(
            entry,
            QStringLiteral("Invalid view XML '%1': %2.").arg(view.filePath, xml.errorString()),
            view.filePath,
            QStringLiteral("built_in_view"));
        return;
    }

    if (!sawRoot) {
        accumulator.addPackageError(entry,
                                    QStringLiteral("View XML is empty."),
                                    view.filePath,
                                    QStringLiteral("built_in_view"));
    }
}

void validateViews(const IpCatalogEntry& entry,
                   ValidationAccumulator& accumulator) {
    if (entry.packageManifest.id.trimmed().isEmpty()) {
        return;
    }

    for (const IpcraftViewDescriptor& view : entry.packageManifest.views) {
        validateViewXml(entry, view, accumulator);
    }
}

void validateInstancesReferenceCatalogEntries(const QList<IpCatalogEntry>& entries,
                                              const QVector<ProjectIpInstanceRecord>& instances,
                                              ValidationAccumulator& accumulator) {
    for (const ProjectIpInstanceRecord& instance : instances) {
        if (findEntry(entries, instance.ipcoreId) == nullptr) {
            accumulator.addInstanceError(
                instance.instanceId,
                QStringLiteral("IP instance '%1' references missing package/catalog entry '%2'.")
                    .arg(instance.instanceId, instance.ipcoreId),
                instance.instanceId,
                QStringLiteral("built_in_package"));
        }
    }
}

void validateModuleOwnershipAndTypes(const Graph* graph,
                                     const QList<IpCatalogEntry>& entries,
                                     const QHash<QString, const ProjectIpInstanceRecord*>& instancesByKey,
                                     ValidationAccumulator& accumulator) {
    if (!graph) {
        return;
    }

    for (const std::unique_ptr<Module>& modulePtr : graph->modules()) {
        const Module& module = *modulePtr;
        const bool hasIpOwner = !module.ipcoreId().trimmed().isEmpty();
        const bool hasInstanceOwner = !module.instanceId().trimmed().isEmpty();
        if (!hasIpOwner && !hasInstanceOwner) {
            continue;
        }
        if (!hasIpOwner || !hasInstanceOwner) {
            accumulator.addModuleError(
                module,
                QStringLiteral("Module '%1' has incomplete package ownership metadata.").arg(module.id()),
                QStringLiteral("built_in_ownership"));
            continue;
        }

        if (!instancesByKey.contains(instanceKey(module.ipcoreId(), module.instanceId()))) {
            accumulator.addModuleError(
                module,
                QStringLiteral("Module '%1' references missing project IP instance '%2' for package '%3'.")
                    .arg(module.id(), module.instanceId(), module.ipcoreId()),
                QStringLiteral("built_in_ownership"));
            continue;
        }

        const IpCatalogEntry* entry = findEntry(entries, module.ipcoreId());
        if (entry == nullptr) {
            accumulator.addModuleError(
                module,
                QStringLiteral("Module '%1' references missing package/catalog entry '%2'.")
                    .arg(module.id(), module.ipcoreId()),
                QStringLiteral("built_in_package"));
            continue;
        }

        const bool hasManifestModules = !entry->packageManifest.modules.isEmpty();
        const bool hasCatalogTypes = !entry->moduleTypes.isEmpty();
        const QString manifestModuleId = moduleManifestId(*entry, module);
        const IpcraftModuleDescriptor* manifestModule = manifestModuleId.isEmpty()
            ? nullptr
            : entry->packageManifest.module(manifestModuleId);
        const bool typeInCatalog = entry->moduleTypes.contains(module.type());
        if ((hasManifestModules && manifestModule == nullptr)
            || (!hasManifestModules && hasCatalogTypes && !typeInCatalog)) {
            accumulator.addModuleError(
                module,
                QStringLiteral("Module '%1' type '%2' is not declared by package '%3'.")
                    .arg(module.id(), module.type(), module.ipcoreId()),
                QStringLiteral("built_in_module"));
            continue;
        }

        if (manifestModule == nullptr || manifestModule->interfaces.isEmpty()) {
            continue;
        }

        for (const Port& port : module.ports()) {
            const QString interfaceId = interfaceIdForPort(port);
            if (manifestModule->interfaceDescriptor(interfaceId) == nullptr) {
                accumulator.addModuleError(
                    module,
                    QStringLiteral("Module '%1' port '%2' references missing manifest interface '%3'.")
                        .arg(module.id(), port.id(), interfaceId),
                    QStringLiteral("built_in_interface"));
            }
        }
    }
}

struct ParticipantBuildResult {
    bool ok = false;
    bool hasClassMetadata = false;
    IpcraftConnectionParticipant participant;
};

ParticipantBuildResult participantFromInterface(const Graph* graph,
                                                const QList<IpCatalogEntry>& entries,
                                                const Connection& connection,
                                                const QString& moduleId,
                                                const QString& interfaceId,
                                                ValidationAccumulator& accumulator) {
    ParticipantBuildResult result;
    const Module* module = findModule(graph, moduleId);
    if (module == nullptr) {
        accumulator.addConnectionError(
            graph,
            connection,
            QStringLiteral("Connection '%1' references missing module '%2'.")
                .arg(connection.id(), moduleId));
        return result;
    }

    const IpCatalogEntry* entry = findEntry(entries, module->ipcoreId());
    if (entry == nullptr || entry->packageManifest.id.trimmed().isEmpty()) {
        return result;
    }

    const QString manifestModuleId = moduleManifestId(*entry, *module);
    if (manifestModuleId.isEmpty()) {
        accumulator.addConnectionError(
            graph,
            connection,
            QStringLiteral("Connection '%1' references module '%2' with missing manifest module type '%3'.")
                .arg(connection.id(), moduleId, module->type()));
        return result;
    }

    const IpcraftInterfaceDescriptor* interfaceDescriptor =
        entry->packageManifest.interfaceDescriptor(manifestModuleId, interfaceId);
    if (interfaceDescriptor == nullptr) {
        accumulator.addConnectionError(
            graph,
            connection,
            QStringLiteral("Connection '%1' references missing interface '%2' on module '%3'.")
                .arg(connection.id(), interfaceId, moduleId));
        return result;
    }

    result.ok = true;
    result.hasClassMetadata = !interfaceDescriptor->accepts.isEmpty()
                              && !entry->packageManifest.connectionClasses.isEmpty();
    result.participant.packageId = manifestPackageId(*entry);
    result.participant.moduleId = manifestModuleId;
    result.participant.interfaceRef = ProjectConnectionInterfaceRef{moduleId, interfaceId};
    return result;
}

QVector<ParticipantBuildResult> participantsForConnection(const Graph* graph,
                                                          const QList<IpCatalogEntry>& entries,
                                                          const Connection& connection,
                                                          ValidationAccumulator& accumulator) {
    QVector<ParticipantBuildResult> participants;
    if (!connection.interfaces().isEmpty()) {
        if (connection.interfaces().size() != 2) {
            accumulator.addConnectionError(
                graph,
                connection,
                QStringLiteral("Connection '%1' requires exactly two interface participants.")
                    .arg(connection.id()));
            return {};
        }
        for (const ConnectionInterfaceRef& interfaceRef : connection.interfaces()) {
            participants.push_back(participantFromInterface(graph,
                                                           entries,
                                                           connection,
                                                           interfaceRef.instanceId,
                                                           interfaceRef.interfaceId,
                                                           accumulator));
        }
        return participants;
    }

    const Module* sourceModule = findModule(graph, connection.source().moduleId);
    const Module* targetModule = findModule(graph, connection.target().moduleId);
    const Port* sourcePort = findPort(sourceModule, connection.source().portId);
    const Port* targetPort = findPort(targetModule, connection.target().portId);
    if (!sourceModule || !targetModule || !sourcePort || !targetPort) {
        accumulator.addConnectionError(
            graph,
            connection,
            QStringLiteral("Connection '%1' references missing modules or ports.")
                .arg(connection.id()));
        return {};
    }

    participants.push_back(participantFromInterface(graph,
                                                   entries,
                                                   connection,
                                                   sourceModule->id(),
                                                   interfaceIdForPort(*sourcePort),
                                                   accumulator));
    participants.push_back(participantFromInterface(graph,
                                                   entries,
                                                   connection,
                                                   targetModule->id(),
                                                   interfaceIdForPort(*targetPort),
                                                   accumulator));
    return participants;
}

void validateConnections(const Graph* graph,
                         const QList<IpCatalogEntry>& entries,
                         ValidationAccumulator& accumulator) {
    if (!graph) {
        return;
    }

    const QVector<IpcraftPackageManifest> manifests = packageManifests(entries);
    if (manifests.isEmpty()) {
        return;
    }

    const QVector<ProjectConnectionRecord> currentConnections = currentProjectConnections(graph);
    IpcraftConnectionValidator validator(manifests, currentConnections);
    for (const std::unique_ptr<Connection>& connectionPtr : graph->connections()) {
        const Connection& connection = *connectionPtr;
        const QVector<ParticipantBuildResult> participantResults =
            participantsForConnection(graph, entries, connection, accumulator);
        if (participantResults.size() != 2) {
            continue;
        }
        if (!std::all_of(participantResults.cbegin(),
                         participantResults.cend(),
                         [](const ParticipantBuildResult& result) { return result.ok; })) {
            continue;
        }
        if (!std::any_of(participantResults.cbegin(),
                         participantResults.cend(),
                         [](const ParticipantBuildResult& result) {
                             return result.hasClassMetadata;
                         })) {
            continue;
        }

        const IpcraftConnectionDecision decision =
            validator.validate({participantResults.at(0).participant,
                                participantResults.at(1).participant},
                               connection.connectionClassId(),
                               connection.id());
        if (decision.status == IpcraftConnectionStatus::Invalid) {
            accumulator.addConnectionError(
                graph,
                connection,
                QStringLiteral("Connection '%1' failed built-in interface validation: %2")
                    .arg(connection.id(), decision.message));
        }
    }
}

std::optional<IpxactConnection> ipxactConnectionForEntry(const Graph* graph,
                                                         const IpCatalogEntry& entry,
                                                         const Connection& connection) {
    IpxactConnection ipxactConnection;
    ipxactConnection.id = connection.id();
    const QVector<ConnectionInterfaceRef> interfaces =
        effectiveConnectionInterfaces(graph, connection);
    if (interfaces.size() != 2) {
        return std::nullopt;
    }

    for (const ConnectionInterfaceRef& interfaceRef : interfaces) {
        const Module* module = findModule(graph, interfaceRef.instanceId);
        if (module == nullptr || !entryMatchesIpcore(entry, module->ipcoreId())) {
            return std::nullopt;
        }

        const QString manifestModuleId = moduleManifestId(entry, *module);
        if (manifestModuleId.isEmpty()) {
            return std::nullopt;
        }
        if (entry.packageManifest.interfaceDescriptor(manifestModuleId,
                                                      interfaceRef.interfaceId) == nullptr) {
            return std::nullopt;
        }

        ipxactConnection.participants.push_back(IpxactConnectionParticipant{
            module->id(),
            manifestModuleId,
            manifestModuleId,
            interfaceRef.interfaceId
        });
    }

    return ipxactConnection;
}

void validateIpxactConnections(const Graph* graph,
                               const QList<IpCatalogEntry>& entries,
                               ValidationAccumulator& accumulator) {
    if (!graph) {
        return;
    }

    const QSet<QString> referencedPackageIds =
        collectReferencedPackageIds(accumulator.instances);
    IpxactConnectionChecker checker;
    for (const IpCatalogEntry& entry : entries) {
        if (!entryMatchesReferencedPackage(entry, referencedPackageIds)) {
            continue;
        }
        if (!entry.packageManifest.ipxact.has_value()) {
            continue;
        }

        QVector<IpxactConnection> connections;
        for (const std::unique_ptr<Connection>& connection : graph->connections()) {
            const std::optional<IpxactConnection> ipxactConnection =
                ipxactConnectionForEntry(graph, entry, *connection);
            if (ipxactConnection.has_value()) {
                connections.push_back(*ipxactConnection);
            }
        }

        const IpxactConnectionCheckResult checkResult =
            checker.check(entry.packageManifest, connections);
        for (const IpxactConnectionDiagnostic& diagnostic : checkResult.diagnostics) {
            if (diagnostic.connectionId.trimmed().isEmpty()) {
                accumulator.addPackageError(entry,
                                            diagnostic.message,
                                            manifestPackageId(entry),
                                            QStringLiteral("built_in_ipxact_connection"));
                continue;
            }

            accumulator.addIpxactConnectionError(graph,
                                                 diagnostic.connectionId,
                                                 diagnostic.message);
        }
    }
}

void validatePackageMetadata(const QList<IpCatalogEntry>& entries,
                             IpcraftBuiltInValidator::CommandPurpose commandPurpose,
                             ValidationAccumulator& accumulator) {
    const QSet<QString> referencedPackageIds =
        collectReferencedPackageIds(accumulator.instances);

    for (const IpCatalogEntry& entry : entries) {
        if (!entryMatchesReferencedPackage(entry, referencedPackageIds)) {
            continue;
        }
        validateManifestReferences(entry, accumulator);
        validateTopologyReferences(entry, accumulator);
        validateViews(entry, accumulator);
        validateCommand(entry, commandPurpose, accumulator);
    }
}

} // namespace

bool IpcraftBuiltInValidator::Result::hasErrors() const {
    for (const ValidationResult& diagnostic : diagnostics) {
        if (diagnostic.severity() == ValidationSeverity::Error) {
            return true;
        }
    }
    return false;
}

IpcraftBuiltInValidator::Result IpcraftBuiltInValidator::validate(
    const Graph* graph,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances,
    CommandPurpose commandPurpose) const {
    ValidationAccumulator accumulator;
    accumulator.instances = instances;

    if (!graph) {
        accumulator.addGlobalError(QStringLiteral("Graph is not available."),
                                   QString(),
                                   QStringLiteral("built_in_graph"));
        return accumulator.result;
    }

    validateInstancesReferenceCatalogEntries(entries, instances, accumulator);
    validatePackageMetadata(entries, commandPurpose, accumulator);
    validateModuleOwnershipAndTypes(graph,
                                    entries,
                                    instanceRecordsByKey(instances),
                                    accumulator);
    validateConnections(graph, entries, accumulator);
    validateIpxactConnections(graph, entries, accumulator);

    return accumulator.result;
}
