#include "features/topology/mesh_resize_dialog.h"

#include "features/domain/presentation/domain_text.h"
#include "features/topology/topology_text.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QString safeSuffix(const QString& value) {
    return QString::fromLatin1(value.toUtf8().toHex());
}

QString assignmentObjectName(const QString& routerId,
                             const QString& domainType,
                             const QString& editorKind) {
    return QStringLiteral("finepaper.meshResize.assignment.%1.%2.%3")
        .arg(safeSuffix(routerId), safeSuffix(domainType), editorKind);
}

QString domainLabel(const MeshResizePlan& plan, const QString& id) {
    const auto found = std::find_if(
        plan.domains.cbegin(), plan.domains.cend(),
        [&](const MeshResizeDomainOption& domain) { return domain.id == id; });
    return found == plan.domains.cend()
        ? domain_text::domainInstanceDisplayText(id)
        : domain_text::domainInstanceDisplayText(found->id, found->name);
}

bool hasFixedAutomaticAssignment(
    const MeshResizeDomainAssignmentPlan& assignment) {
    return assignment.assignmentRule.isValid()
        && !assignment.automaticAssignment.isEmpty()
        && assignment.automaticAssignment.size()
            == assignment.availableDomainIds.size()
        && assignment.assignmentRule.acceptsCount(
            assignment.automaticAssignment.size());
}

QString automaticAssignmentText(
    const MeshResizeDomainAssignmentPlan& assignment) {
    if (assignment.automaticAssignment.size() == 1) {
        return QStringLiteral(
                   "The only available instance is assigned automatically "
                   "because the Router minimum is %1.")
            .arg(assignment.assignmentRule.minimumAssignments);
    }
    return QStringLiteral(
               "All %1 available instances are assigned automatically "
               "because the Router minimum is %2.")
        .arg(assignment.automaticAssignment.size())
        .arg(assignment.assignmentRule.minimumAssignments);
}

QString unavailableAssignmentText(
    const MeshResizeDomainAssignmentPlan& assignment) {
    const qsizetype available = assignment.availableDomainIds.size();
    const qsizetype minimum =
        assignment.assignmentRule.minimumAssignments;
    if (assignment.assignmentRule.isValid() && available < minimum) {
        const qsizetype missing = minimum - available;
        QString repair;
        if (missing == 1) {
            repair = available == 0
                ? QStringLiteral("Create one in Domain Manager")
                : QStringLiteral("Create one more in Domain Manager");
        } else {
            repair = QStringLiteral("Create %1 more in Domain Manager")
                         .arg(missing);
        }
        const QString availability = available == 1
            ? QStringLiteral("1 instance exists")
            : QStringLiteral("%1 instances exist").arg(available);
        return QStringLiteral(
                   "%1, but this Router requires at least %2. %3 before "
                   "resizing the Mesh.")
            .arg(availability)
            .arg(minimum)
            .arg(repair);
    }
    return QStringLiteral(
        "No instances currently exist. This is allowed because the Router "
        "assignment minimum is 0.");
}

QString assignmentsText(const DomainMembership& membership) {
    QStringList types = membership.assignments.keys();
    std::sort(types.begin(), types.end());
    QStringList parts;
    for (const QString& type : types) {
        QStringList ids = membership.assignments.value(type);
        std::sort(ids.begin(), ids.end());
        parts.append(QStringLiteral("%1 = %2")
                         .arg(type, ids.join(QStringLiteral(" + "))));
    }
    return parts.isEmpty() ? QStringLiteral("no assignments")
                           : parts.join(QStringLiteral("; "));
}

QString compactObject(const QJsonObject& object) {
    return QString::fromUtf8(
        QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString impactMembershipText(const DomainMembership& membership) {
    return QStringLiteral("Router %1: %2")
        .arg(membership.element.id, assignmentsText(membership));
}

QString impactOverrideText(const DomainEdgeOverride& edgeOverride) {
    QString text = QStringLiteral("Link %1: %2 → policy %3")
                       .arg(edgeOverride.edge.id,
                            edgeOverride.domainType,
                            edgeOverride.policy);
    if (!edgeOverride.properties.isEmpty()) {
        text += QStringLiteral("; properties %1")
                    .arg(compactObject(edgeOverride.properties));
    }
    return text;
}

QString impactElementConfigurationText(
    const ElementConfiguration& configuration) {
    QString kind;
    switch (configuration.element.kind) {
    case ElementKind::Router:
        kind = QStringLiteral("Router");
        break;
    case ElementKind::RouterLink:
        kind = QStringLiteral("Router link");
        break;
    case ElementKind::EndpointAttachment:
        kind = QStringLiteral("Endpoint attachment");
        break;
    case ElementKind::Endpoint:
        kind = QStringLiteral("Endpoint");
        break;
    case ElementKind::Invalid:
        kind = QStringLiteral("Element");
        break;
    }
    return QStringLiteral("%1 %2: property set %3; sparse overrides %4")
        .arg(kind,
             configuration.element.id,
             configuration.propertySet,
             compactObject(configuration.properties));
}

QStringList errorDiagnosticText(const QVector<Diagnostic>& diagnostics) {
    QStringList result;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != QStringLiteral("error")) {
            continue;
        }
        if (diagnostic.code
                == QStringLiteral(
                    "mesh.resize_missing_membership_confirmation")
            || diagnostic.code
                == QStringLiteral(
                    "mesh.resize_missing_override_confirmation")
            || diagnostic.code
                == QStringLiteral(
                    "mesh.resize_missing_element_configuration_confirmation")) {
            // The impact lists already name every exact record. The dialog
            // adds one concise summary per category below instead of
            // repeating this structural error for every unchecked row.
            continue;
        }
        QString text = diagnostic.message;
        if (!diagnostic.code.isEmpty()) {
            text += QStringLiteral(" [%1]").arg(diagnostic.code);
        }
        if (!diagnostic.path.isEmpty()) {
            text += QStringLiteral(" (%1)").arg(diagnostic.path);
        }
        result.append(std::move(text));
    }
    return result;
}

QString routerItemText(const MeshResizeRouterPlan& router, bool complete) {
    return topology_text::meshResizeRouterListText(
        router.element.id,
        router.position.x,
        router.position.y,
        complete);
}

const MeshResizeRouterPlan* findRouter(const MeshResizePlan& plan,
                                       const QString& routerId) {
    const auto found = std::find_if(
        plan.newRouters.cbegin(), plan.newRouters.cend(),
        [&](const MeshResizeRouterPlan& router) {
            return router.element.id == routerId;
        });
    return found == plan.newRouters.cend() ? nullptr : &*found;
}

} // namespace

MeshResizeDialog::MeshResizeDialog(NocDesign design,
                                   PackageDefinition package,
                                   QWidget* parent)
    : QDialog(parent),
      m_design(std::move(design)),
      m_package(std::move(package)) {
    setObjectName(QStringLiteral("finepaper.meshResizeDialog"));
    setWindowTitle(QStringLiteral("Resize Mesh"));
    setModal(true);
    resize(940, 760);

    auto* root = new QVBoxLayout(this);
    auto* introduction = new QLabel(
        QStringLiteral(
            "Routers and Router links are derived from the Mesh. Change rows "
            "and columns here; new Routers receive explicit Package-driven "
            "Domain assignments before the resize is applied atomically."),
        this);
    introduction->setObjectName(
        QStringLiteral("finepaper.meshResize.introduction"));
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    auto* dimensions = new QGroupBox(QStringLiteral("Mesh dimensions"), this);
    dimensions->setObjectName(QStringLiteral("finepaper.meshResize.dimensions"));
    auto* dimensionForm = new QFormLayout(dimensions);
    m_rows = new QSpinBox(dimensions);
    m_rows->setObjectName(QStringLiteral("finepaper.meshResize.rows"));
    m_columns = new QSpinBox(dimensions);
    m_columns->setObjectName(QStringLiteral("finepaper.meshResize.columns"));
    const int minimumRows = std::max(1, m_package.mesh.minimumRows);
    const int maximumRows = std::max(minimumRows, m_package.mesh.maximumRows);
    const int minimumColumns = std::max(1, m_package.mesh.minimumColumns);
    const int maximumColumns =
        std::max(minimumColumns, m_package.mesh.maximumColumns);
    m_rows->setRange(minimumRows, maximumRows);
    m_columns->setRange(minimumColumns, maximumColumns);
    m_rows->setValue(
        std::clamp(m_design.topology.rows, minimumRows, maximumRows));
    m_columns->setValue(
        std::clamp(m_design.topology.columns,
                   minimumColumns,
                   maximumColumns));
    m_rows->setSuffix(QStringLiteral(" rows"));
    m_columns->setSuffix(QStringLiteral(" columns"));
    dimensionForm->addRow(QStringLiteral("Rows"), m_rows);
    dimensionForm->addRow(QStringLiteral("Columns"), m_columns);
    root->addWidget(dimensions);

    m_deltaSummary = new QLabel(this);
    m_deltaSummary->setObjectName(
        QStringLiteral("finepaper.meshResize.deltaSummary"));
    m_deltaSummary->setWordWrap(true);
    m_deltaSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_deltaSummary);

    m_blockers = new QLabel(this);
    m_blockers->setObjectName(
        QStringLiteral("finepaper.meshResize.blockers"));
    m_blockers->setWordWrap(true);
    m_blockers->setTextFormat(Qt::PlainText);
    m_blockers->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_blockers);

    auto* assignmentGroup = new QGroupBox(
        QStringLiteral("New Router Domain assignments"), this);
    assignmentGroup->setObjectName(
        QStringLiteral("finepaper.meshResize.assignments"));
    auto* assignmentLayout = new QVBoxLayout(assignmentGroup);
    auto* assignmentTools = new QHBoxLayout;
    m_copyToAll = new QPushButton(
        QStringLiteral("Copy current assignments to all"), assignmentGroup);
    m_copyToAll->setObjectName(
        QStringLiteral("finepaper.meshResize.copyToAll"));
    m_nextIncomplete = new QPushButton(
        QStringLiteral("Next incomplete Router"), assignmentGroup);
    m_nextIncomplete->setObjectName(
        QStringLiteral("finepaper.meshResize.nextIncomplete"));
    assignmentTools->addWidget(m_copyToAll);
    assignmentTools->addWidget(m_nextIncomplete);
    assignmentTools->addStretch(1);
    assignmentLayout->addLayout(assignmentTools);

    auto* assignmentSplitter = new QSplitter(Qt::Horizontal, assignmentGroup);
    assignmentSplitter->setObjectName(
        QStringLiteral("finepaper.meshResize.assignmentSplitter"));
    m_routerList = new QListWidget(assignmentSplitter);
    m_routerList->setObjectName(
        QStringLiteral("finepaper.meshResize.routerList"));
    m_routerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_routerList->setMinimumWidth(280);
    m_assignmentScroll = new QScrollArea(assignmentSplitter);
    m_assignmentScroll->setObjectName(
        QStringLiteral("finepaper.meshResize.assignmentScroll"));
    m_assignmentScroll->setWidgetResizable(true);
    m_assignmentScroll->setFrameShape(QFrame::NoFrame);
    assignmentSplitter->addWidget(m_routerList);
    assignmentSplitter->addWidget(m_assignmentScroll);
    assignmentSplitter->setStretchFactor(0, 0);
    assignmentSplitter->setStretchFactor(1, 1);
    assignmentLayout->addWidget(assignmentSplitter, 1);
    root->addWidget(assignmentGroup, 1);

    auto* impactGroup = new QGroupBox(
        QStringLiteral("State removed by this resize"), this);
    impactGroup->setObjectName(QStringLiteral("finepaper.meshResize.impacts"));
    auto* impactLayout = new QVBoxLayout(impactGroup);
    auto* impactNote = new QLabel(
        QStringLiteral(
            "Review every exact record, then confirm items individually or "
            "confirm the complete current list. Confirmations are reset "
            "whenever the requested topology changes."),
        impactGroup);
    impactNote->setWordWrap(true);
    impactLayout->addWidget(impactNote);
    auto* impactTools = new QHBoxLayout;
    m_confirmAllImpacts = new QPushButton(
        QStringLiteral("Confirm all listed removals"), impactGroup);
    m_confirmAllImpacts->setObjectName(
        QStringLiteral("finepaper.meshResize.confirmAllImpacts"));
    m_clearImpactConfirmations = new QPushButton(
        QStringLiteral("Clear confirmations"), impactGroup);
    m_clearImpactConfirmations->setObjectName(
        QStringLiteral("finepaper.meshResize.clearImpactConfirmations"));
    impactTools->addWidget(m_confirmAllImpacts);
    impactTools->addWidget(m_clearImpactConfirmations);
    impactTools->addStretch(1);
    impactLayout->addLayout(impactTools);
    impactLayout->addWidget(new QLabel(
        QStringLiteral("Router Domain memberships"), impactGroup));
    m_removedMemberships = new QListWidget(impactGroup);
    m_removedMemberships->setObjectName(
        QStringLiteral("finepaper.meshResize.removedMemberships"));
    m_removedMemberships->setSelectionMode(QAbstractItemView::NoSelection);
    m_removedMemberships->setMaximumHeight(112);
    impactLayout->addWidget(m_removedMemberships);
    impactLayout->addWidget(new QLabel(
        QStringLiteral("Router-link Domain overrides"), impactGroup));
    m_removedEdgeOverrides = new QListWidget(impactGroup);
    m_removedEdgeOverrides->setObjectName(
        QStringLiteral("finepaper.meshResize.removedEdgeOverrides"));
    m_removedEdgeOverrides->setSelectionMode(QAbstractItemView::NoSelection);
    m_removedEdgeOverrides->setMaximumHeight(112);
    impactLayout->addWidget(m_removedEdgeOverrides);
    impactLayout->addWidget(new QLabel(
        QStringLiteral("Router and Router-link element configurations"),
        impactGroup));
    m_removedElementConfigurations = new QListWidget(impactGroup);
    m_removedElementConfigurations->setObjectName(
        QStringLiteral(
            "finepaper.meshResize.removedElementConfigurations"));
    m_removedElementConfigurations->setSelectionMode(
        QAbstractItemView::NoSelection);
    m_removedElementConfigurations->setMaximumHeight(112);
    impactLayout->addWidget(m_removedElementConfigurations);
    root->addWidget(impactGroup);

    m_diagnostics = new QLabel(this);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.meshResize.diagnostics"));
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextFormat(Qt::PlainText);
    m_diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_diagnostics);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(QStringLiteral("finepaper.meshResize.buttons"));
    m_apply = m_buttons->button(QDialogButtonBox::Ok);
    m_apply->setObjectName(QStringLiteral("finepaper.meshResize.apply"));
    m_apply->setText(QStringLiteral("Apply Mesh Resize"));
    root->addWidget(m_buttons);

    connect(m_rows, &QSpinBox::valueChanged,
            this, [this] { rebuildPlan(); });
    connect(m_columns, &QSpinBox::valueChanged,
            this, [this] { rebuildPlan(); });
    connect(m_routerList, &QListWidget::currentRowChanged,
            this, [this] {
                if (!m_rebuilding) {
                    rebuildAssignmentEditor();
                }
            });
    connect(m_copyToAll, &QPushButton::clicked,
            this, [this] { copyCurrentAssignmentsToAll(); });
    connect(m_nextIncomplete, &QPushButton::clicked,
            this, [this] { jumpToNextIncomplete(); });
    connect(m_removedMemberships, &QListWidget::itemChanged,
            this, [this] {
                if (!m_rebuilding) {
                    updateValidation();
                }
            });
    connect(m_removedEdgeOverrides, &QListWidget::itemChanged,
            this, [this] {
                if (!m_rebuilding) {
                    updateValidation();
                }
            });
    connect(m_removedElementConfigurations, &QListWidget::itemChanged,
            this, [this] {
                if (!m_rebuilding) {
                    updateValidation();
                }
            });
    connect(m_confirmAllImpacts, &QPushButton::clicked,
            this, [this] {
                m_rebuilding = true;
                for (int row = 0; row < m_removedMemberships->count(); ++row) {
                    m_removedMemberships->item(row)->setCheckState(Qt::Checked);
                }
                for (int row = 0; row < m_removedEdgeOverrides->count(); ++row) {
                    m_removedEdgeOverrides->item(row)->setCheckState(Qt::Checked);
                }
                for (int row = 0;
                     row < m_removedElementConfigurations->count(); ++row) {
                    m_removedElementConfigurations->item(row)->setCheckState(
                        Qt::Checked);
                }
                m_rebuilding = false;
                updateValidation();
            });
    connect(m_clearImpactConfirmations, &QPushButton::clicked,
            this, [this] {
                m_rebuilding = true;
                for (int row = 0; row < m_removedMemberships->count(); ++row) {
                    m_removedMemberships->item(row)->setCheckState(Qt::Unchecked);
                }
                for (int row = 0; row < m_removedEdgeOverrides->count(); ++row) {
                    m_removedEdgeOverrides->item(row)->setCheckState(Qt::Unchecked);
                }
                for (int row = 0;
                     row < m_removedElementConfigurations->count(); ++row) {
                    m_removedElementConfigurations->item(row)->setCheckState(
                        Qt::Unchecked);
                }
                m_rebuilding = false;
                updateValidation();
            });
    connect(m_buttons, &QDialogButtonBox::accepted,
            this, [this] { accept(); });
    connect(m_buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    rebuildPlan();
}

int MeshResizeDialog::requestedRows() const {
    return m_rows ? m_rows->value() : m_design.topology.rows;
}

int MeshResizeDialog::requestedColumns() const {
    return m_columns ? m_columns->value() : m_design.topology.columns;
}

QVector<DomainMembership> MeshResizeDialog::newRouterMemberships() const {
    const MeshResizeAssignmentResolution resolution =
        resolveMeshResizeAssignments(
            m_plan, draftMemberships(), impactConfirmation());
    return resolution.success() ? resolution.newRouterMemberships
                                : QVector<DomainMembership>{};
}

MeshResizeImpactConfirmation MeshResizeDialog::impactConfirmation() const {
    MeshResizeImpactConfirmation confirmation;
    if (m_removedMemberships) {
        for (int row = 0; row < m_removedMemberships->count(); ++row) {
            const QListWidgetItem* item = m_removedMemberships->item(row);
            const int index = item->data(Qt::UserRole).toInt();
            if (item->checkState() == Qt::Checked && index >= 0
                && index < m_plan.removedMemberships.size()) {
                confirmation.removedMemberships.append(
                    m_plan.removedMemberships.at(index));
            }
        }
    }
    if (m_removedEdgeOverrides) {
        for (int row = 0; row < m_removedEdgeOverrides->count(); ++row) {
            const QListWidgetItem* item = m_removedEdgeOverrides->item(row);
            const int index = item->data(Qt::UserRole).toInt();
            if (item->checkState() == Qt::Checked && index >= 0
                && index < m_plan.removedEdgeOverrides.size()) {
                confirmation.removedEdgeOverrides.append(
                    m_plan.removedEdgeOverrides.at(index));
            }
        }
    }
    if (m_removedElementConfigurations) {
        for (int row = 0;
             row < m_removedElementConfigurations->count(); ++row) {
            const QListWidgetItem* item =
                m_removedElementConfigurations->item(row);
            const int index = item->data(Qt::UserRole).toInt();
            if (item->checkState() == Qt::Checked && index >= 0
                && index < m_plan.removedElementConfigurations.size()) {
                confirmation.removedElementConfigurations.append(
                    m_plan.removedElementConfigurations.at(index));
            }
        }
    }
    return confirmation;
}

QStringList MeshResizeDialog::localErrors() const {
    return collectLocalErrors();
}

void MeshResizeDialog::accept() {
    updateValidation();
    if (m_apply && m_apply->isEnabled()) {
        QDialog::accept();
    }
}

void MeshResizeDialog::rebuildPlan() {
    if (m_rebuilding) {
        return;
    }
    const QString selectedRouter = currentRouterId();
    m_rebuilding = true;
    m_plan = buildMeshResizePlan(
        m_design, m_package, requestedRows(), requestedColumns());
    normalizeDraftForPlan();

    m_deltaSummary->setText(
        QStringLiteral(
            "Mesh changes from %1 × %2 to %3 × %4. "
            "Routers added: %5; removed: %6. "
            "Router links added: %7; removed: %8. "
            "State removals: %9 membership(s), %10 Domain override(s), %11 element configuration(s).")
            .arg(m_design.topology.rows)
            .arg(m_design.topology.columns)
            .arg(m_plan.requestedTopology.rows)
            .arg(m_plan.requestedTopology.columns)
            .arg(m_plan.newRouters.size())
            .arg(m_plan.removedRouters.size())
            .arg(m_plan.newRouterLinks.size())
            .arg(m_plan.removedRouterLinks.size())
            .arg(m_plan.removedMemberships.size())
            .arg(m_plan.removedEdgeOverrides.size())
            .arg(m_plan.removedElementConfigurations.size()));

    QStringList blockerLines;
    if (!m_plan.detachedEndpoints.isEmpty()) {
        QStringList endpoints;
        for (const ElementRef& endpoint : m_plan.detachedEndpoints) {
            endpoints.append(endpoint.id);
        }
        blockerLines.append(
            QStringLiteral(
                "Blocked: the resize would detach Endpoint(s): %1. Move or remove them before shrinking the Mesh.")
                .arg(endpoints.join(QStringLiteral(", "))));
    }
    for (const Diagnostic& diagnostic : m_plan.diagnostics) {
        if (diagnostic.severity != QStringLiteral("error")
            || diagnostic.code
                == QStringLiteral("mesh.resize_would_detach_endpoint")) {
            continue;
        }
        blockerLines.append(
            QStringLiteral("Blocked: %1 [%2]")
                .arg(diagnostic.message, diagnostic.code));
    }
    m_blockers->setVisible(!blockerLines.isEmpty());
    m_blockers->setText(blockerLines.join(QLatin1Char('\n')));

    rebuildRouterNavigator(selectedRouter);
    rebuildImpactEditors();
    m_rebuilding = false;
    rebuildAssignmentEditor();
    updateValidation();
}

void MeshResizeDialog::normalizeDraftForPlan() {
    QSet<QString> validTypes;
    for (const MeshResizeDomainAssignmentPlan& assignment
         : m_plan.routerAssignmentPlans) {
        validTypes.insert(assignment.domainType);
    }

    for (const MeshResizeRouterPlan& router : m_plan.newRouters) {
        RouterAssignments& assignments =
            m_draftAssignments[router.element.id];
        for (auto iterator = assignments.begin();
             iterator != assignments.end();) {
            if (!validTypes.contains(iterator.key())) {
                iterator = assignments.erase(iterator);
            } else {
                ++iterator;
            }
        }
        for (const MeshResizeDomainAssignmentPlan& assignment
             : m_plan.routerAssignmentPlans) {
            QStringList ids = assignments.value(assignment.domainType);
            ids.erase(std::remove_if(
                          ids.begin(), ids.end(),
                          [&](const QString& id) {
                              return !assignment.availableDomainIds.contains(id);
                          }),
                      ids.end());
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            if (ids.isEmpty() && !assignment.automaticAssignment.isEmpty()) {
                ids = assignment.automaticAssignment;
            }
            if (ids.isEmpty()) {
                assignments.remove(assignment.domainType);
            } else {
                assignments.insert(assignment.domainType, std::move(ids));
            }
        }
    }
}

void MeshResizeDialog::rebuildRouterNavigator(
    const QString& preferredRouterId) {
    m_routerList->clear();
    int preferredRow = -1;
    int firstIncompleteRow = -1;
    for (qsizetype index = 0; index < m_plan.newRouters.size(); ++index) {
        const MeshResizeRouterPlan& router = m_plan.newRouters.at(index);
        const bool complete = routerAssignmentsComplete(router.element.id);
        auto* item = new QListWidgetItem(
            routerItemText(router, complete), m_routerList);
        item->setData(Qt::UserRole, router.element.id);
        item->setData(Qt::UserRole + 1, complete);
        item->setToolTip(
            QStringLiteral(
                "This Router is created by the requested Mesh projection; configure its Domain memberships here."));
        if (router.element.id == preferredRouterId) {
            preferredRow = static_cast<int>(index);
        }
        if (!complete && firstIncompleteRow < 0) {
            firstIncompleteRow = static_cast<int>(index);
        }
    }
    if (preferredRow >= 0) {
        m_routerList->setCurrentRow(preferredRow);
    } else if (firstIncompleteRow >= 0) {
        m_routerList->setCurrentRow(firstIncompleteRow);
    } else if (m_routerList->count() > 0) {
        m_routerList->setCurrentRow(0);
    }
    m_routerList->setEnabled(m_routerList->count() > 0);
}

void MeshResizeDialog::rebuildAssignmentEditor() {
    const QString routerId = currentRouterId();
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);

    if (routerId.isEmpty()) {
        auto* empty = new QLabel(
            QStringLiteral(
                "This resize creates no Routers, so no new Router Domain assignments are required."),
            content);
        empty->setObjectName(
            QStringLiteral("finepaper.meshResize.noNewRouters"));
        empty->setWordWrap(true);
        layout->addWidget(empty);
        layout->addStretch(1);
        m_assignmentScroll->setWidget(content);
        refreshRouterNavigator();
        return;
    }

    const MeshResizeRouterPlan* router = findRouter(m_plan, routerId);
    auto* heading = new QLabel(
        router
            ? topology_text::meshResizeRouterHeadingText(
                  routerId, router->position.x, router->position.y)
            : routerId,
        content);
    heading->setObjectName(
        QStringLiteral("finepaper.meshResize.currentRouter"));
    heading->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(heading);

    for (const MeshResizeDomainAssignmentPlan& assignment
         : m_plan.routerAssignmentPlans) {
        auto* group = new QGroupBox(
            domain_text::domainTypeDisplayText(
                assignment.domainType, assignment.label),
            content);
        group->setObjectName(
            assignmentObjectName(routerId,
                                 assignment.domainType,
                                 QStringLiteral("group")));
        group->setProperty("finepaper.routerId", routerId);
        group->setProperty("finepaper.domainType", assignment.domainType);
        auto* groupLayout = new QVBoxLayout(group);
        auto* constraint = new QLabel(
            domain_text::domainAssignmentConstraintText(
                assignment.assignmentRule),
            group);
        constraint->setObjectName(
            assignmentObjectName(routerId,
                                 assignment.domainType,
                                 QStringLiteral("constraint")));
        constraint->setProperty("finepaper.routerId", routerId);
        constraint->setProperty(
            "finepaper.domainType", assignment.domainType);
        constraint->setWordWrap(true);
        constraint->setTextInteractionFlags(Qt::TextSelectableByMouse);
        groupLayout->addWidget(constraint);

        const bool fixedAutomatic =
            hasFixedAutomaticAssignment(assignment);

        if (assignment.assignmentRule.isSingleAssignment()) {
            auto* combo = new QComboBox(group);
            combo->setObjectName(
                assignmentObjectName(routerId,
                                     assignment.domainType,
                                     QStringLiteral("single")));
            combo->setProperty("finepaper.routerId", routerId);
            combo->setProperty("finepaper.domainType", assignment.domainType);
            if (!fixedAutomatic) {
                combo->addItem(
                    assignment.assignmentRule.requiresAssignment()
                        ? QStringLiteral("Choose a Domain…")
                        : QStringLiteral("Unassigned"),
                    QString());
            }
            for (const QString& id : assignment.availableDomainIds) {
                combo->addItem(domainLabel(m_plan, id), id);
            }
            const QString selected =
                m_draftAssignments.value(routerId)
                    .value(assignment.domainType)
                    .value(0);
            const int selectedIndex = combo->findData(selected);
            if (selectedIndex >= 0) {
                combo->setCurrentIndex(selectedIndex);
            }
            combo->setEnabled(!fixedAutomatic);
            if (fixedAutomatic) {
                combo->setToolTip(automaticAssignmentText(assignment));
            }
            groupLayout->addWidget(combo);
            connect(combo, &QComboBox::currentIndexChanged,
                    this,
                    [this, combo, routerId,
                     domainType = assignment.domainType] {
                        if (m_rebuilding) {
                            return;
                        }
                        const QString id =
                            combo->currentData().toString().trimmed();
                        if (id.isEmpty()) {
                            m_draftAssignments[routerId].remove(domainType);
                        } else {
                            m_draftAssignments[routerId].insert(
                                domainType, QStringList{id});
                        }
                        refreshRouterNavigator();
                        updateValidation();
                    });
        } else if (assignment.assignmentRule.isValid()) {
            auto* list = new QListWidget(group);
            list->setObjectName(
                assignmentObjectName(routerId,
                                     assignment.domainType,
                                     QStringLiteral("multiple")));
            list->setProperty("finepaper.routerId", routerId);
            list->setProperty("finepaper.domainType", assignment.domainType);
            list->setSelectionMode(QAbstractItemView::NoSelection);
            list->setMinimumHeight(88);
            const QStringList selectedIds =
                m_draftAssignments.value(routerId)
                    .value(assignment.domainType);
            const QSet<QString> selected(selectedIds.cbegin(),
                                         selectedIds.cend());
            for (const QString& id : assignment.availableDomainIds) {
                auto* item = new QListWidgetItem(domainLabel(m_plan, id), list);
                item->setData(Qt::UserRole, id);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(id) ? Qt::Checked
                                                          : Qt::Unchecked);
                if (fixedAutomatic) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                    item->setToolTip(automaticAssignmentText(assignment));
                }
            }
            groupLayout->addWidget(list);
            connect(list, &QListWidget::itemChanged,
                    this,
                    [this, list, routerId,
                     domainType = assignment.domainType] {
                        if (m_rebuilding) {
                            return;
                        }
                        QStringList ids;
                        for (int row = 0; row < list->count(); ++row) {
                            const QListWidgetItem* item = list->item(row);
                            if (item->checkState() == Qt::Checked) {
                                ids.append(
                                    item->data(Qt::UserRole).toString());
                            }
                        }
                        std::sort(ids.begin(), ids.end());
                        if (ids.isEmpty()) {
                            m_draftAssignments[routerId].remove(domainType);
                        } else {
                            m_draftAssignments[routerId].insert(
                                domainType, std::move(ids));
                        }
                        refreshRouterNavigator();
                        updateValidation();
                    });
        } else {
            auto* invalid = new QLabel(
                QStringLiteral(
                    "This Domain Type has an invalid Package assignment rule "
                    "and cannot be assigned."),
                group);
            invalid->setWordWrap(true);
            groupLayout->addWidget(invalid);
        }

        if (fixedAutomatic) {
            auto* automatic = new QLabel(
                automaticAssignmentText(assignment), group);
            automatic->setObjectName(
                assignmentObjectName(routerId,
                                     assignment.domainType,
                                     QStringLiteral("automatic")));
            automatic->setWordWrap(true);
            automatic->setTextInteractionFlags(Qt::TextSelectableByMouse);
            groupLayout->addWidget(automatic);
        }

        if (assignment.assignmentRule.isValid()
            && (assignment.availableDomainIds.size()
                    < assignment.assignmentRule.minimumAssignments
                || assignment.availableDomainIds.isEmpty())) {
            auto* unavailable = new QLabel(
                unavailableAssignmentText(assignment),
                group);
            unavailable->setObjectName(
                assignmentObjectName(routerId,
                                     assignment.domainType,
                                     QStringLiteral("unavailable")));
            unavailable->setWordWrap(true);
            groupLayout->addWidget(unavailable);
        }
        layout->addWidget(group);
    }
    if (m_plan.routerAssignmentPlans.isEmpty()) {
        auto* none = new QLabel(
            QStringLiteral(
                "The Package declares no Domain Types that apply to Routers."),
            content);
        none->setWordWrap(true);
        layout->addWidget(none);
    }
    layout->addStretch(1);
    m_assignmentScroll->setWidget(content);
    refreshRouterNavigator();
}

void MeshResizeDialog::rebuildImpactEditors() {
    m_removedMemberships->clear();
    for (qsizetype index = 0; index < m_plan.removedMemberships.size(); ++index) {
        const DomainMembership& membership =
            m_plan.removedMemberships.at(index);
        auto* item = new QListWidgetItem(
            impactMembershipText(membership), m_removedMemberships);
        item->setData(Qt::UserRole, static_cast<int>(index));
        item->setData(Qt::UserRole + 1, membership.element.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setToolTip(
            QStringLiteral("Check to confirm removal of this exact membership record."));
    }
    m_removedMemberships->setEnabled(m_removedMemberships->count() > 0);

    m_removedEdgeOverrides->clear();
    for (qsizetype index = 0;
         index < m_plan.removedEdgeOverrides.size(); ++index) {
        const DomainEdgeOverride& edgeOverride =
            m_plan.removedEdgeOverrides.at(index);
        auto* item = new QListWidgetItem(
            impactOverrideText(edgeOverride), m_removedEdgeOverrides);
        item->setData(Qt::UserRole, static_cast<int>(index));
        item->setData(Qt::UserRole + 1, edgeOverride.edge.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setToolTip(
            QStringLiteral("Check to confirm removal of this exact override record."));
    }
    m_removedEdgeOverrides->setEnabled(
        m_removedEdgeOverrides->count() > 0);
    m_removedElementConfigurations->clear();
    for (qsizetype index = 0;
         index < m_plan.removedElementConfigurations.size(); ++index) {
        const ElementConfiguration& configuration =
            m_plan.removedElementConfigurations.at(index);
        auto* item = new QListWidgetItem(
            impactElementConfigurationText(configuration),
            m_removedElementConfigurations);
        item->setData(Qt::UserRole, static_cast<int>(index));
        item->setData(Qt::UserRole + 1, configuration.element.id);
        item->setData(Qt::UserRole + 2, configuration.propertySet);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setToolTip(
            QStringLiteral(
                "Check to confirm removal of this exact sparse element configuration record."));
    }
    m_removedElementConfigurations->setEnabled(
        m_removedElementConfigurations->count() > 0);
    const bool hasImpacts = m_removedMemberships->count() > 0
        || m_removedEdgeOverrides->count() > 0
        || m_removedElementConfigurations->count() > 0;
    m_confirmAllImpacts->setEnabled(hasImpacts);
    m_clearImpactConfirmations->setEnabled(false);
}

void MeshResizeDialog::refreshRouterNavigator() {
    if (!m_routerList) {
        return;
    }
    bool hasIncomplete = false;
    for (int row = 0; row < m_routerList->count(); ++row) {
        QListWidgetItem* item = m_routerList->item(row);
        const QString routerId = item->data(Qt::UserRole).toString();
        const MeshResizeRouterPlan* router = findRouter(m_plan, routerId);
        if (!router) {
            continue;
        }
        const bool complete = routerAssignmentsComplete(routerId);
        item->setText(routerItemText(*router, complete));
        item->setData(Qt::UserRole + 1, complete);
        hasIncomplete = hasIncomplete || !complete;
    }
    const bool hasCurrent = !currentRouterId().isEmpty();
    m_copyToAll->setEnabled(hasCurrent && m_plan.newRouters.size() > 1);
    m_nextIncomplete->setEnabled(hasIncomplete);
}

void MeshResizeDialog::updateValidation() {
    if (m_rebuilding || !m_apply || !m_diagnostics) {
        return;
    }
    const QStringList errors = collectLocalErrors();
    const int removedRecordCount = m_plan.removedMemberships.size()
        + m_plan.removedEdgeOverrides.size()
        + m_plan.removedElementConfigurations.size();
    m_apply->setText(
        removedRecordCount > 0
            ? QStringLiteral("Resize and remove %1 records")
                  .arg(removedRecordCount)
            : QStringLiteral("Apply Mesh Resize"));
    m_apply->setEnabled(errors.isEmpty());
    const MeshResizeImpactConfirmation confirmation = impactConfirmation();
    m_clearImpactConfirmations->setEnabled(
        !confirmation.removedMemberships.isEmpty()
        || !confirmation.removedEdgeOverrides.isEmpty()
        || !confirmation.removedElementConfigurations.isEmpty());
    if (errors.isEmpty()) {
        QString ready = QStringLiteral(
            "Ready. Applying will commit the Mesh dimensions and all new "
            "Router Domain memberships as one transaction.");
        if (removedRecordCount > 0) {
            ready += QStringLiteral(
                         " It will also delete %1 confirmed state record(s).")
                         .arg(removedRecordCount);
        }
        m_diagnostics->setText(ready);
    } else {
        QStringList lines;
        for (const QString& error : errors) {
            lines.append(QStringLiteral("• %1").arg(error));
        }
        m_diagnostics->setText(lines.join(QLatin1Char('\n')));
    }
}

void MeshResizeDialog::copyCurrentAssignmentsToAll() {
    const QString routerId = currentRouterId();
    if (routerId.isEmpty()) {
        return;
    }
    const RouterAssignments source = m_draftAssignments.value(routerId);
    for (const MeshResizeRouterPlan& router : m_plan.newRouters) {
        m_draftAssignments.insert(router.element.id, source);
    }
    rebuildAssignmentEditor();
    refreshRouterNavigator();
    updateValidation();
}

void MeshResizeDialog::jumpToNextIncomplete() {
    if (!m_routerList || m_routerList->count() == 0) {
        return;
    }
    const int current = std::max(0, m_routerList->currentRow());
    for (int offset = 1; offset <= m_routerList->count(); ++offset) {
        const int row = (current + offset) % m_routerList->count();
        const QString routerId =
            m_routerList->item(row)->data(Qt::UserRole).toString();
        if (!routerAssignmentsComplete(routerId)) {
            m_routerList->setCurrentRow(row);
            return;
        }
    }
}

QString MeshResizeDialog::currentRouterId() const {
    if (!m_routerList || !m_routerList->currentItem()) {
        return {};
    }
    return m_routerList->currentItem()->data(Qt::UserRole).toString();
}

bool MeshResizeDialog::routerAssignmentsComplete(
    const QString& routerId) const {
    const RouterAssignments assignments = m_draftAssignments.value(routerId);
    for (const MeshResizeDomainAssignmentPlan& plan
         : m_plan.routerAssignmentPlans) {
        const QStringList ids = assignments.value(plan.domainType);
        if (!plan.assignmentRule.acceptsCount(ids.size())) {
            return false;
        }
        for (const QString& id : ids) {
            if (!plan.availableDomainIds.contains(id)) {
                return false;
            }
        }
    }
    return true;
}

QVector<DomainMembership> MeshResizeDialog::draftMemberships() const {
    QVector<DomainMembership> memberships;
    for (const MeshResizeRouterPlan& router : m_plan.newRouters) {
        const RouterAssignments assignments =
            m_draftAssignments.value(router.element.id);
        if (!assignments.isEmpty()) {
            memberships.append(DomainMembership{router.element, assignments});
        }
    }
    return memberships;
}

QStringList MeshResizeDialog::collectLocalErrors() const {
    QStringList errors;
    if (requestedRows() == m_design.topology.rows
        && requestedColumns() == m_design.topology.columns) {
        errors.append(QStringLiteral("Choose a different Mesh size."));
    }

    const MeshResizeAssignmentResolution current =
        resolveMeshResizeAssignments(
            m_plan, draftMemberships(), impactConfirmation());
    errors += errorDiagnosticText(current.diagnostics);

    const MeshResizeImpactConfirmation confirmation = impactConfirmation();
    const qsizetype uncheckedMemberships =
        m_plan.removedMemberships.size()
        - confirmation.removedMemberships.size();
    if (uncheckedMemberships > 0) {
        errors.append(
            QStringLiteral(
                "Confirm %1 Router Domain membership removal(s) in the exact-record list above.")
                .arg(uncheckedMemberships));
    }
    const qsizetype uncheckedOverrides =
        m_plan.removedEdgeOverrides.size()
        - confirmation.removedEdgeOverrides.size();
    if (uncheckedOverrides > 0) {
        errors.append(
            QStringLiteral(
                "Confirm %1 Router-link Domain override removal(s) in the exact-record list above.")
                .arg(uncheckedOverrides));
    }
    const qsizetype uncheckedConfigurations =
        m_plan.removedElementConfigurations.size()
        - confirmation.removedElementConfigurations.size();
    if (uncheckedConfigurations > 0) {
        errors.append(
            QStringLiteral(
                "Confirm %1 Router or Router-link element configuration removal(s) in the exact-record list above.")
                .arg(uncheckedConfigurations));
    }

    errors.removeDuplicates();
    return errors;
}

} // namespace finepaper
