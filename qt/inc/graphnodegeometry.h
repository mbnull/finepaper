// GraphNodeGeometry calculates node sizes and port positions for visual rendering
#pragma once

#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include "port.h"

class GraphNodeModel;

class GraphNodeGeometry : public QtNodes::AbstractNodeGeometry {
public:
    explicit GraphNodeGeometry(QtNodes::AbstractGraphModel& graphModel);

    QRectF boundingRect(QtNodes::NodeId nodeId) const override;
    QSize size(QtNodes::NodeId nodeId) const override;
    void recomputeSize(QtNodes::NodeId nodeId) const override;

    QPointF portPosition(QtNodes::NodeId nodeId,
                         QtNodes::PortType portType,
                         QtNodes::PortIndex index) const override;

    QPointF portTextPosition(QtNodes::NodeId nodeId,
                             QtNodes::PortType portType,
                             QtNodes::PortIndex portIndex) const override;

    QPointF captionPosition(QtNodes::NodeId nodeId) const override;
    QRectF captionRect(QtNodes::NodeId nodeId) const override;
    QPointF widgetPosition(QtNodes::NodeId nodeId) const override;
    QRect resizeHandleRect(QtNodes::NodeId nodeId) const override;

    static QRectF xpToggleButtonRect(QSize const& nodeSize);

private:
    const GraphNodeModel* modelFor(QtNodes::NodeId nodeId) const;
    QPointF xpPortPosition(const GraphNodeModel& model, const Port& port, QSize const& nodeSize) const;
    QPointF endpointPortPosition(QtNodes::NodeId nodeId,
                                 QtNodes::PortType portType,
                                 QSize const& nodeSize) const;
    bool endpointPortOnLeft(QtNodes::NodeId nodeId) const;
    static qreal stackedPortY(int slot, int slotCount, qreal top, qreal bottom);
};
