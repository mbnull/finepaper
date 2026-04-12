#include "moduleprovider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
            xml.skipCurrentElement();
        } else if (xml.name() == u"collapsed") {
            const QXmlStreamAttributes collapsedAttrs = xml.attributes();
            type.collapsedNodeMinWidth = intAttribute(collapsedAttrs, u"min_width", type.collapsedNodeMinWidth);
            type.collapsedNodeHeight = intAttribute(collapsedAttrs, u"height", type.collapsedNodeHeight);
            type.collapsedCaptionLeftInset = doubleAttribute(collapsedAttrs, u"caption_left", type.collapsedCaptionLeftInset);
            type.collapsedCaptionTopInset = doubleAttribute(collapsedAttrs, u"caption_top", type.collapsedCaptionTopInset);
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

void overlayPresentationMetadata(QHash<QString, ModuleType>& types, const QString& presentationPath) {
    if (presentationPath.isEmpty()) {
        return;
    }

    QFile file(presentationPath);
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

} // namespace

BundleProvider::BundleProvider(const QString& bundlePath, const QString& presentationPath)
    : m_bundlePath(bundlePath), m_presentationPath(presentationPath) {}

std::vector<ModuleType> BundleProvider::loadModules() {
    QHash<QString, ModuleType> types;
    QFile file(m_bundlePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray modules = doc.object()["modules"].toArray();

    for (const auto& modVal : modules) {
        QJsonObject mod = modVal.toObject();
        ModuleType type;
        type.name = mod["name"].toString();
        type.paletteLabel = mod["palette_label"].toString();
        type.nodeColor = mod["node_color"].toString();
        type.editorLayout = mod["editor_layout"].toString();
        type.graphGroup = mod["graph_group"].toString();

        const QJsonObject identity = mod["identity"].toObject();
        type.externalIdPrefix = identity["external_id_prefix"].toString();
        type.displayPrefix = identity["display_prefix"].toString();
        type.identityWidth = identity["width"].toInt(2);
        type.supportsMeshCoordinates = identity["supports_mesh_coordinates"].toBool(false);

        const QJsonObject capabilities = mod["capabilities"].toObject();
        type.supportsCollapse = capabilities["supports_collapse"].toBool(false);
        if (!type.supportsCollapse) {
            type.collapsedNodeMinWidth = type.expandedNodeMinWidth;
            type.collapsedNodeHeight = type.expandedNodeHeight;
            type.collapsedCaptionLeftInset = type.expandedCaptionLeftInset;
            type.collapsedCaptionTopInset = type.expandedCaptionTopInset;
        }

        for (const auto& portVal : mod["ports"].toArray()) {
            QJsonObject p = portVal.toObject();
            Port::Direction dir = p["direction"].toString() == "input"
                ? Port::Direction::Input : Port::Direction::Output;
            type.defaultPorts.push_back(Port(
                p["id"].toString(), dir, p["type"].toString(), p["name"].toString()));
        }

        for (const auto& paramVal : mod["parameters"].toArray()) {
            QJsonObject p = paramVal.toObject();
            QString pType = p["type"].toString();
            Parameter::Value val;
            if (pType == "int") val = p["default"].toInt();
            else if (pType == "bool") val = p["default"].toBool();
            else val = p["default"].toString();
            type.defaultParameters[p["name"].toString()] = Parameter(p["name"].toString(), val);
        }

        types.insert(type.name, type);
    }

    overlayPresentationMetadata(types, m_presentationPath);

    std::vector<ModuleType> orderedTypes;
    orderedTypes.reserve(types.size());
    for (const auto& module : modules) {
        const QString name = module.toObject()["name"].toString();
        auto it = types.find(name);
        if (it != types.end()) {
            orderedTypes.push_back(it.value());
        }
    }

    return orderedTypes;
}
