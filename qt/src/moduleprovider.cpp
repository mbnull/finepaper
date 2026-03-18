#include "moduleprovider.h"

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
