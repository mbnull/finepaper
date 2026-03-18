#include "graphnodemodel.h"

GraphNodeModel::GraphNodeModel(Module* module) : m_module(module) {}

unsigned int GraphNodeModel::nPorts(QtNodes::PortType portType) const {
    unsigned int count = 0;
    for (const auto& port : m_module->ports()) {
        if ((portType == QtNodes::PortType::In && port.direction() == Port::Direction::Input) ||
            (portType == QtNodes::PortType::Out && port.direction() == Port::Direction::Output)) {
            count++;
        }
    }
    return count;
}

QtNodes::NodeDataType GraphNodeModel::dataType(QtNodes::PortType, QtNodes::PortIndex) const {
    return {"default", "Data"};
}
