// GraphNodeGeometry computes node bounds, port coordinates, and hit-testing for QtNodes.
#include "graphnodegeometry.h"
#include "graphnodemodel.h"
#include "moduletypemetadata.h"
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

struct SideCounts {
    int north = 0;
    int east = 0;
    int south = 0;
    int west = 0;
};

SideCounts fallbackSideCounts(const Module* module) {
    SideCounts counts;
    if (!module) {
        return counts;
    }

    for (const Port& port : module->ports()) {
        const QString side = PortLayout::fallbackSide(port);
        if (side == "north") {
            ++counts.north;
        } else if (side == "east") {
            ++counts.east;
        } else if (side == "south") {
            ++counts.south;
        } else {
            ++counts.west;
        }
    }

    return counts;
}

QSize fallbackSizeForModel(const GraphNodeModel* model, int captionWidth) {
    const Module* module = model ? model->module() : nullptr;
    const SideCounts counts = fallbackSideCounts(module);
    const int horizontalPorts = std::max(counts.north, counts.south);
    const int verticalPorts = std::max(counts.west, counts.east);

    const int width = std::max({
        ModuleTypeMetadata::expandedNodeMinWidth(module),
        captionWidth,
        140,
        84 + (horizontalPorts * 36)
    });
    const int height = std::max(
        ModuleTypeMetadata::expandedNodeHeight(module),
        72 + (verticalPorts * 24) + (horizontalPorts > 0 ? 24 : 0));

    return {width, height};
}

QSize sizeForModel(const GraphNodeModel* model) {
    const QString caption = model ? model->caption() : QString();
    const int captionWidth = QFontMetrics(QFont()).horizontalAdvance(caption) + 26;

    if (!model || !model->module()) {
        return {
            std::max(ModuleTypeMetadata::expandedNodeMinWidth(nullptr), captionWidth),
            ModuleTypeMetadata::expandedNodeHeight(nullptr)
        };
    }

    if (ModuleTypeMetadata::editorLayout(model->module()) == QStringLiteral("fallback")) {
        return fallbackSizeForModel(model, captionWidth);
    }

    if (model->isCollapsed()) {
        return {
            std::max(ModuleTypeMetadata::collapsedNodeMinWidth(model->module()), captionWidth),
            ModuleTypeMetadata::collapsedNodeHeight(model->module())
        };
    }

    return {
        std::max(ModuleTypeMetadata::expandedNodeMinWidth(model->module()), captionWidth),
        ModuleTypeMetadata::expandedNodeHeight(model->module())
    };
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
    if (ModuleTypeMetadata::hasEditorLayout(model->module(), u"mesh_router")) {
        return xpPortPosition(*model, *port, nodeSize);
    }
    if (ModuleTypeMetadata::hasEditorLayout(model->module(), u"endpoint")) {
        return endpointPortPosition(nodeId, portType, nodeSize);
    }

    return fallbackPortPosition(*model, *port, nodeSize);
}

QPointF GraphNodeGeometry::portTextPosition(QtNodes::NodeId nodeId,
                                            QtNodes::PortType portType,
                                            QtNodes::PortIndex portIndex) const {
    const QPointF portPos = portPosition(nodeId, portType, portIndex);
    const QSize nodeSize = size(nodeId);
    const GraphNodeModel* model = modelFor(nodeId);
    const Port* port = model ? model->portAt(portType, portIndex) : nullptr;
    if (!port) {
        return {};
    }

    if (ModuleTypeMetadata::editorLayout(model ? model->module() : nullptr) == QStringLiteral("fallback")) {
        const QString side = PortLayout::fallbackSide(*port);
        if (side == "north") return QPointF(portPos.x() - 28.0, 18.0);
        if (side == "south") return QPointF(portPos.x() - 28.0, nodeSize.height() - 22.0);
        if (side == "east") return QPointF(nodeSize.width() - 68.0, portPos.y() + 4.0);
        return QPointF(12.0, portPos.y() + 4.0);
    }

    const bool onLeft = portPos.x() <= nodeSize.width() / 2.0;

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
    const bool collapsed = model && model->isCollapsed();
    const qreal leftInset = ModuleTypeMetadata::captionLeftInset(model ? model->module() : nullptr, collapsed);
    const qreal topInset = ModuleTypeMetadata::captionTopInset(model ? model->module() : nullptr, collapsed);
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
    if (model.isCollapsed()) {
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
        return cardinalPortPosition(
            endpointSides.at(slot),
            nodeSize,
            ModuleTypeMetadata::collapsedEndpointPortInset(model.module()));
    }

    const qreal inset = ModuleTypeMetadata::expandedPortInset(model.module());
    const qreal bottom = nodeSize.height() - inset;

    if (PortLayout::isEndpointPort(port)) {
        const int slot = PortLayout::endpointPortSlot(port.id());
        return QPointF(0.0, stackedPortY(slot, PortLayout::kEndpointPortCount, inset, bottom));
    }

    const int slot = PortLayout::routerPortSlot(port.id());
    return QPointF(nodeSize.width(), stackedPortY(slot, PortLayout::kRouterPortCount, inset, bottom));
}

QPointF GraphNodeGeometry::fallbackPortPosition(const GraphNodeModel& model,
                                                const Port& port,
                                                QSize const& nodeSize) const {
    const QString side = PortLayout::fallbackSide(port);
    const int slot = fallbackPortSlot(model, port, side);
    const int count = std::max(1, fallbackPortCount(model, side));

    constexpr qreal horizontalInset = 20.0;
    constexpr qreal verticalTop = 28.0;
    const qreal verticalBottom = nodeSize.height() - 18.0;

    if (side == "north") {
        return QPointF(stackedPortX(slot, count, horizontalInset, nodeSize.width() - horizontalInset), 0.0);
    }
    if (side == "south") {
        return QPointF(stackedPortX(slot, count, horizontalInset, nodeSize.width() - horizontalInset),
                       nodeSize.height());
    }
    if (side == "east") {
        return QPointF(nodeSize.width(), stackedPortY(slot, count, verticalTop, verticalBottom));
    }

    return QPointF(0.0, stackedPortY(slot, count, verticalTop, verticalBottom));
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

int GraphNodeGeometry::fallbackPortCount(const GraphNodeModel& model, const QString& side) const {
    Module* module = model.module();
    if (!module) {
        return 0;
    }

    int count = 0;
    for (const Port& port : module->ports()) {
        if (PortLayout::fallbackSide(port) == side) {
            ++count;
        }
    }
    return count;
}

int GraphNodeGeometry::fallbackPortSlot(const GraphNodeModel& model,
                                        const Port& port,
                                        const QString& side) const {
    Module* module = model.module();
    if (!module) {
        return 0;
    }

    int slot = 0;
    for (const Port& candidate : module->ports()) {
        if (PortLayout::fallbackSide(candidate) != side) {
            continue;
        }
        if (candidate.id() == port.id() && candidate.direction() == port.direction()) {
            return slot;
        }
        ++slot;
    }
    return 0;
}

qreal GraphNodeGeometry::stackedPortY(int slot, int slotCount, qreal top, qreal bottom) {
    const qreal span = bottom - top;
    const qreal step = span / static_cast<qreal>(slotCount);
    return top + (static_cast<qreal>(slot) + 0.5) * step;
}

qreal GraphNodeGeometry::stackedPortX(int slot, int slotCount, qreal left, qreal right) {
    const qreal span = right - left;
    const qreal step = span / static_cast<qreal>(slotCount);
    return left + (static_cast<qreal>(slot) + 0.5) * step;
}
