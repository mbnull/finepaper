#include "ui/components/empty_state.h"

#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"

#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finepaper::ui {
namespace {

constexpr int preferredCardWidth = 520;

} // namespace

EmptyState::EmptyState(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("finepaper.canvasEmptyStateCard"));
    setProperty("finepaperRole", QStringLiteral("card"));
    setMaximumWidth(560);
    setAccessibleName(QStringLiteral("NoC editor start actions"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        UiMetrics::spacing32, UiMetrics::spacing24,
        UiMetrics::spacing32, UiMetrics::spacing24);
    layout->setSpacing(UiMetrics::spacing12);

    m_eyebrow = new QLabel(this);
    m_eyebrow->setProperty("finepaperRole", QStringLiteral("muted"));
    layout->addWidget(m_eyebrow);

    m_title = new QLabel(this);
    m_title->setProperty("finepaperRole", QStringLiteral("title"));
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_description = new QLabel(this);
    m_description->setProperty("finepaperRole", QStringLiteral("muted"));
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_description);

    m_actions = new ResponsiveActionLayout;
    m_actions->setContentsMargins(0, UiMetrics::spacing8, 0, 0);
    m_actions->setSpacing(UiMetrics::spacing8);
    layout->addLayout(m_actions);
    applyRoleFonts();
}

void EmptyState::setEyebrow(const QString& text) {
    m_eyebrow->setText(text);
    m_eyebrow->setVisible(!text.isEmpty());
    updateGeometry();
}

void EmptyState::setTitle(const QString& text) {
    m_title->setText(text);
    updateGeometry();
}

void EmptyState::setDescription(const QString& text) {
    m_description->setText(text);
    setAccessibleDescription(text);
    updateGeometry();
}

QPushButton* EmptyState::addActionButton(
    const QString& text,
    const QString& role) {
    auto* button = new QPushButton(text, this);
    if (!role.isEmpty()) {
        button->setProperty("finepaperRole", role);
    }
    m_actions->addWidget(button);
    updateGeometry();
    return button;
}

QSize EmptyState::sizeHint() const {
    const int availableMaximum = maximumWidth() < QWIDGETSIZE_MAX
                                     ? maximumWidth()
                                     : preferredCardWidth;
    const int width = (std::max)(
        minimumSizeHint().width(),
        (std::min)(preferredCardWidth, availableMaximum));
    return QSize(width, heightForWidth(width));
}

bool EmptyState::hasHeightForWidth() const {
    return true;
}

int EmptyState::heightForWidth(int width) const {
    if (!layout()) {
        return QFrame::sizeHint().height();
    }
    return layout()->totalHeightForWidth((std::max)(0, width));
}

void EmptyState::changeEvent(QEvent* event) {
    QFrame::changeEvent(event);
    if (event && event->type() == QEvent::FontChange) {
        applyRoleFonts();
        updateGeometry();
    }
}

void EmptyState::applyRoleFonts() {
    if (m_eyebrow) {
        m_eyebrow->setFont(fontForRole(UiFontRole::Label, font()));
    }
    if (m_title) {
        m_title->setFont(fontForRole(UiFontRole::Title, font()));
    }
    if (m_description) {
        m_description->setFont(fontForRole(UiFontRole::Body, font()));
    }
}

} // namespace finepaper::ui
