#include "graphnodemodel.h"

QString GraphNodeModel::caption() const {
    return m_module ? m_module->type() : "Node";
}

unsigned int GraphNodeModel::nPorts(QtNodes::PortType portType) const {
    if (!m_module) return 0;
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
    if (!m_module) return "";
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
