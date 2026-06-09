// ModuleProvider loaders parse XML/JSON bundles into normalized ModuleType definitions.
#include "modules/moduleprovider.h"
#include "app/appsettings.h"
#include "ipcraft/ipcraftregistry.h"
#include "modules/modulelabels.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QXmlStreamReader>
#include <utility>

namespace {

struct ParameterLoadResult {
    QVector<ModuleConfigField> autoConfigFields;
    QHash<QString, ModuleConfigField> fieldByName;
};

bool parseBoolString(QStringView value, bool fallbackValue = false) {
    if (value.compare(u"true", Qt::CaseInsensitive) == 0 ||
        value.compare(u"1", Qt::CaseInsensitive) == 0 ||
        value.compare(u"yes", Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (value.compare(u"false", Qt::CaseInsensitive) == 0 ||
        value.compare(u"0", Qt::CaseInsensitive) == 0 ||
        value.compare(u"no", Qt::CaseInsensitive) == 0) {
        return false;
    }
    return fallbackValue;
}

void appendUniquePath(QStringList& paths, const QString& path) {
    if (path.trimmed().isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    const QString absolutePath = info.absoluteFilePath();
    if (!paths.contains(absolutePath)) {
        paths.append(absolutePath);
    }
}

bool jsonBoolValue(const QJsonValue& value, bool fallbackValue = false) {
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    if (value.isString()) {
        return parseBoolString(value.toString(), fallbackValue);
    }
    return fallbackValue;
}

QString attributeValue(const QXmlStreamAttributes& attributes, QStringView name) {
    return attributes.value(name).toString();
}

QStringList stringListAttribute(const QXmlStreamAttributes& attributes, QStringView name) {
    const QString value = attributeValue(attributes, name);
    if (value.isEmpty()) {
        return {};
    }
    return value.split(QStringLiteral(","), Qt::SkipEmptyParts);
}

bool boolAttribute(const QXmlStreamAttributes& attributes, QStringView name, bool fallbackValue) {
    const auto value = attributes.value(name);
    return value.isEmpty() ? fallbackValue : parseBoolString(value, fallbackValue);
}

std::optional<bool> optionalBoolAttribute(const QXmlStreamAttributes& attributes, QStringView name) {
    const auto value = attributes.value(name);
    if (value.isEmpty()) {
        return std::nullopt;
    }
    return parseBoolString(value, false);
}

int intAttribute(const QXmlStreamAttributes& attributes, QStringView name, int fallbackValue) {
    const auto value = attributes.value(name);
    if (value.isEmpty()) {
        return fallbackValue;
    }

    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallbackValue;
}

double doubleAttribute(const QXmlStreamAttributes& attributes, QStringView name, double fallbackValue) {
    const auto value = attributes.value(name);
    if (value.isEmpty()) {
        return fallbackValue;
    }

    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : fallbackValue;
}

int positiveIntAttribute(const QXmlStreamAttributes& attributes, QStringView name, int fallbackValue) {
    const int parsed = intAttribute(attributes, name, fallbackValue);
    return parsed > 0 ? parsed : fallbackValue;
}

double nonNegativeDoubleAttribute(const QXmlStreamAttributes& attributes,
                                  QStringView name,
                                  double fallbackValue) {
    const double parsed = doubleAttribute(attributes, name, fallbackValue);
    return parsed >= 0.0 ? parsed : fallbackValue;
}

std::optional<double> optionalDoubleAttribute(const QXmlStreamAttributes& attributes, QStringView name) {
    const auto value = attributes.value(name);
    if (value.isEmpty()) {
        return std::nullopt;
    }

    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? std::optional<double>(parsed) : std::nullopt;
}

ModuleParameterChoice parameterChoice(const QString& value, const QString& label) {
    ModuleParameterChoice choice;
    choice.value = value;
    choice.label = label.isEmpty() ? value : label;
    return choice;
}

void loadParameterChoiceElement(ModuleParameterMetadata& metadata, QXmlStreamReader& xml) {
    const QXmlStreamAttributes attrs = xml.attributes();
    const QString value = attributeValue(attrs, u"value");
    const QString label = attributeValue(attrs, u"label");
    if (!value.isEmpty()) {
        metadata.choices.push_back(parameterChoice(value, label));
    }
    xml.skipCurrentElement();
}

void loadParameterChoicesElement(ModuleParameterMetadata& metadata, QXmlStreamReader& xml) {
    while (xml.readNextStartElement()) {
        if (xml.name() == u"choice") {
            loadParameterChoiceElement(metadata, xml);
        } else {
            xml.skipCurrentElement();
        }
    }
}

ModuleParameterMetadata parameterMetadataFromXml(const QString& name, const QXmlStreamAttributes& attrs) {
    ModuleParameterMetadata metadata;
    metadata.name = name;
    metadata.label = attributeValue(attrs, u"label");
    metadata.description = attributeValue(attrs, u"description");
    metadata.unit = attributeValue(attrs, u"unit");
    metadata.minimumValue = optionalDoubleAttribute(attrs, u"minimum");
    if (!metadata.minimumValue.has_value()) {
        metadata.minimumValue = optionalDoubleAttribute(attrs, u"min");
    }
    metadata.maximumValue = optionalDoubleAttribute(attrs, u"maximum");
    if (!metadata.maximumValue.has_value()) {
        metadata.maximumValue = optionalDoubleAttribute(attrs, u"max");
    }
    metadata.configurable = boolAttribute(attrs, u"configurable", true);
    metadata.readOnly = boolAttribute(attrs, u"read_only", false);
    return metadata;
}

void normalizeCollapsedMetrics(ModuleType& type) {
    if (!type.supportsCollapse) {
        type.collapsedNodeMinWidth = type.expandedNodeMinWidth;
        type.collapsedNodeHeight = type.expandedNodeHeight;
        type.collapsedCaptionLeftInset = type.expandedCaptionLeftInset;
        type.collapsedCaptionTopInset = type.expandedCaptionTopInset;
    }
}

QString defaultEditorLayout(const ModuleType& type) {
    if (type.graphGroup == QStringLiteral("endpoints") && type.defaultPorts.size() <= 2) {
        return QStringLiteral("endpoint");
    }
    return QStringLiteral("fallback");
}

Parameter::Value parameterValue(const QString& parameterType, const QString& defaultText) {
    if (parameterType == QStringLiteral("int")) {
        return defaultText.toInt();
    }
    if (parameterType == QStringLiteral("double")) {
        return defaultText.toDouble();
    }
    if (parameterType == QStringLiteral("bool")) {
        return parseBoolString(defaultText, false);
    }
    return defaultText;
}

Port::Direction portDirection(const QString& directionText) {
    if (directionText == QStringLiteral("input")) {
        return Port::Direction::Input;
    }
    if (directionText == QStringLiteral("inout") || directionText == QStringLiteral("bus")) {
        return Port::Direction::InOut;
    }
    return Port::Direction::Output;
}

const IpcraftConnectionClass* connectionClassForId(const IpcraftPackageManifest& manifest,
                                                   const QString& connectionClassId) {
    for (const IpcraftConnectionClass& connectionClass : manifest.connectionClasses) {
        if (connectionClass.id == connectionClassId) {
            return &connectionClass;
        }
    }
    return nullptr;
}

const IpcraftViewDescriptor* viewForModuleId(const IpcraftPackageManifest& manifest,
                                             const QString& moduleId) {
    for (const IpcraftViewDescriptor& view : manifest.views) {
        if (view.moduleId == moduleId) {
            return &view;
        }
    }
    return nullptr;
}

QString graphGroupForIpcraftRole(const QString& graphRole) {
    if (graphRole == QStringLiteral("host")) {
        return QStringLiteral("xps");
    }
    if (graphRole == QStringLiteral("attached")) {
        return QStringLiteral("endpoints");
    }
    return graphRole;
}

QString firstAcceptConnectionClass(const IpcraftInterfaceDescriptor& interfaceDescriptor) {
    return interfaceDescriptor.accepts.isEmpty()
        ? QString()
        : interfaceDescriptor.accepts.first().connectionClassId;
}

QString firstAcceptRole(const IpcraftInterfaceDescriptor& interfaceDescriptor) {
    if (!interfaceDescriptor.accepts.isEmpty()) {
        return interfaceDescriptor.accepts.first().role;
    }
    return interfaceDescriptor.modes.isEmpty() ? QString() : interfaceDescriptor.modes.first();
}

QStringList compatibleRolesForAccept(const IpcraftPackageManifest& manifest,
                                     const IpcraftInterfaceDescriptor& interfaceDescriptor) {
    const QString role = firstAcceptRole(interfaceDescriptor);
    const IpcraftConnectionClass* connectionClass =
        connectionClassForId(manifest, firstAcceptConnectionClass(interfaceDescriptor));
    if (connectionClass == nullptr) {
        return {};
    }

    QStringList compatibleRoles;
    for (const QString& candidate : connectionClass->roles) {
        if (candidate != role && !compatibleRoles.contains(candidate)) {
            compatibleRoles.append(candidate);
        }
    }
    if (compatibleRoles.isEmpty() && connectionClass->symmetric) {
        compatibleRoles = connectionClass->roles;
    }
    return compatibleRoles;
}

bool isRouterCapableGraphRole(const QString& graphRole) {
    return graphRole == QStringLiteral("host") ||
           graphRole == QStringLiteral("router");
}

QSet<QString> topologyInterfaceIdsForModule(const IpcraftPackageManifest& manifest,
                                            const QString& moduleId) {
    QSet<QString> interfaceIds;
    for (const QJsonObject& topology : manifest.topologies) {
        QString topologyModuleId = topology.value(QStringLiteral("module")).toString().trimmed();
        if (topologyModuleId.isEmpty()) {
            topologyModuleId = topology.value(QStringLiteral("router_module")).toString().trimmed();
        }
        if (topologyModuleId != moduleId) {
            continue;
        }

        const QJsonObject ports = topology.value(QStringLiteral("ports")).toObject();
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            if (!it.value().isString()) {
                continue;
            }
            const QString interfaceId = it.value().toString().trimmed();
            if (!interfaceId.isEmpty()) {
                interfaceIds.insert(interfaceId);
            }
        }
    }
    return interfaceIds;
}

bool hasTopologyMetadata(const IpcraftInterfaceDescriptor& interfaceDescriptor) {
    return !interfaceDescriptor.topology.side.isEmpty() ||
           !interfaceDescriptor.topology.oppositeInterfaceId.isEmpty() ||
           !interfaceDescriptor.topology.role.isEmpty();
}

QString autocompleteGroupForInterface(const IpcraftModuleDescriptor& module,
                                      bool topologyInterface) {
    if (isRouterCapableGraphRole(module.graphRole) && topologyInterface) {
        return QStringLiteral("router_side");
    }
    return QStringLiteral("endpoint_attachment");
}

QString topologyRuleForInterface(const IpcraftModuleDescriptor& module,
                                 bool topologyInterface) {
    if (isRouterCapableGraphRole(module.graphRole) && topologyInterface) {
        return QStringLiteral("opposite_side");
    }
    return {};
}

QString portRoleForInterface(const IpcraftModuleDescriptor& module,
                             bool topologyInterface) {
    return isRouterCapableGraphRole(module.graphRole) && topologyInterface
        ? QStringLiteral("router")
        : QStringLiteral("attachment");
}

std::optional<double> optionalJsonDouble(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    return value.toDouble();
}

ModuleParameterMetadata parameterMetadataFromIpcraft(const QString& name,
                                                     const QJsonObject& object) {
    ModuleParameterMetadata metadata;
    metadata.name = name;
    metadata.label = object.value(QStringLiteral("label")).toString().trimmed();
    metadata.description = object.value(QStringLiteral("description")).toString().trimmed();
    metadata.unit = object.value(QStringLiteral("unit")).toString().trimmed();
    metadata.minimumValue = optionalJsonDouble(object.value(QStringLiteral("minimum")));
    if (!metadata.minimumValue.has_value()) {
        metadata.minimumValue = optionalJsonDouble(object.value(QStringLiteral("min")));
    }
    metadata.maximumValue = optionalJsonDouble(object.value(QStringLiteral("maximum")));
    if (!metadata.maximumValue.has_value()) {
        metadata.maximumValue = optionalJsonDouble(object.value(QStringLiteral("max")));
    }
    metadata.configurable = jsonBoolValue(object.value(QStringLiteral("configurable")), true);
    metadata.readOnly = jsonBoolValue(object.value(QStringLiteral("read_only")), false);

    const QJsonArray enumValues = object.value(QStringLiteral("enum")).toArray();
    const QJsonObject labels = object.value(QStringLiteral("labels")).toObject();
    for (const QJsonValue& value : enumValues) {
        if (!value.isString()) {
            continue;
        }
        const QString choiceValue = value.toString().trimmed();
        metadata.choices.append(parameterChoice(choiceValue,
                                                labels.value(choiceValue).toString(choiceValue)));
    }

    if (metadata.label.isEmpty()) {
        metadata.label = ModuleLabels::humanizeIdentifier(name);
    }
    return metadata;
}

QStringList prioritizedParameterNames(const QJsonObject& parameters) {
    QStringList names = parameters.keys();
    names.sort(Qt::CaseInsensitive);

    const auto promote = [&names](const QString& name) {
        const int index = names.indexOf(name);
        if (index > 0) {
            names.move(index, 0);
        }
    };
    promote(QStringLiteral("external_id"));
    promote(QStringLiteral("display_name"));
    return names;
}

QStringList stringListFromJsonArray(const QJsonArray& values) {
    QStringList result;
    for (const QJsonValue& value : values) {
        if (!value.isString()) {
            continue;
        }
        const QString text = value.toString().trimmed();
        if (!text.isEmpty() && !result.contains(text)) {
            result.append(text);
        }
    }
    return result;
}

QStringList attachHostsFromIpcraft(const QJsonObject& attach) {
    QStringList result = stringListFromJsonArray(attach.value(QStringLiteral("hosts")).toArray());
    const QString host = attach.value(QStringLiteral("host")).toString().trimmed();
    if (!host.isEmpty() && !result.contains(host)) {
        result.append(host);
    }
    return result;
}

Parameter::Value ipcraftParameterValue(const QString& parameterType, const QJsonValue& value) {
    if (parameterType == QStringLiteral("int")) {
        return value.toInt();
    }
    if (parameterType == QStringLiteral("double")) {
        return value.toDouble();
    }
    if (parameterType == QStringLiteral("bool")) {
        return jsonBoolValue(value, false);
    }
    return value.toString();
}

void loadIpcraftParameters(ModuleType& type, const QJsonObject& parameters) {
    for (const QString& name : prioritizedParameterNames(parameters)) {
        const QJsonObject parameterObject = parameters.value(name).toObject();
        const QString parameterType = parameterObject.value(QStringLiteral("type")).toString().trimmed();
        ModuleParameterMetadata metadata = parameterMetadataFromIpcraft(name, parameterObject);
        type.defaultParameters[name] =
            Parameter(name, ipcraftParameterValue(parameterType,
                                                 parameterObject.value(QStringLiteral("default"))));
        type.parameterMetadata.insert(name, metadata);

        if (metadata.configurable) {
            type.configFields.push_back(ModuleConfigField{
                name,
                metadata.label,
                metadata.description
            });
        }
    }
}

ModuleInterfaceMetadata interfaceMetadataFromIpcraft(const IpcraftPackageManifest& manifest,
                                                     const IpcraftModuleDescriptor& module,
                                                     const IpcraftInterfaceDescriptor& interfaceDescriptor,
                                                     const QSet<QString>& topologyInterfaceIds) {
    const bool topologyInterface =
        topologyInterfaceIds.contains(interfaceDescriptor.id) ||
        hasTopologyMetadata(interfaceDescriptor);
    ModuleInterfaceMetadata metadata;
    metadata.id = interfaceDescriptor.id;
    metadata.label = interfaceDescriptor.label.isEmpty()
        ? ModuleLabels::humanizeIdentifier(interfaceDescriptor.id)
        : interfaceDescriptor.label;
    metadata.bus = firstAcceptConnectionClass(interfaceDescriptor);
    metadata.role = firstAcceptRole(interfaceDescriptor);
    metadata.compatibleRoles = compatibleRolesForAccept(manifest, interfaceDescriptor);
    metadata.cardinality = interfaceDescriptor.multiConnection
        ? QStringLiteral("many")
        : QStringLiteral("one");
    metadata.autocompleteGroup = autocompleteGroupForInterface(module, topologyInterface);
    metadata.topologyRule = topologyRuleForInterface(module, topologyInterface);
    metadata.topologySide = interfaceDescriptor.topology.side;
    metadata.oppositeInterfaceId = interfaceDescriptor.topology.oppositeInterfaceId;
    metadata.topologyRole = interfaceDescriptor.topology.role;
    metadata.acceptRules = interfaceDescriptor.accepts;
    return metadata;
}

QString stringDefaultParameter(const QJsonObject& parameters, const QString& parameterName) {
    return parameters.value(parameterName).toObject()
        .value(QStringLiteral("default")).toString().trimmed();
}

QString externalIdPrefixFromIpcraft(const IpcraftModuleDescriptor& module) {
    QString externalId = stringDefaultParameter(module.parameters, QStringLiteral("external_id"));
    const int underscore = externalId.lastIndexOf(QLatin1Char('_'));
    if (underscore > 0) {
        const QString suffix = externalId.mid(underscore + 1);
        bool numeric = !suffix.isEmpty();
        for (const QChar& ch : suffix) {
            numeric = numeric && ch.isDigit();
        }
        if (numeric) {
            externalId = externalId.left(underscore);
        }
    }
    if (!externalId.isEmpty()) {
        return externalId;
    }

    QString fallback = module.id.toLower();
    fallback.replace(QLatin1Char('-'), QLatin1Char('_'));
    fallback.replace(QLatin1Char(' '), QLatin1Char('_'));
    return fallback;
}

QString displayPrefixFromIpcraft(const IpcraftModuleDescriptor& module) {
    const QString displayName = stringDefaultParameter(module.parameters, QStringLiteral("display_name"));
    if (!displayName.isEmpty()) {
        return displayName.section(QLatin1Char(' '), 0, 0);
    }
    return module.name.isEmpty() ? module.id : module.name;
}

ModuleType moduleTypeFromIpcraft(const IpcraftPackageManifest& manifest,
                                 const IpcraftModuleDescriptor& module) {
    ModuleType type;
    type.name = ModuleRegistry::scopedTypeName(manifest.id, module.id);
    type.packageId = manifest.id;
    type.moduleId = module.id;
    type.graphRole = module.graphRole;
    type.attachHostModuleIds = attachHostsFromIpcraft(module.attach);
    type.attachZoneId = module.attach.value(QStringLiteral("zone")).toString().trimmed();
    type.ipcoreId = manifest.id;
    type.paletteLabel = module.name.isEmpty() ? module.id : module.name;
    type.description = module.description;
    type.displayLabelParameter = module.displayLabelParameter;
    type.shortLabelParameter = module.shortLabelParameter;
    type.graphGroup = graphGroupForIpcraftRole(module.graphRole);
    type.externalIdPrefix = externalIdPrefixFromIpcraft(module);
    type.displayPrefix = displayPrefixFromIpcraft(module);
    type.supportsMeshCoordinates =
        module.parameters.contains(QStringLiteral("mesh_col")) &&
        module.parameters.contains(QStringLiteral("mesh_row"));

    if (const IpcraftViewDescriptor* view = viewForModuleId(manifest, module.id)) {
        type.viewFilePath = view->resolvedFilePath;
    }

    const QSet<QString> topologyInterfaceIds =
        topologyInterfaceIdsForModule(manifest, module.id);
    for (const IpcraftInterfaceDescriptor& interfaceDescriptor : module.interfaces) {
        const bool topologyInterface =
            topologyInterfaceIds.contains(interfaceDescriptor.id) ||
            hasTopologyMetadata(interfaceDescriptor);
        ModuleInterfaceMetadata metadata =
            interfaceMetadataFromIpcraft(manifest,
                                         module,
                                         interfaceDescriptor,
                                         topologyInterfaceIds);
        type.interfaceMetadata.insert(metadata.id, metadata);
        type.defaultPorts.emplace_back(interfaceDescriptor.id,
                                       Port::Direction::InOut,
                                       QStringLiteral("bus"),
                                       metadata.label,
                                       QStringLiteral("%1 interface").arg(metadata.label),
                                       portRoleForInterface(module,
                                                            topologyInterface),
                                       metadata.bus,
                                       interfaceDescriptor.id);
    }

    loadIpcraftParameters(type, module.parameters);

    if (type.paletteLabel.isEmpty()) {
        type.paletteLabel = ModuleLabels::humanizeIdentifier(type.name);
    }
    if (type.editorLayout.isEmpty()) {
        type.editorLayout = defaultEditorLayout(type);
    }

    normalizeCollapsedMetrics(type);
    return type;
}

void applyGraphicsElement(ModuleType& type, QXmlStreamReader& xml) {
    const QXmlStreamAttributes attrs = xml.attributes();
    if (attrs.hasAttribute(u"layout")) {
        type.editorLayout = attributeValue(attrs, u"layout");
    }
    if (attrs.hasAttribute(u"node_color")) {
        type.nodeColor = attributeValue(attrs, u"node_color");
    }
    if (attrs.hasAttribute(u"supports_collapse")) {
        type.supportsCollapse = boolAttribute(attrs, u"supports_collapse", type.supportsCollapse);
    }

    while (xml.readNextStartElement()) {
        if (xml.name() == u"expanded") {
            const QXmlStreamAttributes expandedAttrs = xml.attributes();
            type.expandedNodeMinWidth = positiveIntAttribute(
                expandedAttrs, u"min_width", type.expandedNodeMinWidth);
            type.expandedNodeHeight = positiveIntAttribute(
                expandedAttrs, u"height", type.expandedNodeHeight);
            type.expandedCaptionLeftInset = nonNegativeDoubleAttribute(
                expandedAttrs, u"caption_left", type.expandedCaptionLeftInset);
            type.expandedCaptionTopInset = nonNegativeDoubleAttribute(
                expandedAttrs, u"caption_top", type.expandedCaptionTopInset);
            type.expandedPortInset = nonNegativeDoubleAttribute(
                expandedAttrs, u"port_inset", type.expandedPortInset);
            xml.skipCurrentElement();
        } else if (xml.name() == u"collapsed") {
            const QXmlStreamAttributes collapsedAttrs = xml.attributes();
            type.collapsedNodeMinWidth = positiveIntAttribute(
                collapsedAttrs, u"min_width", type.collapsedNodeMinWidth);
            type.collapsedNodeHeight = positiveIntAttribute(
                collapsedAttrs, u"height", type.collapsedNodeHeight);
            type.collapsedCaptionLeftInset = nonNegativeDoubleAttribute(
                collapsedAttrs, u"caption_left", type.collapsedCaptionLeftInset);
            type.collapsedCaptionTopInset = nonNegativeDoubleAttribute(
                collapsedAttrs, u"caption_top", type.collapsedCaptionTopInset);
            type.collapsedEndpointPortInset = nonNegativeDoubleAttribute(
                collapsedAttrs, u"endpoint_inset", type.collapsedEndpointPortInset);
            xml.skipCurrentElement();
        } else if (xml.name() == u"arrangement") {
            const QXmlStreamAttributes arrangementAttrs = xml.attributes();
            type.linkedEndpointOffsetX = positiveIntAttribute(
                arrangementAttrs, u"endpoint_offset_x", type.linkedEndpointOffsetX);
            type.meshSpacingX = positiveIntAttribute(arrangementAttrs, u"mesh_spacing_x", type.meshSpacingX);
            type.meshSpacingY = positiveIntAttribute(arrangementAttrs, u"mesh_spacing_y", type.meshSpacingY);
            type.looseEndpointSpacingX = positiveIntAttribute(
                arrangementAttrs, u"loose_endpoint_spacing_x", type.looseEndpointSpacingX);
            type.looseEndpointSpacingY = positiveIntAttribute(
                arrangementAttrs, u"loose_endpoint_spacing_y", type.looseEndpointSpacingY);
            type.looseEndpointMarginY = positiveIntAttribute(
                arrangementAttrs, u"loose_endpoint_margin_y", type.looseEndpointMarginY);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

void loadAnchorsFromXml(ModuleType& type, QXmlStreamReader& xml) {
    while (xml.readNextStartElement()) {
        if (xml.name() != u"anchor") {
            xml.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attrs = xml.attributes();
        const QString interfaceId = attributeValue(attrs, u"ref");
        const std::optional<double> x = optionalDoubleAttribute(attrs, u"x");
        const std::optional<double> y = optionalDoubleAttribute(attrs, u"y");
        if (!interfaceId.isEmpty() && x.has_value() && y.has_value()) {
            ModuleInterfaceAnchor anchor;
            anchor.interfaceId = interfaceId;
            anchor.x = *x;
            anchor.y = *y;
            anchor.normalX = optionalDoubleAttribute(attrs, u"normal_x");
            anchor.normalY = optionalDoubleAttribute(attrs, u"normal_y");
            anchor.label = attributeValue(attrs, u"label");
            anchor.labelX = optionalDoubleAttribute(attrs, u"label_x");
            anchor.labelY = optionalDoubleAttribute(attrs, u"label_y");
            type.interfaceAnchors.insert(anchor.interfaceId, anchor);
        }
        xml.skipCurrentElement();
    }
}

void loadAttachmentZoneElement(ModuleType& type, QXmlStreamReader& xml) {
    const QXmlStreamAttributes attrs = xml.attributes();
    QString zoneId = attributeValue(attrs, u"id");
    if (zoneId.isEmpty()) {
        zoneId = attributeValue(attrs, u"ref");
    }

    const std::optional<double> x = optionalDoubleAttribute(attrs, u"x");
    const std::optional<double> y = optionalDoubleAttribute(attrs, u"y");
    if (!zoneId.isEmpty() && x.has_value() && y.has_value()) {
        ModuleAttachmentZone zone;
        zone.id = zoneId;
        zone.x = *x;
        zone.y = *y;
        zone.normalX = optionalDoubleAttribute(attrs, u"normal_x");
        zone.normalY = optionalDoubleAttribute(attrs, u"normal_y");
        zone.label = attributeValue(attrs, u"label");
        zone.mirrorAttachedNode = optionalBoolAttribute(attrs, u"mirror");
        type.attachmentZones.insert(zone.id, zone);
    }
    xml.skipCurrentElement();
}

void loadAttachmentZonesFromXml(ModuleType& type, QXmlStreamReader& xml) {
    while (xml.readNextStartElement()) {
        if (xml.name() == u"zone" || xml.name() == u"attachment-zone") {
            loadAttachmentZoneElement(type, xml);
        } else {
            xml.skipCurrentElement();
        }
    }
}

QVector<ModuleConfigField> configFieldsFromXml(QXmlStreamReader& xml) {
    QVector<ModuleConfigField> fields;

    while (xml.readNextStartElement()) {
        if (xml.name() == u"field") {
            ModuleConfigField field;
            field.parameterName = attributeValue(xml.attributes(), u"parameter");
            field.label = attributeValue(xml.attributes(), u"label");
            field.description = attributeValue(xml.attributes(), u"description");
            if (!field.parameterName.isEmpty()) {
                fields.push_back(field);
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }

    return fields;
}

void fillConfigFieldDefaults(QVector<ModuleConfigField>& fields,
                             const QHash<QString, ModuleConfigField>& defaults) {
    for (ModuleConfigField& field : fields) {
        const auto it = defaults.find(field.parameterName);
        if (it != defaults.end()) {
            if (field.label.isEmpty()) {
                field.label = it.value().label;
            }
            if (field.description.isEmpty()) {
                field.description = it.value().description;
            }
        }

        if (field.label.isEmpty()) {
            field.label = ModuleLabels::humanizeIdentifier(field.parameterName);
        }
    }
}

void loadPortsFromXml(ModuleType& type, QXmlStreamReader& xml) {
    while (xml.readNextStartElement()) {
        if (xml.name() != u"port") {
            xml.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attrs = xml.attributes();
        const Port::Direction direction = portDirection(attributeValue(attrs, u"direction"));
        type.defaultPorts.emplace_back(attributeValue(attrs, u"id"),
                                       direction,
                                       attributeValue(attrs, u"type"),
                                       attributeValue(attrs, u"name"),
                                       attributeValue(attrs, u"description"),
                                       attributeValue(attrs, u"role"),
                                       attributeValue(attrs, u"bus_type"),
                                       attributeValue(attrs, u"interface"));
        xml.skipCurrentElement();
    }
}

void loadInterfacesFromXml(ModuleType& type, QXmlStreamReader& xml) {
    while (xml.readNextStartElement()) {
        if (xml.name() != u"interface") {
            xml.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attrs = xml.attributes();
        ModuleInterfaceMetadata metadata;
        metadata.id = attributeValue(attrs, u"id");
        metadata.label = attributeValue(attrs, u"label");
        metadata.bus = attributeValue(attrs, u"bus");
        metadata.role = attributeValue(attrs, u"role");
        metadata.compatibleRoles = stringListAttribute(attrs, u"connects_to");
        metadata.matchFields = stringListAttribute(attrs, u"match");
        metadata.cardinality = attributeValue(attrs, u"cardinality");
        if (metadata.cardinality.isEmpty()) {
            metadata.cardinality = QStringLiteral("one");
        }
        metadata.autocompleteGroup = attributeValue(attrs, u"autocomplete_group");
        metadata.topologyRule = attributeValue(attrs, u"topology_rule");

        while (xml.readNextStartElement()) {
            if (xml.name() == u"accept") {
                const QXmlStreamAttributes acceptAttrs = xml.attributes();
                const QString field = attributeValue(acceptAttrs, u"field");
                if (!field.isEmpty()) {
                    metadata.acceptedValues.insert(field, stringListAttribute(acceptAttrs, u"values"));
                }
                xml.skipCurrentElement();
            } else if (xml.name() == u"config") {
                const QXmlStreamAttributes configAttrs = xml.attributes();
                const QString field = attributeValue(configAttrs, u"field");
                const QString parameter = attributeValue(configAttrs, u"parameter");
                if (!field.isEmpty() && !parameter.isEmpty()) {
                    metadata.parameterBindings.insert(field, parameter);
                }
                xml.skipCurrentElement();
            } else {
                xml.skipCurrentElement();
            }
        }

        if (!metadata.id.isEmpty()) {
            type.interfaceMetadata.insert(metadata.id, metadata);
        }
    }
}

QHash<QString, ModuleType>::iterator findTypeForManifestModule(QHash<QString, ModuleType>& types,
                                                               const QString& moduleIdOrTypeName) {
    auto typeIt = types.find(moduleIdOrTypeName);
    if (typeIt != types.end()) {
        return typeIt;
    }

    for (auto candidate = types.begin(); candidate != types.end(); ++candidate) {
        if (candidate.value().moduleId == moduleIdOrTypeName) {
            return candidate;
        }
    }
    return types.end();
}

ParameterLoadResult loadParametersFromXml(ModuleType& type, QXmlStreamReader& xml) {
    ParameterLoadResult result;

    while (xml.readNextStartElement()) {
        if (xml.name() != u"parameter") {
            xml.skipCurrentElement();
            continue;
        }

        const QXmlStreamAttributes attrs = xml.attributes();
        const QString name = attributeValue(attrs, u"name");
        const QString parameterType = attributeValue(attrs, u"type");
        QString defaultText = attributeValue(attrs, u"default");
        QString inlineDefaultText;

        ModuleParameterMetadata metadata = parameterMetadataFromXml(name, attrs);
        while (!xml.atEnd()) {
            xml.readNext();

            if (xml.isEndElement() && xml.name() == u"parameter") {
                break;
            }

            if (xml.isCharacters() && !xml.isWhitespace()) {
                inlineDefaultText += xml.text().toString();
                continue;
            }

            if (!xml.isStartElement()) {
                continue;
            }

            if (xml.name() == u"choice") {
                loadParameterChoiceElement(metadata, xml);
            } else if (xml.name() == u"choices") {
                loadParameterChoicesElement(metadata, xml);
            } else {
                xml.skipCurrentElement();
            }
        }
        if (defaultText.isEmpty()) {
            defaultText = inlineDefaultText.trimmed();
        }

        if (metadata.label.isEmpty()) {
            metadata.label = ModuleLabels::humanizeIdentifier(name);
        }

        type.defaultParameters[name] = Parameter(name, parameterValue(parameterType, defaultText));
        type.parameterMetadata.insert(name, metadata);

        ModuleConfigField field = {
            name,
            metadata.label,
            metadata.description
        };
        result.fieldByName.insert(name, field);

        if (metadata.configurable) {
            result.autoConfigFields.push_back(field);
        }
    }

    return result;
}

ModuleType loadModuleTypeFromXml(QXmlStreamReader& xml) {
    ModuleType type;
    const QXmlStreamAttributes attrs = xml.attributes();
    type.name = attributeValue(attrs, u"name");
    type.paletteLabel = attributeValue(attrs, u"palette_label");
    type.graphGroup = attributeValue(attrs, u"graph_group");
    type.description = attributeValue(attrs, u"description");

    ParameterLoadResult parameterResult;
    QVector<ModuleConfigField> explicitConfigFields;
    bool hasExplicitConfigZone = false;

    while (xml.readNextStartElement()) {
        if (xml.name() == u"identity") {
            const QXmlStreamAttributes identityAttrs = xml.attributes();
            type.externalIdPrefix = attributeValue(identityAttrs, u"external_id_prefix");
            type.displayPrefix = attributeValue(identityAttrs, u"display_prefix");
            type.identityWidth = intAttribute(identityAttrs, u"width", type.identityWidth);
            type.supportsMeshCoordinates = boolAttribute(
                identityAttrs, u"supports_mesh_coordinates", type.supportsMeshCoordinates);
            xml.skipCurrentElement();
        } else if (xml.name() == u"capabilities") {
            type.supportsCollapse = boolAttribute(xml.attributes(), u"supports_collapse", type.supportsCollapse);
            xml.skipCurrentElement();
        } else if (xml.name() == u"graphics") {
            applyGraphicsElement(type, xml);
        } else if (xml.name() == u"anchors") {
            loadAnchorsFromXml(type, xml);
        } else if (xml.name() == u"attachment-zones") {
            loadAttachmentZonesFromXml(type, xml);
        } else if (xml.name() == u"attachment-zone") {
            loadAttachmentZoneElement(type, xml);
        } else if (xml.name() == u"interfaces") {
            loadInterfacesFromXml(type, xml);
        } else if (xml.name() == u"ports") {
            loadPortsFromXml(type, xml);
        } else if (xml.name() == u"parameters") {
            parameterResult = loadParametersFromXml(type, xml);
        } else if (xml.name() == u"config-zone") {
            explicitConfigFields = configFieldsFromXml(xml);
            hasExplicitConfigZone = true;
        } else {
            xml.skipCurrentElement();
        }
    }

    if (hasExplicitConfigZone) {
        fillConfigFieldDefaults(explicitConfigFields, parameterResult.fieldByName);
        type.configFields = explicitConfigFields;
    } else {
        type.configFields = parameterResult.autoConfigFields;
    }

    if (type.paletteLabel.isEmpty()) {
        type.paletteLabel = ModuleLabels::humanizeIdentifier(type.name);
    }
    if (type.editorLayout.isEmpty()) {
        type.editorLayout = defaultEditorLayout(type);
    }

    normalizeCollapsedMetrics(type);
    return type;
}

void logIpcraftDiagnostics(const QVector<IpcraftDiagnostic>& diagnostics) {
    for (const IpcraftDiagnostic& diagnostic : diagnostics) {
        qWarning().noquote()
            << QStringLiteral("Ipcraft package diagnostic [%1] %2 %3: %4")
                   .arg(diagnostic.severity,
                        diagnostic.packageRootPath,
                        diagnostic.path,
                        diagnostic.message);
    }
}

} // namespace

QStringList defaultIpcraftPackageRoots() {
    QStringList roots;

    for (const QString& path : AppSettings().ipcorePaths()) {
        appendUniquePath(roots, path);
    }

    return roots;
}

QVector<IpcraftPackageManifest> loadIpcraftPackageManifests(const QStringList& rootPaths) {
    IpcraftRegistry registry;
    if (!registry.loadPackageRoots(rootPaths)) {
        logIpcraftDiagnostics(registry.diagnostics());
    }
    return registry.packages();
}

XmlModuleTypeSource::XmlModuleTypeSource(const QString& bundlePath)
    : m_bundlePath(bundlePath) {}

QHash<QString, ModuleType> XmlModuleTypeSource::loadModuleTypes() {
    QHash<QString, ModuleType> types;
    QStringList orderedTypeNames;

    QFile file(m_bundlePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_orderedTypeNames.clear();
        return {};
    }

    QXmlStreamReader xml(&file);
    while (xml.readNextStartElement()) {
        if (xml.name() != u"module-bundle") {
            xml.skipCurrentElement();
            continue;
        }

        while (xml.readNextStartElement()) {
            if (xml.name() != u"module") {
                xml.skipCurrentElement();
                continue;
            }

            const ModuleType type = loadModuleTypeFromXml(xml);
            if (!type.name.isEmpty()) {
                orderedTypeNames.push_back(type.name);
                types.insert(type.name, type);
            }
        }
    }

    if (xml.hasError()) {
        m_orderedTypeNames.clear();
        return {};
    }
    m_orderedTypeNames = std::move(orderedTypeNames);
    return types;
}

QStringList XmlModuleTypeSource::orderedTypeNames() const {
    return m_orderedTypeNames;
}

IpcraftModuleTypeSource::IpcraftModuleTypeSource(IpcraftPackageManifest manifest)
    : m_manifest(std::move(manifest)) {}

QHash<QString, ModuleType> IpcraftModuleTypeSource::loadModuleTypes() {
    QHash<QString, ModuleType> types;
    m_orderedTypeNames.clear();

    for (const IpcraftModuleDescriptor& module : m_manifest.modules) {
        if (module.id.isEmpty()) {
            continue;
        }

        ModuleType type = moduleTypeFromIpcraft(m_manifest, module);
        m_orderedTypeNames.push_back(type.name);
        types.insert(type.name, type);
    }

    return types;
}

QStringList IpcraftModuleTypeSource::orderedTypeNames() const {
    return m_orderedTypeNames;
}

XmlModuleGraphicsOverlay::XmlModuleGraphicsOverlay(const QString& graphicsDirectory)
    : m_graphicsDirectory(graphicsDirectory) {}

void XmlModuleGraphicsOverlay::apply(QHash<QString, ModuleType>& types) {
    if (m_graphicsDirectory.isEmpty()) {
        return;
    }

    const QDir directory(m_graphicsDirectory);
    const QStringList entries = directory.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
    for (const QString& entry : entries) {
        QHash<QString, ModuleType> updatedTypes;
        QFile file(directory.filePath(entry));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QXmlStreamReader xml(&file);
        while (xml.readNextStartElement()) {
            if (xml.name() != u"module-graphics") {
                xml.skipCurrentElement();
                continue;
            }

            const QString moduleTypeName = attributeValue(xml.attributes(), u"type");
            const auto sourceTypeIt = types.constFind(moduleTypeName);
            if (sourceTypeIt == types.cend()) {
                xml.skipCurrentElement();
                continue;
            }
            auto typeIt = updatedTypes.find(moduleTypeName);
            if (typeIt == updatedTypes.end()) {
                typeIt = updatedTypes.insert(moduleTypeName, sourceTypeIt.value());
            }

            while (xml.readNextStartElement()) {
                if (xml.name() == u"graphics") {
                    applyGraphicsElement(typeIt.value(), xml);
                } else if (xml.name() == u"anchors") {
                    loadAnchorsFromXml(typeIt.value(), xml);
                } else if (xml.name() == u"attachment-zones") {
                    loadAttachmentZonesFromXml(typeIt.value(), xml);
                } else if (xml.name() == u"attachment-zone") {
                    loadAttachmentZoneElement(typeIt.value(), xml);
                } else {
                    xml.skipCurrentElement();
                }
            }
        }
        if (!xml.hasError()) {
            for (auto typeIt = updatedTypes.cbegin(); typeIt != updatedTypes.cend(); ++typeIt) {
                types.insert(typeIt.key(), typeIt.value());
            }
        }
    }
}

IpcraftModuleViewOverlay::IpcraftModuleViewOverlay(QVector<IpcraftViewDescriptor> views)
    : m_views(std::move(views)) {}

void IpcraftModuleViewOverlay::apply(QHash<QString, ModuleType>& types) {
    for (const IpcraftViewDescriptor& view : m_views) {
        if (view.resolvedFilePath.isEmpty()) {
            continue;
        }

        QFile file(view.resolvedFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QXmlStreamReader xml(&file);
        while (xml.readNextStartElement()) {
            if (xml.name() != u"module-view") {
                xml.skipCurrentElement();
                continue;
            }

            const QString moduleAttribute = attributeValue(xml.attributes(), u"module");
            const QString moduleTypeName =
                moduleAttribute.isEmpty() ? view.moduleId : moduleAttribute;
            auto typeIt = findTypeForManifestModule(types, moduleTypeName);
            if (typeIt == types.end()) {
                xml.skipCurrentElement();
                continue;
            }

            typeIt.value().viewFilePath = view.resolvedFilePath;
            while (xml.readNextStartElement()) {
                if (xml.name() == u"graphics") {
                    applyGraphicsElement(typeIt.value(), xml);
                } else if (xml.name() == u"anchors") {
                    loadAnchorsFromXml(typeIt.value(), xml);
                } else if (xml.name() == u"attachment-zones") {
                    loadAttachmentZonesFromXml(typeIt.value(), xml);
                } else if (xml.name() == u"attachment-zone") {
                    loadAttachmentZoneElement(typeIt.value(), xml);
                } else {
                    xml.skipCurrentElement();
                }
            }
        }
    }
}

LayeredModuleProvider::LayeredModuleProvider(std::unique_ptr<ModuleTypeSource> source)
    : m_source(std::move(source)) {}

void LayeredModuleProvider::addOverlay(std::unique_ptr<ModuleTypeOverlay> overlay) {
    m_overlays.push_back(std::move(overlay));
}

std::vector<ModuleType> LayeredModuleProvider::loadModules() {
    if (!m_source) {
        return {};
    }

    QHash<QString, ModuleType> types = m_source->loadModuleTypes();
    for (const auto& overlay : m_overlays) {
        overlay->apply(types);
    }

    std::vector<ModuleType> orderedTypes;
    const QStringList orderedTypeNames = m_source->orderedTypeNames();
    orderedTypes.reserve(types.size());

    for (const QString& typeName : orderedTypeNames) {
        const auto it = types.find(typeName);
        if (it != types.end()) {
            orderedTypes.push_back(it.value());
        }
    }

    return orderedTypes;
}
