// AnimatedGraphicsView implementation for drag-overlay animations and guarded delete behavior.
#include "nodeeditor/animatedgraphicsview.h"

#include <QFontMetrics>
#include <QPainter>
#include <algorithm>

AnimatedGraphicsView::AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene, QWidget* parent)
    : QtNodes::GraphicsView(scene, parent),
      m_pulseAnimation(new QVariantAnimation(this)),
      m_fadeAnimation(new QVariantAnimation(this)) {
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    m_pulseAnimation->setStartValue(0.0);
    m_pulseAnimation->setEndValue(1.0);
    m_pulseAnimation->setDuration(1400);
    m_pulseAnimation->setLoopCount(-1);
    connect(m_pulseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_pulsePhase = value.toDouble();
        if (m_overlayOpacity > 0.0) {
            viewport()->update();
        }
    });

    m_fadeAnimation->setDuration(180);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fadeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_overlayOpacity = value.toDouble();
        if (m_overlayOpacity <= 0.001 && !m_dragActive) {
            m_overlayOpacity = 0.0;
            m_pulseAnimation->stop();
        }
        viewport()->update();
    });
}

void AnimatedGraphicsView::beginPaletteDrag(const QPoint& viewportPos, const QString& moduleType) {
    m_dragPosition = viewportPos;
    if (!moduleType.isEmpty()) {
        m_moduleType = moduleType;
    }

    const bool needsFadeIn = !m_dragActive && m_overlayOpacity < 0.99;
    m_dragActive = true;

    if (m_pulseAnimation->state() != QAbstractAnimation::Running) {
        m_pulseAnimation->start();
    }
    if (needsFadeIn) {
        animateOverlayTo(1.0);
    } else {
        viewport()->update();
    }
}

void AnimatedGraphicsView::updatePaletteDrag(const QPoint& viewportPos, const QString& moduleType) {
    if (!m_dragActive) {
        beginPaletteDrag(viewportPos, moduleType);
        return;
    }

    m_dragPosition = viewportPos;
    if (!moduleType.isEmpty()) {
        m_moduleType = moduleType;
    }
    viewport()->update();
}

void AnimatedGraphicsView::endPaletteDrag() {
    if (!m_dragActive && m_overlayOpacity == 0.0) {
        return;
    }

    m_dragActive = false;
    animateOverlayTo(0.0);
}

void AnimatedGraphicsView::setEditingLocked(bool locked) {
    m_editingLocked = locked;
}

void AnimatedGraphicsView::onDeleteSelectedObjects() {
    if (m_editingLocked) {
        return;
    }
    QtNodes::GraphicsView::onDeleteSelectedObjects();
}

void AnimatedGraphicsView::drawForeground(QPainter* painter, const QRectF& rect) {
    QtNodes::GraphicsView::drawForeground(painter, rect);

    if (m_overlayOpacity <= 0.0 || m_moduleType.isEmpty()) {
        return;
    }

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(m_overlayOpacity);

    const QRectF viewportRect = viewport()->rect();
    const QRectF frameRect = viewportRect.adjusted(16.0, 16.0, -16.0, -16.0);

    QColor accent = palette().highlight().color();
    QColor textColor = palette().text().color();
    QColor surfaceColor = palette().base().color();

    QColor frameFill = accent;
    frameFill.setAlpha(18);
    QPen framePen(accent, 2.0);
    framePen.setStyle(Qt::DashLine);
    framePen.setDashPattern({8.0, 8.0});
    framePen.setDashOffset(-m_pulsePhase * 18.0);
    QColor frameStroke = accent;
    frameStroke.setAlpha(140);
    framePen.setColor(frameStroke);

    painter->setBrush(frameFill);
    painter->setPen(framePen);
    painter->drawRoundedRect(frameRect, 18.0, 18.0);

    const QPointF center(
        std::clamp(static_cast<qreal>(m_dragPosition.x()), frameRect.left() + 28.0, frameRect.right() - 28.0),
        std::clamp(static_cast<qreal>(m_dragPosition.y()), frameRect.top() + 28.0, frameRect.bottom() - 28.0));

    const qreal glowRadius = 78.0 + (m_pulsePhase * 22.0);
    QRadialGradient glow(center, glowRadius);
    QColor glowInner = accent;
    glowInner.setAlpha(84);
    QColor glowMid = accent;
    glowMid.setAlpha(24);
    QColor glowOuter = accent;
    glowOuter.setAlpha(0);
    glow.setColorAt(0.0, glowInner);
    glow.setColorAt(0.55, glowMid);
    glow.setColorAt(1.0, glowOuter);

    painter->setPen(Qt::NoPen);
    painter->setBrush(glow);
    painter->drawEllipse(center, glowRadius, glowRadius);

    QColor ringColor = accent;
    ringColor.setAlpha(static_cast<int>(170 - (m_pulsePhase * 70.0)));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(ringColor, 2.5));
    painter->drawEllipse(center, 26.0 + (m_pulsePhase * 16.0), 26.0 + (m_pulsePhase * 16.0));

    QColor coreColor = accent;
    coreColor.setAlpha(220);
    painter->setPen(Qt::NoPen);
    painter->setBrush(coreColor);
    painter->drawEllipse(center, 6.0, 6.0);

    QFont chipFont = font();
    chipFont.setBold(true);
    QFontMetrics chipMetrics(chipFont);
    const QSizeF chipSize(chipMetrics.horizontalAdvance(m_moduleType) + 52.0, chipMetrics.height() + 18.0);

    QPointF chipTopLeft = center + QPointF(24.0, -chipSize.height() - 10.0);
    chipTopLeft.setX(std::clamp(chipTopLeft.x(), frameRect.left(), frameRect.right() - chipSize.width()));
    chipTopLeft.setY(std::clamp(chipTopLeft.y(), frameRect.top(), frameRect.bottom() - chipSize.height()));

    const QRectF chipRect(chipTopLeft, chipSize);
    QColor chipFill = surfaceColor;
    chipFill.setAlpha(230);
    QColor chipStroke = accent;
    chipStroke.setAlpha(180);
    painter->setBrush(chipFill);
    painter->setPen(QPen(chipStroke, 1.5));
    painter->drawRoundedRect(chipRect, 14.0, 14.0);

    painter->setPen(Qt::NoPen);
    painter->setBrush(coreColor);
    painter->drawEllipse(QRectF(chipRect.left() + 14.0, chipRect.center().y() - 4.0, 8.0, 8.0));

    QColor chipText = textColor;
    chipText.setAlpha(230);
    painter->setFont(chipFont);
    painter->setPen(chipText);
    painter->drawText(chipRect.adjusted(30.0, 0.0, -14.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, m_moduleType);

    painter->restore();
}

void AnimatedGraphicsView::animateOverlayTo(qreal targetOpacity) {
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_overlayOpacity);
    m_fadeAnimation->setEndValue(targetOpacity);
    m_fadeAnimation->start();
}
