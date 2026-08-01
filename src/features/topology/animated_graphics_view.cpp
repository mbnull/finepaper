#include "features/topology/animated_graphics_view.h"

#include "features/topology/noc_editor_style.h"
#include "features/topology/topology_text.h"

#include <QBrush>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRadialGradient>

#include <algorithm>
#include <utility>

namespace finepaper {

AnimatedGraphicsView::AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene,
                                           QWidget* parent)
    : QtNodes::GraphicsView(scene, parent),
      m_pulseAnimation(new QVariantAnimation(this)),
      m_fadeAnimation(new QVariantAnimation(this)) {
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);

    m_pulseAnimation->setObjectName(
        QStringLiteral("finepaper.endpointDragPulse"));
    m_pulseAnimation->setStartValue(0.0);
    m_pulseAnimation->setEndValue(1.0);
    m_pulseAnimation->setDuration(480);
    m_pulseAnimation->setEasingCurve(QEasingCurve::InOutSine);
    m_pulseAnimation->setLoopCount(-1);
    connect(m_pulseAnimation, &QVariantAnimation::valueChanged,
            this, [this](const QVariant& value) {
                m_pulsePhase = value.toDouble();
                if (m_overlayOpacity > 0.0) {
                    viewport()->update();
                }
            });

    m_fadeAnimation->setDuration(180);
    m_fadeAnimation->setObjectName(
        QStringLiteral("finepaper.endpointDragFade"));
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

void AnimatedGraphicsView::onDeleteSelectedObjects() {
    if (semanticDeleteRequested) {
        semanticDeleteRequested();
    }
}

void AnimatedGraphicsView::setPersistentDragMode(
    QGraphicsView::DragMode mode) {
    m_persistentDragMode = mode;
    setDragMode(mode);
}

void AnimatedGraphicsView::beginEndpointDrag(const QPoint& viewportPosition,
                                              const QString& endpointLabel,
                                              EndpointDragTarget target) {
    m_dragPosition = viewportPosition;
    m_endpointLabel = endpointLabel;
    m_dragTarget = target;
    const bool fadeIn = !m_dragActive && m_overlayOpacity < 0.99;
    m_dragActive = true;
    if (!m_reducedMotion
        && m_pulseAnimation->state() != QAbstractAnimation::Running) {
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
                                               EndpointDragTarget target) {
    if (!m_dragActive) {
        beginEndpointDrag(viewportPosition, endpointLabel, target);
        return;
    }
    m_dragPosition = viewportPosition;
    if (!endpointLabel.isEmpty()) {
        m_endpointLabel = endpointLabel;
    }
    m_dragTarget = target;
    viewport()->update();
}

void AnimatedGraphicsView::endEndpointDrag() {
    if (!m_dragActive && m_overlayOpacity == 0.0) {
        return;
    }
    m_dragActive = false;
    m_dragTarget = EndpointDragTarget::Canvas;
    animateOverlayTo(0.0);
}

void AnimatedGraphicsView::setReducedMotion(bool reduced) {
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    if (m_reducedMotion) {
        m_pulseAnimation->stop();
        m_fadeAnimation->stop();
        m_pulsePhase = 0.0;
        m_overlayOpacity = m_dragActive ? 1.0 : 0.0;
        viewport()->update();
    } else if (m_dragActive
               && m_pulseAnimation->state()
                   != QAbstractAnimation::Running) {
        m_pulseAnimation->start();
        viewport()->update();
    }
}

void AnimatedGraphicsView::setDomainLegend(
    QString layerLabel,
    QVector<CanvasDomainLegendEntry> entries,
    QString emptyMessage) {
    m_domainLayerLabel = std::move(layerLabel);
    m_domainLegendEntries = std::move(entries);
    m_domainLegendEmptyMessage = std::move(emptyMessage);
    viewport()->update();
}

void AnimatedGraphicsView::drawForeground(QPainter* painter,
                                          const QRectF& rectangle) {
    QtNodes::GraphicsView::drawForeground(painter, rectangle);
    drawEndpointDragOverlay(painter);
    drawDomainLegend(painter);
}

void AnimatedGraphicsView::drawEndpointDragOverlay(QPainter* painter) const {
    if (m_overlayOpacity <= 0.0 || m_endpointLabel.isEmpty()) {
        return;
    }

    const QRectF viewportRectangle = viewport()->rect();
    const QRectF frame = viewportRectangle.adjusted(
        16.0, 16.0, -16.0, -16.0);
    // A dock can transiently squeeze the viewport below the overlay's minimum
    // geometry while Qt is relaying out the workbench. Avoid inverted clamp
    // bounds and simply omit the decorative preview for that frame.
    if (frame.width() < 56.0 || frame.height() < 56.0) {
        return;
    }

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(m_overlayOpacity);

    const NocEditorColors visual = nocEditorColors(palette());
    QColor accent = visual.accent;
    if (m_dragTarget == EndpointDragTarget::AttachToRouter) {
        accent = visual.success;
    } else if (m_dragTarget == EndpointDragTarget::Blocked) {
        accent = visual.error;
    }

    QColor frameFill = accent;
    frameFill.setAlpha(18);
    QColor frameStroke = accent;
    frameStroke.setAlpha(150);
    QPen framePen(frameStroke, 2.0, Qt::DashLine);
    framePen.setDashPattern({8.0, 8.0});
    framePen.setDashOffset(-m_pulsePhase * 18.0);
    framePen.setCapStyle(Qt::RoundCap);
    framePen.setJoinStyle(Qt::RoundJoin);
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
    QPen ringPen(ring, 2.5);
    ringPen.setCapStyle(Qt::RoundCap);
    ringPen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(ringPen);
    painter->drawEllipse(center,
                         26.0 + m_pulsePhase * 16.0,
                         26.0 + m_pulsePhase * 16.0);

    QString instruction = QStringLiteral("Place on canvas");
    if (m_dragTarget == EndpointDragTarget::AttachToRouter) {
        instruction = QStringLiteral("Attach to Router");
    } else if (m_dragTarget == EndpointDragTarget::Blocked) {
        instruction = QStringLiteral("Not an attachment target");
    }
    QFont chipFont = nocEditorFont(NocEditorFontRole::Label, font());
    const QFontMetrics metrics(chipFont);
    const QString requestedChipText = QStringLiteral("%1  ·  %2")
                                          .arg(m_endpointLabel, instruction);
    const int maximumTextWidth = (std::max)(
        1, qFloor(frame.width() - 34.0));
    const QString chipText = metrics.elidedText(
        requestedChipText, Qt::ElideRight, maximumTextWidth);
    const QSizeF chipSize(metrics.horizontalAdvance(chipText) + 34.0,
                          metrics.height() + 18.0);
    QPointF chipTopLeft = center + QPointF(22.0, -chipSize.height() - 10.0);
    chipTopLeft.setX(std::clamp(chipTopLeft.x(),
                                frame.left(), frame.right() - chipSize.width()));
    chipTopLeft.setY(std::clamp(chipTopLeft.y(),
                                frame.top(), frame.bottom() - chipSize.height()));
    const QRectF chip(chipTopLeft, chipSize);
    QColor chipFill = visual.surfaceRaised;
    chipFill.setAlpha(235);
    painter->setBrush(chipFill);
    painter->setPen(QPen(frameStroke, 1.5));
    painter->drawRoundedRect(chip, 12.0, 12.0);
    painter->setFont(chipFont);
    painter->setPen(visual.text);
    painter->drawText(chip.adjusted(14.0, 0.0, -14.0, 0.0),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      chipText);
    painter->restore();
}

void AnimatedGraphicsView::drawDomainLegend(QPainter* painter) const {
    if (m_domainLayerLabel.trimmed().isEmpty() || !viewport()) {
        return;
    }

    const QRect viewportRectangle = viewport()->rect();
    if (viewportRectangle.width() < 400 || viewportRectangle.height() < 160) {
        return;
    }

    const bool compact = viewportRectangle.width() < 720;
    const qsizetype maximumVisibleEntries = compact ? 1 : 6;
    constexpr qreal panelMargin = 16.0;
    constexpr qreal horizontalPadding = 14.0;
    constexpr qreal verticalPadding = 12.0;
    constexpr qreal swatchWidth = 22.0;
    constexpr qreal swatchHeight = 14.0;
    constexpr qreal swatchGap = 9.0;

    const NocEditorColors visual = nocEditorColors(palette());
    const QFont titleFont = nocEditorFont(NocEditorFontRole::Label, font());
    const QFont entryFont = nocEditorFont(NocEditorFontRole::Caption, font());
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics entryMetrics(entryFont);
    const QString title = QStringLiteral("Domain · %1").arg(m_domainLayerLabel);

    const qsizetype visibleEntryCount = (std::min)(
        maximumVisibleEntries, m_domainLegendEntries.size());
    const auto entryText = [this, compact](qsizetype index) {
        const CanvasDomainLegendEntry& entry = m_domainLegendEntries.at(index);
        const QString label = entry.label.isEmpty() ? entry.id : entry.label;
        if (!compact || m_domainLegendEntries.size() <= 1) {
            return label;
        }
        return topology_text::compactDomainLegendEntryText(
            label, m_domainLegendEntries.size());
    };
    QString emptyText = m_domainLegendEmptyMessage.trimmed();
    if (emptyText.isEmpty() && visibleEntryCount == 0) {
        emptyText = QStringLiteral("No assigned domains");
    }

    qreal contentWidth = titleMetrics.horizontalAdvance(title);
    for (qsizetype index = 0; index < visibleEntryCount; ++index) {
        contentWidth = std::max<qreal>(
            contentWidth,
            swatchWidth + swatchGap
                + entryMetrics.horizontalAdvance(entryText(index)));
    }
    if (visibleEntryCount == 0) {
        contentWidth = std::max<qreal>(
            contentWidth, entryMetrics.horizontalAdvance(emptyText));
    } else if (!compact
               && m_domainLegendEntries.size() > visibleEntryCount) {
        const QString moreText = QStringLiteral("+ %1 more").arg(
            m_domainLegendEntries.size() - visibleEntryCount);
        contentWidth = std::max<qreal>(
            contentWidth, entryMetrics.horizontalAdvance(moreText));
    }

    const qreal maximumPanelWidth = compact
        ? viewportRectangle.width() * 0.40
        : std::min<qreal>(
              300.0, viewportRectangle.width() - panelMargin * 2.0);
    const qreal minimumPanelWidth = std::min<qreal>(
        176.0, maximumPanelWidth);
    const qreal panelWidth = std::clamp(
        contentWidth + horizontalPadding * 2.0,
        minimumPanelWidth,
        maximumPanelWidth);
    const qreal rowHeight = std::max<qreal>(22.0, entryMetrics.height() + 5.0);
    const bool hasOverflow = m_domainLegendEntries.size() > visibleEntryCount;
    const bool showsOverflowFooter = hasOverflow && !compact;
    const qsizetype contentRows = visibleEntryCount == 0
        ? 1 : visibleEntryCount + (showsOverflowFooter ? 1 : 0);
    const qreal panelHeight = verticalPadding * 2.0
        + titleMetrics.height() + 8.0
        + static_cast<qreal>(contentRows) * rowHeight;
    const QRectF panel(
        viewportRectangle.right() - panelMargin - panelWidth,
        viewportRectangle.top() + panelMargin,
        panelWidth,
        panelHeight);

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing, true);
    QColor panelFill = visual.surfaceRaised;
    panelFill.setAlpha(242);
    QPen panelPen(visual.outline, 1.0);
    panelPen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(panelPen);
    painter->setBrush(panelFill);
    painter->drawRoundedRect(panel, 10.0, 10.0);

    QRectF titleRect = panel.adjusted(
        horizontalPadding, verticalPadding,
        -horizontalPadding, -verticalPadding);
    titleRect.setHeight(titleMetrics.height());
    painter->setFont(titleFont);
    painter->setPen(visual.text);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      titleMetrics.elidedText(
                          title, Qt::ElideRight, qRound(titleRect.width())));

    qreal rowTop = titleRect.bottom() + 8.0;
    painter->setFont(entryFont);
    for (qsizetype index = 0; index < visibleEntryCount; ++index) {
        const CanvasDomainLegendEntry& entry = m_domainLegendEntries.at(index);
        const QRectF row(panel.left() + horizontalPadding,
                         rowTop,
                         panel.width() - horizontalPadding * 2.0,
                         rowHeight);
        const QRectF swatch(row.left(),
                            row.center().y() - swatchHeight / 2.0,
                            swatchWidth,
                            swatchHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(entry.color);
        painter->drawRoundedRect(swatch, 3.0, 3.0);
        QColor patternInk = nocDomainPatternInk(entry.color, palette());
        patternInk.setAlpha(150);
        painter->setBrush(QBrush(patternInk, nocDomainPattern(entry.id)));
        painter->drawRoundedRect(swatch, 3.0, 3.0);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(visual.outlineStrong, 1.0));
        painter->drawRoundedRect(swatch, 3.0, 3.0);

        const QRectF textRect = row.adjusted(
            swatchWidth + swatchGap, 0.0, 0.0, 0.0);
        painter->setPen(visual.text);
        painter->drawText(
            textRect, Qt::AlignLeft | Qt::AlignVCenter,
            entryMetrics.elidedText(
                entryText(index), Qt::ElideRight, qRound(textRect.width())));
        rowTop += rowHeight;
    }

    if (visibleEntryCount == 0 || showsOverflowFooter) {
        const QRectF footer(panel.left() + horizontalPadding,
                            rowTop,
                            panel.width() - horizontalPadding * 2.0,
                            rowHeight);
        const QString footerText = visibleEntryCount == 0
            ? emptyText
            : QStringLiteral("+ %1 more").arg(
                  m_domainLegendEntries.size() - visibleEntryCount);
        painter->setPen(visual.mutedText);
        painter->drawText(
            footer, Qt::AlignLeft | Qt::AlignVCenter,
            entryMetrics.elidedText(
                footerText, Qt::ElideRight, qRound(footer.width())));
    }
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
    if (m_reducedMotion) {
        m_fadeAnimation->stop();
        m_overlayOpacity = targetOpacity;
        if (m_overlayOpacity <= 0.001 && !m_dragActive) {
            m_overlayOpacity = 0.0;
        }
        viewport()->update();
        return;
    }
    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_overlayOpacity);
    m_fadeAnimation->setEndValue(targetOpacity);
    m_fadeAnimation->start();
}

} // namespace finepaper
