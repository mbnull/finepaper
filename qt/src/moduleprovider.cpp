#include "moduleprovider.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>

namespace {

bool parseBoolAttribute(QStringView value, bool fallbackValue = false) {
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

QString attributeValue(const QXmlStreamAttributes& attributes, QStringView name) {
    return attributes.value(name).toString();
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

void applyGraphicsElement(ModuleType& type, QXmlStreamReader& xml) {
    const QXmlStreamAttributes attrs = xml.attributes();
    if (attrs.hasAttribute(u"layout")) {
        type.editorLayout = attributeValue(attrs, u"layout");
    }
    if (attrs.hasAttribute(u"node_color")) {
        type.nodeColor = attributeValue(attrs, u"node_color");
    }
    if (attrs.hasAttribute(u"supports_collapse")) {
        type.supportsCollapse = parseBoolAttribute(attrs.value(u"supports_collapse"), type.supportsCollapse);
    }

    while (xml.readNextStartElement()) {
        if (xml.name() == u"expanded") {
            const QXmlStreamAttributes expandedAttrs = xml.attributes();
            type.expandedNodeMinWidth = intAttribute(expandedAttrs, u"min_width", type.expandedNodeMinWidth);
            type.expandedNodeHeight = intAttribute(expandedAttrs, u"height", type.expandedNodeHeight);
            type.expandedCaptionLeftInset = doubleAttribute(expandedAttrs, u"caption_left", type.expandedCaptionLeftInset);
            type.expandedCaptionTopInset = doubleAttribute(expandedAttrs, u"caption_top", type.expandedCaptionTopInset);
            type.expandedPortInset = doubleAttribute(expandedAttrs, u"port_inset", type.expandedPortInset);
            xml.skipCurrentElement();
        } else if (xml.name() == u"collapsed") {
            const QXmlStreamAttributes collapsedAttrs = xml.attributes();
            type.collapsedNodeMinWidth = intAttribute(collapsedAttrs, u"min_width", type.collapsedNodeMinWidth);
            type.collapsedNodeHeight = intAttribute(collapsedAttrs, u"height", type.collapsedNodeHeight);
            type.collapsedCaptionLeftInset = doubleAttribute(collapsedAttrs, u"caption_left", type.collapsedCaptionLeftInset);
            type.collapsedCaptionTopInset = doubleAttribute(collapsedAttrs, u"caption_top", type.collapsedCaptionTopInset);
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

    if (!type.supportsCollapse) {
        type.collapsedNodeMinWidth = type.expandedNodeMinWidth;
        type.collapsedNodeHeight = type.expandedNodeHeight;
        type.collapsedCaptionLeftInset = type.expandedCaptionLeftInset;
        type.collapsedCaptionTopInset = type.expandedCaptionTopInset;
    }
}

void applyConfigZoneElement(ModuleType& type, QXmlStreamReader& xml) {
    type.configFields.clear();

    while (xml.readNextStartElement()) {
        if (xml.name() == u"field") {
            ModuleConfigField field;
            field.parameterName = attributeValue(xml.attributes(), u"parameter");
            field.label = attributeValue(xml.attributes(), u"label");
            if (!field.parameterName.isEmpty()) {
                type.configFields.push_back(field);
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

QJsonArray moduleArray(const QByteArray& jsonBytes) {
    return QJsonDocument::fromJson(jsonBytes).object()["modules"].toArray();
}

void loadPorts(ModuleType& type, const QJsonArray& ports) {
    for (const auto& portVal : ports) {
        const QJsonObject port = portVal.toObject();
        const Port::Direction direction = port["direction"].toString() == "input"
            ? Port::Direction::Input
            : Port::Direction::Output;
        type.defaultPorts.push_back(Port(
            port["id"].toString(), direction, port["type"].toString(), port["name"].toString()));
    }
}

void loadParameters(ModuleType& type, const QJsonArray& parameters) {
    for (const auto& paramVal : parameters) {
        const QJsonObject parameter = paramVal.toObject();
        const QString parameterType = parameter["type"].toString();
        Parameter::Value defaultValue;
        if (parameterType == "int") {
            defaultValue = parameter["default"].toInt();
        } else if (parameterType == "bool") {
            defaultValue = parameter["default"].toBool();
        } else {
            defaultValue = parameter["default"].toString();
        }
        type.defaultParameters[parameter["name"].toString()] =
            Parameter(parameter["name"].toString(), defaultValue);
    }
}

ModuleType loadModuleType(const QJsonObject& moduleObject) {
    ModuleType type;
    type.name = moduleObject["name"].toString();
    type.paletteLabel = moduleObject["palette_label"].toString();
    type.nodeColor = moduleObject["node_color"].toString();
    type.editorLayout = moduleObject["editor_layout"].toString();
    type.graphGroup = moduleObject["graph_group"].toString();

    const QJsonObject identity = moduleObject["identity"].toObject();
    type.externalIdPrefix = identity["external_id_prefix"].toString();
    type.displayPrefix = identity["display_prefix"].toString();
    type.identityWidth = identity["width"].toInt(2);
    type.supportsMeshCoordinates = identity["supports_mesh_coordinates"].toBool(false);

    const QJsonObject capabilities = moduleObject["capabilities"].toObject();
    type.supportsCollapse = capabilities["supports_collapse"].toBool(false);
    if (!type.supportsCollapse) {
        type.collapsedNodeMinWidth = type.expandedNodeMinWidth;
        type.collapsedNodeHeight = type.expandedNodeHeight;
        type.collapsedCaptionLeftInset = type.expandedCaptionLeftInset;
        type.collapsedCaptionTopInset = type.expandedCaptionTopInset;
    }

    loadPorts(type, moduleObject["ports"].toArray());
    loadParameters(type, moduleObject["parameters"].toArray());
    return type;
}

} // namespace

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
        const ModuleType type = loadModuleType(moduleValue.toObject());
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

    if (orderedTypes.size() == static_cast<std::size_t>(types.size())) {
        return orderedTypes;
    }

    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        if (!orderedTypeNames.contains(it.key())) {
            orderedTypes.push_back(it.value());
        }
    }
    return orderedTypes;
}
