#include "features/domain/domain_assignment_task_bar.h"

#include "ui/common/focus_target.h"
#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"

#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

namespace finepaper {
namespace {

QString visibleText(const QString& requested, const QString& fallback) {
    const QString normalized = requested.trimmed();
    return normalized.isEmpty() ? fallback : normalized;
}

void configureTextLabel(QLabel* label,
                        const QString& objectName,
                        const QString& accessibleName) {
    label->setObjectName(objectName);
    label->setAccessibleName(accessibleName);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setFocusPolicy(Qt::NoFocus);
}

void configureAction(QPushButton* button, const QString& objectName) {
    button->setObjectName(objectName);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setMinimumWidth(0);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

} // namespace

DomainAssignmentTaskBar::DomainAssignmentTaskBar(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("finepaper.domainAssignmentTaskBar"));
    setProperty("finepaperRole", QStringLiteral("card"));
    setProperty("finepaperTaskActive", false);
    setAccessibleName(QStringLiteral("Domain assignment task"));
    setFocusPolicy(Qt::NoFocus);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing8,
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing8);
    root->setSpacing(ui::UiMetrics::spacing8);

    m_title = new QLabel(this);
    configureTextLabel(
        m_title,
        QStringLiteral("finepaper.domainAssignmentTaskBar.title"),
        QStringLiteral("Domain assignment task title"));
    m_title->setProperty("finepaperRole", QStringLiteral("subtitle"));
    root->addWidget(m_title);

    m_status = new QLabel(this);
    configureTextLabel(
        m_status,
        QStringLiteral("finepaper.domainAssignmentTaskBar.status"),
        QStringLiteral("Domain assignment task status"));
    m_status->setProperty("finepaperRole", QStringLiteral("muted"));
    root->addWidget(m_status);

    auto* actions = new ui::ResponsiveActionLayout;
    actions->setContentsMargins(0, ui::UiMetrics::spacing4, 0, 0);
    actions->setSpacing(ui::UiMetrics::spacing8);

    m_apply = new QPushButton(QStringLiteral("Apply"), this);
    configureAction(
        m_apply,
        QStringLiteral("finepaper.domainManager.applyAssignment"));
    m_apply->setProperty("finepaperRole", QStringLiteral("primary"));
    actions->addWidget(m_apply);

    m_discard = new QPushButton(QStringLiteral("Discard"), this);
    configureAction(
        m_discard,
        QStringLiteral("finepaper.domainManager.discardAssignment"));
    actions->addWidget(m_discard);
    root->addLayout(actions);

    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (applyRequested) {
            applyRequested();
        }
    });
    connect(m_discard, &QPushButton::clicked, this, [this] {
        if (discardRequested) {
            discardRequested();
        }
    });

    updatePresentation();
}

void DomainAssignmentTaskBar::setState(
    const DomainAssignmentTaskBarState& state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    updatePresentation();
}

QWidget* DomainAssignmentTaskBar::preferredFocusTarget() {
    return ui::firstAvailableFocusTarget(this, {m_apply, m_discard});
}

void DomainAssignmentTaskBar::updatePresentation() {
    const QString title = visibleText(
        m_state.title,
        m_state.taskActive
            ? QStringLiteral("Assign selected nodes")
            : QStringLiteral("Domain assignment"));
    const QString status = visibleText(
        m_state.status,
        m_state.taskActive
            ? QStringLiteral("Review the selection, then apply or discard changes.")
            : QStringLiteral("Select Routers or Endpoints to begin."));
    const QString applyText = visibleText(
        m_state.applyText, QStringLiteral("Apply"));
    const QString discardText = visibleText(
        m_state.discardText, QStringLiteral("Discard"));

    m_title->setText(title);
    m_title->setAccessibleDescription(title);
    m_status->setText(status);
    m_status->setAccessibleDescription(status);

    const bool applyEnabled =
        m_state.taskActive && m_state.applyEnabled;
    const bool discardEnabled =
        m_state.taskActive && m_state.discardEnabled;
    QString applyReason = m_state.applyUnavailableReason.trimmed();
    QString discardReason = m_state.discardUnavailableReason.trimmed();
    if (!m_state.taskActive) {
        if (applyReason.isEmpty()) {
            applyReason = QStringLiteral(
                "Select assignable Routers or Endpoints before applying changes.");
        }
        if (discardReason.isEmpty()) {
            discardReason = QStringLiteral(
                "There are no staged Domain assignment changes to discard.");
        }
    } else {
        if (!applyEnabled && applyReason.isEmpty()) {
            applyReason = QStringLiteral(
                "No staged Domain assignment changes are ready to apply.");
        }
        if (!discardEnabled && discardReason.isEmpty()) {
            discardReason = QStringLiteral(
                "There are no staged Domain assignment changes to discard.");
        }
    }

    m_apply->setText(applyText);
    m_apply->setAccessibleName(applyText);
    m_apply->setAccessibleDescription(
        applyEnabled
            ? QStringLiteral(
                  "Apply the staged Domain assignments to the current selection.")
            : applyReason);
    m_apply->setToolTip(applyEnabled ? QString() : applyReason);
    m_apply->setStatusTip(applyEnabled ? QString() : applyReason);
    m_apply->setEnabled(applyEnabled);

    m_discard->setText(discardText);
    m_discard->setAccessibleName(discardText);
    const QString discardDescription = visibleText(
        m_state.discardAccessibleDescription,
        QStringLiteral(
            "Discard the staged Domain assignment changes without applying them."));
    m_discard->setAccessibleDescription(
        discardEnabled ? discardDescription : discardReason);
    m_discard->setToolTip(discardEnabled ? QString() : discardReason);
    m_discard->setStatusTip(discardEnabled ? QString() : discardReason);
    m_discard->setEnabled(discardEnabled);

    setProperty("finepaperTaskActive", m_state.taskActive);
    setAccessibleName(title);
    QStringList descriptionParts = {status};
    if (!applyEnabled && !applyReason.isEmpty()) {
        descriptionParts.append(applyReason);
    }
    if (!discardEnabled && !discardReason.isEmpty()) {
        descriptionParts.append(discardReason);
    }
    setAccessibleDescription(descriptionParts.join(QStringLiteral(" ")));

    if (layout()) {
        layout()->invalidate();
    }
    updateGeometry();
}

} // namespace finepaper
