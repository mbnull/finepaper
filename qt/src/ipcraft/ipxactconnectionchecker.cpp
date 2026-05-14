#include "ipcraft/ipxactconnectionchecker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <algorithm>

namespace {

struct IpxactBusInterface {
    QString name;
    QString mode;
    QString busType;
    QString abstractionType;
};

struct IpxactComponent {
    QString id;
    QHash<QString, IpxactBusInterface> busInterfaces;
};

struct ParsedIpxact {
    QHash<QString, IpxactComponent> components;
    QHash<QString, QString> instanceComponentRefs;
    QHash<QString, IpxactBusInterface> documentBusInterfaces;
    QString error;
};

struct ResolvedParticipant {
    IpxactConnectionParticipant participant;
    QString busInterfaceName;
    QStringList expectedModes;
    IpxactBusInterface ipxactInterface;
};

QString localName(const QXmlStreamReader& xml) {
    return xml.name().toString();
}

QString trimmedText(QXmlStreamReader& xml) {
    return xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
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

QString identityFromAttributes(const QXmlStreamAttributes& attributes) {
    const QString name = attributes.value(QStringLiteral("name")).toString().trimmed();
    const QString vendor = attributes.value(QStringLiteral("vendor")).toString().trimmed();
    const QString library = attributes.value(QStringLiteral("library")).toString().trimmed();
    const QString version = attributes.value(QStringLiteral("version")).toString().trimmed();
    if (vendor.isEmpty() && library.isEmpty() && version.isEmpty()) {
        return name;
    }

    return QStringList{vendor, library, name, version}.join(QLatin1Char(':'));
}

QString componentRefFromAttributes(const QXmlStreamAttributes& attributes) {
    const QString name = attributes.value(QStringLiteral("name")).toString().trimmed();
    if (!name.isEmpty()) {
        return name;
    }
    return identityFromAttributes(attributes);
}

void addUnique(QStringList& values, const QString& value) {
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !values.contains(trimmed)) {
        values.append(trimmed);
    }
}

void parseAbstractionRefContainer(QXmlStreamReader& xml, IpxactBusInterface& busInterface) {
    while (xml.readNextStartElement()) {
        if (localName(xml) == QStringLiteral("abstractionRef")) {
            if (busInterface.abstractionType.isEmpty()) {
                busInterface.abstractionType = identityFromAttributes(xml.attributes());
            }
            xml.skipCurrentElement();
            continue;
        }

        parseAbstractionRefContainer(xml, busInterface);
    }
}

IpxactBusInterface parseBusInterface(QXmlStreamReader& xml) {
    IpxactBusInterface busInterface;
    while (xml.readNextStartElement()) {
        const QString name = localName(xml);
        if (name == QStringLiteral("name") && busInterface.name.isEmpty()) {
            busInterface.name = trimmedText(xml);
            continue;
        }
        if (name == QStringLiteral("busType")) {
            busInterface.busType = identityFromAttributes(xml.attributes());
            xml.skipCurrentElement();
            continue;
        }
        if (name == QStringLiteral("abstractionTypes")
            || name == QStringLiteral("abstractionType")) {
            parseAbstractionRefContainer(xml, busInterface);
            continue;
        }
        if (name == QStringLiteral("abstractionRef")) {
            if (busInterface.abstractionType.isEmpty()) {
                busInterface.abstractionType = identityFromAttributes(xml.attributes());
            }
            xml.skipCurrentElement();
            continue;
        }
        if (isNativeIpxactMode(name)) {
            busInterface.mode = name;
            xml.skipCurrentElement();
            continue;
        }

        xml.skipCurrentElement();
    }
    return busInterface;
}

void parseBusInterfaces(QXmlStreamReader& xml,
                        QHash<QString, IpxactBusInterface>& busInterfaces) {
    while (xml.readNextStartElement()) {
        if (localName(xml) != QStringLiteral("busInterface")) {
            xml.skipCurrentElement();
            continue;
        }

        const IpxactBusInterface busInterface = parseBusInterface(xml);
        if (!busInterface.name.isEmpty()) {
            busInterfaces.insert(busInterface.name, busInterface);
        }
    }
}

void parseComponentInstance(QXmlStreamReader& xml, ParsedIpxact& parsed) {
    QString instanceName;
    QString componentRef;
    while (xml.readNextStartElement()) {
        const QString name = localName(xml);
        if (name == QStringLiteral("instanceName")) {
            instanceName = trimmedText(xml);
            continue;
        }
        if (name == QStringLiteral("componentRef")) {
            componentRef = componentRefFromAttributes(xml.attributes());
            if (componentRef.isEmpty()) {
                componentRef = trimmedText(xml);
            } else {
                xml.skipCurrentElement();
            }
            continue;
        }

        xml.skipCurrentElement();
    }

    if (!instanceName.isEmpty() && !componentRef.isEmpty()) {
        parsed.instanceComponentRefs.insert(instanceName, componentRef);
    }
}

void parseComponentInstances(QXmlStreamReader& xml, ParsedIpxact& parsed) {
    while (xml.readNextStartElement()) {
        if (localName(xml) == QStringLiteral("componentInstance")) {
            parseComponentInstance(xml, parsed);
        } else {
            xml.skipCurrentElement();
        }
    }
}

IpxactComponent parseComponent(QXmlStreamReader& xml, ParsedIpxact& parsed);

void parseComponentDefinitions(QXmlStreamReader& xml, ParsedIpxact& parsed) {
    while (xml.readNextStartElement()) {
        const QString name = localName(xml);
        if (name == QStringLiteral("component") || name == QStringLiteral("componentDefinition")) {
            const IpxactComponent component = parseComponent(xml, parsed);
            if (!component.id.isEmpty()) {
                parsed.components.insert(component.id, component);
            }
            continue;
        }

        xml.skipCurrentElement();
    }
}

IpxactComponent parseComponent(QXmlStreamReader& xml, ParsedIpxact& parsed) {
    IpxactComponent component;
    component.id = xml.attributes().value(QStringLiteral("name")).toString().trimmed();

    while (xml.readNextStartElement()) {
        const QString name = localName(xml);
        if (name == QStringLiteral("name") && component.id.isEmpty()) {
            component.id = trimmedText(xml);
            continue;
        }
        if (name == QStringLiteral("busInterfaces")) {
            parseBusInterfaces(xml, component.busInterfaces);
            continue;
        }
        if (name == QStringLiteral("componentInstances")) {
            parseComponentInstances(xml, parsed);
            continue;
        }
        if (name == QStringLiteral("componentDefinitions")) {
            parseComponentDefinitions(xml, parsed);
            continue;
        }

        xml.skipCurrentElement();
    }

    return component;
}

void parseRootContainer(QXmlStreamReader& xml, ParsedIpxact& parsed) {
    while (xml.readNextStartElement()) {
        const QString name = localName(xml);
        if (name == QStringLiteral("componentInstances")) {
            parseComponentInstances(xml, parsed);
            continue;
        }
        if (name == QStringLiteral("componentDefinitions")) {
            parseComponentDefinitions(xml, parsed);
            continue;
        }
        if (name == QStringLiteral("busInterfaces")) {
            parseBusInterfaces(xml, parsed.documentBusInterfaces);
            continue;
        }
        if (name == QStringLiteral("component") || name == QStringLiteral("componentDefinition")) {
            const IpxactComponent component = parseComponent(xml, parsed);
            if (!component.id.isEmpty()) {
                parsed.components.insert(component.id, component);
            }
            continue;
        }

        xml.skipCurrentElement();
    }
}

ParsedIpxact parseIpxactFile(const QString& path) {
    ParsedIpxact parsed;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        parsed.error = QStringLiteral("Could not open IP-XACT root '%1'.").arg(path);
        return parsed;
    }

    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement()) {
        parsed.error = QStringLiteral("IP-XACT root '%1' is empty.").arg(path);
        return parsed;
    }

    const QString rootName = localName(xml);
    if (rootName == QStringLiteral("component")
        || rootName == QStringLiteral("componentDefinition")) {
        const IpxactComponent component = parseComponent(xml, parsed);
        if (!component.id.isEmpty()) {
            parsed.components.insert(component.id, component);
        }
    } else {
        parseRootContainer(xml, parsed);
    }

    if (xml.hasError() && parsed.error.isEmpty()) {
        parsed.error = QStringLiteral("Invalid IP-XACT XML '%1': %2.")
                           .arg(path, xml.errorString());
    }
    return parsed;
}

QString ipxactRootPath(const IpcraftPackageManifest& manifest) {
    if (!manifest.ipxact.has_value()) {
        return {};
    }

    QString path = manifest.ipxact->resolvedRootPath.trimmed();
    if (path.isEmpty()) {
        path = manifest.ipxact->rootPath.trimmed();
    }
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    if (info.isAbsolute() || manifest.packageRootPath.trimmed().isEmpty()) {
        return path;
    }
    return QDir(manifest.packageRootPath).filePath(path);
}

QString extensionModeMapping(const IpcraftPackageManifest& manifest,
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
            return mappedMode;
        }
    }

    return {};
}

QString explicitModeMapping(const IpcraftInterfaceDescriptor& interfaceDescriptor,
                            const QString& mode) {
    const QJsonObject modes = interfaceDescriptor.ipxact.value(QStringLiteral("modes")).toObject();
    const QJsonObject modeConfig = modes.value(mode.trimmed()).toObject();
    const QString specificMode = modeConfig.value(QStringLiteral("mode")).toString().trimmed();
    if (isNativeIpxactMode(specificMode)) {
        return specificMode;
    }

    const QString directMode =
        interfaceDescriptor.ipxact.value(QStringLiteral("mode")).toString().trimmed();
    if (isNativeIpxactMode(directMode)) {
        return directMode;
    }

    return {};
}

QStringList expectedIpxactModes(const IpcraftPackageManifest& manifest,
                                const IpcraftInterfaceDescriptor& interfaceDescriptor) {
    QStringList expectedModes;
    for (const QString& mode : interfaceDescriptor.modes) {
        const QString trimmedMode = mode.trimmed();
        const QString explicitMode = explicitModeMapping(interfaceDescriptor, trimmedMode);
        if (!explicitMode.isEmpty()) {
            addUnique(expectedModes, explicitMode);
            continue;
        }
        if (isNativeIpxactMode(trimmedMode)) {
            addUnique(expectedModes, trimmedMode);
            continue;
        }

        addUnique(expectedModes, extensionModeMapping(manifest, trimmedMode));
    }
    return expectedModes;
}

const IpxactBusInterface* findBusInterface(const ParsedIpxact& parsed,
                                           const IpxactConnectionParticipant& participant,
                                           const QString& busInterfaceName) {
    if (!parsed.instanceComponentRefs.isEmpty()) {
        const QString componentKey = parsed.instanceComponentRefs.value(participant.instanceId.trimmed());
        const auto componentIt = parsed.components.constFind(componentKey);
        if (componentIt == parsed.components.constEnd()) {
            return nullptr;
        }

        const auto interfaceIt = componentIt->busInterfaces.constFind(busInterfaceName);
        if (interfaceIt != componentIt->busInterfaces.constEnd()) {
            return &interfaceIt.value();
        }
        return nullptr;
    }

    const auto documentIt = parsed.documentBusInterfaces.constFind(busInterfaceName);
    if (documentIt != parsed.documentBusInterfaces.constEnd()) {
        return &documentIt.value();
    }

    if (parsed.components.size() == 1) {
        const IpxactComponent& component = parsed.components.constBegin().value();
        const auto interfaceIt = component.busInterfaces.constFind(busInterfaceName);
        if (interfaceIt != component.busInterfaces.constEnd()) {
            return &interfaceIt.value();
        }
    }

    return nullptr;
}

QString modeList(const QStringList& modes) {
    return modes.join(QStringLiteral(", "));
}

QString participantInstanceLabel(const IpxactConnectionParticipant& participant) {
    const QString instanceId = participant.instanceId.trimmed();
    return instanceId.isEmpty() ? QStringLiteral("<empty>") : instanceId;
}

QString resolvedModeList(const QVector<ResolvedParticipant>& participants) {
    QStringList modes;
    modes.reserve(participants.size());
    for (const ResolvedParticipant& participant : participants) {
        modes.append(participant.ipxactInterface.mode);
    }
    return modeList(modes);
}

bool isAllMode(const QVector<ResolvedParticipant>& participants, const QString& mode) {
    return std::all_of(participants.constBegin(),
                       participants.constEnd(),
                       [&](const ResolvedParticipant& participant) {
                           return participant.ipxactInterface.mode.trimmed() == mode;
                       });
}

bool hasSingleInitiatorGroup(const QVector<ResolvedParticipant>& participants,
                             const QString& initiatorMode,
                             const QString& targetMode) {
    int initiators = 0;
    int targets = 0;
    for (const ResolvedParticipant& participant : participants) {
        const QString mode = participant.ipxactInterface.mode.trimmed();
        if (mode == initiatorMode) {
            ++initiators;
        } else if (mode == targetMode) {
            ++targets;
        } else {
            return false;
        }
    }

    return initiators == 1 && targets == participants.size() - 1;
}

bool interconnectionModesAreCompatible(const QVector<ResolvedParticipant>& participants) {
    return hasSingleInitiatorGroup(participants,
                                   QStringLiteral("initiator"),
                                   QStringLiteral("target"))
           || hasSingleInitiatorGroup(participants,
                                      QStringLiteral("mirroredInitiator"),
                                      QStringLiteral("mirroredTarget"))
           || isAllMode(participants, QStringLiteral("system"))
           || isAllMode(participants, QStringLiteral("mirroredSystem"));
}

void addDiagnostic(IpxactConnectionCheckResult& result,
                   const QString& connectionId,
                   const QString& message) {
    result.diagnostics.append(IpxactConnectionDiagnostic{connectionId, message});
}

bool identitiesConflict(const QString& lhs, const QString& rhs) {
    return !lhs.trimmed().isEmpty()
           && !rhs.trimmed().isEmpty()
           && lhs.trimmed() != rhs.trimmed();
}

bool identityGroupConflicts(const QVector<ResolvedParticipant>& participants,
                            bool useAbstractionType,
                            QString* expected,
                            QString* actual) {
    QString firstDeclared;
    for (const ResolvedParticipant& participant : participants) {
        const QString value = useAbstractionType
            ? participant.ipxactInterface.abstractionType.trimmed()
            : participant.ipxactInterface.busType.trimmed();
        if (value.isEmpty()) {
            continue;
        }

        if (firstDeclared.isEmpty()) {
            firstDeclared = value;
            continue;
        }
        if (identitiesConflict(firstDeclared, value)) {
            if (expected != nullptr) {
                *expected = firstDeclared;
            }
            if (actual != nullptr) {
                *actual = value;
            }
            return true;
        }
    }

    return false;
}

} // namespace

IpxactConnectionCheckResult IpxactConnectionChecker::check(
    const IpcraftPackageManifest& manifest,
    const QVector<IpxactConnection>& connections) const {
    IpxactConnectionCheckResult result;
    const QString rootPath = ipxactRootPath(manifest);
    if (rootPath.isEmpty()) {
        return result;
    }

    const ParsedIpxact parsed = parseIpxactFile(rootPath);
    if (!parsed.error.isEmpty()) {
        addDiagnostic(result, QString(), parsed.error);
        return result;
    }

    for (const IpxactConnection& connection : connections) {
        if (connection.participants.size() < 2) {
            addDiagnostic(result,
                          connection.id,
                          QStringLiteral("Connection '%1' requires at least two IP-XACT participants.")
                              .arg(connection.id));
            continue;
        }

        QVector<ResolvedParticipant> resolvedParticipants;
        bool connectionHasError = false;
        for (const IpxactConnectionParticipant& participant : connection.participants) {
            const IpcraftModuleDescriptor* module = manifest.module(participant.moduleId);
            if (module == nullptr) {
                addDiagnostic(result,
                              connection.id,
                              QStringLiteral("Connection '%1' references missing manifest module '%2'.")
                                  .arg(connection.id, participant.moduleId));
                connectionHasError = true;
                break;
            }

            const IpcraftInterfaceDescriptor* interfaceDescriptor =
                module->interfaceDescriptor(participant.interfaceId);
            if (interfaceDescriptor == nullptr) {
                addDiagnostic(result,
                              connection.id,
                              QStringLiteral("Connection '%1' references missing interface '%2' on module '%3'.")
                                  .arg(connection.id, participant.interfaceId, participant.moduleId));
                connectionHasError = true;
                break;
            }

            const QString busInterfaceName = interfaceDescriptor->ipxactBusInterface.trimmed();
            if (busInterfaceName.isEmpty()) {
                addDiagnostic(result,
                              connection.id,
                              QStringLiteral("Connection '%1' participant '%2.%3' has no ipxact.bus_interface mapping.")
                                  .arg(connection.id,
                                       participant.instanceId,
                                       participant.interfaceId));
                connectionHasError = true;
                break;
            }

            if (!parsed.instanceComponentRefs.isEmpty()) {
                const QString instanceId = participant.instanceId.trimmed();
                if (instanceId.isEmpty() || !parsed.instanceComponentRefs.contains(instanceId)) {
                    addDiagnostic(result,
                                  connection.id,
                                  QStringLiteral("Connection '%1' participant '%2.%3' references missing component instance '%2'.")
                                      .arg(connection.id,
                                           participantInstanceLabel(participant),
                                           participant.interfaceId));
                    connectionHasError = true;
                    break;
                }
            }

            const IpxactBusInterface* busInterface =
                findBusInterface(parsed, participant, busInterfaceName);
            if (busInterface == nullptr) {
                addDiagnostic(result,
                              connection.id,
                              QStringLiteral("Connection '%1' participant '%2.%3' references missing IP-XACT bus interface '%4'.")
                                  .arg(connection.id,
                                       participant.instanceId,
                                       participant.interfaceId,
                                       busInterfaceName));
                connectionHasError = true;
                break;
            }

            const QStringList expectedModes = expectedIpxactModes(manifest, *interfaceDescriptor);
            if (busInterface->mode.trimmed().isEmpty()) {
                addDiagnostic(result,
                              connection.id,
                              QStringLiteral("Connection '%1' IP-XACT bus interface '%2' does not declare a mode.")
                                  .arg(connection.id, busInterfaceName));
                connectionHasError = true;
                break;
            }

            if (!expectedModes.isEmpty() && !expectedModes.contains(busInterface->mode)) {
                addDiagnostic(result,
                              connection.id,
                              QStringLiteral("Connection '%1' IP-XACT bus interface '%2' mode '%3' does not match manifest modes [%4].")
                                  .arg(connection.id,
                                       busInterfaceName,
                                       busInterface->mode,
                                       modeList(expectedModes)));
                connectionHasError = true;
                break;
            }

            resolvedParticipants.push_back(ResolvedParticipant{participant,
                                                               busInterfaceName,
                                                               expectedModes,
                                                               *busInterface});
        }

        if (connectionHasError) {
            continue;
        }

        QString expectedIdentity;
        QString actualIdentity;
        if (identityGroupConflicts(resolvedParticipants, false, &expectedIdentity, &actualIdentity)) {
            addDiagnostic(result,
                          connection.id,
                          QStringLiteral("Connection '%1' IP-XACT bus types are incompatible: '%2' vs '%3'.")
                              .arg(connection.id, expectedIdentity, actualIdentity));
            continue;
        }
        if (identityGroupConflicts(resolvedParticipants, true, &expectedIdentity, &actualIdentity)) {
            addDiagnostic(result,
                          connection.id,
                          QStringLiteral("Connection '%1' IP-XACT abstraction types are incompatible: '%2' vs '%3'.")
                              .arg(connection.id, expectedIdentity, actualIdentity));
            continue;
        }
        if (!interconnectionModesAreCompatible(resolvedParticipants)) {
            addDiagnostic(result,
                          connection.id,
                          QStringLiteral("Connection '%1' has incompatible IP-XACT modes [%2].")
                              .arg(connection.id, resolvedModeList(resolvedParticipants)));
        }
    }

    return result;
}
