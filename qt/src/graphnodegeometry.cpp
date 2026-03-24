#include "graphnodegeometry.h"
#include "graphnodemodel.h"
#include "portlayout.h"
#include <QtNodes/DataFlowGraphModel>
#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <algorithm>

namespace {

QPointF cardinalPortPosition(const QString& side, QSize const& nodeSize, qreal inset) {
    if (side == "north") return QPointF(nodeSize.width() / 2.0, inset);
    if (side == "east") return QPointF(nodeSize.width() - inset, nodeSize.height() / 2.0);
    if (side == "south") return QPointF(nodeSize.width() / 2.0, nodeSize.height() - inset);
    if (side == "west") return QPointF(inset, nodeSize.height() / 2.0);
    return {};
}

QSize sizeForModel(const GraphNodeModel* model) {
    const QString caption = model ? model->caption() : QStringLiteral("Node");
    const int captionWidth = QFontMetrics(QFont()).horizontalAdvance(caption) + 26;

    if (model && model->module() && model->module()->type() == "XP") {
        if (model->isXpCollapsed()) {
            return {std::max(104, captionWidth), 92};
        }
        return {std::max(136, captionWidth), 116};
    }

    return {std::max(104, captionWidth), 54};
}

} // namespace

GraphNodeGeometry::GraphNodeGeometry(QtNodes::AbstractGraphModel& graphModel)
    : QtNodes::AbstractNodeGeometry(graphModel) {
}

QRectF GraphNodeGeometry::boundingRect(QtNodes::NodeId nodeId) const {
    const QSize nodeSize = size(nodeId);
    constexpr qreal margin = 12.0;
    return QRectF(-margin, -margin, nodeSize.width() + margin * 2.0, nodeSize.height() + margin * 2.0);
}

QSize GraphNodeGeometry::size(QtNodes::NodeId nodeId) const {
    return sizeForModel(modelFor(nodeId));
}

void GraphNodeGeometry::recomputeSize(QtNodes::NodeId) const {
}

QPointF GraphNodeGeometry::portPosition(QtNodes::NodeId nodeId,
                                        QtNodes::PortType portType,
                                        QtNodes::PortIndex index) const {
    const GraphNodeModel* model = modelFor(nodeId);
    if (!model) return {};

    const Port* port = model->portAt(portType, index);
    if (!port) return {};

    const QSize nodeSize = size(nodeId);
    if (model->module() && model->module()->type() == "XP") {
        return xpPortPosition(*model, *port, nodeSize);
    }

    return endpointPortPosition(nodeId, portType, nodeSize);
}

QPointF GraphNodeGeometry::portTextPosition(QtNodes::NodeId nodeId,
                                            QtNodes::PortType portType,
                                            QtNodes::PortIndex portIndex) const {
    const QPointF portPos = portPosition(nodeId, portType, portIndex);
    const QSize nodeSize = size(nodeId);
    const GraphNodeModel* model = modelFor(nodeId);
    const Port* port = model ? model->portAt(portType, portIndex) : nullptr;
    const bool onLeft = port && PortLayout::isEndpointPort(*port);

    return onLeft
        ? QPointF(12.0, portPos.y() + 4.0)
        : QPointF(nodeSize.width() - 36.0, portPos.y() + 4.0);
}

QPointF GraphNodeGeometry::captionPosition(QtNodes::NodeId) const {
    return QPointF(12.0, 22.0);
}

QRectF GraphNodeGeometry::captionRect(QtNodes::NodeId nodeId) const {
    const QSize nodeSize = size(nodeId);
    const GraphNodeModel* model = modelFor(nodeId);
    const bool isXp = model && model->module() && model->module()->type() == "XP";
    const qreal leftInset = isXp ? 30.0 : 8.0;
    const qreal topInset = isXp && model->isXpCollapsed() ? 26.0 : 6.0;
    return QRectF(leftInset, topInset, nodeSize.width() - leftInset - 8.0, 20.0);
}

QPointF GraphNodeGeometry::widgetPosition(QtNodes::NodeId) const {
    return QPointF(0.0, 0.0);
}

QRect GraphNodeGeometry::resizeHandleRect(QtNodes::NodeId) const {
    return {};
}

QRectF GraphNodeGeometry::xpToggleButtonRect(QSize const&) {
    return QRectF(8.0, 8.0, 14.0, 14.0);
}

const GraphNodeModel* GraphNodeGeometry::modelFor(QtNodes::NodeId nodeId) const {
    auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&_graphModel);
    return graphModel ? graphModel->delegateModel<GraphNodeModel>(nodeId) : nullptr;
}

QPointF GraphNodeGeometry::xpPortPosition(const GraphNodeModel& model, const Port& port, QSize const& nodeSize) const {
    if (model.isXpCollapsed()) {
        if (PortLayout::isRouterPort(port)) {
            return cardinalPortPosition(PortLayout::routerSideId(port.id()), nodeSize, 0.0);
        }

        static const QStringList endpointSides{QStringLiteral("north"),
                                               QStringLiteral("east"),
                                               QStringLiteral("south"),
                                               QStringLiteral("west")};
        const int slot = std::clamp(PortLayout::endpointPortSlot(port.id()),
                                    0,
                                    static_cast<int>(endpointSides.size()) - 1);
        return cardinalPortPosition(endpointSides.at(slot), nodeSize, 18.0);
    }

    constexpr qreal inset = 16.0;
    const qreal bottom = nodeSize.height() - inset;

    if (PortLayout::isEndpointPort(port)) {
        const int slot = PortLayout::endpointPortSlot(port.id());
        return QPointF(0.0, stackedPortY(slot, PortLayout::kEndpointPortCount, inset, bottom));
    }

    const int slot = PortLayout::routerPortSlot(port.id());
    return QPointF(nodeSize.width(), stackedPortY(slot, PortLayout::kRouterPortCount, inset, bottom));
}

QPointF GraphNodeGeometry::endpointPortPosition(QtNodes::NodeId nodeId,
                                                QtNodes::PortType portType,
                                                QSize const& nodeSize) const {
    const qreal y = nodeSize.height() / 2.0;
    const bool onLeft = endpointPortOnLeft(nodeId);
    const qreal x = portType == QtNodes::PortType::In
        ? (onLeft ? 0.0 : nodeSize.width())
        : (onLeft ? nodeSize.width() : 0.0);
    return QPointF(x, y);
}

bool GraphNodeGeometry::endpointPortOnLeft(QtNodes::NodeId nodeId) const {
    const auto endpointPosition = _graphModel.nodeData(nodeId, QtNodes::NodeRole::Position).value<QPointF>();
    const auto connections = _graphModel.connections(nodeId, QtNodes::PortType::In, 0);

    for (const auto& connection : connections) {
        const QPointF sourcePosition = _graphModel.nodeData(connection.outNodeId, QtNodes::NodeRole::Position).value<QPointF>();
        return sourcePosition.x() <= endpointPosition.x();
    }

    return true;
}

qreal GraphNodeGeometry::stackedPortY(int slot, int slotCount, qreal top, qreal bottom) {
    const qreal span = bottom - top;
    const qreal step = span / static_cast<qreal>(slotCount);
    return top + (static_cast<qreal>(slot) + 0.5) * step;
}
