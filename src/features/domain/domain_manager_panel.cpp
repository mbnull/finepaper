#include "features/domain/domain_manager_panel.h"

#include "features/domain/domain_assignment_task_bar.h"
#include "features/domain/domain_instance_dialog.h"
#include "features/domain/presentation/domain_text.h"
#include "ui/common/focus_target.h"
#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QString diagnosticSummary(const QVector<Diagnostic>& diagnostics) {
    QStringList lines;
    for (const Diagnostic& diagnostic : diagnostics) {
        lines.append(QStringLiteral("<b>%1</b>: %2%3")
                         .arg(diagnostic.code.toHtmlEscaped(),
                              diagnostic.message.toHtmlEscaped(),
                              diagnostic.path.isEmpty()
                                  ? QString()
                                  : QStringLiteral(" <code>%1</code>")
                                        .arg(diagnostic.path.toHtmlEscaped())));
    }
    return lines.join(QStringLiteral("<br>"));
}

QString assignmentStateId(DomainAssignmentAggregateState state) {
    switch (state) {
    case DomainAssignmentAggregateState::Unavailable:
        return QStringLiteral("unavailable");
    case DomainAssignmentAggregateState::NoEligible:
        return QStringLiteral("no-eligible");
    case DomainAssignmentAggregateState::Unassigned:
        return QStringLiteral("unassigned");
    case DomainAssignmentAggregateState::Common:
        return QStringLiteral("common");
    case DomainAssignmentAggregateState::Mixed:
        return QStringLiteral("mixed");
    }
    return QStringLiteral("unavailable");
}

QString domainDisplayName(const DomainDefinition& domain) {
    return domain.name.trimmed().isEmpty() ? domain.id : domain.name;
}

QString assignmentLimitsText(
    const QVector<DomainAssignmentRule>& rules) {
    QStringList limits;
    for (const DomainAssignmentRule& rule : rules) {
        limits.append(
            domain_text::domainAssignmentConstraintText(rule));
    }
    return limits.join(QLatin1Char(' '));
}

QString assignmentPatchFeedback(
    const DomainAssignmentPatchEvaluation& evaluation) {
    if (evaluation.accepted) {
        return {};
    }
    if (!evaluation.violatingElement || !evaluation.violatedRule) {
        return QStringLiteral(
            "Cannot apply this incomplete Domain assignment change.");
    }

    const DomainAssignmentRule& rule = *evaluation.violatedRule;
    const QString kind = domain_text::elementKindDisplayText(
        evaluation.violatingElement->kind);
    const QString count = evaluation.resultingAssignmentCount == 1
        ? QStringLiteral("1 Domain assignment")
        : QStringLiteral("%1 Domain assignments")
              .arg(evaluation.resultingAssignmentCount);
    if (!rule.isValid()) {
        return QStringLiteral(
                   "Cannot apply: the Package assignment rule for %1 %2 is "
                   "invalid.")
            .arg(kind, evaluation.violatingElement->id);
    }
    if (evaluation.resultingAssignmentCount < rule.minimumAssignments) {
        return QStringLiteral(
                   "Cannot apply: %1 %2 would have %3; minimum is %4.")
            .arg(kind,
                 evaluation.violatingElement->id,
                 count,
                 QString::number(rule.minimumAssignments));
    }
    if (rule.maximumAssignments
        && evaluation.resultingAssignmentCount > *rule.maximumAssignments) {
        return QStringLiteral(
                   "Cannot apply: %1 %2 would have %3; maximum is %4.")
            .arg(kind,
                 evaluation.violatingElement->id,
                 count,
                 QString::number(*rule.maximumAssignments));
    }
    return QStringLiteral("Cannot apply this Domain assignment change.");
}

void allowHorizontalShrink(QWidget* widget) {
    if (!widget) {
        return;
    }
    widget->setMinimumWidth(0);
    QSizePolicy policy = widget->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    widget->setSizePolicy(policy);
}

} // namespace

DomainManagerPanel::DomainManagerPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.domainManager"));
    setAccessibleName(QStringLiteral("Domain Manager"));
    setFocusPolicy(Qt::StrongFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        ui::UiMetrics::spacing8, ui::UiMetrics::spacing8,
        ui::UiMetrics::spacing8, ui::UiMetrics::spacing8);

    m_contentScroll = new QScrollArea(this);
    m_contentScroll->setObjectName(
        QStringLiteral("finepaper.domainManagerScroll"));
    m_contentScroll->setAccessibleName(QStringLiteral("Domain Manager content"));
    m_contentScroll->setWidgetResizable(true);
    m_contentScroll->setFrameShape(QFrame::NoFrame);
    m_contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(m_contentScroll);
    allowHorizontalShrink(content);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(ui::UiMetrics::spacing8);
    contentLayout->setSizeConstraint(QLayout::SetNoConstraint);
    m_contentScroll->setWidget(content);
    root->addWidget(m_contentScroll, 1);

    m_status = new QLabel(QStringLiteral(
        "Open a design to configure its Package-defined Domains."));
    m_status->setObjectName(QStringLiteral("finepaper.domainManager.status"));
    m_status->setAccessibleName(QStringLiteral("Domain Manager status"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    allowHorizontalShrink(m_status);
    contentLayout->addWidget(m_status);

    m_completeConfiguration = new QPushButton(
        QStringLiteral("Open full Domain configuration"));
    m_completeConfiguration->setObjectName(
        QStringLiteral("finepaper.domainManager.completeConfiguration"));
    m_completeConfiguration->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    allowHorizontalShrink(m_completeConfiguration);
    contentLayout->addWidget(m_completeConfiguration);

    m_typeControls = new QWidget(content);
    m_typeControls->setObjectName(
        QStringLiteral("finepaper.domainManager.typeControls"));
    allowHorizontalShrink(m_typeControls);
    auto* typeRow = new QGridLayout(m_typeControls);
    typeRow->setContentsMargins(0, 0, 0, 0);
    auto* typeLabel = new QLabel(
        QStringLiteral("Domain type"), m_typeControls);
    typeRow->addWidget(typeLabel, 0, 0);
    m_typeSelector = new QComboBox(m_typeControls);
    m_typeSelector->setObjectName(
        QStringLiteral("finepaper.domainManager.typeSelector"));
    m_typeSelector->setAccessibleName(QStringLiteral("Domain type"));
    typeLabel->setBuddy(m_typeSelector);
    typeRow->addWidget(m_typeSelector, 0, 1);
    m_showOnCanvas = new QPushButton(
        QStringLiteral("Show on canvas"), m_typeControls);
    m_showOnCanvas->setObjectName(
        QStringLiteral("finepaper.domainManager.showOnCanvas"));
    m_showOnCanvas->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    typeRow->addWidget(m_showOnCanvas, 1, 0, 1, 2);
    typeRow->setColumnStretch(1, 1);
    contentLayout->addWidget(m_typeControls);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("finepaper.domainManager.tabs"));
    allowHorizontalShrink(m_tabs);

    m_instancesPage = new QWidget;
    m_instancesPage->setObjectName(
        QStringLiteral("finepaper.domainManager.instancesPage"));
    allowHorizontalShrink(m_instancesPage);
    auto* instancesLayout = new QVBoxLayout(m_instancesPage);
    instancesLayout->setContentsMargins(
        0, ui::UiMetrics::spacing8, 0, 0);
    m_instances = new QTableWidget;
    m_instances->setObjectName(
        QStringLiteral("finepaper.domainManager.instanceView"));
    m_instances->setAccessibleName(QStringLiteral("Domain instances and legend"));
    m_instances->setColumnCount(5);
    m_instances->setHorizontalHeaderLabels({
        QStringLiteral("Marker"), QStringLiteral("Name"), QStringLiteral("ID"),
        QStringLiteral("Members"), QStringLiteral("Crossings")});
    m_instances->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_instances->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_instances->setSelectionMode(QAbstractItemView::SingleSelection);
    m_instances->setAlternatingRowColors(true);
    m_instances->verticalHeader()->hide();
    m_instances->horizontalHeader()->setStretchLastSection(true);
    instancesLayout->addWidget(m_instances, 1);
    auto* instanceButtons = new ui::ResponsiveActionLayout;
    m_addDomain = new QPushButton(QStringLiteral("Add…"));
    m_addDomain->setObjectName(
        QStringLiteral("finepaper.domainManager.addDomain"));
    m_editDomain = new QPushButton(QStringLiteral("Edit…"));
    m_editDomain->setObjectName(
        QStringLiteral("finepaper.domainManager.editDomain"));
    m_removeDomain = new QPushButton(QStringLiteral("Delete…"));
    m_removeDomain->setObjectName(
        QStringLiteral("finepaper.domainManager.deleteDomain"));
    m_removeDomain->setProperty(
        "finepaperRole", QStringLiteral("danger"));
    m_selectDomainMembers = new QPushButton(QStringLiteral("Select members"));
    m_selectDomainMembers->setObjectName(
        QStringLiteral("finepaper.domainManager.selectMembers"));
    m_selectDomainMembers->setToolTip(
        QStringLiteral("Select every Router or Endpoint assigned to this Domain instance."));
    instanceButtons->addWidget(m_addDomain);
    instanceButtons->addWidget(m_editDomain);
    instanceButtons->addWidget(m_removeDomain);
    instanceButtons->addWidget(m_selectDomainMembers);
    instancesLayout->addLayout(instanceButtons);
    m_tabs->addTab(m_instancesPage, QStringLiteral("Instances"));

    m_assignmentPage = new QWidget;
    m_assignmentPage->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentPage"));
    allowHorizontalShrink(m_assignmentPage);
    auto* assignmentLayout = new QVBoxLayout(m_assignmentPage);
    assignmentLayout->setContentsMargins(
        0, ui::UiMetrics::spacing8, 0, 0);
    m_selectionHelpers = new QWidget(m_assignmentPage);
    m_selectionHelpers->setObjectName(
        QStringLiteral("finepaper.domainManager.selectionHelpers"));
    allowHorizontalShrink(m_selectionHelpers);
    auto* selectionButtons = new ui::ResponsiveActionLayout(
        m_selectionHelpers);
    m_selectAllEligible = new QPushButton(QStringLiteral("Select all eligible"));
    m_selectAllEligible->setObjectName(
        QStringLiteral("finepaper.domainManager.selectAllEligible"));
    m_selectAllEligible->setToolTip(
        QStringLiteral("Select every Mesh Router or Endpoint to which this Domain type applies."));
    m_selectUnassigned = new QPushButton(QStringLiteral("Select unassigned"));
    m_selectUnassigned->setObjectName(
        QStringLiteral("finepaper.domainManager.selectUnassigned"));
    m_selectUnassigned->setToolTip(
        QStringLiteral("Select applicable Routers and Endpoints with no assignment for this Domain type."));
    selectionButtons->addWidget(m_selectAllEligible);
    selectionButtons->addWidget(m_selectUnassigned);
    assignmentLayout->addWidget(m_selectionHelpers);
    m_assignmentState = new QLabel;
    m_assignmentState->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentState"));
    m_assignmentState->setWordWrap(true);
    allowHorizontalShrink(m_assignmentState);
    assignmentLayout->addWidget(m_assignmentState);
    m_singleAssignment = new QComboBox;
    m_singleAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentEditor"));
    allowHorizontalShrink(m_singleAssignment);
    assignmentLayout->addWidget(m_singleAssignment);
    m_multipleAssignment = new QListWidget;
    m_multipleAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentEditor.multiple"));
    m_multipleAssignment->setAccessibleName(
        QStringLiteral("Domain assignments for the current selection"));
    m_multipleAssignment->setAlternatingRowColors(true);
    allowHorizontalShrink(m_multipleAssignment);
    assignmentLayout->addWidget(m_multipleAssignment, 1);
    m_assignmentFeedback = new QLabel;
    m_assignmentFeedback->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentFeedback"));
    m_assignmentFeedback->setAccessibleName(
        QStringLiteral("Domain assignment constraint feedback"));
    m_assignmentFeedback->setTextFormat(Qt::PlainText);
    m_assignmentFeedback->setWordWrap(true);
    allowHorizontalShrink(m_assignmentFeedback);
    m_assignmentFeedback->hide();
    assignmentLayout->addWidget(m_assignmentFeedback);
    m_clearAssignment = new QPushButton(
        QStringLiteral("Remove assignments"));
    m_clearAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.clearAssignment"));
    m_clearAssignment->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    allowHorizontalShrink(m_clearAssignment);
    assignmentLayout->addWidget(m_clearAssignment);
    m_tabs->addTab(m_assignmentPage, QStringLiteral("Assign selection"));

    contentLayout->addWidget(m_tabs, 1);

    m_diagnostics = new QLabel;
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.domainManager.diagnostics"));
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextFormat(Qt::RichText);
    allowHorizontalShrink(m_diagnostics);
    m_diagnostics->hide();
    contentLayout->addWidget(m_diagnostics);

    m_assignmentTaskBar = new DomainAssignmentTaskBar(this);
    root->addWidget(m_assignmentTaskBar);

    connect(m_typeSelector, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_updating) {
            refreshCurrentType();
        }
    });
    connect(m_showOnCanvas, &QPushButton::clicked, this, [this] {
        if (showDomainLayerRequested && !currentDomainType().isEmpty()) {
            showDomainLayerRequested(currentDomainType());
        }
    });
    connect(m_completeConfiguration, &QPushButton::clicked, this, [this] {
        if (completeConfigurationRequested) {
            completeConfigurationRequested();
        }
    });
    connect(m_instances, &QTableWidget::itemSelectionChanged,
            this, [this] { updateActionState(); });
    connect(m_addDomain, &QPushButton::clicked,
            this, [this] { addDomain(); });
    connect(m_editDomain, &QPushButton::clicked,
            this, [this] { editDomain(); });
    connect(m_removeDomain, &QPushButton::clicked,
            this, [this] { removeDomain(); });
    connect(m_selectDomainMembers, &QPushButton::clicked,
            this, [this] { selectDomainMembers(); });
    connect(m_selectAllEligible, &QPushButton::clicked,
            this, [this] { selectAllEligible(); });
    connect(m_selectUnassigned, &QPushButton::clicked,
            this, [this] { selectUnassigned(); });
    connect(m_singleAssignment, &QComboBox::currentIndexChanged,
            this, [this] {
                if (!m_updating) {
                    updateSingleAssignmentEdited();
                }
            });
    connect(m_multipleAssignment, &QListWidget::itemChanged,
            this, [this](QListWidgetItem* item) {
                handleAssignmentItemChanged(item);
            });
    connect(m_clearAssignment, &QPushButton::clicked,
            this, [this] { clearAssignment(); });
    m_assignmentTaskBar->applyRequested = [this] {
        applyAssignment();
    };
    m_assignmentTaskBar->discardRequested = [this] {
        const bool hadPendingChanges = m_assignmentEdited;
        if (hadPendingChanges) {
            discardAssignment();
        }
        if (m_assignmentTaskActive && assignmentTaskExitRequested) {
            assignmentTaskExitRequested();
        } else if (!m_assignmentTaskActive && !hadPendingChanges
                   && m_tabs && m_instancesPage) {
            m_tabs->setCurrentWidget(m_instancesPage);
        }
    };
    connect(m_tabs, &QTabWidget::currentChanged,
            this, [this](int) { updateAssignmentTaskPresentation(); });

    setContext(nullptr, nullptr, nullptr, {});
}

void DomainManagerPanel::setContext(const NocDesign* design,
                                    const ResolvedDesign* resolved,
                                    const PackageDefinition* package,
                                    const QString& canvasDomainType) {
    const QString previousType = currentDomainType();
    m_design = design;
    m_resolved = resolved;
    m_package = package;
    m_canvasDomainType = canvasDomainType;

    QString preferredType = previousType;
    if (preferredType.isEmpty() && package
        && package->domainType(canvasDomainType)) {
        preferredType = canvasDomainType;
    }
    rebuildTypeSelector(preferredType);

    if (!m_design) {
        m_status->setText(QStringLiteral(
            "Create or open a design before configuring Domains."));
    } else if (!m_package) {
        m_status->setText(QStringLiteral(
            "The design Package metadata is unavailable. Domain data remains "
            "read-only until the exact Package is restored."));
    } else if (!formatVersionSupportsDomains(m_design->formatVersion)
               || !formatVersionSupportsDomains(m_package->formatVersion)) {
        m_status->setText(QStringLiteral(
            "This Design/Package version does not expose Domain configuration."));
    } else if (m_package->domainTypes.isEmpty()) {
        m_status->setText(QStringLiteral(
            "This Package explicitly declares no Domain types."));
    } else {
        m_status->setText(QStringLiteral(
            "Assign selected Routers or Endpoints here. Use full Domain "
            "configuration for relationships and crossing policies; Routers "
            "remain fixed projections of the Mesh."));
    }
    refreshCurrentType();
}

void DomainManagerPanel::setSelection(QVector<ElementRef> selection) {
    if (selection == m_selection) {
        return;
    }
    m_selection = std::move(selection);
    if (m_assignmentEdited) {
        if (!m_selectionChangedWhileEditing) {
            m_selectionChangedWhileEditing = true;
            m_assignmentState->setText(
                m_assignmentState->text()
                + QStringLiteral(
                    "\nCanvas selection changed. Pending edits still target the "
                    "original %1 eligible item(s); Apply or discard them first.")
                      .arg(m_assignment.eligibleElements));
        }
        updateActionState();
        return;
    }
    refreshAssignment();
}

void DomainManagerPanel::setCanvasDomainType(const QString& domainType) {
    if (m_canvasDomainType == domainType) {
        return;
    }
    m_canvasDomainType = domainType;
    updateActionState();
}

void DomainManagerPanel::setBusy(bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    updateActionState();
}

void DomainManagerPanel::setDiagnostics(
    const QVector<Diagnostic>& diagnostics) {
    const QString summary = diagnosticSummary(diagnostics);
    m_diagnostics->setText(summary);
    m_diagnostics->setVisible(!summary.isEmpty());
}

bool DomainManagerPanel::canActivateAssignmentPage() const {
    return m_assignmentEdited
        || assignableDomainTypeForSelection().has_value();
}

bool DomainManagerPanel::activateAssignmentPage() {
    if (!m_assignmentEdited) {
        const std::optional<QString> assignableType =
            assignableDomainTypeForSelection();
        if (!assignableType) {
            return false;
        }
        const int typeIndex = m_typeSelector->findData(*assignableType);
        if (typeIndex >= 0
            && typeIndex != m_typeSelector->currentIndex()) {
            m_typeSelector->setCurrentIndex(typeIndex);
        }
    }
    if (m_tabs && m_assignmentPage) {
        m_tabs->setCurrentWidget(m_assignmentPage);
    }
    if (!m_tabs || m_tabs->currentWidget() != m_assignmentPage) {
        return false;
    }
    QTimer::singleShot(0, this, [this] {
        if (!m_contentScroll || !m_tabs
            || m_tabs->currentWidget() != m_assignmentPage) {
            return;
        }
        if (m_assignmentTaskActive) {
            m_contentScroll->verticalScrollBar()->setValue(0);
            return;
        }
        QWidget* editor = m_singleAssignment->isVisible()
            ? static_cast<QWidget*>(m_singleAssignment)
            : static_cast<QWidget*>(m_multipleAssignment);
        m_contentScroll->ensureWidgetVisible(
            editor, ui::UiMetrics::spacing12, ui::UiMetrics::spacing12);
    });
    return true;
}

bool DomainManagerPanel::setAssignmentTaskActive(bool active) {
    if (m_assignmentTaskActive == active) {
        return true;
    }
    if (active) {
        if (!activateAssignmentPage()) {
            return false;
        }
    }
    m_assignmentTaskActive = active;
    m_status->setVisible(!active);
    m_completeConfiguration->setVisible(!active);
    m_typeControls->setVisible(!active);
    m_selectionHelpers->setVisible(!active);
    if (m_tabs && m_tabs->tabBar()) {
        m_tabs->tabBar()->setVisible(!active);
    }
    updateAssignmentTaskPresentation();
    return true;
}

QWidget* DomainManagerPanel::preferredFocusTarget() {
    if ((!m_design || !m_package || m_typeSelector->count() == 0)
        && m_status) {
        return ui::firstAvailableFocusTarget(this, {m_status});
    }
    return ui::firstAvailableFocusTarget(
        this,
        {m_typeSelector, m_instances, m_completeConfiguration});
}

QWidget* DomainManagerPanel::preferredAssignmentFocusTarget() {
    return ui::firstAvailableFocusTarget(
        this,
        {m_singleAssignment, m_multipleAssignment,
         m_assignmentTaskBar
             ? m_assignmentTaskBar->preferredFocusTarget() : nullptr,
         m_typeSelector, m_completeConfiguration});
}

void DomainManagerPanel::discardPendingAssignmentChanges() {
    discardAssignment();
}

std::optional<QString>
DomainManagerPanel::assignableDomainTypeForSelection() const {
    if (!m_design || !m_package || m_selection.isEmpty()
        || !formatVersionSupportsDomains(m_design->formatVersion)
        || !formatVersionSupportsDomains(m_package->formatVersion)) {
        return std::nullopt;
    }

    const auto aggregateIsAssignable = [](
        const DomainAssignmentAggregate& aggregate) {
        return aggregate.eligibleElements > 0
            && !aggregate.domainIds.isEmpty()
            && aggregate.assignmentRulesAreValid();
    };
    const auto typeIsAssignable = [this, &aggregateIsAssignable](
                                      const QString& typeId) {
        if (typeId.isEmpty() || !m_package->domainType(typeId)) {
            return false;
        }
        const DomainAssignmentAggregate aggregate =
            buildDomainAssignmentAggregate(
                *m_design, *m_package, m_selection, typeId);
        return aggregateIsAssignable(aggregate);
    };

    QString currentType = currentDomainType();
    if (m_assignment.domainType == currentType
        && aggregateIsAssignable(m_assignment)) {
        return currentType;
    }
    for (const DomainTypeDefinition& type : m_package->domainTypes) {
        if (type.id != currentType && typeIsAssignable(type.id)) {
            return type.id;
        }
    }
    return std::nullopt;
}

QString DomainManagerPanel::currentDomainType() const {
    return m_typeSelector ? m_typeSelector->currentData().toString() : QString();
}

const DomainTypeDefinition* DomainManagerPanel::selectedType() const {
    return m_package ? m_package->domainType(currentDomainType()) : nullptr;
}

const DomainDefinition* DomainManagerPanel::selectedDomain() const {
    if (!m_design) {
        return nullptr;
    }
    const QString domainId = selectedDomainId();
    const auto iterator = std::find_if(
        m_design->domains.cbegin(), m_design->domains.cend(),
        [&domainId](const DomainDefinition& domain) {
            return domain.id == domainId;
        });
    return iterator == m_design->domains.cend() ? nullptr : &(*iterator);
}

void DomainManagerPanel::rebuildTypeSelector(const QString& preferredType) {
    const QSignalBlocker blocker(m_typeSelector);
    m_updating = true;
    m_typeSelector->clear();
    if (m_package && m_design
        && formatVersionSupportsDomains(m_design->formatVersion)
        && formatVersionSupportsDomains(m_package->formatVersion)) {
        for (const DomainTypeDefinition& type : m_package->domainTypes) {
            m_typeSelector->addItem(
                domain_text::domainTypeDisplayText(type), type.id);
        }
    }
    int selectedIndex = m_typeSelector->findData(preferredType);
    if (selectedIndex < 0 && m_typeSelector->count() > 0) {
        selectedIndex = 0;
    }
    m_typeSelector->setCurrentIndex(selectedIndex);
    m_updating = false;
}

void DomainManagerPanel::refreshCurrentType() {
    setDiagnostics({});
    refreshInstances();
    refreshAssignment();
    updateActionState();
}

void DomainManagerPanel::refreshInstances() {
    const QString previousId = selectedDomainId();
    m_instances->setRowCount(0);
    if (!m_design || !m_package || !selectedType()) {
        return;
    }

    DomainPresentationSnapshot snapshot;
    if (m_resolved) {
        snapshot = buildDomainPresentationSnapshot(
            *m_resolved, *m_package, currentDomainType());
    }
    QHash<QString, DomainLegendEntry> legendById;
    for (const DomainLegendEntry& entry : snapshot.legend) {
        legendById.insert(entry.id, entry);
    }

    QVector<const DomainDefinition*> domains;
    for (const DomainDefinition& domain : m_design->domains) {
        if (domain.type == currentDomainType()) {
            domains.append(&domain);
        }
    }
    std::sort(domains.begin(), domains.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });
    m_instances->setRowCount(domains.size());
    for (qsizetype row = 0; row < domains.size(); ++row) {
        const DomainDefinition& domain = *domains.at(row);
        const DomainLegendEntry legend = legendById.value(
            domain.id,
            DomainLegendEntry{
                domain.id,
                domainDisplayName(domain),
                domainPresentationColor(domain.type, domain.id),
                0,
                0});
        auto* colorItem = new QTableWidgetItem;
        colorItem->setBackground(QBrush(
            legend.color, domainPresentationPattern(domain.id)));
        colorItem->setData(domainManagerColorRole, legend.color);
        colorItem->setData(
            Qt::AccessibleTextRole,
            QStringLiteral("%1 marker for Domain %2")
                .arg(domainPresentationPatternLabel(domain.id),
                     domainDisplayName(domain)));
        const QString markerTooltip =
            QStringLiteral("%1 marker for %2 (%3)")
                .arg(domainPresentationPatternLabel(domain.id),
                     domainDisplayName(domain), domain.id);
        colorItem->setToolTip(
            QStringLiteral("<qt>%1</qt>")
                .arg(markerTooltip.toHtmlEscaped()));
        auto* nameItem = new QTableWidgetItem(domainDisplayName(domain));
        auto* idItem = new QTableWidgetItem(domain.id);
        auto* membersItem = new QTableWidgetItem(QString::number(legend.memberCount));
        auto* crossingsItem = new QTableWidgetItem(QString::number(legend.crossingCount));
        for (QTableWidgetItem* item
             : {colorItem, nameItem, idItem, membersItem, crossingsItem}) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setData(domainManagerDomainIdRole, domain.id);
            item->setData(domainManagerDomainTypeRole, domain.type);
            item->setData(domainManagerMemberCountRole, legend.memberCount);
            item->setData(domainManagerCrossingCountRole, legend.crossingCount);
        }
        m_instances->setItem(row, 0, colorItem);
        m_instances->setItem(row, 1, nameItem);
        m_instances->setItem(row, 2, idItem);
        m_instances->setItem(row, 3, membersItem);
        m_instances->setItem(row, 4, crossingsItem);
        if (domain.id == previousId) {
            m_instances->selectRow(row);
        }
    }
    if (m_instances->currentRow() < 0 && m_instances->rowCount() > 0) {
        m_instances->selectRow(0);
    }
    m_instances->resizeColumnsToContents();
}

void DomainManagerPanel::refreshAssignment() {
    const bool hadPendingDraft = m_assignmentEdited;
    m_assignmentEdited = false;
    m_clearAssignmentStaged = false;
    m_selectionChangedWhileEditing = false;
    m_touchedAssignmentDomains.clear();
    m_assignment = {};

    m_updating = true;
    m_singleAssignment->clear();
    m_multipleAssignment->clear();
    if (m_design && m_package && selectedType()) {
        m_assignment = buildDomainAssignmentAggregate(
            *m_design, *m_package, m_selection, currentDomainType());
    }

    m_assignmentState->setProperty(
        "assignmentState", assignmentStateId(m_assignment.state));
    switch (m_assignment.state) {
    case DomainAssignmentAggregateState::Unavailable:
        m_assignmentState->setText(QStringLiteral(
            "Choose a Package-declared Domain type."));
        break;
    case DomainAssignmentAggregateState::NoEligible:
        m_assignmentState->setText(QStringLiteral(
            "No assignable Router or Endpoint is selected (%1 semantic item(s)).")
                                       .arg(m_assignment.totalElements));
        break;
    case DomainAssignmentAggregateState::Unassigned:
        m_assignmentState->setText(QStringLiteral(
            "%1 of %2 selected item(s) are eligible and currently unassigned.")
                                       .arg(m_assignment.eligibleElements)
                                       .arg(m_assignment.totalElements));
        break;
    case DomainAssignmentAggregateState::Common:
        m_assignmentState->setText(QStringLiteral(
            "%1 of %2 selected item(s) are eligible and share: %3")
                                       .arg(m_assignment.eligibleElements)
                                       .arg(m_assignment.totalElements)
                                       .arg(m_assignment.commonAssignments.join(
                                           QStringLiteral(", "))));
        break;
    case DomainAssignmentAggregateState::Mixed:
        m_assignmentState->setText(QStringLiteral(
            "%1 of %2 selected item(s) are eligible. Assignment: Mixed.")
                                       .arg(m_assignment.eligibleElements)
                                       .arg(m_assignment.totalElements));
        break;
    }
    const DomainTypeDefinition* type = selectedType();
    const QString limits = assignmentLimitsText(m_assignment.editingRules());
    if (!limits.isEmpty()) {
        m_assignmentState->setText(
            m_assignmentState->text()
            + QStringLiteral("\nAssignment limits: %1").arg(limits));
    }

    const bool single = type && m_assignment.usesSingleAssignmentEditor();
    m_singleAssignment->setVisible(single);
    m_multipleAssignment->setVisible(type && !single);
    if (type && single) {
        if (m_assignment.state == DomainAssignmentAggregateState::Mixed) {
            m_singleAssignment->addItem(
                QStringLiteral("Mixed — choose a replacement"));
        }
        if (!m_assignment.requiresAssignment()) {
            m_singleAssignment->addItem(QStringLiteral("Unassigned"), QString());
        }
        for (const QString& domainId : m_assignment.domainIds) {
            const auto domain = std::find_if(
                m_design->domains.cbegin(), m_design->domains.cend(),
                [&](const DomainDefinition& candidate) {
                    return candidate.id == domainId;
                });
            const QString label = domain == m_design->domains.cend()
                ? domainId
                : QStringLiteral("%1 — %2")
                      .arg(domainDisplayName(*domain), domainId);
            m_singleAssignment->addItem(label, domainId);
        }
        if (m_assignment.state == DomainAssignmentAggregateState::Common
            && m_assignment.commonAssignments.size() == 1) {
            const int index = m_singleAssignment->findData(
                m_assignment.commonAssignments.front());
            if (index >= 0) {
                m_singleAssignment->setCurrentIndex(index);
            }
        } else if (m_assignment.state == DomainAssignmentAggregateState::Unassigned) {
            const int index = m_singleAssignment->findData(QString());
            if (index >= 0) {
                m_singleAssignment->setCurrentIndex(index);
            }
        }
    } else if (type) {
        for (const QString& domainId : m_assignment.domainIds) {
            const auto domain = std::find_if(
                m_design->domains.cbegin(), m_design->domains.cend(),
                [&](const DomainDefinition& candidate) {
                    return candidate.id == domainId;
                });
            const QString label = domain == m_design->domains.cend()
                ? domainId
                : QStringLiteral("%1 — %2")
                      .arg(domainDisplayName(*domain), domainId);
            auto* item = new QListWidgetItem(label, m_multipleAssignment);
            item->setData(domainManagerDomainIdRole, domainId);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            Qt::CheckState state = Qt::Unchecked;
            if (m_assignment.presence(domainId) == DomainAssignmentPresence::All) {
                state = Qt::Checked;
            } else if (m_assignment.presence(domainId)
                       == DomainAssignmentPresence::Some) {
                state = Qt::PartiallyChecked;
            }
            item->setCheckState(state);
            item->setData(domainManagerInitialCheckStateRole,
                          static_cast<int>(state));
        }
    }
    m_updating = false;
    updateActionState();
    if (hadPendingDraft) {
        notifyDraftStateChanged();
    }
}

void DomainManagerPanel::updateActionState() {
    const bool editable = !m_busy && m_design && m_package
        && formatVersionSupportsDomains(m_design->formatVersion)
        && formatVersionSupportsDomains(m_package->formatVersion)
        && selectedType();
    const bool hasSelectedDomain = selectedDomain();
    const bool completeEditable = !m_busy && m_design && m_package
        && formatVersionSupportsDomains(m_design->formatVersion)
        && formatVersionSupportsDomains(m_package->formatVersion)
        && !m_assignmentEdited;
    m_completeConfiguration->setEnabled(completeEditable);
    m_completeConfiguration->setToolTip(
        m_assignmentEdited
            ? QStringLiteral(
                  "Apply or discard the pending selection assignment first.")
            : QStringLiteral(
                  "Edit all Package-defined Domain data as one atomic working "
                  "copy. Routers and Router Links remain fixed Mesh projections."));
    m_typeSelector->setEnabled(
        m_typeSelector->count() > 0 && !m_assignmentEdited);
    m_showOnCanvas->setEnabled(
        selectedType() && currentDomainType() != m_canvasDomainType);
    m_showOnCanvas->setText(
        selectedType() && currentDomainType() == m_canvasDomainType
            ? QStringLiteral("Shown on canvas")
            : QStringLiteral("Show on canvas"));
    m_addDomain->setEnabled(editable && !m_assignmentEdited);
    m_editDomain->setEnabled(
        editable && !m_assignmentEdited && hasSelectedDomain);
    m_removeDomain->setEnabled(
        editable && !m_assignmentEdited && hasSelectedDomain);
    QVector<ElementRef> allEligible;
    QVector<ElementRef> unassigned;
    QVector<ElementRef> selectedMembers;
    if (m_resolved && selectedType()) {
        allEligible = buildDomainAssignmentSelection(
            *m_resolved, *selectedType(),
            DomainAssignmentSelectionScope::AllEligible);
        unassigned = buildDomainAssignmentSelection(
            *m_resolved, *selectedType(),
            DomainAssignmentSelectionScope::Unassigned);
        if (const DomainDefinition* domain = selectedDomain()) {
            selectedMembers = buildDomainAssignmentSelection(
                *m_resolved, *selectedType(),
                DomainAssignmentSelectionScope::AssignedToDomain,
                domain->id);
        }
    }
    const bool selectionEnabled = !m_busy && m_resolved && selectedType()
        && !m_assignmentEdited;
    m_selectDomainMembers->setText(
        QStringLiteral("Select members (%1)").arg(selectedMembers.size()));
    m_selectDomainMembers->setEnabled(
        selectionEnabled && hasSelectedDomain && !selectedMembers.isEmpty());
    m_selectAllEligible->setText(
        QStringLiteral("Select all eligible (%1)").arg(allEligible.size()));
    m_selectAllEligible->setEnabled(
        selectionEnabled && !allEligible.isEmpty());
    m_selectUnassigned->setText(
        QStringLiteral("Select unassigned (%1)").arg(unassigned.size()));
    m_selectUnassigned->setEnabled(
        selectionEnabled && !unassigned.isEmpty());
    const bool eligible = editable && m_assignment.eligibleElements > 0
        && m_assignment.assignmentRulesAreValid();
    m_singleAssignment->setEnabled(eligible && !m_clearAssignmentStaged);
    m_multipleAssignment->setEnabled(eligible && !m_clearAssignmentStaged);
    m_clearAssignment->setEnabled(
        eligible && m_assignment.permitsClearing()
        && m_assignment.state != DomainAssignmentAggregateState::Unassigned
        && !m_assignmentEdited);

    updateAssignmentTaskPresentation();
}

void DomainManagerPanel::updateAssignmentTaskPresentation() {
    const bool editable = !m_busy && m_design && m_package
        && formatVersionSupportsDomains(m_design->formatVersion)
        && formatVersionSupportsDomains(m_package->formatVersion)
        && selectedType();
    const bool eligible = editable && m_assignment.eligibleElements > 0
        && m_assignment.assignmentRulesAreValid();
    const std::optional<DomainAssignmentPatch> stagedPatch =
        stagedAssignmentPatch();
    const DomainAssignmentPatchEvaluation patchEvaluation = stagedPatch
        ? m_assignment.evaluatePatch(*stagedPatch)
        : DomainAssignmentPatchEvaluation{};
    const QString patchFeedback = m_assignmentEdited
        ? assignmentPatchFeedback(patchEvaluation) : QString();
    m_assignmentFeedback->setText(patchFeedback);
    m_assignmentFeedback->setVisible(!patchFeedback.isEmpty());
    const bool applyReady = eligible && m_assignmentEdited && stagedPatch
        && patchEvaluation.accepted;

    const bool assignmentPageCurrent = m_tabs
        && m_tabs->currentWidget() == m_assignmentPage;
    DomainAssignmentTaskBarState taskState;
    taskState.taskActive = assignmentPageCurrent;
    if (const DomainTypeDefinition* type = selectedType()) {
        const QString taskType = type->label.trimmed().isEmpty()
            ? type->id : type->label.trimmed();
        taskState.title = QStringLiteral("Assign %1").arg(taskType);
    } else {
        taskState.title = QStringLiteral("Domain assignment");
    }
    if (m_busy) {
        taskState.status = QStringLiteral(
            "Domain editing is unavailable while another operation is running.");
    } else if (m_assignmentEdited && !patchFeedback.isEmpty()) {
        taskState.status = patchFeedback;
    } else if (m_assignmentEdited && m_selectionChangedWhileEditing) {
        taskState.status = QStringLiteral(
            "Changes still target the original %1 eligible item(s).")
                               .arg(m_assignment.eligibleElements);
    } else if (m_assignmentEdited) {
        taskState.status = QStringLiteral(
            "Changes are ready for %1 eligible item(s).")
                               .arg(m_assignment.eligibleElements);
    } else if (m_assignment.domainIds.isEmpty()
               && m_assignment.eligibleElements > 0) {
        taskState.status = QStringLiteral(
            "No Domain instances exist for this type. Add one on Instances before assigning.");
    } else if (eligible) {
        taskState.status = QStringLiteral(
            "%1 eligible item(s). Choose assignments, then apply.")
                               .arg(m_assignment.eligibleElements);
    } else {
        taskState.status = m_assignmentState->text().section(
            QLatin1Char('\n'), 0, 0);
    }
    taskState.applyText = QStringLiteral("Apply changes");
    taskState.discardText = m_assignmentEdited
        ? QStringLiteral("Discard changes")
        : QStringLiteral("Done");
    if (m_assignmentEdited) {
        taskState.discardAccessibleDescription = QStringLiteral(
            "Discard the staged Domain assignment changes without applying them.");
    } else if (m_assignmentTaskActive) {
        taskState.discardAccessibleDescription = QStringLiteral(
            "Finish Domain assignment and return to the previous workbench layout.");
    } else {
        taskState.discardAccessibleDescription = QStringLiteral(
            "Finish Domain assignment and return to Domain instances.");
    }
    taskState.applyEnabled = applyReady;
    taskState.discardEnabled = assignmentPageCurrent;
    if (!applyReady) {
        if (m_busy) {
            taskState.applyUnavailableReason = QStringLiteral(
                "Wait for the current operation to finish before applying Domain assignments.");
        } else if (!patchFeedback.isEmpty()) {
            taskState.applyUnavailableReason = patchFeedback;
        } else if (!m_design || !m_package || !selectedType()) {
            taskState.applyUnavailableReason = QStringLiteral(
                "Open a design with a Package-defined Domain type before applying assignments.");
        } else if (m_assignment.eligibleElements > 0
                   && !m_assignment.assignmentRulesAreValid()) {
            taskState.applyUnavailableReason = QStringLiteral(
                "The Package defines an invalid Domain assignment rule; repair the Package before applying changes.");
        } else if (m_assignment.eligibleElements > 0
                   && m_assignment.domainIds.isEmpty()) {
            taskState.applyUnavailableReason = QStringLiteral(
                "Add a Domain instance on Instances before applying assignments.");
        } else if (!eligible) {
            taskState.applyUnavailableReason = QStringLiteral(
                "Select assignable Routers or Endpoints first.");
        } else {
            taskState.applyUnavailableReason = QStringLiteral(
                "Change at least one assignment before applying.");
        }
    }
    if (!taskState.discardEnabled) {
        taskState.discardUnavailableReason = QStringLiteral(
            "Open this assignment task from the Inspector or stage a change first.");
    }
    m_assignmentTaskBar->setState(taskState);
    m_assignmentTaskBar->setVisible(assignmentPageCurrent);
}

void DomainManagerPanel::addDomain() {
    if (!m_design || !m_package || !selectedType()) {
        return;
    }
    DomainInstanceDialog dialog(
        QVector<DomainTypeDefinition>{*selectedType()},
        m_design->domains,
        std::nullopt,
        currentDomainType(),
        [this](const DomainDefinition& candidate) {
            return validateAddDomain
                ? validateAddDomain(candidate) : QVector<Diagnostic>{};
        },
        this);
    if (dialog.exec() == QDialog::Accepted && addDomainRequested) {
        addDomainRequested(dialog.candidate());
    }
}

void DomainManagerPanel::editDomain() {
    const DomainDefinition* domain = selectedDomain();
    if (!domain || !m_design || !m_package) {
        return;
    }
    const QString originalId = domain->id;
    DomainInstanceDialog dialog(
        m_package->domainTypes,
        m_design->domains,
        *domain,
        domain->type,
        [this, originalId](const DomainDefinition& candidate) {
            return validateUpdateDomain
                ? validateUpdateDomain(originalId, candidate)
                : QVector<Diagnostic>{};
        },
        this);
    if (dialog.exec() == QDialog::Accepted && updateDomainRequested) {
        updateDomainRequested(originalId, dialog.candidate());
    }
}

void DomainManagerPanel::removeDomain() {
    const DomainDefinition* domain = selectedDomain();
    if (!domain || !removeDomainRequested) {
        return;
    }
    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Delete Domain"),
        QStringLiteral("Delete Domain %1 (%2)? References are not removed automatically.")
            .arg(domainDisplayName(*domain), domain->id),
        QMessageBox::Cancel | QMessageBox::Yes,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.domainManager.deleteConfirmation"));
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.setEscapeButton(QMessageBox::Cancel);
    if (QPushButton* deleteButton = qobject_cast<QPushButton*>(
            confirmation.button(QMessageBox::Yes))) {
        deleteButton->setText(QStringLiteral("Delete Domain"));
        deleteButton->setProperty(
            "finepaperRole", QStringLiteral("danger"));
    }
    if (confirmation.exec() == QMessageBox::Yes) {
        removeDomainRequested(domain->id);
    }
}

void DomainManagerPanel::selectDomainMembers() {
    const DomainDefinition* domain = selectedDomain();
    if (!selectElementsRequested || !m_resolved || !selectedType() || !domain
        || m_assignmentEdited) {
        return;
    }
    selectElementsRequested(buildDomainAssignmentSelection(
        *m_resolved, *selectedType(),
        DomainAssignmentSelectionScope::AssignedToDomain, domain->id));
}

void DomainManagerPanel::selectAllEligible() {
    if (!selectElementsRequested || !m_resolved || !selectedType()
        || m_assignmentEdited) {
        return;
    }
    selectElementsRequested(buildDomainAssignmentSelection(
        *m_resolved, *selectedType(),
        DomainAssignmentSelectionScope::AllEligible));
}

void DomainManagerPanel::selectUnassigned() {
    if (!selectElementsRequested || !m_resolved || !selectedType()
        || m_assignmentEdited) {
        return;
    }
    selectElementsRequested(buildDomainAssignmentSelection(
        *m_resolved, *selectedType(),
        DomainAssignmentSelectionScope::Unassigned));
}

std::optional<DomainAssignmentPatch>
DomainManagerPanel::stagedAssignmentPatch() const {
    DomainAssignmentPatch patch;
    if (m_clearAssignmentStaged) {
        patch.replacement = QStringList{};
    } else if (m_assignment.usesSingleAssignmentEditor()) {
        if (!m_singleAssignment->currentData().isValid()) {
            return std::nullopt;
        }
        const QString domainId = m_singleAssignment->currentData().toString();
        patch.replacement = domainId.isEmpty()
            ? QStringList{} : QStringList{domainId};
    } else {
        for (int row = 0; row < m_multipleAssignment->count(); ++row) {
            QListWidgetItem* item = m_multipleAssignment->item(row);
            const QString domainId = item->data(domainManagerDomainIdRole).toString();
            if (!m_touchedAssignmentDomains.contains(domainId)) {
                continue;
            }
            if (item->checkState() == Qt::Checked) {
                patch.ensurePresent.append(domainId);
            } else if (item->checkState() == Qt::Unchecked) {
                patch.ensureAbsent.append(domainId);
            } else {
                return std::nullopt;
            }
        }
    }
    return patch;
}

void DomainManagerPanel::applyAssignment() {
    if (!assignmentPatchRequested || m_assignment.eligibleElementRefs.isEmpty()
        || !selectedType()) {
        return;
    }
    std::optional<DomainAssignmentPatch> patch = stagedAssignmentPatch();
    if (!patch || !m_assignment.acceptsPatch(*patch)) {
        return;
    }
    assignmentPatchRequested(
        m_assignment.eligibleElementRefs,
        currentDomainType(),
        std::move(*patch));
}

void DomainManagerPanel::clearAssignment() {
    if (m_assignment.eligibleElementRefs.isEmpty()
        || !m_assignment.permitsClearing()
        || m_assignmentEdited) {
        return;
    }
    m_clearAssignmentStaged = true;
    m_assignmentEdited = true;
    m_touchedAssignmentDomains.clear();
    m_updating = true;
    if (selectedType() && m_assignment.usesSingleAssignmentEditor()) {
        const int unassigned = m_singleAssignment->findData(QString());
        if (unassigned >= 0) {
            m_singleAssignment->setCurrentIndex(unassigned);
        }
    } else {
        for (int row = 0; row < m_multipleAssignment->count(); ++row) {
            m_multipleAssignment->item(row)->setCheckState(Qt::Unchecked);
        }
    }
    m_updating = false;
    m_assignmentState->setText(
        m_assignmentState->text()
        + QStringLiteral(
            "\nPending: clear this Domain assignment from the original %1 eligible item(s).")
              .arg(m_assignment.eligibleElements));
    updateActionState();
    notifyDraftStateChanged();
}

void DomainManagerPanel::discardAssignment() {
    if (!m_assignmentEdited) {
        return;
    }
    setDiagnostics({});
    refreshAssignment();
}

void DomainManagerPanel::handleAssignmentItemChanged(QListWidgetItem* item) {
    if (m_updating || !item) {
        return;
    }
    const QString domainId = item->data(domainManagerDomainIdRole).toString();
    const auto initial = static_cast<Qt::CheckState>(
        item->data(domainManagerInitialCheckStateRole).toInt());
    if (item->checkState() == initial) {
        m_touchedAssignmentDomains.remove(domainId);
    } else {
        m_touchedAssignmentDomains.insert(domainId);
    }
    const bool wasEdited = m_assignmentEdited;
    m_assignmentEdited = !m_touchedAssignmentDomains.isEmpty();
    if (!m_assignmentEdited && m_selectionChangedWhileEditing) {
        if (wasEdited) {
            notifyDraftStateChanged();
        }
        refreshAssignment();
        return;
    }
    updateActionState();
    if (wasEdited != m_assignmentEdited) {
        notifyDraftStateChanged();
    }
}

void DomainManagerPanel::updateSingleAssignmentEdited() {
    const bool wasEdited = m_assignmentEdited;
    if (!selectedType()
        || !m_assignment.usesSingleAssignmentEditor()
        || !m_singleAssignment->currentData().isValid()) {
        m_assignmentEdited = false;
        if (m_selectionChangedWhileEditing) {
            if (wasEdited) {
                notifyDraftStateChanged();
            }
            refreshAssignment();
            return;
        }
        updateActionState();
        if (wasEdited) {
            notifyDraftStateChanged();
        }
        return;
    }

    const QString selectedId = m_singleAssignment->currentData().toString();
    if (m_assignment.state == DomainAssignmentAggregateState::Mixed) {
        m_assignmentEdited = true;
    } else if (m_assignment.state == DomainAssignmentAggregateState::Common) {
        m_assignmentEdited = m_assignment.commonAssignments
                                 != QStringList{selectedId};
    } else if (m_assignment.state
               == DomainAssignmentAggregateState::Unassigned) {
        m_assignmentEdited = !selectedId.isEmpty();
    } else {
        m_assignmentEdited = false;
    }
    if (!m_assignmentEdited && m_selectionChangedWhileEditing) {
        if (wasEdited) {
            notifyDraftStateChanged();
        }
        refreshAssignment();
        return;
    }
    updateActionState();
    if (wasEdited != m_assignmentEdited) {
        notifyDraftStateChanged();
    }
}

void DomainManagerPanel::notifyDraftStateChanged() {
    if (draftStateChanged) {
        draftStateChanged();
    }
}

QString DomainManagerPanel::selectedDomainId() const {
    const int row = m_instances ? m_instances->currentRow() : -1;
    QTableWidgetItem* item = row >= 0 ? m_instances->item(row, 2) : nullptr;
    return item ? item->data(domainManagerDomainIdRole).toString() : QString();
}

} // namespace finepaper
