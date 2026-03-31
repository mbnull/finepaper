#include "moduleprovider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

JsonBundleProvider::JsonBundleProvider(const QString& bundlePath)
    : m_bundlePath(bundlePath) {}

std::vector<ModuleType> JsonBundleProvider::loadModules() {
    std::vector<ModuleType> types;

    QFile file(m_bundlePath);
    if (!file.open(QIODevice::ReadOnly)) return types;

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

        types.push_back(type);
    }

    return types;
}
