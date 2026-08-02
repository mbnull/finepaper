#include "ui/workbench/inspector_summary_panel.h"

#include "ui/common/focus_target.h"
#include "ui/theme/ui_tokens.h"
#include "ui/layouts/responsive_action_layout.h"
#include "ui/workbench/workbench_config.h"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

namespace finepaper::ui {
namespace {

void configureTextLabel(QLabel* label, const QString& objectName, const QString& accessibleName,
                        const QString& role = QString()) {
    label->setObjectName(objectName);
    label->setAccessibleName(accessibleName);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);

    QSizePolicy policy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    label->setSizePolicy(policy);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    if (!role.isEmpty()) {
        label->setProperty("finepaperRole", role);
    }
}

QString joinedAccessibleDescription(const QStringList& parts) {
    QStringList populatedParts;
    populatedParts.reserve(parts.size());
    for (const QString& part : parts) {
        if (!part.trimmed().isEmpty()) {
            populatedParts.push_back(part.trimmed());
        }
    }
    return populatedParts.join(QStringLiteral(". "));
}

void setOptionalLabelText(QLabel* label, const QString& text) {
    label->setText(text);
    label->setVisible(!text.trimmed().isEmpty());
}

} // namespace

InspectorSummaryPanel::InspectorSummaryPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.inspectorSummaryPanel"));
    setAccessibleName(QStringLiteral("Inspector context summary"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(UiMetrics::spacing12);

    m_designContext = new QWidget(this);
    m_designContext->setObjectName(QStringLiteral("finepaper.inspectorDesignContext"));
    m_designContext->setAccessibleName(QStringLiteral("Design context"));
    m_designContext->setMinimumWidth(0);
    m_designContext->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* designLayout = new QVBoxLayout(m_designContext);
    designLayout->setContentsMargins(0, 0, 0, 0);
    designLayout->setSpacing(UiMetrics::spacing4);

    m_designTitle = new QLabel(m_designContext);
    configureTextLabel(m_designTitle, QStringLiteral("finepaper.designOverview"),
                       QStringLiteral("Design title"), QStringLiteral("subtitle"));
    designLayout->addWidget(m_designTitle);

    m_designMetadata = new QLabel(m_designContext);
    configureTextLabel(m_designMetadata, QStringLiteral("finepaper.designMetadata"),
                       QStringLiteral("Design details"), QStringLiteral("muted"));
    designLayout->addWidget(m_designMetadata);

    m_designAvailability = new QLabel(m_designContext);
    configureTextLabel(m_designAvailability, QStringLiteral("finepaper.designAvailability"),
                       QStringLiteral("Design availability"), QStringLiteral("warning"));
    designLayout->addWidget(m_designAvailability);
    rootLayout->addWidget(m_designContext);

    m_selectionContext = new QFrame(this);
    m_selectionContext->setObjectName(workbench::selectionInspectorName);
    m_selectionContext->setAccessibleName(QStringLiteral("Selection context"));
    m_selectionContext->setProperty("finepaperRole", QStringLiteral("card"));
    m_selectionContext->setMinimumWidth(0);
    m_selectionContext->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* selectionLayout = new QVBoxLayout(m_selectionContext);
    selectionLayout->setContentsMargins(UiMetrics::spacing12, UiMetrics::spacing12,
                                        UiMetrics::spacing12, UiMetrics::spacing12);
    selectionLayout->setSpacing(UiMetrics::spacing4);

    m_selectionTitle = new QLabel(m_selectionContext);
    configureTextLabel(m_selectionTitle, QStringLiteral("finepaper.inspectorSelectionTitle"),
                       QStringLiteral("Selection title"), QStringLiteral("subtitle"));
    selectionLayout->addWidget(m_selectionTitle);

    m_selectionMetadata = new QLabel(m_selectionContext);
    configureTextLabel(m_selectionMetadata, QStringLiteral("finepaper.inspectorSelectionMetadata"),
                       QStringLiteral("Selection details"), QStringLiteral("muted"));
    selectionLayout->addWidget(m_selectionMetadata);

    m_selectionDetail = new QLabel(m_selectionContext);
    configureTextLabel(m_selectionDetail, QStringLiteral("finepaper.inspectorSelectionDetail"),
                       QStringLiteral("Selection guidance"));
    selectionLayout->addWidget(m_selectionDetail);

    m_contextActions = new QWidget(this);
    m_contextActions->setObjectName(
        QStringLiteral("finepaper.inspectorContextActions"));
    auto* actionLayout = new ResponsiveActionLayout(m_contextActions);
    actionLayout->setContentsMargins(0, UiMetrics::spacing8, 0, 0);
    actionLayout->setSpacing(UiMetrics::spacing8);
    m_editDomainAssignments = new QPushButton(
        tr("Edit Domain assignments"), m_contextActions);
    m_editDomainAssignments->setObjectName(
        QStringLiteral("finepaper.inspectorEditDomainAssignments"));
    actionLayout->addWidget(m_editDomainAssignments);
    m_reviewDiagnostics = new QPushButton(
        tr("Review diagnostics"), m_contextActions);
    m_reviewDiagnostics->setObjectName(
        QStringLiteral("finepaper.inspectorReviewDiagnostics"));
    actionLayout->addWidget(m_reviewDiagnostics);
    // Contextual task routes are the primary action for the current
    // selection. Keep them before descriptive design metadata so they remain
    // reachable in compact windows and at large system font sizes.
    rootLayout->insertWidget(0, m_contextActions);
    rootLayout->addWidget(m_selectionContext);

    connect(m_editDomainAssignments, &QPushButton::clicked,
            this, [this] {
                if (editDomainAssignmentsRequested) {
                    editDomainAssignmentsRequested();
                }
            });
    connect(m_reviewDiagnostics, &QPushButton::clicked,
            this, [this] {
                if (reviewDiagnosticsRequested) {
                    reviewDiagnosticsRequested();
                }
            });

    applyRoleFonts();
    setDesignSummary({});
    setSelectionSummary(std::nullopt);
    setContextActions({});
}

void InspectorSummaryPanel::setContextActions(
    const InspectorContextActions& actions) {
    m_editDomainAssignments->setVisible(actions.editDomainAssignments);
    m_reviewDiagnostics->setVisible(actions.reviewDiagnostics);
    m_contextActions->setVisible(
        actions.editDomainAssignments || actions.reviewDiagnostics);
    updateGeometry();
}

QWidget* InspectorSummaryPanel::preferredFocusTarget() {
    return firstAvailableFocusTarget(
        this,
        {m_editDomainAssignments, m_reviewDiagnostics,
         m_selectionTitle, m_designTitle});
}

void InspectorSummaryPanel::setDesignSummary(const InspectorDesignSummary& summary) {
    setOptionalLabelText(m_designTitle, summary.title);
    setOptionalLabelText(m_designMetadata, summary.metadata);
    setOptionalLabelText(m_designAvailability, summary.availability);

    const QString description =
        joinedAccessibleDescription({summary.title, summary.metadata, summary.availability});
    m_designContext->setAccessibleDescription(description);
    setAccessibleDescription(description);
    updateGeometry();
}

void InspectorSummaryPanel::setSelectionSummary(
    const std::optional<InspectorSelectionSummary>& summary) {
    if (!summary) {
        m_selectionTitle->clear();
        m_selectionMetadata->clear();
        m_selectionDetail->clear();
        m_selectionContext->setAccessibleDescription(QString());
        m_selectionContext->hide();
        updateGeometry();
        return;
    }

    setOptionalLabelText(m_selectionTitle, summary->title);
    setOptionalLabelText(m_selectionMetadata, summary->metadata);
    setOptionalLabelText(m_selectionDetail, summary->detail);
    m_selectionContext->setAccessibleDescription(
        joinedAccessibleDescription({summary->title, summary->metadata, summary->detail}));
    m_selectionContext->show();
    updateGeometry();
}

void InspectorSummaryPanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event && event->type() == QEvent::FontChange) {
        applyRoleFonts();
        updateGeometry();
    }
}

void InspectorSummaryPanel::applyRoleFonts() {
    if (m_designTitle) {
        m_designTitle->setFont(fontForRole(UiFontRole::Subtitle, font()));
    }
    if (m_designMetadata) {
        m_designMetadata->setFont(fontForRole(UiFontRole::Caption, font()));
    }
    if (m_designAvailability) {
        m_designAvailability->setFont(fontForRole(UiFontRole::Caption, font()));
    }
    if (m_selectionTitle) {
        m_selectionTitle->setFont(fontForRole(UiFontRole::Subtitle, font()));
    }
    if (m_selectionMetadata) {
        m_selectionMetadata->setFont(fontForRole(UiFontRole::Caption, font()));
    }
    if (m_selectionDetail) {
        m_selectionDetail->setFont(fontForRole(UiFontRole::Body, font()));
    }
}

} // namespace finepaper::ui
