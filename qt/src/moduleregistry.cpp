#include "moduleregistry.h"

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

ModuleRegistry::ModuleRegistry() {
    // Register hardcoded example types
    ModuleType cpu;
    cpu.name = "CPU";
    cpu.defaultPorts = {
        Port("mem_bus", Port::Direction::Output, "bus", "mem_bus"),
        Port("irq", Port::Direction::Input, "irq", "irq")
    };
    cpu.defaultParameters = {
        {"frequency", Parameter("frequency", Parameter::Type::Integer, 1000)}
    };
    registerType(cpu);

    ModuleType memory;
    memory.name = "Memory";
    memory.defaultPorts = {
        Port("bus", Port::Direction::Input, "bus", "bus")
    };
    memory.defaultParameters = {
        {"size", Parameter("size", Parameter::Type::Integer, 4096)}
    };
    registerType(memory);

    ModuleType router;
    router.name = "Router";
    router.defaultPorts = {
        Port("in0", Port::Direction::Input, "data", "in0"),
        Port("in1", Port::Direction::Input, "data", "in1"),
        Port("out0", Port::Direction::Output, "data", "out0"),
        Port("out1", Port::Direction::Output, "data", "out1")
    };
    registerType(router);

    ModuleType dma;
    dma.name = "DMA";
    dma.defaultPorts = {
        Port("control", Port::Direction::Slave),
        Port("mem", Port::Direction::Master)
    };
    registerType(dma);
}

void ModuleRegistry::registerType(const ModuleType& type) {
    m_types[type.name] = type;
}

const ModuleType* ModuleRegistry::getType(const QString& name) const {
    auto it = m_types.find(name);
    return it != m_types.end() ? &it->second : nullptr;
}

QStringList ModuleRegistry::availableTypes() const {
    QStringList types;
    for (const auto& pair : m_types) {
        types.append(pair.first);
    }
    types.sort();
    return types;
}
