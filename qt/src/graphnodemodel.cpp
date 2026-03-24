#include "graphnodemodel.h"
#include "modulelabels.h"
#include <QColor>

QString GraphNodeModel::caption() const {
    return ModuleLabels::displayName(m_module);
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

QString GraphNodeModel::portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    const Port* port = portAt(portType, portIndex);
    return port ? port->name() : "";
}

QtNodes::ConnectionPolicy GraphNodeModel::portConnectionPolicy(QtNodes::PortType, QtNodes::PortIndex) const {
    return QtNodes::ConnectionPolicy::One;
}

void GraphNodeModel::setModule(Module* module) {
    m_module = module;
    applyTypeStyle();
}

const Port* GraphNodeModel::portAt(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    if (!m_module) return nullptr;

    unsigned int idx = 0;
    for (const auto& port : m_module->ports()) {
        if ((portType == QtNodes::PortType::In && port.direction() == Port::Direction::Input) ||
            (portType == QtNodes::PortType::Out && port.direction() == Port::Direction::Output)) {
            if (idx == portIndex) {
                return &port;
            }
            idx++;
        }
    }

    return nullptr;
}

QtNodes::NodeDataType GraphNodeModel::dataType(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    const Port* port = portAt(portType, portIndex);
    if (!port) return {"default", "Data"};

    if (port->type() == "router") {
        return {"router", port->name()};
    }

    const QString typeId = port->type().isEmpty() ? QStringLiteral("default") : port->type();
    return {typeId, port->name().isEmpty() ? typeId : port->name()};
}

void GraphNodeModel::applyTypeStyle() {
    auto style = nodeStyle();
    style.ConnectionPointDiameter = 9.0f;
    style.ConnectionPointColor = QColor(24, 24, 24);
    style.FilledConnectionPointColor = QColor(24, 24, 24);
    style.FontColor = QColor(24, 24, 24);
    style.NormalBoundaryColor = QColor(44, 44, 44);
    style.SelectedBoundaryColor = QColor(214, 108, 32);
    style.PenWidth = 1.5f;
    style.HoveredPenWidth = 2.0f;

    if (m_module && m_module->type() == "XP") {
        style.setBackgroundColor(QColor(124, 185, 232));
    } else if (m_module && m_module->type() == "Endpoint") {
        style.setBackgroundColor(QColor(132, 230, 129));
    } else {
        style.setBackgroundColor(QColor(224, 224, 224));
    }

    setNodeStyle(style);
}
