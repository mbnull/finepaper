#include "features/domain/domain_manager_panel.h"

#include "features/domain/domain_instance_dialog.h"
#include "ui/theme/ui_tokens.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
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

    m_status = new QLabel(QStringLiteral("Open a Package V2 design to edit Domains."));
    m_status->setObjectName(QStringLiteral("finepaper.domainManager.status"));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_completeConfiguration = new QPushButton(
        QStringLiteral("Open Domain Configuration workspace"));
    m_completeConfiguration->setObjectName(
        QStringLiteral("finepaper.domainManager.completeConfiguration"));
    m_completeConfiguration->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    root->addWidget(m_completeConfiguration);

    auto* typeRow = new QGridLayout;
    auto* typeLabel = new QLabel(QStringLiteral("Domain type"));
    typeRow->addWidget(typeLabel, 0, 0);
    m_typeSelector = new QComboBox;
    m_typeSelector->setObjectName(
        QStringLiteral("finepaper.domainManager.typeSelector"));
    m_typeSelector->setAccessibleName(QStringLiteral("Domain type"));
    typeLabel->setBuddy(m_typeSelector);
    typeRow->addWidget(m_typeSelector, 0, 1);
    m_showOnCanvas = new QPushButton(QStringLiteral("Show on canvas"));
    m_showOnCanvas->setObjectName(
        QStringLiteral("finepaper.domainManager.showOnCanvas"));
    m_showOnCanvas->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    typeRow->addWidget(m_showOnCanvas, 1, 0, 1, 2);
    typeRow->setColumnStretch(1, 1);
    root->addLayout(typeRow);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("finepaper.domainManager.tabs"));

    auto* instancesPage = new QWidget;
    auto* instancesLayout = new QVBoxLayout(instancesPage);
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
    auto* instanceButtons = new QGridLayout;
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
    instanceButtons->addWidget(m_addDomain, 0, 0);
    instanceButtons->addWidget(m_editDomain, 0, 1);
    instanceButtons->addWidget(m_removeDomain, 0, 2);
    instanceButtons->addWidget(m_selectDomainMembers, 1, 0, 1, 3);
    instanceButtons->setColumnStretch(0, 1);
    instanceButtons->setColumnStretch(1, 1);
    instanceButtons->setColumnStretch(2, 1);
    instancesLayout->addLayout(instanceButtons);
    m_tabs->addTab(instancesPage, QStringLiteral("Instances / Legend"));

    auto* assignmentPage = new QWidget;
    auto* assignmentLayout = new QVBoxLayout(assignmentPage);
    assignmentLayout->setContentsMargins(
        0, ui::UiMetrics::spacing8, 0, 0);
    auto* selectionButtons = new QHBoxLayout;
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
    selectionButtons->addStretch();
    assignmentLayout->addLayout(selectionButtons);
    m_assignmentState = new QLabel;
    m_assignmentState->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentState"));
    m_assignmentState->setWordWrap(true);
    assignmentLayout->addWidget(m_assignmentState);
    m_singleAssignment = new QComboBox;
    m_singleAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentEditor"));
    assignmentLayout->addWidget(m_singleAssignment);
    m_multipleAssignment = new QListWidget;
    m_multipleAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.assignmentEditor.multiple"));
    m_multipleAssignment->setAccessibleName(
        QStringLiteral("Domain assignments for the current selection"));
    m_multipleAssignment->setAlternatingRowColors(true);
    assignmentLayout->addWidget(m_multipleAssignment, 1);
    auto* assignmentButtons = new QGridLayout;
    m_applyAssignment = new QPushButton(QStringLiteral("Apply changes"));
    m_applyAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.applyAssignment"));
    m_applyAssignment->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    m_clearAssignment = new QPushButton(QStringLiteral("Clear assignment"));
    m_clearAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.clearAssignment"));
    m_discardAssignment = new QPushButton(QStringLiteral("Discard changes"));
    m_discardAssignment->setObjectName(
        QStringLiteral("finepaper.domainManager.discardAssignment"));
    m_discardAssignment->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    assignmentButtons->addWidget(m_applyAssignment, 0, 0, 1, 2);
    assignmentButtons->addWidget(m_clearAssignment, 1, 0);
    assignmentButtons->addWidget(m_discardAssignment, 1, 1);
    assignmentButtons->setColumnStretch(1, 1);
    assignmentLayout->addLayout(assignmentButtons);
    m_tabs->addTab(assignmentPage, QStringLiteral("Assign selection"));

    root->addWidget(m_tabs, 1);

    m_diagnostics = new QLabel;
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.domainManager.diagnostics"));
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextFormat(Qt::RichText);
    m_diagnostics->hide();
    root->addWidget(m_diagnostics);

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
    connect(m_applyAssignment, &QPushButton::clicked,
            this, [this] { applyAssignment(); });
    connect(m_clearAssignment, &QPushButton::clicked,
            this, [this] { clearAssignment(); });
    connect(m_discardAssignment, &QPushButton::clicked,
            this, [this] { discardAssignment(); });

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
            "Use the quick tabs for common instance/selection edits, or switch "
            "to the persistent Domain Configuration workspace for memberships, relations, default "
            "crossing policies, and edge overrides. Use the selection helpers "
            "or the canvas Select mode for bulk assignment. Routers and Router "
            "Links remain fixed projections of the Mesh."));
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

void DomainManagerPanel::discardPendingAssignmentChanges() {
    discardAssignment();
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
                type.label.trimmed().isEmpty() ? type.id : type.label,
                type.id);
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
    const bool single = type && type->cardinality == DomainCardinality::Single;
    m_singleAssignment->setVisible(single);
    m_multipleAssignment->setVisible(type && !single);
    if (type && single) {
        if (m_assignment.state == DomainAssignmentAggregateState::Mixed) {
            m_singleAssignment->addItem(
                QStringLiteral("Mixed — choose a replacement"));
        }
        if (!type->required) {
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
    const bool eligible = editable && m_assignment.eligibleElements > 0;
    m_singleAssignment->setEnabled(eligible && !m_clearAssignmentStaged);
    m_multipleAssignment->setEnabled(eligible && !m_clearAssignmentStaged);
    m_applyAssignment->setEnabled(eligible && m_assignmentEdited);
    m_clearAssignment->setEnabled(
        eligible && !m_assignment.required
        && m_assignment.state != DomainAssignmentAggregateState::Unassigned
        && !m_assignmentEdited);
    m_discardAssignment->setEnabled(m_assignmentEdited);
    m_discardAssignment->setVisible(m_assignmentEdited);
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

void DomainManagerPanel::applyAssignment() {
    if (!assignmentPatchRequested || m_assignment.eligibleElementRefs.isEmpty()
        || !selectedType()) {
        return;
    }
    DomainAssignmentPatch patch;
    if (m_clearAssignmentStaged) {
        patch.replacement = QStringList{};
    } else if (selectedType()->cardinality == DomainCardinality::Single) {
        if (!m_singleAssignment->currentData().isValid()) {
            return;
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
            }
        }
    }
    assignmentPatchRequested(
        m_assignment.eligibleElementRefs, currentDomainType(), std::move(patch));
}

void DomainManagerPanel::clearAssignment() {
    if (m_assignment.eligibleElementRefs.isEmpty() || m_assignment.required
        || m_assignmentEdited) {
        return;
    }
    m_clearAssignmentStaged = true;
    m_assignmentEdited = true;
    m_touchedAssignmentDomains.clear();
    m_updating = true;
    if (selectedType()
        && selectedType()->cardinality == DomainCardinality::Single) {
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
    m_assignmentEdited = !m_touchedAssignmentDomains.isEmpty();
    if (!m_assignmentEdited && m_selectionChangedWhileEditing) {
        refreshAssignment();
        return;
    }
    updateActionState();
}

void DomainManagerPanel::updateSingleAssignmentEdited() {
    if (!selectedType()
        || selectedType()->cardinality != DomainCardinality::Single
        || !m_singleAssignment->currentData().isValid()) {
        m_assignmentEdited = false;
        if (m_selectionChangedWhileEditing) {
            refreshAssignment();
            return;
        }
        updateActionState();
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
        refreshAssignment();
        return;
    }
    updateActionState();
}

QString DomainManagerPanel::selectedDomainId() const {
    const int row = m_instances ? m_instances->currentRow() : -1;
    QTableWidgetItem* item = row >= 0 ? m_instances->item(row, 2) : nullptr;
    return item ? item->data(domainManagerDomainIdRole).toString() : QString();
}

} // namespace finepaper
