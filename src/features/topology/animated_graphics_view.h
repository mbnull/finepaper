#pragma once

#include <QtNodes/GraphicsView>

#include <QColor>
#include <QString>
#include <QVariantAnimation>
#include <QVector>

#include <functional>

class QFocusEvent;
class QKeyEvent;
class QPainter;

namespace finepaper {

enum class EndpointDragTarget {
    Canvas,
    AttachToRouter,
    Blocked,
};

struct CanvasDomainLegendEntry {
    QString id;
    QString label;
    QColor color;
};

// Reused from the original NodeEditor canvas: renders a pulsing drag overlay
// while an Endpoint type is being dragged from the runtime Package library.
class AnimatedGraphicsView final : public QtNodes::GraphicsView {
public:
    explicit AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene,
                                  QWidget* parent = nullptr);

    // QtNodes owns the platform Delete shortcut. Route that entry point back
    // through the NoC semantic model instead of its generic graph deletion.
    void onDeleteSelectedObjects() override;
    std::function<void()> semanticDeleteRequested;

    void beginEndpointDrag(const QPoint& viewportPosition,
                           const QString& endpointLabel,
                           EndpointDragTarget target);
    void updateEndpointDrag(const QPoint& viewportPosition,
                            const QString& endpointLabel,
                            EndpointDragTarget target);
    void endEndpointDrag();
    void setPersistentDragMode(QGraphicsView::DragMode mode);
    void setReducedMotion(bool reduced);
    bool reducedMotion() const { return m_reducedMotion; }
    void setDomainLegend(QString layerLabel,
                         QVector<CanvasDomainLegendEntry> entries,
                         QString emptyMessage = {});
    QGraphicsView::DragMode persistentDragMode() const {
        return m_persistentDragMode;
    }

    bool endpointDragActive() const { return m_dragActive; }
    bool endpointDragOverRouter() const {
        return m_dragTarget == EndpointDragTarget::AttachToRouter;
    }
    bool endpointDragBlocked() const {
        return m_dragTarget == EndpointDragTarget::Blocked;
    }

protected:
    void drawForeground(QPainter* painter, const QRectF& rectangle) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void animateOverlayTo(qreal targetOpacity);
    void drawEndpointDragOverlay(QPainter* painter) const;
    void drawDomainLegend(QPainter* painter) const;

    QPoint m_dragPosition;
    QString m_endpointLabel;
    QString m_domainLayerLabel;
    QString m_domainLegendEmptyMessage;
    QVector<CanvasDomainLegendEntry> m_domainLegendEntries;
    QVariantAnimation* m_pulseAnimation = nullptr;
    QVariantAnimation* m_fadeAnimation = nullptr;
    qreal m_pulsePhase = 0.0;
    qreal m_overlayOpacity = 0.0;
    bool m_dragActive = false;
    bool m_reducedMotion = false;
    EndpointDragTarget m_dragTarget = EndpointDragTarget::Canvas;
    QGraphicsView::DragMode m_persistentDragMode =
        QGraphicsView::ScrollHandDrag;
};

} // namespace finepaper
