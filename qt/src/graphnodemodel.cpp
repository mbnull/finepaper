#include "graphnodemodel.h"
#include "modulelabels.h"
#include "moduletypemetadata.h"
#include "portlayout.h"
#include <QColor>

namespace {

bool boolParameter(const Module* module, const QString& name, bool fallbackValue) {
    if (!module) return fallbackValue;

    const auto it = module->parameters().find(name);
    if (it == module->parameters().end()) return fallbackValue;

    const Parameter::Value value = it.value().value();
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }

    return fallbackValue;
}

bool isVisiblePort(const Module* module, const Port& port) {
    if (!module || !ModuleTypeMetadata::supportsCollapse(module)) {
        return true;
    }

    const bool collapsed = boolParameter(module, "collapsed", true);
    return !collapsed || !PortLayout::isEndpointPort(port);
}

bool matchesPortType(const Port& port, QtNodes::PortType portType) {
    return portType == QtNodes::PortType::In
        ? PortLayout::supportsInput(port)
        : PortLayout::supportsOutput(port);
}

} // namespace

QString GraphNodeModel::caption() const {
    return ModuleLabels::displayName(m_module);
}

// Count ports matching the given direction
unsigned int GraphNodeModel::nPorts(QtNodes::PortType portType) const {
    if (!m_module) return 0;
    unsigned int count = 0;
    for (const auto& port : m_module->ports()) {
        if (!isVisiblePort(m_module, port)) {
            continue;
        }
        if (matchesPortType(port, portType)) {
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

bool GraphNodeModel::isCollapsed() const {
    return m_module && ModuleTypeMetadata::supportsCollapse(m_module) && boolParameter(m_module, "collapsed", true);
}

// Find port by type and index
const Port* GraphNodeModel::portAt(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    if (!m_module) return nullptr;

    unsigned int idx = 0;
    for (const auto& port : m_module->ports()) {
        if (!isVisiblePort(m_module, port)) {
            continue;
        }
        if (matchesPortType(port, portType)) {
            if (idx == portIndex) {
                return &port;
            }
            idx++;
        }
    }

    return nullptr;
}

QtNodes::PortIndex GraphNodeModel::portIndex(const QString& portId, QtNodes::PortType portType) const {
    if (!m_module) return QtNodes::InvalidPortIndex;

    unsigned int idx = 0;
    for (const auto& port : m_module->ports()) {
        if (!isVisiblePort(m_module, port)) {
            continue;
        }
        if (matchesPortType(port, portType)) {
            if (port.id() == portId) {
                return idx;
            }
            ++idx;
        }
    }

    return QtNodes::InvalidPortIndex;
}

QtNodes::NodeDataType GraphNodeModel::dataType(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    const Port* port = portAt(portType, portIndex);
    if (!port) return {"default", "Data"};

    const QString typeId = PortLayout::normalizedType(*port).isEmpty()
        ? QStringLiteral("default")
        : PortLayout::normalizedType(*port);
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

    const QColor background(ModuleTypeMetadata::nodeColor(m_module));
    style.setBackgroundColor(background.isValid() ? background : QColor(224, 224, 224));

    setNodeStyle(style);
}
