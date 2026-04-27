// GraphNodeGeometry computes node bounds, port coordinates, and hit-testing for QtNodes.
#include "nodeeditor/graphnodegeometry.h"
#include "nodeeditor/graphnodemodel.h"
#include "nodeeditor/endpointattachmentlayout.h"
#include "nodeeditor/portanchorgeometry.h"
#include "modules/moduletypemetadata.h"
#include "common/portlayout.h"
#include <QtNodes/DataFlowGraphModel>
#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <algorithm>
#include <optional>

namespace {

QPointF cardinalPortPosition(const QString& side, QSize const& nodeSize, qreal inset) {
    if (side == "north") return QPointF(nodeSize.width() / 2.0, inset);
    if (side == "east") return QPointF(nodeSize.width() - inset, nodeSize.height() / 2.0);
    if (side == "south") return QPointF(nodeSize.width() / 2.0, nodeSize.height() - inset);
    if (side == "west") return QPointF(inset, nodeSize.height() / 2.0);
    return {};
}

QPointF normalForSide(const QString& side) {
    if (side == QStringLiteral("north")) return QPointF(0.0, -1.0);
    if (side == QStringLiteral("east")) return QPointF(1.0, 0.0);
    if (side == QStringLiteral("south")) return QPointF(0.0, 1.0);
    if (side == QStringLiteral("west")) return QPointF(-1.0, 0.0);
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

    const auto applyStoredSize = [](const Module* module, QSize baseSize) {
        if (!module) {
            return baseSize;
        }

        const auto intParameter = [module](const QString& name) -> std::optional<int> {
            auto it = module->parameters().find(name);
            if (it == module->parameters().end()) {
                return std::nullopt;
            }

            const auto& value = it.value().value();
            if (const auto* intValue = std::get_if<int>(&value)) {
                return *intValue;
            }
            if (const auto* doubleValue = std::get_if<double>(&value)) {
                return static_cast<int>(*doubleValue);
            }
            return std::nullopt;
        };

        if (const std::optional<int> width = intParameter(QStringLiteral("node_width"));
            width.has_value()) {
            baseSize.setWidth(std::max(baseSize.width(), *width));
        }
        if (const std::optional<int> height = intParameter(QStringLiteral("node_height"));
            height.has_value()) {
            baseSize.setHeight(std::max(baseSize.height(), *height));
        }

        return baseSize;
    };

    if (!model || !model->module()) {
        return applyStoredSize(nullptr, {
            std::max(ModuleTypeMetadata::expandedNodeMinWidth(nullptr), captionWidth),
            ModuleTypeMetadata::expandedNodeHeight(nullptr)
        });
    }

    if (ModuleTypeMetadata::editorLayout(model->module()) == QStringLiteral("fallback")) {
        return applyStoredSize(model->module(), fallbackSizeForModel(model, captionWidth));
    }

    if (model->isCollapsed()) {
        return applyStoredSize(model->module(), {
            std::max(ModuleTypeMetadata::collapsedNodeMinWidth(model->module()), captionWidth),
            ModuleTypeMetadata::collapsedNodeHeight(model->module())
        });
    }

    return applyStoredSize(model->module(), {
        std::max(ModuleTypeMetadata::expandedNodeMinWidth(model->module()), captionWidth),
        ModuleTypeMetadata::expandedNodeHeight(model->module())
    });
}

bool shouldUseInterfaceAnchors(const GraphNodeModel& model) {
    return !(model.isCollapsed() && ModuleTypeMetadata::supportsCollapse(model.module()));
}

bool hasStatefulPortLayout(const Module* module) {
    return ModuleTypeMetadata::hasEditorLayout(module, u"mesh_router") ||
           ModuleTypeMetadata::hasEditorLayout(module, u"endpoint");
}

QPointF scaledAnchorPoint(const Module* module,
                          const QSize& nodeSize,
                          double anchorX,
                          double anchorY) {
    const double baselineWidth = static_cast<double>(ModuleTypeMetadata::expandedNodeMinWidth(module));
    const double baselineHeight = static_cast<double>(ModuleTypeMetadata::expandedNodeHeight(module));
    const double xScale = baselineWidth > 0.0 ? static_cast<double>(nodeSize.width()) / baselineWidth : 1.0;
    const double yScale = baselineHeight > 0.0 ? static_cast<double>(nodeSize.height()) / baselineHeight : 1.0;
    return QPointF(anchorX * xScale, anchorY * yScale);
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
    if (ModuleTypeMetadata::hasEditorLayout(model->module(), u"endpoint")) {
        return endpointPortPosition(nodeId, portType, index, *port, nodeSize);
    }

    if (ModuleTypeMetadata::hasEditorLayout(model->module(), u"mesh_router")) {
        return xpPortPosition(*model, *port, nodeSize);
    }

    if (shouldUseInterfaceAnchors(*model)) {
        if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(model->module(), *port)) {
            return scaledAnchorPoint(model->module(), nodeSize, anchor->x, anchor->y);
        }
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

    if (model &&
        !ModuleTypeMetadata::hasEditorLayout(model->module(), u"mesh_router") &&
        !ModuleTypeMetadata::hasEditorLayout(model->module(), u"endpoint") &&
        shouldUseInterfaceAnchors(*model)) {
        if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(model->module(), *port)) {
            if (anchor->labelX.has_value() && anchor->labelY.has_value()) {
                return scaledAnchorPoint(model->module(), nodeSize, *anchor->labelX, *anchor->labelY);
            }
        }
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

QRect GraphNodeGeometry::resizeHandleRect(QtNodes::NodeId nodeId) const {
    const QSize nodeSize = size(nodeId);
    constexpr int handleSize = 14;
    return QRect(nodeSize.width() - handleSize,
                 nodeSize.height() - handleSize,
                 handleSize,
                 handleSize);
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
                                                QtNodes::PortIndex portIndex,
                                                const Port& port,
                                                QSize const& nodeSize) const {
    std::optional<QPointF> hostNormal = connectedNodeHorizontalDirection(nodeId, portType, portIndex);
    if (!hostNormal.has_value() || hostNormal->isNull()) {
        hostNormal = connectedPortNormal(nodeId, portType, portIndex);
    }
    if ((!hostNormal.has_value() || hostNormal->isNull()) && port.direction() == Port::Direction::InOut) {
        if (const GraphNodeModel* model = modelFor(nodeId)) {
            const QtNodes::PortType oppositePortType = portType == QtNodes::PortType::Out
                ? QtNodes::PortType::In
                : QtNodes::PortType::Out;
            const QtNodes::PortIndex oppositePortIndex = model->portIndex(port.id(), oppositePortType);
            if (oppositePortIndex != QtNodes::InvalidPortIndex) {
                hostNormal = connectedNodeHorizontalDirection(nodeId, oppositePortType, oppositePortIndex);
                if (!hostNormal.has_value() || hostNormal->isNull()) {
                    hostNormal = connectedPortNormal(nodeId, oppositePortType, oppositePortIndex);
                }
            }
        }
    }

    if (hostNormal.has_value() && !hostNormal->isNull()) {
        return EndpointAttachmentLayout::endpointAnchorForHostNormal(nodeSize, *hostNormal);
    }

    if (const GraphNodeModel* model = modelFor(nodeId)) {
        if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(model->module(), port)) {
            return scaledAnchorPoint(model->module(), nodeSize, anchor->x, anchor->y);
        }
    }

    const QPointF defaultNormal = portType == QtNodes::PortType::In
        ? QPointF(-1.0, 0.0)
        : QPointF(1.0, 0.0);
    return PortAnchorGeometry::anchorForNormal(nodeSize, defaultNormal);
}

std::optional<QPointF> GraphNodeGeometry::connectedPortNormal(QtNodes::NodeId nodeId,
                                                              QtNodes::PortType portType,
                                                              QtNodes::PortIndex portIndex) const {
    const auto connections = _graphModel.connections(nodeId, portType, portIndex);

    for (const auto& connection : connections) {
        const bool thisIsOutput = portType == QtNodes::PortType::Out &&
                                  connection.outNodeId == nodeId &&
                                  connection.outPortIndex == portIndex;
        const bool thisIsInput = portType == QtNodes::PortType::In &&
                                 connection.inNodeId == nodeId &&
                                 connection.inPortIndex == portIndex;
        if (!thisIsOutput && !thisIsInput) {
            continue;
        }

        const QtNodes::NodeId otherNodeId = thisIsOutput ? connection.inNodeId : connection.outNodeId;
        const QtNodes::PortType otherPortType = thisIsOutput ? QtNodes::PortType::In : QtNodes::PortType::Out;
        const QtNodes::PortIndex otherPortIndex = thisIsOutput ? connection.inPortIndex : connection.outPortIndex;
        const GraphNodeModel* otherModel = modelFor(otherNodeId);
        const Port* otherPort = otherModel ? otherModel->portAt(otherPortType, otherPortIndex) : nullptr;
        if (!otherModel || !otherPort) {
            continue;
        }

        const QSize otherSize = size(otherNodeId);
        const QPointF otherPortPosition = portPosition(otherNodeId, otherPortType, otherPortIndex);
        const QPointF edgeNormal = PortAnchorGeometry::normalFromEdge(otherPortPosition, otherSize);
        if (hasStatefulPortLayout(otherModel->module()) && !edgeNormal.isNull()) {
            return edgeNormal;
        }

        if (const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(otherModel->module(), *otherPort);
            anchor && anchor->normalX.has_value() && anchor->normalY.has_value()) {
            return QPointF(*anchor->normalX, *anchor->normalY);
        }

        if (!edgeNormal.isNull()) {
            return edgeNormal;
        }

        if (PortLayout::isRouterPort(*otherPort)) {
            return normalForSide(PortLayout::routerSideId(otherPort->id()));
        }

        return normalForSide(PortLayout::fallbackSide(*otherPort));
    }

    return std::nullopt;
}

std::optional<QPointF> GraphNodeGeometry::connectedNodeHorizontalDirection(QtNodes::NodeId nodeId,
                                                                          QtNodes::PortType portType,
                                                                          QtNodes::PortIndex portIndex) const {
    const auto connections = _graphModel.connections(nodeId, portType, portIndex);

    for (const auto& connection : connections) {
        const bool thisIsOutput = portType == QtNodes::PortType::Out &&
                                  connection.outNodeId == nodeId &&
                                  connection.outPortIndex == portIndex;
        const bool thisIsInput = portType == QtNodes::PortType::In &&
                                 connection.inNodeId == nodeId &&
                                 connection.inPortIndex == portIndex;
        if (!thisIsOutput && !thisIsInput) {
            continue;
        }

        const QtNodes::NodeId otherNodeId = thisIsOutput ? connection.inNodeId : connection.outNodeId;
        const QPointF nodePosition =
            _graphModel.nodeData(nodeId, QtNodes::NodeRole::Position).value<QPointF>();
        const QPointF otherPosition =
            _graphModel.nodeData(otherNodeId, QtNodes::NodeRole::Position).value<QPointF>();
        const QPointF nodeCenter = nodePosition + QPointF(size(nodeId).width() / 2.0, size(nodeId).height() / 2.0);
        const QPointF otherCenter = otherPosition + QPointF(size(otherNodeId).width() / 2.0, size(otherNodeId).height() / 2.0);
        const qreal dx = nodeCenter.x() - otherCenter.x();
        if (std::abs(dx) <= 0.5) {
            continue;
        }

        return dx > 0.0 ? QPointF(1.0, 0.0) : QPointF(-1.0, 0.0);
    }

    return std::nullopt;
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
