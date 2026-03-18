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

QString GraphNodeModel::portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    unsigned int idx = 0;
    for (const auto& port : m_module->ports()) {
        if ((portType == QtNodes::PortType::In && port.direction() == Port::Direction::Input) ||
            (portType == QtNodes::PortType::Out && port.direction() == Port::Direction::Output)) {
            if (idx == portIndex) {
                return port.name();
            }
            idx++;
        }
    }
    return "";
}
