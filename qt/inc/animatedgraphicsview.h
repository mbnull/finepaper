// AnimatedGraphicsView renders the node canvas and an overlay used during palette drag operations.
#pragma once

#include <QtNodes/GraphicsView>
#include <QVariantAnimation>

class AnimatedGraphicsView final : public QtNodes::GraphicsView {
public:
    explicit AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene, QWidget* parent = nullptr);

    void beginPaletteDrag(const QPoint& viewportPos, const QString& moduleType);
    void updatePaletteDrag(const QPoint& viewportPos, const QString& moduleType);
    void endPaletteDrag();
    void setEditingLocked(bool locked);

    void onDeleteSelectedObjects() override;

protected:
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void animateOverlayTo(qreal targetOpacity);

    QPoint m_dragPosition;
    QString m_moduleType;
    QVariantAnimation* m_pulseAnimation = nullptr;
    QVariantAnimation* m_fadeAnimation = nullptr;
    qreal m_pulsePhase = 0.0;
    qreal m_overlayOpacity = 0.0;
    bool m_dragActive = false;
    bool m_editingLocked = false;
};
