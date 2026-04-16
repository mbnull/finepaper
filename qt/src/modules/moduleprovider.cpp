// ModuleProvider loaders parse XML/JSON bundles into normalized ModuleType definitions.
#include "modules/moduleprovider.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QXmlStreamReader>

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

QString humanizeIdentifier(const QString& identifier) {
    if (identifier.isEmpty()) {
        return {};
    }

    QString text = identifier;
    text.replace('-', ' ');
    text.replace('_', ' ');

    bool capitalizeNext = true;
    for (int index = 0; index < text.size(); ++index) {
        if (text[index].isSpace()) {
            capitalizeNext = true;
            continue;
        }

        if (capitalizeNext) {
            text[index] = text[index].toUpper();
            capitalizeNext = false;
        }
    }

    return text;
}

QString attributeValue(const QXmlStreamAttributes& attributes, QStringView name) {
    return attributes.value(name).toString();
}

bool boolAttribute(const QXmlStreamAttributes& attributes, QStringView name, bool fallbackValue) {
    const auto value = attributes.value(name);
    return value.isEmpty() ? fallbackValue : parseBoolString(value, fallbackValue);
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

bool boolValue(const QJsonValue& value, bool fallbackValue) {
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isString()) {
        return parseBoolString(value.toString(), fallbackValue);
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    return fallbackValue;
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

std::optional<double> optionalDoubleValue(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }

    return value.toDouble();
}

ModuleParameterChoice parameterChoice(const QString& value, const QString& label) {
    ModuleParameterChoice choice;
    choice.value = value;
    choice.label = label.isEmpty() ? value : label;
    return choice;
}

void loadParameterChoicesFromJson(ModuleParameterMetadata& metadata, const QJsonValue& choicesValue) {
    if (!choicesValue.isArray()) {
        return;
    }

    for (const QJsonValue& choiceValue : choicesValue.toArray()) {
        if (choiceValue.isString()) {
            metadata.choices.push_back(parameterChoice(choiceValue.toString(), QString()));
        } else if (choiceValue.isObject()) {
            const QJsonObject choiceObject = choiceValue.toObject();
            const QString value = choiceObject.value(QStringLiteral("value")).toString();
            if (!value.isEmpty()) {
                metadata.choices.push_back(parameterChoice(
                    value,
                    choiceObject.value(QStringLiteral("label")).toString()));
            }
        }
    }
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

ModuleParameterMetadata parameterMetadataFromJson(const QString& name, const QJsonObject& parameter) {
    ModuleParameterMetadata metadata;
    metadata.name = name;
    metadata.label = parameter.value(QStringLiteral("label")).toString();
    metadata.description = parameter.value(QStringLiteral("description")).toString();
    metadata.unit = parameter.value(QStringLiteral("unit")).toString();
    metadata.minimumValue = optionalDoubleValue(parameter.value(QStringLiteral("minimum")));
    if (!metadata.minimumValue.has_value()) {
        metadata.minimumValue = optionalDoubleValue(parameter.value(QStringLiteral("min")));
    }
    metadata.maximumValue = optionalDoubleValue(parameter.value(QStringLiteral("maximum")));
    if (!metadata.maximumValue.has_value()) {
        metadata.maximumValue = optionalDoubleValue(parameter.value(QStringLiteral("max")));
    }
    metadata.configurable = !parameter.contains(QStringLiteral("configurable")) ||
                            boolValue(parameter.value(QStringLiteral("configurable")), true);
    metadata.readOnly = boolValue(parameter.value(QStringLiteral("read_only")), false);
    loadParameterChoicesFromJson(metadata, parameter.value(QStringLiteral("choices")));
    return metadata;
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
            type.expandedNodeMinWidth = intAttribute(expandedAttrs, u"min_width", type.expandedNodeMinWidth);
            type.expandedNodeHeight = intAttribute(expandedAttrs, u"height", type.expandedNodeHeight);
            type.expandedCaptionLeftInset = doubleAttribute(
                expandedAttrs, u"caption_left", type.expandedCaptionLeftInset);
            type.expandedCaptionTopInset = doubleAttribute(
                expandedAttrs, u"caption_top", type.expandedCaptionTopInset);
            type.expandedPortInset = doubleAttribute(expandedAttrs, u"port_inset", type.expandedPortInset);
            xml.skipCurrentElement();
        } else if (xml.name() == u"collapsed") {
            const QXmlStreamAttributes collapsedAttrs = xml.attributes();
            type.collapsedNodeMinWidth = intAttribute(collapsedAttrs, u"min_width", type.collapsedNodeMinWidth);
            type.collapsedNodeHeight = intAttribute(collapsedAttrs, u"height", type.collapsedNodeHeight);
            type.collapsedCaptionLeftInset = doubleAttribute(
                collapsedAttrs, u"caption_left", type.collapsedCaptionLeftInset);
            type.collapsedCaptionTopInset = doubleAttribute(
                collapsedAttrs, u"caption_top", type.collapsedCaptionTopInset);
            type.collapsedEndpointPortInset = doubleAttribute(
                collapsedAttrs, u"endpoint_inset", type.collapsedEndpointPortInset);
            xml.skipCurrentElement();
        } else if (xml.name() == u"arrangement") {
            const QXmlStreamAttributes arrangementAttrs = xml.attributes();
            type.linkedEndpointOffsetX = intAttribute(
                arrangementAttrs, u"endpoint_offset_x", type.linkedEndpointOffsetX);
            type.meshSpacingX = intAttribute(arrangementAttrs, u"mesh_spacing_x", type.meshSpacingX);
            type.meshSpacingY = intAttribute(arrangementAttrs, u"mesh_spacing_y", type.meshSpacingY);
            type.looseEndpointSpacingX = intAttribute(
                arrangementAttrs, u"loose_endpoint_spacing_x", type.looseEndpointSpacingX);
            type.looseEndpointSpacingY = intAttribute(
                arrangementAttrs, u"loose_endpoint_spacing_y", type.looseEndpointSpacingY);
            type.looseEndpointMarginY = intAttribute(
                arrangementAttrs, u"loose_endpoint_margin_y", type.looseEndpointMarginY);
            xml.skipCurrentElement();
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

void applyConfigZoneElement(ModuleType& type, QXmlStreamReader& xml) {
    type.configFields = configFieldsFromXml(xml);
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
            field.label = humanizeIdentifier(field.parameterName);
        }
    }
}

void loadPortsFromJson(ModuleType& type, const QJsonArray& ports) {
    for (const auto& portVal : ports) {
        const QJsonObject port = portVal.toObject();
        const Port::Direction direction = portDirection(port.value(QStringLiteral("direction")).toString());
        type.defaultPorts.emplace_back(
            port.value(QStringLiteral("id")).toString(),
            direction,
            port.value(QStringLiteral("type")).toString(),
            port.value(QStringLiteral("name")).toString(),
            port.value(QStringLiteral("description")).toString(),
            port.value(QStringLiteral("role")).toString(),
            port.value(QStringLiteral("bus_type")).toString());
    }
}

ParameterLoadResult loadParametersFromJson(ModuleType& type, const QJsonArray& parameters) {
    ParameterLoadResult result;

    for (const auto& paramVal : parameters) {
        const QJsonObject parameter = paramVal.toObject();
        const QString name = parameter.value(QStringLiteral("name")).toString();
        const QString parameterType = parameter.value(QStringLiteral("type")).toString();
        Parameter::Value defaultValue;

        if (parameterType == QStringLiteral("int")) {
            defaultValue = parameter.value(QStringLiteral("default")).toInt();
        } else if (parameterType == QStringLiteral("double")) {
            defaultValue = parameter.value(QStringLiteral("default")).toDouble();
        } else if (parameterType == QStringLiteral("bool")) {
            defaultValue = boolValue(parameter.value(QStringLiteral("default")), false);
        } else {
            defaultValue = parameter.value(QStringLiteral("default")).toString();
        }

        type.defaultParameters[name] = Parameter(name, defaultValue);

        ModuleParameterMetadata metadata = parameterMetadataFromJson(name, parameter);
        if (metadata.label.isEmpty()) {
            metadata.label = humanizeIdentifier(name);
        }
        type.parameterMetadata.insert(name, metadata);

        ModuleConfigField field{
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
                                       attributeValue(attrs, u"bus_type"));
        xml.skipCurrentElement();
    }
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
            metadata.label = humanizeIdentifier(name);
        }

        type.defaultParameters[name] = Parameter(name, parameterValue(parameterType, defaultText));
        type.parameterMetadata.insert(name, metadata);

        ModuleConfigField field{
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

QVector<ModuleConfigField> configFieldsFromJson(const QJsonValue& configZoneValue,
                                                const QHash<QString, ModuleConfigField>& defaults) {
    QJsonArray fields;
    if (configZoneValue.isArray()) {
        fields = configZoneValue.toArray();
    } else if (configZoneValue.isObject()) {
        fields = configZoneValue.toObject().value(QStringLiteral("fields")).toArray();
    } else {
        return {};
    }

    QVector<ModuleConfigField> configFields;
    for (const auto& fieldValue : fields) {
        ModuleConfigField field;
        if (fieldValue.isString()) {
            field.parameterName = fieldValue.toString();
        } else if (fieldValue.isObject()) {
            const QJsonObject object = fieldValue.toObject();
            field.parameterName = object.value(QStringLiteral("parameter")).toString();
            field.label = object.value(QStringLiteral("label")).toString();
            field.description = object.value(QStringLiteral("description")).toString();
        }

        if (!field.parameterName.isEmpty()) {
            configFields.push_back(field);
        }
    }

    fillConfigFieldDefaults(configFields, defaults);
    return configFields;
}

void loadIdentityFromJson(ModuleType& type, const QJsonObject& identity) {
    type.externalIdPrefix = identity.value(QStringLiteral("external_id_prefix")).toString();
    type.displayPrefix = identity.value(QStringLiteral("display_prefix")).toString();
    type.identityWidth = identity.value(QStringLiteral("width")).toInt(2);
    type.supportsMeshCoordinates = boolValue(identity.value(QStringLiteral("supports_mesh_coordinates")), false);
}

void loadPresentationFromJson(ModuleType& type, const QJsonObject& presentation) {
    if (presentation.isEmpty()) {
        return;
    }

    if (presentation.contains(QStringLiteral("layout"))) {
        type.editorLayout = presentation.value(QStringLiteral("layout")).toString(type.editorLayout);
    }
    if (presentation.contains(QStringLiteral("node_color"))) {
        type.nodeColor = presentation.value(QStringLiteral("node_color")).toString(type.nodeColor);
    }
    if (presentation.contains(QStringLiteral("supports_collapse"))) {
        type.supportsCollapse = boolValue(
            presentation.value(QStringLiteral("supports_collapse")), type.supportsCollapse);
    }

    const QJsonObject graphics = presentation.value(QStringLiteral("graphics")).toObject();
    const QJsonObject expanded = graphics.value(QStringLiteral("expanded")).toObject();
    type.expandedNodeMinWidth = expanded.value(QStringLiteral("min_width")).toInt(type.expandedNodeMinWidth);
    type.expandedNodeHeight = expanded.value(QStringLiteral("height")).toInt(type.expandedNodeHeight);
    type.expandedCaptionLeftInset = expanded.value(QStringLiteral("caption_left")).toDouble(type.expandedCaptionLeftInset);
    type.expandedCaptionTopInset = expanded.value(QStringLiteral("caption_top")).toDouble(type.expandedCaptionTopInset);
    type.expandedPortInset = expanded.value(QStringLiteral("port_inset")).toDouble(type.expandedPortInset);

    const QJsonObject collapsed = graphics.value(QStringLiteral("collapsed")).toObject();
    type.collapsedNodeMinWidth = collapsed.value(QStringLiteral("min_width")).toInt(type.collapsedNodeMinWidth);
    type.collapsedNodeHeight = collapsed.value(QStringLiteral("height")).toInt(type.collapsedNodeHeight);
    type.collapsedCaptionLeftInset = collapsed.value(QStringLiteral("caption_left")).toDouble(type.collapsedCaptionLeftInset);
    type.collapsedCaptionTopInset = collapsed.value(QStringLiteral("caption_top")).toDouble(type.collapsedCaptionTopInset);
    type.collapsedEndpointPortInset = collapsed.value(QStringLiteral("endpoint_inset")).toDouble(type.collapsedEndpointPortInset);

    const QJsonObject arrangement = graphics.value(QStringLiteral("arrangement")).toObject();
    type.linkedEndpointOffsetX = arrangement.value(QStringLiteral("endpoint_offset_x")).toInt(type.linkedEndpointOffsetX);
    type.meshSpacingX = arrangement.value(QStringLiteral("mesh_spacing_x")).toInt(type.meshSpacingX);
    type.meshSpacingY = arrangement.value(QStringLiteral("mesh_spacing_y")).toInt(type.meshSpacingY);
    type.looseEndpointSpacingX = arrangement.value(QStringLiteral("loose_endpoint_spacing_x")).toInt(type.looseEndpointSpacingX);
    type.looseEndpointSpacingY = arrangement.value(QStringLiteral("loose_endpoint_spacing_y")).toInt(type.looseEndpointSpacingY);
    type.looseEndpointMarginY = arrangement.value(QStringLiteral("loose_endpoint_margin_y")).toInt(type.looseEndpointMarginY);
}

QJsonArray moduleArray(const QByteArray& jsonBytes) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    if (document.isArray()) {
        return document.array();
    }

    return document.object().value(QStringLiteral("modules")).toArray();
}

ModuleType loadModuleTypeFromJson(const QJsonObject& moduleObject) {
    ModuleType type;
    type.name = moduleObject.value(QStringLiteral("name")).toString();
    type.paletteLabel = moduleObject.value(QStringLiteral("palette_label")).toString();
    type.description = moduleObject.value(QStringLiteral("description")).toString();
    type.nodeColor = moduleObject.value(QStringLiteral("node_color")).toString();
    type.editorLayout = moduleObject.value(QStringLiteral("editor_layout")).toString();
    type.graphGroup = moduleObject.value(QStringLiteral("graph_group")).toString();

    loadIdentityFromJson(type, moduleObject.value(QStringLiteral("identity")).toObject());

    const QJsonObject capabilities = moduleObject.value(QStringLiteral("capabilities")).toObject();
    type.supportsCollapse = boolValue(capabilities.value(QStringLiteral("supports_collapse")), false);

    loadPortsFromJson(type, moduleObject.value(QStringLiteral("ports")).toArray());
    const ParameterLoadResult parameterResult =
        loadParametersFromJson(type, moduleObject.value(QStringLiteral("parameters")).toArray());

    if (moduleObject.contains(QStringLiteral("config_zone"))) {
        type.configFields = configFieldsFromJson(moduleObject.value(QStringLiteral("config_zone")),
                                                 parameterResult.fieldByName);
    } else {
        type.configFields = parameterResult.autoConfigFields;
    }

    loadPresentationFromJson(type, moduleObject.value(QStringLiteral("presentation")).toObject());

    if (type.paletteLabel.isEmpty()) {
        type.paletteLabel = humanizeIdentifier(type.name);
    }
    if (type.editorLayout.isEmpty()) {
        type.editorLayout = defaultEditorLayout(type);
    }

    normalizeCollapsedMetrics(type);
    return type;
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
        type.paletteLabel = humanizeIdentifier(type.name);
    }
    if (type.editorLayout.isEmpty()) {
        type.editorLayout = defaultEditorLayout(type);
    }

    normalizeCollapsedMetrics(type);
    return type;
}

} // namespace

XmlModuleTypeSource::XmlModuleTypeSource(const QString& bundlePath)
    : m_bundlePath(bundlePath) {}

QHash<QString, ModuleType> XmlModuleTypeSource::loadModuleTypes() {
    QHash<QString, ModuleType> types;
    m_orderedTypeNames.clear();

    QFile file(m_bundlePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
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
                m_orderedTypeNames.push_back(type.name);
                types.insert(type.name, type);
            }
        }
    }

    return types;
}

QStringList XmlModuleTypeSource::orderedTypeNames() const {
    return m_orderedTypeNames;
}

JsonModuleTypeSource::JsonModuleTypeSource(const QString& bundlePath)
    : m_bundlePath(bundlePath) {}

QHash<QString, ModuleType> JsonModuleTypeSource::loadModuleTypes() {
    QHash<QString, ModuleType> types;
    m_orderedTypeNames.clear();

    QFile file(m_bundlePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    for (const auto& moduleValue : moduleArray(file.readAll())) {
        const ModuleType type = loadModuleTypeFromJson(moduleValue.toObject());
        if (type.name.isEmpty()) {
            continue;
        }
        m_orderedTypeNames.push_back(type.name);
        types.insert(type.name, type);
    }

    return types;
}

QStringList JsonModuleTypeSource::orderedTypeNames() const {
    return m_orderedTypeNames;
}

XmlModulePresentationOverlay::XmlModulePresentationOverlay(const QString& presentationPath)
    : m_presentationPath(presentationPath) {}

void XmlModulePresentationOverlay::apply(QHash<QString, ModuleType>& types) {
    if (m_presentationPath.isEmpty()) {
        return;
    }

    QFile file(m_presentationPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QXmlStreamReader xml(&file);
    while (xml.readNextStartElement()) {
        if (xml.name() != u"module-presentations") {
            xml.skipCurrentElement();
            continue;
        }

        while (xml.readNextStartElement()) {
            if (xml.name() != u"module") {
                xml.skipCurrentElement();
                continue;
            }

            const QString moduleTypeName = attributeValue(xml.attributes(), u"type");
            auto typeIt = types.find(moduleTypeName);
            if (typeIt == types.end()) {
                xml.skipCurrentElement();
                continue;
            }

            while (xml.readNextStartElement()) {
                if (xml.name() == u"graphics") {
                    applyGraphicsElement(typeIt.value(), xml);
                } else if (xml.name() == u"config-zone") {
                    applyConfigZoneElement(typeIt.value(), xml);
                } else {
                    xml.skipCurrentElement();
                }
            }
        }
    }
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
            auto typeIt = types.find(moduleTypeName);
            if (typeIt == types.end()) {
                xml.skipCurrentElement();
                continue;
            }

            while (xml.readNextStartElement()) {
                if (xml.name() == u"graphics") {
                    applyGraphicsElement(typeIt.value(), xml);
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
