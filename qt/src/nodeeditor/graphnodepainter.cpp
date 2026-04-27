// GraphNodePainter handles custom node visuals including title, ports, and XP toggle UI.
#include "nodeeditor/graphnodepainter.h"
#include "nodeeditor/graphnodegeometry.h"
#include "nodeeditor/graphnodemodel.h"
#include "modules/moduletypemetadata.h"
#include "nodeeditor/portcolors.h"
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QPainter>
#include <QFontMetricsF>
#include <QSet>
#include <algorithm>

namespace {

QColor nodeBackground(const GraphNodeModel* model) {
    if (!model || !model->module()) return QColor(224, 224, 224);
    const QColor background(ModuleTypeMetadata::nodeColor(model->module()));
    return background.isValid() ? background : QColor(224, 224, 224);
}

void drawXpToggleButton(QPainter* painter,
                        const GraphNodeModel& model,
                        QSize const& nodeSize) {
    const QRectF buttonRect = GraphNodeGeometry::xpToggleButtonRect(nodeSize);

    painter->setPen(QPen(QColor(48, 48, 48), 1.0));
    painter->setBrush(QColor(248, 248, 248, 230));
    painter->drawRoundedRect(buttonRect, 3.5, 3.5);

    painter->setPen(QColor(30, 30, 30));
    painter->setFont(QFont(QStringLiteral("Sans Serif"), 8, QFont::Bold));
    painter->drawText(buttonRect, Qt::AlignCenter, model.isCollapsed() ? QStringLiteral("+") : QStringLiteral("-"));
}

void drawResizeHandle(QPainter* painter, const QRect& rect) {
    if (rect.isEmpty()) {
        return;
    }

    painter->setPen(QPen(QColor(52, 52, 52, 180), 1.1));
    painter->setBrush(QColor(255, 255, 255, 80));
    painter->drawRect(rect.adjusted(2, 2, -1, -1));

    const int right = rect.right() - 3;
    const int bottom = rect.bottom() - 3;
    painter->drawLine(QPoint(right - 7, bottom), QPoint(right, bottom - 7));
    painter->drawLine(QPoint(right - 3, bottom), QPoint(right, bottom - 3));
}

QRectF routerLabelRect(const QString& side, const QPointF& center) {
    if (side == "north") return QRectF(center.x() - 30.0, center.y() + 8.0, 60.0, 14.0);
    if (side == "south") return QRectF(center.x() - 30.0, center.y() - 22.0, 60.0, 14.0);
    if (side == "east") return QRectF(center.x() - 64.0, center.y() - 8.0, 56.0, 16.0);
    return QRectF(center.x() + 8.0, center.y() - 8.0, 56.0, 16.0);
}

QRectF fallbackLabelRect(const QString& side, const QPointF& center, QSize const& nodeSize) {
    if (side == "north") return QRectF(center.x() - 30.0, 10.0, 60.0, 14.0);
    if (side == "south") return QRectF(center.x() - 30.0, nodeSize.height() - 24.0, 60.0, 14.0);
    if (side == "east") return QRectF(nodeSize.width() / 2.0, center.y() - 8.0, nodeSize.width() / 2.0 - 14.0, 16.0);
    return QRectF(14.0, center.y() - 8.0, nodeSize.width() / 2.0 - 18.0, 16.0);
}

QRectF meshRouterLabelRect(const Port& port, const QPointF& center, QSize const& nodeSize) {
    if (PortLayout::isEndpointPort(port)) {
        return QRectF(center.x() + 12.0,
                      center.y() - 9.0,
                      std::min<qreal>(72.0, nodeSize.width() / 2.0 - 18.0),
                      18.0);
    }

    return QRectF(std::max<qreal>(nodeSize.width() / 2.0 + 10.0, center.x() - 76.0),
                  center.y() - 9.0,
                  std::min<qreal>(68.0, nodeSize.width() / 2.0 - 18.0),
                  18.0);
}

QRectF centeredTextRect(const QPointF& center, const QString& text, const QFont& font) {
    const qreal width = std::max<qreal>(24.0, QFontMetricsF(font).horizontalAdvance(text) + 8.0);
    return QRectF(center.x() - (width / 2.0), center.y() - 8.0, width, 16.0);
}

QFont fittedLabelFont(const QString& text, qreal maxWidth, int basePointSize, int weight = QFont::Normal) {
    QFont font(QStringLiteral("Sans Serif"), basePointSize, weight);
    for (int pointSize = basePointSize; pointSize >= 5; --pointSize) {
        font.setPointSize(pointSize);
        if (QFontMetricsF(font).horizontalAdvance(text) <= maxWidth) {
            return font;
        }
    }
    return font;
}

void drawPorts(QPainter* painter,
               QtNodes::NodeGraphicsObject& ngo,
               const GraphNodeModel* model,
               QtNodes::PortType portType,
               QSet<QString>& paintedPortIds,
               QSet<QString>& paintedRouterSides) {
    if (!model) return;

    auto const& geometry = ngo.nodeScene()->nodeGeometry();
    const unsigned int portCount = model->nPorts(portType);
    const bool collapsedNode = model->isCollapsed();
    const QSize nodeSize = geometry.size(ngo.nodeId());
    const bool fallbackLayout = model->module() &&
                                ModuleTypeMetadata::editorLayout(model->module()) == QStringLiteral("fallback");
    const bool meshRouterLayout = model->module() &&
                                  ModuleTypeMetadata::hasEditorLayout(model->module(), u"mesh_router");

    for (unsigned int index = 0; index < portCount; ++index) {
        const Port* port = model->portAt(portType, index);
        if (!port) continue;
        if (collapsedNode && PortLayout::isEndpointPort(*port)) {
            continue;
        }
        if (paintedPortIds.contains(port->id())) {
            continue;
        }

        if (PortLayout::isRouterPort(*port)) {
            const QString side = PortLayout::routerSideId(port->id());
            if (paintedRouterSides.contains(side)) {
                continue;
            }
            paintedRouterSides.insert(side);
        }
        paintedPortIds.insert(port->id());

        const QPointF center = geometry.portPosition(ngo.nodeId(), portType, index);
        const QColor fill = PortColors::colorForPort(*port);

        painter->setPen(QPen(QColor(38, 38, 38), 1.25));
        painter->setBrush(fill);
        painter->drawEllipse(center, 5.5, 5.5);

        if (meshRouterLayout && !collapsedNode) {
            const QString label = ModuleTypeMetadata::interfaceLabel(model->module(), *port);
            if (!label.isEmpty()) {
                const QRectF textRect = meshRouterLabelRect(*port, center, nodeSize);
                painter->setPen(QColor(22, 22, 22));
                painter->setFont(fittedLabelFont(label, textRect.width() - 4.0, 8, QFont::DemiBold));
                painter->drawText(textRect, Qt::AlignCenter, label);
            }
            continue;
        }

        if (!collapsedNode) {
            const ModuleInterfaceAnchor* anchor = ModuleTypeMetadata::interfaceAnchor(model->module(), *port);
            const QString label = ModuleTypeMetadata::interfaceLabel(model->module(), *port);
            if (anchor && !label.isEmpty()) {
                const QPointF labelCenter = geometry.portTextPosition(ngo.nodeId(), portType, index);
                const QFont labelFont = fittedLabelFont(label, 78.0, 7);
                const QRectF textRect = centeredTextRect(labelCenter, label, labelFont);
                painter->setPen(QColor(22, 22, 22));
                painter->setFont(labelFont);
                painter->drawText(textRect, Qt::AlignCenter, label);
                continue;
            }
        }

        if (PortLayout::isRouterPort(*port)) {
            const QString side = PortLayout::routerSideId(port->id());
            const QRectF textRect = collapsedNode
                ? routerLabelRect(side, center)
                : QRectF(center.x() - 22.0, center.y() - 8.0, 14.0, 16.0);
            painter->setPen(QColor(22, 22, 22));
            painter->setFont(fittedLabelFont(port->name(), textRect.width() - 4.0, 7, QFont::Bold));
            painter->drawText(textRect, Qt::AlignCenter, port->name());
        } else if (fallbackLayout && !port->name().isEmpty()) {
            const QString side = PortLayout::fallbackSide(*port);
            const QRectF textRect = fallbackLabelRect(side, center, nodeSize);
            painter->setPen(QColor(32, 32, 32));
            painter->setFont(QFont(QStringLiteral("Sans Serif"), 7));
            painter->drawText(textRect, Qt::AlignCenter, port->name());
        }
    }
}

} // namespace

void GraphNodePainter::paint(QPainter* painter, QtNodes::NodeGraphicsObject& ngo) const {
    auto* graphModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&ngo.graphModel());
    auto* model = graphModel ? graphModel->delegateModel<GraphNodeModel>(ngo.nodeId()) : nullptr;
    auto const& geometry = ngo.nodeScene()->nodeGeometry();
    const QSize nodeSize = geometry.size(ngo.nodeId());
    const QRectF bodyRect(0.0, 0.0, nodeSize.width(), nodeSize.height());

    painter->setRenderHint(QPainter::Antialiasing, true);

    QPen borderPen(ngo.isSelected() ? QColor(214, 108, 32) : QColor(44, 44, 44),
                   ngo.isSelected() ? 2.4 : 1.4);
    painter->setPen(borderPen);
    painter->setBrush(nodeBackground(model));
    painter->drawRoundedRect(bodyRect, 8.0, 8.0);

    if (model && ModuleTypeMetadata::supportsCollapse(model->module())) {
        drawXpToggleButton(painter, *model, nodeSize);
    }

    painter->setPen(QColor(22, 22, 22));
    const bool expandedMeshRouter = model &&
                                    !model->isCollapsed() &&
                                    ModuleTypeMetadata::hasEditorLayout(model->module(), u"mesh_router");
    painter->setFont(QFont(QStringLiteral("Sans Serif"), expandedMeshRouter ? 8 : 9, QFont::DemiBold));
    const QRectF captionRect = expandedMeshRouter
        ? QRectF(28.0, 4.0, nodeSize.width() - 56.0, 14.0)
        : geometry.captionRect(ngo.nodeId());
    painter->drawText(captionRect, Qt::AlignCenter, model ? model->caption() : QString());

    QSet<QString> paintedRouterSides;
    QSet<QString> paintedPortIds;
    drawPorts(painter, ngo, model, QtNodes::PortType::In, paintedPortIds, paintedRouterSides);
    drawPorts(painter, ngo, model, QtNodes::PortType::Out, paintedPortIds, paintedRouterSides);
    drawResizeHandle(painter, geometry.resizeHandleRect(ngo.nodeId()));
}
