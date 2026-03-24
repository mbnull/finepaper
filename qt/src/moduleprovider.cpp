#include "moduleprovider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Return hardcoded module type definitions
std::vector<ModuleType> HardcodedProvider::loadModules() {
    std::vector<ModuleType> types;

    ModuleType cpu;
    cpu.name = "CPU";
    cpu.defaultPorts = {
        Port("mem_bus", Port::Direction::Output, "bus", "mem_bus"),
        Port("irq", Port::Direction::Input, "irq", "irq")
    };
    cpu.defaultParameters = {
        {"frequency", Parameter("frequency", 1000)}
    };
    types.push_back(cpu);

    ModuleType memory;
    memory.name = "Memory";
    memory.defaultPorts = {
        Port("bus", Port::Direction::Input, "bus", "bus")
    };
    memory.defaultParameters = {
        {"size", Parameter("size", 4096)}
    };
    types.push_back(memory);

    ModuleType router;
    router.name = "Router";
    router.defaultPorts = {
        Port("in0", Port::Direction::Input, "data", "in0"),
        Port("in1", Port::Direction::Input, "data", "in1"),
        Port("out0", Port::Direction::Output, "data", "out0"),
        Port("out1", Port::Direction::Output, "data", "out1")
    };
    types.push_back(router);

    ModuleType dma;
    dma.name = "DMA";
    dma.defaultPorts = {
        Port("control", Port::Direction::Input, "control", "control"),
        Port("mem", Port::Direction::Output, "bus", "mem")
    };
    types.push_back(dma);

    return types;
}

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
