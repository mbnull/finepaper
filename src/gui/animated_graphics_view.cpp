#include "gui/animated_graphics_view.h"

#include <QFontMetrics>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRadialGradient>

#include <algorithm>

namespace finepaper {

AnimatedGraphicsView::AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene,
                                           QWidget* parent)
    : QtNodes::GraphicsView(scene, parent),
      m_pulseAnimation(new QVariantAnimation(this)),
      m_fadeAnimation(new QVariantAnimation(this)) {
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);

    m_pulseAnimation->setStartValue(0.0);
    m_pulseAnimation->setEndValue(1.0);
    m_pulseAnimation->setDuration(1400);
    m_pulseAnimation->setLoopCount(-1);
    connect(m_pulseAnimation, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                m_pulsePhase = value.toDouble();
                if (m_overlayOpacity > 0.0) {
                    viewport()->update();
                }
            });

    m_fadeAnimation->setDuration(180);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fadeAnimation, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                m_overlayOpacity = value.toDouble();
                if (m_overlayOpacity <= 0.001 && !m_dragActive) {
                    m_overlayOpacity = 0.0;
                    m_pulseAnimation->stop();
                }
                viewport()->update();
            });
}

void AnimatedGraphicsView::setPersistentDragMode(
    QGraphicsView::DragMode mode) {
    m_persistentDragMode = mode;
    setDragMode(mode);
}

void AnimatedGraphicsView::beginEndpointDrag(const QPoint& viewportPosition,
                                              const QString& endpointLabel,
                                              bool overRouter) {
    m_dragPosition = viewportPosition;
    m_endpointLabel = endpointLabel;
    m_overRouter = overRouter;
    const bool fadeIn = !m_dragActive && m_overlayOpacity < 0.99;
    m_dragActive = true;
    if (m_pulseAnimation->state() != QAbstractAnimation::Running) {
        m_pulseAnimation->start();
    }
    if (fadeIn) {
        animateOverlayTo(1.0);
    } else {
        viewport()->update();
    }
}

void AnimatedGraphicsView::updateEndpointDrag(const QPoint& viewportPosition,
                                               const QString& endpointLabel,
                                               bool overRouter) {
    if (!m_dragActive) {
        beginEndpointDrag(viewportPosition, endpointLabel, overRouter);
        return;
    }
    m_dragPosition = viewportPosition;
    if (!endpointLabel.isEmpty()) {
        m_endpointLabel = endpointLabel;
    }
    m_overRouter = overRouter;
    viewport()->update();
}

void AnimatedGraphicsView::endEndpointDrag() {
    if (!m_dragActive && m_overlayOpacity == 0.0) {
        return;
    }
    m_dragActive = false;
    m_overRouter = false;
    animateOverlayTo(0.0);
}

void AnimatedGraphicsView::drawForeground(QPainter* painter,
                                          const QRectF& rectangle) {
    QtNodes::GraphicsView::drawForeground(painter, rectangle);
    if (m_overlayOpacity <= 0.0 || m_endpointLabel.isEmpty()) {
        return;
    }

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(m_overlayOpacity);

    const QRectF viewportRectangle = viewport()->rect();
    const QRectF frame = viewportRectangle.adjusted(16.0, 16.0, -16.0, -16.0);
    const QColor accent = m_overRouter
        ? QColor(QStringLiteral("#16a34a"))
        : palette().highlight().color();

    QColor frameFill = accent;
    frameFill.setAlpha(18);
    QColor frameStroke = accent;
    frameStroke.setAlpha(150);
    QPen framePen(frameStroke, 2.0, Qt::DashLine);
    framePen.setDashPattern({8.0, 8.0});
    framePen.setDashOffset(-m_pulsePhase * 18.0);
    painter->setBrush(frameFill);
    painter->setPen(framePen);
    painter->drawRoundedRect(frame, 18.0, 18.0);

    const QPointF center(
        std::clamp(static_cast<qreal>(m_dragPosition.x()),
                   frame.left() + 28.0, frame.right() - 28.0),
        std::clamp(static_cast<qreal>(m_dragPosition.y()),
                   frame.top() + 28.0, frame.bottom() - 28.0));
    const qreal glowRadius = 70.0 + m_pulsePhase * 20.0;
    QRadialGradient glow(center, glowRadius);
    QColor inner = accent;
    inner.setAlpha(84);
    QColor middle = accent;
    middle.setAlpha(24);
    QColor outer = accent;
    outer.setAlpha(0);
    glow.setColorAt(0.0, inner);
    glow.setColorAt(0.55, middle);
    glow.setColorAt(1.0, outer);
    painter->setPen(Qt::NoPen);
    painter->setBrush(glow);
    painter->drawEllipse(center, glowRadius, glowRadius);

    QColor ring = accent;
    ring.setAlpha(static_cast<int>(180 - m_pulsePhase * 70.0));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(ring, 2.5));
    painter->drawEllipse(center,
                         26.0 + m_pulsePhase * 16.0,
                         26.0 + m_pulsePhase * 16.0);

    const QString instruction = m_overRouter
        ? QStringLiteral("Attach to Router")
        : QStringLiteral("Place on canvas");
    const QString chipText = QStringLiteral("%1  ·  %2")
                                 .arg(m_endpointLabel, instruction);
    QFont chipFont = font();
    chipFont.setBold(true);
    const QFontMetrics metrics(chipFont);
    const QSizeF chipSize(metrics.horizontalAdvance(chipText) + 34.0,
                          metrics.height() + 18.0);
    QPointF chipTopLeft = center + QPointF(22.0, -chipSize.height() - 10.0);
    chipTopLeft.setX(std::clamp(chipTopLeft.x(),
                                frame.left(), frame.right() - chipSize.width()));
    chipTopLeft.setY(std::clamp(chipTopLeft.y(),
                                frame.top(), frame.bottom() - chipSize.height()));
    const QRectF chip(chipTopLeft, chipSize);
    QColor chipFill = palette().base().color();
    chipFill.setAlpha(235);
    painter->setBrush(chipFill);
    painter->setPen(QPen(frameStroke, 1.5));
    painter->drawRoundedRect(chip, 12.0, 12.0);
    painter->setFont(chipFont);
    painter->setPen(palette().text().color());
    painter->drawText(chip.adjusted(14.0, 0.0, -14.0, 0.0),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      chipText);
    painter->restore();
}

void AnimatedGraphicsView::keyReleaseEvent(QKeyEvent* event) {
    QtNodes::GraphicsView::keyReleaseEvent(event);
    if (event && event->key() == Qt::Key_Shift) {
        setDragMode(m_persistentDragMode);
    }
}

void AnimatedGraphicsView::focusOutEvent(QFocusEvent* event) {
    QtNodes::GraphicsView::focusOutEvent(event);
    setDragMode(m_persistentDragMode);
}

void AnimatedGraphicsView::animateOverlayTo(qreal targetOpacity) {
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_overlayOpacity);
    m_fadeAnimation->setEndValue(targetOpacity);
    m_fadeAnimation->start();
}

} // namespace finepaper
