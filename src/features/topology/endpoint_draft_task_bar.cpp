#include "features/topology/endpoint_draft_task_bar.h"

#include "ui/common/focus_target.h"
#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"

#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

namespace finepaper {
namespace {

QString actionText(const QString& requested, const QString& fallback) {
    const QString normalized = requested.trimmed();
    return normalized.isEmpty() ? fallback : normalized;
}

void configureStaticLabel(QLabel* label,
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

} // namespace

EndpointDraftTaskBar::EndpointDraftTaskBar(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("finepaper.endpointDraftTaskBar"));
    setProperty("finepaperRole", QStringLiteral("card"));
    setAccessibleName(QStringLiteral("Endpoint draft task"));
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
    configureStaticLabel(
        m_title,
        QStringLiteral("finepaper.endpointDraftTaskTitle"),
        QStringLiteral("Endpoint draft task title"));
    m_title->setProperty("finepaperRole", QStringLiteral("subtitle"));
    root->addWidget(m_title);

    m_guidance = new QLabel(this);
    configureStaticLabel(
        m_guidance,
        QStringLiteral("finepaper.endpointCanvasDraftNotice"),
        QStringLiteral("Endpoint draft guidance"));
    root->addWidget(m_guidance);

    m_connectStatus = new QLabel(this);
    configureStaticLabel(
        m_connectStatus,
        QStringLiteral("finepaper.endpointDraftConnectStatus"),
        QStringLiteral("Endpoint connection status"));
    m_connectStatus->setProperty(
        "finepaperRole", QStringLiteral("warning"));
    root->addWidget(m_connectStatus);

    auto* actions = new ui::ResponsiveActionLayout;
    actions->setContentsMargins(0, ui::UiMetrics::spacing4, 0, 0);
    actions->setSpacing(ui::UiMetrics::spacing8);

    m_review = new QPushButton(QStringLiteral("Review"), this);
    m_review->setObjectName(QStringLiteral("finepaper.endpointDraftReview"));
    m_review->setFocusPolicy(Qt::StrongFocus);
    m_review->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    actions->addWidget(m_review);

    m_connect = new QToolButton(this);
    m_connect->setObjectName(
        QStringLiteral("finepaper.endpointDraftConnect"));
    m_connect->setText(QStringLiteral("Connect"));
    m_connect->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_connect->setPopupMode(QToolButton::InstantPopup);
    m_connect->setProperty("finepaperRole", QStringLiteral("primary"));
    m_connect->setFocusPolicy(Qt::StrongFocus);
    m_connect->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    actions->addWidget(m_connect);

    m_discard = new QPushButton(QStringLiteral("Discard"), this);
    m_discard->setObjectName(
        QStringLiteral("finepaper.endpointDraftDiscard"));
    m_discard->setProperty("finepaperRole", QStringLiteral("danger"));
    m_discard->setFocusPolicy(Qt::StrongFocus);
    m_discard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    actions->addWidget(m_discard);
    root->addLayout(actions);

    connect(m_review, &QPushButton::clicked, this, [this] {
        if (reviewRequested) {
            reviewRequested();
        }
    });
    connect(m_discard, &QPushButton::clicked, this, [this] {
        if (discardRequested) {
            discardRequested();
        }
    });

    updatePresentation();
}

void EndpointDraftTaskBar::setState(
    const EndpointDraftTaskBarState& state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    updatePresentation();
}

void EndpointDraftTaskBar::setConnectMenu(QMenu* menu) {
    if (m_connectMenu == menu && m_connect->menu() == menu) {
        updateConnectPresentation();
        return;
    }
    m_connectMenu = menu;
    m_connect->setMenu(menu);
    if (menu && menu->accessibleName().trimmed().isEmpty()) {
        menu->setAccessibleName(QStringLiteral("Endpoint connection targets"));
    }
    if (menu) {
        connect(menu, &QObject::destroyed, this, [this] {
            if (m_connectMenu.isNull()) {
                updateConnectPresentation();
            }
        });
    }
    updateConnectPresentation();
}

QWidget* EndpointDraftTaskBar::preferredFocusTarget() {
    if (!m_state.visible) {
        return nullptr;
    }
    return ui::firstAvailableFocusTarget(
        this, {m_connect, m_review, m_discard});
}

void EndpointDraftTaskBar::updatePresentation() {
    const QString title = m_state.title.trimmed();
    const QString guidance = m_state.guidance.trimmed();
    m_title->setText(title);
    m_title->setVisible(!title.isEmpty());
    m_title->setAccessibleDescription(title);
    m_guidance->setText(guidance);
    m_guidance->setVisible(!guidance.isEmpty());
    m_guidance->setAccessibleDescription(guidance);

    m_review->setText(actionText(
        m_state.reviewText, QStringLiteral("Review")));
    m_review->setAccessibleName(m_review->text());
    m_review->setAccessibleDescription(
        QStringLiteral("Review unresolved Endpoint drafts on the canvas."));
    m_review->setEnabled(m_state.reviewEnabled);

    m_connect->setText(actionText(
        m_state.connectText, QStringLiteral("Connect")));
    m_connect->setAccessibleName(m_connect->text());

    m_discard->setText(actionText(
        m_state.discardText, QStringLiteral("Discard")));
    m_discard->setAccessibleName(m_discard->text());
    m_discard->setAccessibleDescription(
        m_state.deletesDetachedEndpoints
            ? QStringLiteral(
                  "Review a confirmation before permanently deleting "
                  "disconnected Endpoints and discarding new drafts.")
            : QStringLiteral("Discard unresolved new Endpoint drafts."));
    m_discard->setEnabled(m_state.discardEnabled);
    const QString discardUnavailableReason =
        m_state.discardUnavailableReason.trimmed();
    m_discard->setToolTip(
        m_state.discardEnabled ? QString() : discardUnavailableReason);
    m_discard->setStatusTip(
        m_state.discardEnabled ? QString() : discardUnavailableReason);
    if (!m_state.discardEnabled && !discardUnavailableReason.isEmpty()) {
        m_discard->setAccessibleDescription(discardUnavailableReason);
    }

    updateConnectPresentation();

    QStringList descriptionParts;
    if (!guidance.isEmpty()) {
        descriptionParts.append(guidance);
    }
    const QString unavailableReason =
        m_state.connectUnavailableReason.trimmed();
    if (!unavailableReason.isEmpty()
        && !m_state.connectEnabled) {
        descriptionParts.append(unavailableReason);
    }
    if (!discardUnavailableReason.isEmpty()
        && !m_state.discardEnabled) {
        descriptionParts.append(discardUnavailableReason);
    }
    setAccessibleName(
        title.isEmpty() ? QStringLiteral("Endpoint draft task") : title);
    setAccessibleDescription(descriptionParts.join(QStringLiteral(" ")));

    if (layout()) {
        layout()->invalidate();
    }
    updateGeometry();
    setVisible(m_state.visible);
}

void EndpointDraftTaskBar::updateConnectPresentation() {
    if (!m_connect || !m_connectStatus) {
        return;
    }
    const bool hasMenu = !m_connectMenu.isNull();
    const bool enabled = m_state.connectEnabled && hasMenu;
    m_connect->setEnabled(enabled);

    QString unavailableReason =
        m_state.connectUnavailableReason.trimmed();
    if (!enabled && unavailableReason.isEmpty()) {
        if (!hasMenu) {
            unavailableReason = QStringLiteral(
                "No Endpoint connection targets are available.");
        } else {
            unavailableReason = QStringLiteral(
                "Select an Endpoint draft before connecting it.");
        }
    }
    m_connect->setToolTip(enabled ? QString() : unavailableReason);
    m_connect->setStatusTip(enabled ? QString() : unavailableReason);
    m_connect->setAccessibleDescription(
        enabled
            ? QStringLiteral(
                  "Choose a Router target for the selected Endpoint draft.")
            : unavailableReason);

    const bool showReason = m_state.visible && !enabled
        && !m_state.connectUnavailableReason.trimmed().isEmpty();
    m_connectStatus->setText(
        showReason ? m_state.connectUnavailableReason.trimmed() : QString());
    m_connectStatus->setAccessibleDescription(m_connectStatus->text());
    m_connectStatus->setVisible(showReason);
}

} // namespace finepaper
