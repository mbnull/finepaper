#pragma once

#include <QtNodes/GraphicsView>

#include <QVariantAnimation>

class QFocusEvent;
class QKeyEvent;

namespace finepaper {

enum class EndpointDragTarget {
    Canvas,
    AttachToRouter,
    Blocked,
};

// Reused from the original NodeEditor canvas: renders a pulsing drag overlay
// while an Endpoint type is being dragged from the runtime Package library.
class AnimatedGraphicsView final : public QtNodes::GraphicsView {
public:
    explicit AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene,
                                  QWidget* parent = nullptr);

    void beginEndpointDrag(const QPoint& viewportPosition,
                           const QString& endpointLabel,
                           EndpointDragTarget target);
    void updateEndpointDrag(const QPoint& viewportPosition,
                            const QString& endpointLabel,
                            EndpointDragTarget target);
    void endEndpointDrag();
    void setPersistentDragMode(QGraphicsView::DragMode mode);
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

    QPoint m_dragPosition;
    QString m_endpointLabel;
    QVariantAnimation* m_pulseAnimation = nullptr;
    QVariantAnimation* m_fadeAnimation = nullptr;
    qreal m_pulsePhase = 0.0;
    qreal m_overlayOpacity = 0.0;
    bool m_dragActive = false;
    EndpointDragTarget m_dragTarget = EndpointDragTarget::Canvas;
    QGraphicsView::DragMode m_persistentDragMode =
        QGraphicsView::ScrollHandDrag;
};

} // namespace finepaper
