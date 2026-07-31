#include "gui/main_window.h"

#include "features/domain/domain_configuration_dialog.h"
#include "features/domain/domain_configuration_workspace.h"
#include "features/design_extensions/design_extensions_workspace.h"
#include "features/domain/domain_manager_panel.h"
#include "gui/element_configuration_panel.h"
#include "gui/endpoint_configuration_panel.h"
#include "features/domain/endpoint_domain_assignment_dialog.h"
#include "features/topology/mesh_resize_dialog.h"
#include "gui/package_parameter_form.h"
#include "ui/workbench/workbench_config.h"
#include "storage/json.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QJsonDocument>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QString diagnosticText(const QVector<Diagnostic>& diagnostics) {
    QStringList lines;
    for (const Diagnostic& diagnostic : diagnostics) {
        lines.append(QStringLiteral("[%1] %2: %3%4")
                         .arg(diagnostic.severity,
                              diagnostic.code,
                              diagnostic.message,
                              diagnostic.path.isEmpty()
                                  ? QString()
                                  : QStringLiteral(" (%1)").arg(diagnostic.path)));
    }
    return lines.isEmpty() ? QStringLiteral("No diagnostics.")
                           : lines.join(QLatin1Char('\n'));
}

QString domainSetHtml(const QStringList& domainIds) {
    if (domainIds.isEmpty()) {
        return QStringLiteral("∅");
    }
    QStringList escapedIds;
    escapedIds.reserve(domainIds.size());
    for (const QString& domainId : domainIds) {
        escapedIds.append(domainId.toHtmlEscaped());
    }
    return QStringLiteral("{ %1 }").arg(escapedIds.join(QStringLiteral(", ")));
}

QString propertiesHtml(const QJsonObject& properties) {
    return QStringLiteral("<code>%1</code>")
        .arg(QString::fromUtf8(
                 QJsonDocument(properties).toJson(QJsonDocument::Compact))
                 .toHtmlEscaped());
}

QString domainCrossingInspectorHtml(
    const DomainPresentationSnapshot& snapshot,
    const ElementRef& edge) {
    const DomainCrossingPresentation* crossing = snapshot.crossing(edge);
    if (!crossing || snapshot.activeDomainType.isEmpty()) {
        return {};
    }

    const QString label = snapshot.domainTypeLabel.trimmed().isEmpty()
        ? snapshot.activeDomainType : snapshot.domainTypeLabel;
    const QString layer = label == snapshot.activeDomainType
        ? label.toHtmlEscaped()
        : QStringLiteral("%1 (<code>%2</code>)")
              .arg(label.toHtmlEscaped(),
                   snapshot.activeDomainType.toHtmlEscaped());

    QStringList lines{
        QStringLiteral("<b>Color-by Domain crossing — %1</b>").arg(layer),
        QStringLiteral("From set: %1").arg(
            domainSetHtml(crossing->fromDomainIds)),
        QStringLiteral("To set: %1").arg(
            domainSetHtml(crossing->toDomainIds))
    };
    if (crossing->defaultPolicy) {
        lines.append(
            QStringLiteral("Default policy: <code>%1</code>")
                .arg(crossing->defaultPolicy->toHtmlEscaped()));
        lines.append(
            QStringLiteral("Default properties: %1")
                .arg(propertiesHtml(crossing->defaultProperties)));
    } else {
        const bool singleton = crossing->fromDomainIds.size() == 1
            && crossing->toDomainIds.size() == 1;
        lines.append(
            singleton
                ? QStringLiteral(
                      "Default policy: <i>none resolved for this exact "
                      "canonical boundary orientation</i>")
                : QStringLiteral(
                      "Default policy: <i>unavailable for a set-valued "
                      "crossing</i>"));
    }

    if (crossing->overridePolicy || !crossing->overrideProperties.isEmpty()) {
        lines.append(
            QStringLiteral("Edge override policy: %1")
                .arg(crossing->overridePolicy
                         ? QStringLiteral("<code>%1</code>")
                               .arg(crossing->overridePolicy->toHtmlEscaped())
                         : QStringLiteral("<i>not specified</i>")));
        lines.append(
            QStringLiteral("Override properties: %1")
                .arg(propertiesHtml(crossing->overrideProperties)));
    } else {
        lines.append(crossing->defaultPolicy
            ? QStringLiteral("Edge override: <i>none; the default applies unchanged</i>")
            : QStringLiteral("Edge override: <i>none</i>"));
    }
    return QStringLiteral("<br><br>%1").arg(lines.join(QStringLiteral("<br>")));
}

QString normalizedAbsolutePath(const QString& path) {
    const QFileInfo info(path);
    const QString canonicalPath = info.canonicalFilePath();
    return canonicalPath.isEmpty()
        ? QDir::cleanPath(info.absoluteFilePath())
        : canonicalPath;
}

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QWidget* placeholderPage(const QString& title, const QString& description, QLabel** summary) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(32, 32, 32, 32);
    auto* heading = new QLabel(title);
    QFont font = heading->font();
    font.setPointSize(18);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    auto* text = new QLabel(description);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(text);
    layout->addStretch();
    if (summary) {
        *summary = text;
    }
    return page;
}

bool hasOnlyDomainConfigurationErrors(
    const QVector<Diagnostic>& diagnostics) {
    bool hasError = false;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != QStringLiteral("error")) {
            continue;
        }
        hasError = true;
        if (!domain_configuration::ownsDiagnostic(diagnostic)) {
            return false;
        }
    }
    return hasError;
}

class NewDesignDialog final : public QDialog {
public:
    NewDesignDialog(QVector<PackageDefinition> packages,
                    const QString& preferredPackageKey,
                    const QString& suggestedName,
                    QWidget* parent)
        : QDialog(parent),
          m_packages(std::move(packages)) {
        setObjectName(QStringLiteral("finepaper.newDesignDialog"));
        setWindowTitle(QStringLiteral("New NoC Design"));
        setModal(true);
        setMinimumWidth(480);

        auto* layout = new QVBoxLayout(this);
        auto* introduction = new QLabel(
            QStringLiteral("Choose the NoC IP first, then configure the initial Mesh. "
                           "The created design remains bound to this Package version."));
        introduction->setWordWrap(true);
        layout->addWidget(introduction);

        auto* form = new QFormLayout;
        m_packageSelector = new QComboBox;
        m_packageSelector->setObjectName(
            QStringLiteral("finepaper.newDesignPackageSelector"));
        for (const PackageDefinition& package : m_packages) {
            m_packageSelector->addItem(
                QStringLiteral("%1 — %2 (%3)")
                    .arg(package.name, package.version, package.id),
                package.key());
        }
        const int preferredIndex = m_packageSelector->findData(preferredPackageKey);
        if (preferredIndex >= 0) {
            m_packageSelector->setCurrentIndex(preferredIndex);
        }
        form->addRow(QStringLiteral("NoC IP"), m_packageSelector);

        m_designName = new QLineEdit(
            suggestedName.trimmed().isEmpty() ? QStringLiteral("my_noc") : suggestedName);
        m_designName->setObjectName(QStringLiteral("finepaper.newDesignName"));
        form->addRow(QStringLiteral("Design name"), m_designName);

        m_rows = new QSpinBox;
        m_rows->setObjectName(QStringLiteral("finepaper.newDesignRows"));
        m_columns = new QSpinBox;
        m_columns->setObjectName(QStringLiteral("finepaper.newDesignColumns"));
        form->addRow(QStringLiteral("Mesh rows"), m_rows);
        form->addRow(QStringLiteral("Mesh columns"), m_columns);
        layout->addLayout(form);

        m_packageDetails = new QLabel;
        m_packageDetails->setObjectName(QStringLiteral("finepaper.newDesignPackageDetails"));
        m_packageDetails->setWordWrap(true);
        m_packageDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(m_packageDetails);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        m_createButton = buttons->button(QDialogButtonBox::Ok);
        m_createButton->setText(QStringLiteral("Create Design"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_packageSelector, &QComboBox::currentIndexChanged,
                this, [this] { updatePackageSelection(); });
        connect(m_designName, &QLineEdit::textChanged,
                this, [this] { updateAcceptState(); });
        layout->addWidget(buttons);

        updatePackageSelection();
    }

    QJsonObject createRequest() const {
        const PackageDefinition* package = selectedPackage();
        if (!package) {
            return {};
        }
        return QJsonObject{
            {QStringLiteral("name"), m_designName->text().trimmed()},
            {QStringLiteral("package"), QJsonObject{
                {QStringLiteral("id"), package->id},
                {QStringLiteral("version"), package->version}}},
            {QStringLiteral("topology"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("mesh")},
                {QStringLiteral("rows"), m_rows->value()},
                {QStringLiteral("columns"), m_columns->value()}}}
        };
    }

private:
    const PackageDefinition* selectedPackage() const {
        const QString key = m_packageSelector->currentData().toString();
        const auto it = std::find_if(
            m_packages.cbegin(), m_packages.cend(), [&](const PackageDefinition& package) {
                return package.key() == key;
            });
        return it == m_packages.cend() ? nullptr : &*it;
    }

    void updatePackageSelection() {
        const PackageDefinition* package = selectedPackage();
        if (!package) {
            m_rows->setRange(1, 1);
            m_columns->setRange(1, 1);
            m_packageDetails->setText(
                QStringLiteral("No valid NoC IP Package is available."));
            updateAcceptState();
            return;
        }

        m_rows->setRange(package->mesh.minimumRows, package->mesh.maximumRows);
        m_columns->setRange(package->mesh.minimumColumns, package->mesh.maximumColumns);
        m_rows->setValue(package->mesh.defaultRows);
        m_columns->setValue(package->mesh.defaultColumns);
        m_packageDetails->setText(
            QStringLiteral("<b>%1</b><br>%2@%3<br>Mesh: %4–%5 rows × %6–%7 columns")
                .arg(package->name.toHtmlEscaped(),
                     package->id.toHtmlEscaped(),
                     package->version.toHtmlEscaped())
                .arg(package->mesh.minimumRows)
                .arg(package->mesh.maximumRows)
                .arg(package->mesh.minimumColumns)
                .arg(package->mesh.maximumColumns));
        updateAcceptState();
    }

    void updateAcceptState() {
        m_createButton->setEnabled(
            selectedPackage() && !m_designName->text().trimmed().isEmpty());
    }

    QVector<PackageDefinition> m_packages;
    QComboBox* m_packageSelector = nullptr;
    QLineEdit* m_designName = nullptr;
    QSpinBox* m_rows = nullptr;
    QSpinBox* m_columns = nullptr;
    QLabel* m_packageDetails = nullptr;
    QPushButton* m_createButton = nullptr;
};

} // namespace

class EndpointPaletteList final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override {
        return {workbench::endpointTypeMime};
    }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        if (items.isEmpty()) {
            return nullptr;
        }
        const QString endpointType = items.front()->data(Qt::UserRole).toString();
        if (endpointType.isEmpty()) {
            return nullptr;
        }
        auto* mimeData = new QMimeData;
        mimeData->setData(workbench::endpointTypeMime, endpointType.toUtf8());
        return mimeData;
    }

    Qt::DropActions supportedDropActions() const override {
        return Qt::CopyAction;
    }

    void startDrag(Qt::DropActions) override {
        QList<QListWidgetItem*> items = selectedItems();
        if (items.isEmpty() && currentItem()) {
            items.append(currentItem());
        }
        QMimeData* payload = mimeData(items);
        if (!payload) {
            return;
        }
        QDrag drag(this);
        drag.setMimeData(payload);
        drag.setPixmap(style()->standardIcon(QStyle::SP_ArrowRight).pixmap(32, 32));
        drag.setHotSpot(QPoint(16, 16));
        drag.exec(Qt::CopyAction, Qt::CopyAction);
    }
};

FinepaperMainWindow::FinepaperMainWindow(RuntimeLocations locations, QWidget* parent)
    : QMainWindow(parent),
      m_locations(std::move(locations)) {
    loadInstalledPackageRoots();
    createUi();
    restoreWorkbenchState();
    reloadPackages();
    if (statusBar()->currentMessage().isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("Install a NoC IP Package, then create or open a NoC design."));
    }
}

FinepaperMainWindow::~FinepaperMainWindow() {
    if (m_nodeEditor) {
        m_nodeEditor->endpointTypeDropped = {};
        m_nodeEditor->endpointMoveRequested = {};
        m_nodeEditor->detachedEndpointDropped = {};
        m_nodeEditor->endpointRemovalRequested = {};
        m_nodeEditor->endpointDeletionRequested = {};
        m_nodeEditor->detachedEndpointDeletionRequested = {};
        m_nodeEditor->selectionChanged = {};
        m_nodeEditor->semanticSelectionChanged = {};
        m_nodeEditor->workspaceDiagnosticRaised = {};
        m_nodeEditor->attachmentRejected = {};
    }
    if (m_domainManager) {
        m_domainManager->validateAddDomain = {};
        m_domainManager->validateUpdateDomain = {};
        m_domainManager->addDomainRequested = {};
        m_domainManager->updateDomainRequested = {};
        m_domainManager->removeDomainRequested = {};
        m_domainManager->assignmentPatchRequested = {};
        m_domainManager->completeConfigurationRequested = {};
        m_domainManager->showDomainLayerRequested = {};
        m_domainManager->selectElementsRequested = {};
    }
    if (m_domainConfigurationWorkspace) {
        m_domainConfigurationWorkspace->applyRequested = {};
        m_domainConfigurationWorkspace->draftStateChanged = {};
    }
    if (m_designExtensionsWorkspace) {
        m_designExtensionsWorkspace->applyRequested = {};
        m_designExtensionsWorkspace->removeRequested = {};
    }
    if (m_elementConfigurationPanel) {
        m_elementConfigurationPanel->applyRequested = {};
        m_elementConfigurationPanel->resetRequested = {};
    }
    if (m_endpointConfigurationPanel) {
        m_endpointConfigurationPanel->draftStateChanged = {};
        m_endpointConfigurationPanel->planTypeChangeRequested = {};
        m_endpointConfigurationPanel->updateParametersRequested = {};
        m_endpointConfigurationPanel->changeTypeRequested = {};
    }
}

bool FinepaperMainWindow::operationBusy() const {
    return m_operationBusy;
}

void FinepaperMainWindow::createUi() {
    setWindowTitle(QStringLiteral("Finepaper — NoC Workbench[*]"));
    resize(1480, 920);
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks
                   | QMainWindow::GroupedDragging);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    createCentralViews();
    createPackageDock();
    createInspectorDock();
    createDomainDock();
    createResultsDock();
    createActions();

    m_operationProgress = new QProgressBar(this);
    m_operationProgress->setObjectName(QStringLiteral("finepaper.operationProgress"));
    m_operationProgress->setRange(0, 0);
    m_operationProgress->setMaximumWidth(180);
    m_operationProgress->setTextVisible(false);
    m_operationProgress->hide();
    statusBar()->addPermanentWidget(m_operationProgress);

    resizeDocks({m_packageDock, m_inspectorDock}, {285, 360}, Qt::Horizontal);
    resizeDocks({m_resultsDock}, {250}, Qt::Vertical);
    updateUiState();
}

void FinepaperMainWindow::createCentralViews() {
    m_centerViews = new QTabWidget(this);
    m_centerViews->setDocumentMode(true);
    m_centerViews->setMovable(true);
    m_centerViews->setTabsClosable(false);
    setCentralWidget(m_centerViews);
    m_viewRegistry.emplace(m_centerViews);

    m_nodeEditor = new NocNodeEditor(m_centerViews);
    m_nodeEditor->setObjectName(QStringLiteral("finepaper.nodeEditor"));
    m_viewRegistry->addView(
        {workbench::editorViewId, workbench::editorViewTitle}, m_nodeEditor);

    m_domainConfigurationWorkspace = new DomainConfigurationWorkspace(
        m_centerViews);
    m_viewRegistry->addView(
        {workbench::domainConfigurationViewId,
         workbench::domainConfigurationViewTitle},
        m_domainConfigurationWorkspace);
    m_domainConfigurationWorkspace->applyRequested = [this](
        const DesignResult& result) {
        if (m_operationBusy || !m_design) {
            return false;
        }
        if (m_domainManager
            && m_domainManager->hasPendingAssignmentChanges()) {
            QMessageBox warning(
                QMessageBox::Warning,
                QStringLiteral("Pending quick Domain assignment"),
                QStringLiteral(
                    "Apply or discard the staged assignment in the Domain "
                    "Manager before applying the complete five-section "
                    "configuration."),
                QMessageBox::Ok,
                this);
            warning.setObjectName(QStringLiteral(
                "finepaper.domainConfigurationWorkspace.assignmentBlocker"));
            warning.exec();
            return false;
        }
        adoptDomainResult(
            result,
            QStringLiteral("Apply complete Domain configuration"));
        return result.success;
    };
    m_domainConfigurationWorkspace->draftStateChanged = [this](bool) {
        updateUiState();
    };

    m_designExtensionsWorkspace = new DesignExtensionsWorkspace(m_centerViews);
    m_viewRegistry->addView(
        {workbench::designExtensionsViewId,
         workbench::designExtensionsViewTitle},
        m_designExtensionsWorkspace);
    m_designExtensionsWorkspace->applyRequested = [this](
        QString extensionId, QJsonValue value) {
        if (m_operationBusy || !m_design || !packageForDesign()) {
            return DesignResult{};
        }
        const QString action = QStringLiteral("Apply Design Extension %1")
                                   .arg(extensionId);
        DesignResult result = m_application.setDesignExtension(
            *m_design, extensionId, value);
        if (!result.success) {
            showDiagnostics(result.diagnostics, action, false);
            appendActivity(action + QStringLiteral(" failed."));
            statusBar()->showMessage(action + QStringLiteral(" failed."), 6000);
            return result;
        }
        adoptDesignResult(result, action, DesignRefreshScope::InspectorOnly);
        updateDesignExtensionsWorkspace();
        return result;
    };
    m_designExtensionsWorkspace->removeRequested = [this](
        QString extensionId) {
        if (m_operationBusy || !m_design || !packageForDesign()) {
            return DesignResult{};
        }
        const QString action = QStringLiteral("Remove Design Extension %1")
                                   .arg(extensionId);
        DesignResult result = m_application.removeDesignExtension(
            *m_design, extensionId);
        if (!result.success) {
            showDiagnostics(result.diagnostics, action, false);
            appendActivity(action + QStringLiteral(" failed."));
            statusBar()->showMessage(action + QStringLiteral(" failed."), 6000);
            return result;
        }
        adoptDesignResult(result, action, DesignRefreshScope::InspectorOnly);
        updateDesignExtensionsWorkspace();
        return result;
    };

    QWidget* performance = placeholderPage(
        QStringLiteral("Performance Analysis"),
        QStringLiteral("Performance analysis is a separate workbench view. It will consume "
                       "generated reports or Package/IP Engine results without replacing the "
                       "NoC design model."),
        &m_performanceSummary);
    m_viewRegistry->addView(
        {workbench::performanceViewId, workbench::performanceViewTitle}, performance);

    auto* problemPage = new QWidget;
    auto* problemLayout = new QVBoxLayout(problemPage);
    problemLayout->setContentsMargins(16, 16, 16, 16);
    auto* problemHeading = new QLabel(QStringLiteral("Problem Report"));
    QFont problemFont = problemHeading->font();
    problemFont.setPointSize(18);
    problemFont.setBold(true);
    problemHeading->setFont(problemFont);
    m_problemReport = new QPlainTextEdit;
    m_problemReport->setReadOnly(true);
    m_problemReport->setPlaceholderText(
        QStringLiteral("Run validation to create a readable problem report."));
    problemLayout->addWidget(problemHeading);
    problemLayout->addWidget(m_problemReport, 1);
    m_viewRegistry->addView(
        {workbench::problemReportViewId, workbench::problemReportViewTitle}, problemPage);

    m_nodeEditor->endpointTypeDropped = [this](const QString& endpointType,
                                                NocAttachmentTarget target) {
        return addEndpoint(endpointType, std::move(target));
    };
    m_nodeEditor->endpointMoveRequested = [this](const QString& endpointId,
                                                  NocAttachmentTarget target) {
        return moveEndpoint(endpointId, std::move(target));
    };
    m_nodeEditor->detachedEndpointDropped = [this](
        const NocDetachedEndpointSnapshot& detached,
        NocAttachmentTarget target) {
        if (!m_design || !packageForDesign()) {
            return false;
        }
        if (!confirmDiscardPendingDomainChanges(
                QStringLiteral("Reconnecting an Endpoint"))) {
            return false;
        }
        const AttachmentSlotChoice slotChoice = chooseAttachmentSlot(target);
        if (!slotChoice.accepted) {
            return false;
        }
        EndpointInstance endpoint = detached.endpoint;
        endpoint.attachment.router = target.router;
        endpoint.attachment.slot = slotChoice.slot;
        const auto assignments = chooseEndpointDomainAssignments(
            endpoint.id, detached.domainAssignments);
        if (!assignments) {
            return false;
        }
        const DesignResult result = m_application.addEndpoint(
            *m_design,
            endpoint,
            *assignments,
            detached.attachmentOverrides,
            detached.attachmentConfigurations);
        if (result.success) {
            discardPendingDomainChanges();
        }
        adoptDesignResult(result,
                          QStringLiteral("Reconnect Endpoint %1").arg(endpoint.id));
        return result.success;
    };
    m_nodeEditor->endpointRemovalRequested = [this](const QString& endpointId) {
        return removeEndpoint(endpointId, false);
    };
    m_nodeEditor->endpointDeletionRequested = [this](const QString& endpointId) {
        return removeEndpoint(endpointId, true);
    };
    m_nodeEditor->detachedEndpointDeletionRequested = [this](
        const QString& endpointId) {
        if (m_endpointConfigurationPanel) {
            m_endpointConfigurationPanel->discardDraft(
                m_designSessionIdentity, endpointId);
        }
    };
    m_nodeEditor->attachmentRejected = [this](
        attachment::Rejection rejection,
        std::optional<RouterPosition> router) {
        const QString message = attachment::rejectionMessage(rejection, router);
        if (message.isEmpty()) {
            return;
        }
        statusBar()->showMessage(message, 6000);
        appendActivity(QStringLiteral("Endpoint attachment unchanged: %1")
                           .arg(message));
    };
    m_nodeEditor->semanticSelectionChanged = [this](
        const NocEditorSelectionSet& selection) {
        updateInspector(selection);
    };
    m_nodeEditor->workspaceDiagnosticRaised = [this](
        const TopologyWorkspaceDiagnostic& diagnostic) {
        QString message;
        switch (diagnostic.kind) {
        case TopologyWorkspaceDiagnosticKind::LoadFailed:
            message = QStringLiteral(
                "Canvas layout is damaged; the design is still usable. Choose "
                "Regularize Layout to repair it.");
            break;
        case TopologyWorkspaceDiagnosticKind::SaveFailed:
            message = QStringLiteral(
                "Canvas layout could not be saved. Node positions may not be "
                "restored next time.");
            break;
        case TopologyWorkspaceDiagnosticKind::SaveRecovered:
            message = QStringLiteral(
                "Canvas layout storage is available again.");
            break;
        case TopologyWorkspaceDiagnosticKind::LegacyImportSkipped:
            message = QStringLiteral(
                "An old canvas layout could not be imported. A new layout will "
                "be used without changing the old data.");
            break;
        case TopologyWorkspaceDiagnosticKind::RepairSucceeded:
            message = QStringLiteral(
                "Canvas layout was regularized and its workspace storage was "
                "repaired.");
            break;
        }
        const bool workspaceRecovered =
            diagnostic.kind == TopologyWorkspaceDiagnosticKind::SaveRecovered
            || diagnostic.kind
                   == TopologyWorkspaceDiagnosticKind::RepairSucceeded;
        if (workspaceRecovered) {
            m_workspaceStatusMessage.clear();
        } else {
            m_workspaceStatusMessage = message;
        }
        QString activity = message;
        if (!diagnostic.details.isEmpty()) {
            activity += QStringLiteral(" Details: %1").arg(diagnostic.details);
        }
        appendActivity(activity);
        if (workspaceRecovered) {
            statusBar()->showMessage(message, 5000);
        } else {
            showWorkspaceStatusMessage();
        }
    };
}

void FinepaperMainWindow::createPackageDock() {
    m_packageDock = new QDockWidget(QStringLiteral("NoC IP && Endpoint Library"), this);
    m_packageDock->setObjectName(workbench::packageDockName);
    m_packageDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 10, 10, 10);

    auto* currentDesignGroup = new QGroupBox(QStringLiteral("Current Design"));
    auto* currentDesignLayout = new QVBoxLayout(currentDesignGroup);
    m_activePackageLabel = new QLabel(QStringLiteral("No design is open."));
    m_activePackageLabel->setObjectName(QStringLiteral("finepaper.activePackage"));
    m_activePackageLabel->setWordWrap(true);
    m_activePackageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentDesignLayout->addWidget(m_activePackageLabel);
    m_createDesignButton = new QPushButton(QStringLiteral("New NoC Design…"));
    m_createDesignButton->setObjectName(QStringLiteral("finepaper.createDesign"));
    currentDesignLayout->addWidget(m_createDesignButton);
    layout->addWidget(currentDesignGroup);

    auto* packageGroup = new QGroupBox(QStringLiteral("NoC IP Packages"));
    auto* packageLayout = new QVBoxLayout(packageGroup);
    m_availablePackagesLabel = new QLabel;
    m_availablePackagesLabel->setObjectName(
        QStringLiteral("finepaper.availablePackages"));
    m_availablePackagesLabel->setWordWrap(true);
    m_availablePackagesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    packageLayout->addWidget(m_availablePackagesLabel);
    auto* packageButtons = new QHBoxLayout;
    m_installPackageButton = new QPushButton(QStringLiteral("Install…"));
    m_reloadPackagesButton = new QPushButton(QStringLiteral("Reload"));
    packageButtons->addWidget(m_installPackageButton);
    packageButtons->addWidget(m_reloadPackagesButton);
    packageLayout->addLayout(packageButtons);
    layout->addWidget(packageGroup);

    auto* paletteHeading = new QLabel(QStringLiteral("Endpoint Types"));
    QFont paletteFont = paletteHeading->font();
    paletteFont.setBold(true);
    paletteHeading->setFont(paletteFont);
    layout->addWidget(paletteHeading);
    layout->addWidget(new QLabel(
        QStringLiteral("Drag onto the canvas. Drop on a Router to attach immediately, "
                       "or connect the new Endpoint afterward.")));
    m_endpointPalette = new EndpointPaletteList;
    m_endpointPalette->setObjectName(QStringLiteral("finepaper.endpointPalette"));
    m_endpointPalette->setDragEnabled(true);
    m_endpointPalette->setDragDropMode(QAbstractItemView::DragOnly);
    m_endpointPalette->setDefaultDropAction(Qt::CopyAction);
    m_endpointPalette->setSelectionMode(QAbstractItemView::SingleSelection);
    m_endpointPalette->setAlternatingRowColors(true);
    layout->addWidget(m_endpointPalette, 1);

    m_packageDock->setWidget(content);
    addDockWidget(Qt::LeftDockWidgetArea, m_packageDock);

    connect(m_installPackageButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::installPackage);
    connect(m_reloadPackagesButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::reloadPackages);
    connect(m_createDesignButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::createDesign);
    connect(m_endpointPalette, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (!item) {
                    return;
                }
                if (!m_selectedRouter) {
                    statusBar()->showMessage(
                        QStringLiteral("Select a Router before double-clicking an Endpoint type."),
                        5000);
                    return;
                }
                addEndpoint(
                    item->data(Qt::UserRole).toString(),
                    NocAttachmentTarget{*m_selectedRouter, std::nullopt});
            });
}

void FinepaperMainWindow::createInspectorDock() {
    m_inspectorDock = new QDockWidget(QStringLiteral("Inspector"), this);
    m_inspectorDock->setObjectName(workbench::inspectorDockName);
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* inspectorScroll = new QScrollArea(m_inspectorDock);
    inspectorScroll->setObjectName(
        QStringLiteral("finepaper.inspectorScroll"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(inspectorScroll);
    content->setObjectName(QStringLiteral("finepaper.inspectorContent"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 10, 10, 10);
    m_designOverview = new QLabel(QStringLiteral("No design is open."));
    m_designOverview->setObjectName(
        QStringLiteral("finepaper.designOverview"));
    m_designOverview->setWordWrap(true);
    m_designOverview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_designOverview);

    auto* topologyGroup = new QGroupBox(QStringLiteral("Mesh Topology"));
    topologyGroup->setObjectName(QStringLiteral("finepaper.meshTopologyGroup"));
    auto* topologyLayout = new QVBoxLayout(topologyGroup);
    auto* topologyNote = new QLabel(
        QStringLiteral(
            "Routers and Router links are generated by the Mesh. Resize the "
            "topology here; Router creation and deletion are not exposed as "
            "independent operations."),
        topologyGroup);
    topologyNote->setWordWrap(true);
    m_resizeMeshButton = new QPushButton(
        QStringLiteral("Resize Mesh…"), topologyGroup);
    m_resizeMeshButton->setObjectName(QStringLiteral("finepaper.resizeMesh"));
    topologyLayout->addWidget(topologyNote);
    topologyLayout->addWidget(m_resizeMeshButton);
    layout->addWidget(topologyGroup);

    auto* selectionGroup = new QGroupBox(QStringLiteral("Selection"));
    selectionGroup->setObjectName(workbench::selectionInspectorName);
    auto* selectionLayout = new QVBoxLayout(selectionGroup);
    m_selectionSummary = new QLabel(QStringLiteral("Nothing selected."));
    m_selectionSummary->setWordWrap(true);
    m_selectionSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    selectionLayout->addWidget(m_selectionSummary);
    layout->addWidget(selectionGroup);

    m_endpointConfigurationGroup = new QGroupBox(
        QStringLiteral("Endpoint Configuration"), content);
    m_endpointConfigurationGroup->setObjectName(
        QStringLiteral("finepaper.endpointConfigurationGroup"));
    auto* endpointConfigurationLayout =
        new QVBoxLayout(m_endpointConfigurationGroup);
    m_endpointConfigurationPanel = new EndpointConfigurationPanel(
        m_endpointConfigurationGroup);
    endpointConfigurationLayout->addWidget(m_endpointConfigurationPanel);
    m_endpointConfigurationGroup->setVisible(false);
    layout->addWidget(m_endpointConfigurationGroup);

    m_elementConfigurationGroup = new QGroupBox(
        QStringLiteral("Element Configuration"), content);
    m_elementConfigurationGroup->setObjectName(
        QStringLiteral("finepaper.elementConfigurationGroup"));
    auto* elementConfigurationLayout =
        new QVBoxLayout(m_elementConfigurationGroup);
    m_elementConfigurationPanel = new ElementConfigurationPanel(
        m_elementConfigurationGroup);
    elementConfigurationLayout->addWidget(m_elementConfigurationPanel);
    m_elementConfigurationGroup->setVisible(false);
    layout->addWidget(m_elementConfigurationGroup);

    m_parameterGroup = new QGroupBox(QStringLiteral("NoC Parameters"));
    m_parameterGroup->setObjectName(QStringLiteral("finepaper.parameterGroup"));
    auto* parameterGroupLayout = new QVBoxLayout(m_parameterGroup);
    m_parameterForm = new PackageParameterForm(
        QStringLiteral("finepaper.parameter"), m_parameterGroup);
    auto* parameterScroll = new QScrollArea;
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setWidget(m_parameterForm);
    m_applyParametersButton = new QPushButton(QStringLiteral("Apply Parameters"));
    m_applyParametersButton->setObjectName(QStringLiteral("finepaper.applyParameters"));
    parameterGroupLayout->addWidget(parameterScroll, 1);
    parameterGroupLayout->addWidget(m_applyParametersButton);
    layout->addWidget(m_parameterGroup, 1);

    inspectorScroll->setWidget(content);
    m_inspectorDock->setWidget(inspectorScroll);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    connect(m_applyParametersButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::applyParameters);
    m_parameterForm->valueChanged = [this] { updateUiState(); };
    connect(m_resizeMeshButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::resizeMesh);
    m_elementConfigurationPanel->applyRequested = [this](
        ElementRef element,
        QString propertySet,
        QJsonObject effectiveValues) {
        if (m_operationBusy || !m_design || !packageForDesign()) {
            return;
        }
        if (!confirmDiscardPendingDomainChanges(
                QStringLiteral("Applying element configuration"))) {
            return;
        }
        const DesignResult result = m_application.setElementConfiguration(
            *m_design,
            std::move(element),
            propertySet,
            effectiveValues);
        if (result.success) {
            discardPendingDomainChanges();
        }
        adoptDesignResult(
            result,
            QStringLiteral("Apply Element Configuration"),
            DesignRefreshScope::InspectorOnly);
    };
    m_elementConfigurationPanel->resetRequested = [this](
        ElementRef element,
        QString propertySet) {
        if (m_operationBusy || !m_design || !packageForDesign()) {
            return;
        }
        if (!confirmDiscardPendingDomainChanges(
                QStringLiteral("Resetting element configuration"))) {
            return;
        }
        const DesignResult result = m_application.clearElementConfiguration(
            *m_design, std::move(element), propertySet);
        if (result.success) {
            discardPendingDomainChanges();
        }
        adoptDesignResult(
            result,
            QStringLiteral("Reset Element Configuration"),
            DesignRefreshScope::InspectorOnly);
    };
    m_endpointConfigurationPanel->planTypeChangeRequested = [this](
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migration,
        QJsonObject parameterPatch) {
        if (!m_design) {
            return EndpointTypeChangePlan{};
        }
        return m_application.planEndpointTypeChange(
            *m_design,
            endpointId,
            targetType,
            migration,
            parameterPatch);
    };
    m_endpointConfigurationPanel->draftStateChanged = [this] {
        updateUiState();
    };
    m_endpointConfigurationPanel->updateParametersRequested = [this](
        QString endpointId,
        QJsonObject parameters) {
        if (m_operationBusy || !m_design || !packageForDesign()) {
            return;
        }
        if (!confirmDiscardPendingDomainChanges(
                QStringLiteral("Applying Endpoint parameters"))) {
            return;
        }
        const DesignResult result = m_application.updateEndpointParameters(
            *m_design, endpointId, parameters);
        if (result.success) {
            discardPendingDomainChanges();
            m_endpointConfigurationPanel->discardDraft(
                m_designSessionIdentity, endpointId);
        }
        adoptDesignResult(
            result,
            QStringLiteral("Apply Endpoint Parameters %1").arg(endpointId),
            DesignRefreshScope::InspectorOnly);
    };
    m_endpointConfigurationPanel->changeTypeRequested = [this](
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migration,
        QJsonObject parameterPatch,
        EndpointTypeChangeImpactConfirmation confirmation) {
        if (m_operationBusy || !m_design || !packageForDesign()) {
            return;
        }
        if (!confirmDiscardPendingDomainChanges(
                QStringLiteral("Changing an Endpoint type"))) {
            return;
        }
        const DesignResult result = m_application.changeEndpointType(
            *m_design,
            endpointId,
            targetType,
            migration,
            parameterPatch,
            confirmation);
        if (result.success) {
            discardPendingDomainChanges();
            m_endpointConfigurationPanel->discardDraft(
                m_designSessionIdentity, endpointId);
        }
        adoptDesignResult(
            result,
            QStringLiteral("Change Endpoint Type %1").arg(endpointId));
        if (result.success && m_nodeEditor) {
            m_nodeEditor->selectElements({
                ElementRef{ElementKind::Endpoint, endpointId}
            });
        }
    };
}

void FinepaperMainWindow::createDomainDock() {
    m_domainDock = new QDockWidget(QStringLiteral("Domain Manager"), this);
    m_domainDock->setObjectName(workbench::domainManagerDockName);
    m_domainDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_domainManager = new DomainManagerPanel(m_domainDock);
    m_domainDock->setWidget(m_domainManager);
    addDockWidget(Qt::RightDockWidgetArea, m_domainDock);
    tabifyDockWidget(m_inspectorDock, m_domainDock);
    m_inspectorDock->raise();

    m_domainManager->validateAddDomain = [this](
        const DomainDefinition& domain) {
        return m_design
            ? m_application.addDomain(*m_design, domain).diagnostics
            : QVector<Diagnostic>{};
    };
    m_domainManager->validateUpdateDomain = [this](
        const QString& domainId,
        const DomainDefinition& domain) {
        return m_design
            ? m_application.updateDomain(*m_design, domainId, domain).diagnostics
            : QVector<Diagnostic>{};
    };
    m_domainManager->addDomainRequested = [this](DomainDefinition domain) {
        if (!m_design) {
            return;
        }
        if (!confirmDiscardPendingDomainWorkspace(
                QStringLiteral("Adding a Domain from the quick Manager"))) {
            return;
        }
        const DesignResult result = m_application.addDomain(
            *m_design, std::move(domain));
        if (result.success) {
            discardPendingDomainWorkspace();
        }
        adoptDomainResult(result, QStringLiteral("Add Domain"));
    };
    m_domainManager->updateDomainRequested = [this](
        QString domainId,
        DomainDefinition domain) {
        if (!m_design) {
            return;
        }
        if (!confirmDiscardPendingDomainWorkspace(
                QStringLiteral("Updating a Domain from the quick Manager"))) {
            return;
        }
        const DesignResult result = m_application.updateDomain(
            *m_design, domainId, std::move(domain));
        if (result.success) {
            discardPendingDomainWorkspace();
        }
        adoptDomainResult(
            result, QStringLiteral("Update Domain %1").arg(domainId));
    };
    m_domainManager->removeDomainRequested = [this](QString domainId) {
        if (!m_design) {
            return;
        }
        if (!confirmDiscardPendingDomainWorkspace(
                QStringLiteral("Deleting a Domain from the quick Manager"))) {
            return;
        }
        const DesignResult result = m_application.removeDomain(
            *m_design, domainId);
        if (result.success) {
            discardPendingDomainWorkspace();
        }
        adoptDomainResult(
            result, QStringLiteral("Delete Domain %1").arg(domainId));
    };
    m_domainManager->assignmentPatchRequested = [this](
        QVector<ElementRef> elements,
        QString domainType,
        DomainAssignmentPatch patch) {
        if (!m_design) {
            return;
        }
        if (!confirmDiscardPendingDomainWorkspace(
                QStringLiteral("Applying a quick Domain assignment"))) {
            return;
        }
        const DesignResult result = m_application.patchDomainAssignments(
            *m_design,
            elements,
            domainType,
            std::move(patch));
        if (result.success) {
            discardPendingDomainWorkspace();
        }
        adoptDomainResult(
            result,
            QStringLiteral("Update %1 assignments").arg(domainType));
    };
    m_domainManager->completeConfigurationRequested = [this] {
        const PackageDefinition* package = packageForDesign();
        if (!m_design || !package
            || !formatVersionSupportsDomains(m_design->formatVersion)
            || !formatVersionSupportsDomains(package->formatVersion)) {
            return;
        }
        selectCenterView(workbench::domainConfigurationViewId);
    };
    m_domainManager->showDomainLayerRequested = [this](
        const QString& domainType) {
        if (!m_domainLayerSelector) {
            return;
        }
        const int index = m_domainLayerSelector->findData(domainType);
        if (index >= 0) {
            m_domainLayerSelector->setCurrentIndex(index);
        }
    };
    m_domainManager->selectElementsRequested = [this](
        QVector<ElementRef> elements) {
        selectCenterView(workbench::editorViewId);
        if (m_nodeEditor) {
            m_nodeEditor->selectElements(elements);
        }
    };
}

void FinepaperMainWindow::createResultsDock() {
    m_resultsDock = new QDockWidget(QStringLiteral("Diagnostics & Output"), this);
    m_resultsDock->setObjectName(workbench::resultsDockName);
    m_resultsDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_resultTabs = new QTabWidget;
    m_resultTabs->setDocumentMode(true);

    m_drcTable = new QTableWidget;
    m_drcTable->setObjectName(QStringLiteral("finepaper.drcTable"));
    m_drcTable->setColumnCount(4);
    m_drcTable->setHorizontalHeaderLabels({
        QStringLiteral("Severity"), QStringLiteral("Code"),
        QStringLiteral("Message"), QStringLiteral("Location")});
    m_drcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_drcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_drcTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTabs->addTab(m_drcTable, workbench::drcTabTitle);

    m_activityLog = new QPlainTextEdit;
    m_activityLog->setObjectName(QStringLiteral("finepaper.activityLog"));
    m_activityLog->setReadOnly(true);
    m_activityLog->setMaximumBlockCount(5000);
    m_resultTabs->addTab(m_activityLog, workbench::activityTabTitle);

    auto* outputPage = new QWidget;
    auto* outputLayout = new QVBoxLayout(outputPage);
    outputLayout->setContentsMargins(8, 8, 8, 8);
    auto* outputControls = new QHBoxLayout;
    m_outputRoot = new QLineEdit(m_locations.defaultOutputRoot);
    m_outputRoot->setObjectName(QStringLiteral("finepaper.outputRoot"));
    m_browseOutputButton = new QPushButton(QStringLiteral("Browse…"));
    m_generateButton = new QPushButton(QStringLiteral("Generate RTL"));
    m_generateButton->setObjectName(QStringLiteral("finepaper.generateButton"));
    outputControls->addWidget(new QLabel(QStringLiteral("Output root")));
    outputControls->addWidget(m_outputRoot, 1);
    outputControls->addWidget(m_browseOutputButton);
    outputControls->addWidget(m_generateButton);
    outputLayout->addLayout(outputControls);

    auto* outputSplitter = new QSplitter(Qt::Vertical);
    m_artifactTable = new QTableWidget;
    m_artifactTable->setObjectName(QStringLiteral("finepaper.artifactTable"));
    m_artifactTable->setColumnCount(4);
    m_artifactTable->setHorizontalHeaderLabels({
        QStringLiteral("Artifact"), QStringLiteral("Type"),
        QStringLiteral("Path"), QStringLiteral("Primary")});
    m_artifactTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_artifactTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_artifactTable->horizontalHeader()->setStretchLastSection(true);
    m_generationDetails = new QPlainTextEdit;
    m_generationDetails->setReadOnly(true);
    outputSplitter->addWidget(m_artifactTable);
    outputSplitter->addWidget(m_generationDetails);
    outputSplitter->setStretchFactor(0, 2);
    outputSplitter->setStretchFactor(1, 1);
    outputLayout->addWidget(outputSplitter, 1);
    m_resultTabs->addTab(outputPage, workbench::generationTabTitle);

    m_resultsDock->setWidget(m_resultTabs);
    addDockWidget(Qt::BottomDockWidgetArea, m_resultsDock);

    connect(m_browseOutputButton, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Select output root"), m_outputRoot->text());
        if (!directory.isEmpty()) {
            m_outputRoot->setText(directory);
        }
    });
    connect(m_generateButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::generateDesign);
}

void FinepaperMainWindow::createActions() {
    m_newAction = new QAction(
        style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("New NoC Design…"), this);
    m_newAction->setShortcut(QKeySequence::New);
    m_openAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("Open…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_saveAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAsAction = new QAction(QStringLiteral("Save As…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_installAction = new QAction(QStringLiteral("Install Package Directory…"), this);
    m_reloadAction = new QAction(QStringLiteral("Reload Packages"), this);
    m_validateAction = new QAction(QStringLiteral("Validate / DRC"), this);
    m_validateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    m_generateAction = new QAction(QStringLiteral("Generate RTL"), this);
    m_generateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_resizeMeshAction = new QAction(QStringLiteral("Resize Mesh…"), this);
    m_resizeMeshAction->setObjectName(
        QStringLiteral("finepaper.resizeMeshAction"));
    m_resizeMeshAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    m_regularizeAction = new QAction(
        style()->standardIcon(QStyle::SP_BrowserReload),
        QStringLiteral("Regularize Layout"), this);
    m_regularizeAction->setObjectName(workbench::regularizeActionName);
    m_regularizeAction->setShortcut(QKeySequence(QStringLiteral("R")));
    m_regularizeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_regularizeAction->setStatusTip(
        QStringLiteral("Restore Router and Endpoint positions to the topology layout"));
    m_fitAction = new QAction(QStringLiteral("Fit NoC in View"), this);
    m_fitAction->setShortcut(QKeySequence(QStringLiteral("F")));
    m_fitAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_selectCanvasAction = new QAction(QStringLiteral("Select"), this);
    m_selectCanvasAction->setObjectName(workbench::selectCanvasActionName);
    m_selectCanvasAction->setCheckable(true);
    m_selectCanvasAction->setShortcut(QKeySequence(QStringLiteral("V")));
    m_selectCanvasAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_selectCanvasAction->setStatusTip(
        QStringLiteral(
            "Select nodes and connections; drag empty canvas to box-select"));
    m_panCanvasAction = new QAction(QStringLiteral("Pan"), this);
    m_panCanvasAction->setObjectName(workbench::panCanvasActionName);
    m_panCanvasAction->setCheckable(true);
    m_panCanvasAction->setChecked(true);
    m_panCanvasAction->setShortcut(QKeySequence(QStringLiteral("H")));
    m_panCanvasAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_panCanvasAction->setStatusTip(
        QStringLiteral(
            "Drag the canvas to pan; hold Shift and drag to box-select"));
    auto* canvasModeGroup = new QActionGroup(this);
    canvasModeGroup->setExclusive(true);
    canvasModeGroup->addAction(m_selectCanvasAction);
    canvasModeGroup->addAction(m_panCanvasAction);
    m_nodeEditor->addAction(m_regularizeAction);
    m_nodeEditor->addAction(m_fitAction);
    m_nodeEditor->addAction(m_selectCanvasAction);
    m_nodeEditor->addAction(m_panCanvasAction);

    connect(m_newAction, &QAction::triggered,
            this, &FinepaperMainWindow::createDesign);
    connect(m_openAction, &QAction::triggered,
            this, &FinepaperMainWindow::openDesign);
    connect(m_saveAction, &QAction::triggered, this, [this] { saveDesign(); });
    connect(m_saveAsAction, &QAction::triggered, this, [this] { saveDesignAs(); });
    connect(m_installAction, &QAction::triggered,
            this, &FinepaperMainWindow::installPackage);
    connect(m_reloadAction, &QAction::triggered,
            this, &FinepaperMainWindow::reloadPackages);
    connect(m_validateAction, &QAction::triggered,
            this, &FinepaperMainWindow::validateDesign);
    connect(m_generateAction, &QAction::triggered,
            this, &FinepaperMainWindow::generateDesign);
    connect(m_resizeMeshAction, &QAction::triggered,
            this, &FinepaperMainWindow::resizeMesh);
    connect(m_regularizeAction, &QAction::triggered,
            m_nodeEditor, &NocNodeEditor::regularizeLayout);
    connect(m_fitAction, &QAction::triggered, m_nodeEditor, &NocNodeEditor::zoomToFit);
    connect(m_selectCanvasAction, &QAction::triggered, this, [this] {
        m_nodeEditor->setCanvasInteractionMode(NocCanvasInteractionMode::Select);
        statusBar()->showMessage(
            QStringLiteral("Select mode: drag empty canvas to box-select; Ctrl adds or toggles."),
            5000);
    });
    connect(m_panCanvasAction, &QAction::triggered, this, [this] {
        m_nodeEditor->setCanvasInteractionMode(NocCanvasInteractionMode::Pan);
        statusBar()->showMessage(
            QStringLiteral(
                "Pan mode: drag to move the view; hold Shift and drag to box-select."),
            5000);
    });

    QAction* packagePanelAction = m_packageDock->toggleViewAction();
    packagePanelAction->setObjectName(workbench::packageToggleActionName);
    packagePanelAction->setText(QStringLiteral("NoC IP && Endpoint Library"));
    packagePanelAction->setIcon(QIcon::fromTheme(
        QStringLiteral("folder-symbolic"), style()->standardIcon(QStyle::SP_DirIcon)));
    packagePanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    packagePanelAction->setStatusTip(QStringLiteral("Show or hide the NoC IP and Endpoint panel"));

    QAction* inspectorPanelAction = m_inspectorDock->toggleViewAction();
    inspectorPanelAction->setObjectName(workbench::inspectorToggleActionName);
    inspectorPanelAction->setText(QStringLiteral("Inspector"));
    inspectorPanelAction->setIcon(QIcon::fromTheme(
        QStringLiteral("view-list-details-symbolic"),
        style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    inspectorPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    inspectorPanelAction->setStatusTip(QStringLiteral("Show or hide the right Inspector panel"));

    QAction* domainManagerPanelAction = m_domainDock->toggleViewAction();
    domainManagerPanelAction->setObjectName(
        workbench::domainManagerToggleActionName);
    domainManagerPanelAction->setText(QStringLiteral("Domain Manager"));
    domainManagerPanelAction->setIcon(QIcon::fromTheme(
        QStringLiteral("preferences-system-symbolic"),
        style()->standardIcon(QStyle::SP_DriveNetIcon)));
    domainManagerPanelAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    domainManagerPanelAction->setStatusTip(
        QStringLiteral("Show or hide the Package-driven Domain Manager"));

    QAction* resultsPanelAction = m_resultsDock->toggleViewAction();
    resultsPanelAction->setObjectName(workbench::resultsToggleActionName);
    resultsPanelAction->setText(QStringLiteral("Diagnostics && Output"));
    resultsPanelAction->setIcon(QIcon::fromTheme(
        QStringLiteral("dialog-warning-symbolic"),
        style()->standardIcon(QStyle::SP_MessageBoxWarning)));
    resultsPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    resultsPanelAction->setStatusTip(QStringLiteral("Show or hide the bottom diagnostics panel"));

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    QMenu* packageMenu = menuBar()->addMenu(QStringLiteral("&Package"));
    packageMenu->addAction(m_installAction);
    packageMenu->addAction(m_reloadAction);
    QMenu* designMenu = menuBar()->addMenu(QStringLiteral("&Design"));
    designMenu->addAction(m_resizeMeshAction);
    QMenu* runMenu = menuBar()->addMenu(QStringLiteral("&Run"));
    runMenu->addAction(m_validateAction);
    runMenu->addAction(m_generateAction);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* centerViews = viewMenu->addMenu(QStringLiteral("Center View"));
    auto* viewGroup = new QActionGroup(this);
    viewGroup->setExclusive(true);
    for (const WorkbenchViewDefinition& view : m_viewRegistry->views()) {
        QAction* action = centerViews->addAction(view.title);
        action->setCheckable(true);
        action->setData(view.id);
        action->setChecked(view.id == workbench::editorViewId);
        viewGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, action] {
            selectCenterView(action->data().toString());
        });
    }
    viewMenu->addSeparator();
    auto* panelsMenu = viewMenu->addMenu(QStringLiteral("Panels"));
    panelsMenu->addAction(packagePanelAction);
    panelsMenu->addAction(inspectorPanelAction);
    panelsMenu->addAction(domainManagerPanelAction);
    panelsMenu->addAction(resultsPanelAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_selectCanvasAction);
    viewMenu->addAction(m_panCanvasAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_regularizeAction);
    viewMenu->addAction(m_fitAction);
    connect(m_centerViews, &QTabWidget::currentChanged, this, [this, viewGroup](int index) {
        Q_UNUSED(index);
        const QString id = m_viewRegistry->currentViewId();
        for (QAction* action : viewGroup->actions()) {
            action->setChecked(action->data().toString() == id);
        }
    });

    QToolBar* toolbar = addToolBar(QStringLiteral("NoC Workbench"));
    toolbar->setObjectName(QStringLiteral("finepaper.mainToolbar"));
    toolbar->setMovable(true);
    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(m_validateAction);
    toolbar->addAction(m_generateAction);
    toolbar->addSeparator();
    toolbar->addAction(m_regularizeAction);
    toolbar->addSeparator();
    toolbar->addAction(m_selectCanvasAction);
    toolbar->addAction(m_panCanvasAction);
    if (auto* button = qobject_cast<QToolButton*>(
            toolbar->widgetForAction(m_selectCanvasAction))) {
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    if (auto* button = qobject_cast<QToolButton*>(
            toolbar->widgetForAction(m_panCanvasAction))) {
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("Color by"), toolbar));
    m_domainLayerSelector = new QComboBox(toolbar);
    m_domainLayerSelector->setObjectName(workbench::domainLayerSelectorName);
    m_domainLayerSelector->setMinimumContentsLength(16);
    m_domainLayerSelector->addItem(QStringLiteral("None"), QString());
    m_domainLayerSelector->setEnabled(false);
    toolbar->addWidget(m_domainLayerSelector);
    connect(m_domainLayerSelector, &QComboBox::currentIndexChanged, this, [this] {
        const QString domainType = m_domainLayerSelector->currentData().toString();
        if (m_design) {
            const QString workspaceKey = workbench::designWorkspaceKey(
                m_design->package.id, m_design->package.version, m_design->id);
            QSettings settings;
            QVariantMap selections = settings.value(
                workbench::domainLayerSelectionsSetting).toMap();
            selections.insert(workspaceKey, domainType);
            settings.setValue(workbench::domainLayerSelectionsSetting, selections);
        }
        applyDomainLayer(domainType);
        updateInspector(m_editorSelection);
    });

    QToolBar* activityBar = new QToolBar(QStringLiteral("Workbench Panels"), this);
    activityBar->setObjectName(workbench::activityBarName);
    activityBar->setMovable(false);
    activityBar->setFloatable(false);
    activityBar->setOrientation(Qt::Vertical);
    activityBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    activityBar->setIconSize(QSize(24, 24));
    activityBar->addAction(packagePanelAction);
    activityBar->addAction(inspectorPanelAction);
    activityBar->addAction(domainManagerPanelAction);
    activityBar->addSeparator();
    activityBar->addAction(resultsPanelAction);
    addToolBar(Qt::LeftToolBarArea, activityBar);
}

void FinepaperMainWindow::restoreWorkbenchState() {
    QSettings settings;
    const QByteArray geometry = settings.value(workbench::geometrySetting).toByteArray();
    const QByteArray state = settings.value(workbench::windowStateSetting).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    if (!state.isEmpty()) {
        restoreState(state);
    }
    selectCenterView(settings.value(workbench::centerViewSetting,
                                    workbench::editorViewId).toString());
    m_resultTabs->setCurrentIndex(
        qBound(0, settings.value(workbench::resultTabSetting, 0).toInt(),
               m_resultTabs->count() - 1));
}

void FinepaperMainWindow::closeEvent(QCloseEvent* event) {
    if (m_operationBusy) {
        QMessageBox::information(
            this,
            QStringLiteral("Operation in progress"),
            QStringLiteral("Wait for the current validation or generation operation to finish."));
        event->ignore();
        return;
    }
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();
    QSettings settings;
    settings.setValue(workbench::geometrySetting, saveGeometry());
    settings.setValue(workbench::windowStateSetting, saveState());
    settings.setValue(workbench::centerViewSetting,
                      m_viewRegistry->currentViewId());
    settings.setValue(workbench::resultTabSetting, m_resultTabs->currentIndex());
    QMainWindow::closeEvent(event);
}

void FinepaperMainWindow::loadInstalledPackageRoots() {
    QSettings settings;
    appendPackageRoots(
        m_locations,
        settings.value(workbench::packageRootsSetting).toStringList());
}

void FinepaperMainWindow::reloadPackages() {
    if (m_operationBusy) {
        return;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Reloading Packages"))) {
        return;
    }
    if (!confirmDiscardPendingEndpointDrafts(
            QStringLiteral("Reloading Packages"))) {
        return;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();
    const QVector<Diagnostic> diagnostics = m_application.reloadPackages(m_locations.packageRoots);
    if (m_design) {
        refreshDesignViews();
    } else {
        updatePackageControls();
    }
    showDiagnostics(diagnostics, QStringLiteral("Package discovery"), false);
    if (hasErrors(diagnostics)) {
        m_resultTabs->setCurrentIndex(0);
        m_resultsDock->show();
        QString summary = QStringLiteral("Package reload failed.");
        for (const Diagnostic& diagnostic : diagnostics) {
            if (diagnostic.severity == QStringLiteral("error")) {
                summary = QStringLiteral("Package reload failed: %1").arg(diagnostic.message);
                break;
            }
        }
        appendActivity(summary);
        statusBar()->showMessage(summary);
    } else if (m_design && !packageForDesign()) {
        const qsizetype packageCount = runtimePackages().size();
        appendActivity(
            QStringLiteral("Reloaded %1 NoC IP %2; the current design Package is missing.")
                .arg(packageCount)
                .arg(packageCount == 1
                         ? QStringLiteral("Package")
                         : QStringLiteral("Packages")));
        statusBar()->showMessage(
            QStringLiteral("Read-only design: Package %1@%2 is not loaded.")
                .arg(m_design->package.id, m_design->package.version));
    } else {
        const qsizetype packageCount = runtimePackages().size();
        const QString packageNoun = packageCount == 1
            ? QStringLiteral("Package")
            : QStringLiteral("Packages");
        const QString summary = diagnostics.isEmpty()
            ? QStringLiteral("Loaded %1 NoC IP %2.")
                  .arg(packageCount)
                  .arg(packageNoun)
            : QStringLiteral("Loaded %1 NoC IP %2 with %3 %4.")
                  .arg(packageCount)
                  .arg(packageNoun)
                  .arg(diagnostics.size())
                  .arg(diagnostics.size() == 1
                           ? QStringLiteral("warning")
                           : QStringLiteral("warnings"));
        appendActivity(summary);
        statusBar()->showMessage(summary);
    }
    updateUiState();
}

void FinepaperMainWindow::installPackage() {
    if (m_operationBusy) {
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select a NoC Package directory"),
        QDir::currentPath(),
        QFileDialog::ShowDirsOnly);
    if (directory.isEmpty()) {
        return;
    }

    installPackageDirectory(directory);
}

bool FinepaperMainWindow::installPackageDirectory(const QString& directory) {
    if (m_operationBusy || directory.trimmed().isEmpty()) {
        return false;
    }
    const PackageLoadResult package = loadPackage(directory);
    if (!package.success || !package.package) {
        showDiagnostics(package.diagnostics, QStringLiteral("Install Package"));
        return false;
    }

    // Recheck the retained catalog snapshot before deciding whether this is a
    // duplicate. A failed reload may have kept metadata for a Package whose
    // directory is no longer usable.
    updatePackageControls();
    if (const PackageDefinition* existing = runtimePackageByKey(package.package->key())) {
        QMessageBox::information(
            this,
            QStringLiteral("NoC IP already available"),
            QStringLiteral("%1 is already loaded from:\n%2")
                .arg(existing->key(), existing->rootPath));
        statusBar()->showMessage(
            QStringLiteral("NoC IP %1 is already available.").arg(existing->key()));
        return false;
    }

    const PackageDefinition* retainedPackage = packageByKey(package.package->key());
    const QString retainedRoot = retainedPackage
        ? normalizedAbsolutePath(retainedPackage->rootPath)
        : QString();

    RuntimeLocations candidateLocations = m_locations;
    if (!retainedRoot.isEmpty()) {
        QStringList retainedLocations;
        for (const QString& root : candidateLocations.packageRoots) {
            if (normalizedAbsolutePath(root) != retainedRoot) {
                retainedLocations.append(root);
            }
        }
        candidateLocations.packageRoots = std::move(retainedLocations);
    }
    appendPackageRoots(candidateLocations, QStringList{package.package->rootPath});

    FinepaperApplication candidateApplication = m_application;
    const QVector<Diagnostic> diagnostics = candidateApplication.reloadPackages(
        candidateLocations.packageRoots);
    if (hasErrors(diagnostics)) {
        showDiagnostics(diagnostics, QStringLiteral("Install Package"));
        appendActivity(
            QStringLiteral("Failed to install NoC IP Package from %1.")
                .arg(package.package->rootPath));
        statusBar()->showMessage(QStringLiteral("NoC IP Package installation failed."));
        return false;
    }

    const auto installedIt = std::find_if(
        candidateApplication.packages().cbegin(),
        candidateApplication.packages().cend(),
        [&](const PackageDefinition& candidatePackage) {
            return candidatePackage.key() == package.package->key();
        });
    const PackageDefinition* installedPackage =
        installedIt == candidateApplication.packages().cend() ? nullptr : &*installedIt;
    if (!installedPackage || installedPackage->rootPath != package.package->rootPath) {
        const QString loadedFrom = installedPackage
            ? installedPackage->rootPath
            : QStringLiteral("an existing Package root");
        QMessageBox::information(
            this,
            QStringLiteral("NoC IP already available"),
            QStringLiteral("%1 is already provided by:\n%2\n\n"
                           "The selected directory was not added.")
                .arg(package.package->key(), loadedFrom));
        return false;
    }

    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Installing a Package"))) {
        return false;
    }
    if (!confirmDiscardPendingEndpointDrafts(
            QStringLiteral("Installing a Package"))) {
        return false;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();

    m_application = std::move(candidateApplication);
    m_locations = std::move(candidateLocations);

    QSettings settings;
    const QString selectedRoot = normalizedAbsolutePath(package.package->rootPath);
    const QStringList installed =
        settings.value(workbench::packageRootsSetting).toStringList();
    QStringList updatedInstalled;
    QSet<QString> seenRoots;
    for (const QString& installedRoot : installed) {
        const QString normalizedRoot = normalizedAbsolutePath(installedRoot);
        if ((!retainedRoot.isEmpty() && normalizedRoot == retainedRoot)
            || seenRoots.contains(normalizedRoot)) {
            continue;
        }
        seenRoots.insert(normalizedRoot);
        updatedInstalled.append(normalizedRoot);
    }
    if (!seenRoots.contains(selectedRoot)) {
        updatedInstalled.append(selectedRoot);
    }
    settings.setValue(workbench::packageRootsSetting, updatedInstalled);

    if (m_design) {
        refreshDesignViews();
    } else {
        updatePackageControls();
    }
    showDiagnostics(diagnostics, QStringLiteral("Install Package"));

    appendActivity(QStringLiteral("Installed runtime Package %1 from %2.")
                       .arg(package.package->key(), package.package->rootPath));
    statusBar()->showMessage(
        QStringLiteral("Installed NoC IP %1.").arg(package.package->key()));
    return true;
}

void FinepaperMainWindow::updatePackageControls() {
    m_runtimeAvailablePackageKeys.clear();
    QStringList availablePackages;
    for (const PackageDefinition& package : m_application.packages()) {
        const PackageLoadResult loaded = loadPackage(package.rootPath);
        if (!loaded.success || !loaded.package
            || loaded.package->key() != package.key()
            || normalizedAbsolutePath(loaded.package->rootPath)
                != normalizedAbsolutePath(package.rootPath)) {
            continue;
        }
        m_runtimeAvailablePackageKeys.insert(package.key());
        availablePackages.append(
            QStringLiteral("%1 — %2").arg(package.name, package.key()));
    }
    if (availablePackages.isEmpty()) {
        m_availablePackagesLabel->setText(
            QStringLiteral("No runnable NoC IP Package is available."));
        m_availablePackagesLabel->setToolTip(
            QStringLiteral("Use Install to add or repair a runtime NoC IP Package."));
    } else {
        m_availablePackagesLabel->setText(
            availablePackages.size() == 1
                ? QStringLiteral("1 NoC IP Package available. Choose it when creating a design.")
                : QStringLiteral("%1 NoC IP Packages available. Choose one when creating a design.")
                      .arg(availablePackages.size()));
        m_availablePackagesLabel->setToolTip(availablePackages.join(QLatin1Char('\n')));
    }

    if (!m_design) {
        m_activePackageLabel->setText(QStringLiteral("No design is open."));
        m_activePackageLabel->setToolTip({});
    } else if (const PackageDefinition* package = packageForDesign()) {
        if (runtimePackageByKey(package->key())) {
            m_activePackageLabel->setText(
                QStringLiteral("%1 — %2").arg(package->name, package->key()));
            m_activePackageLabel->setToolTip(package->rootPath);
        } else {
            m_activePackageLabel->setText(
                QStringLiteral("%1 — %2 (runtime unavailable)")
                    .arg(package->name, package->key()));
            m_activePackageLabel->setToolTip(
                QStringLiteral("Retained metadata from %1 keeps editing available. "
                               "Reload or reinstall this exact Package before validation "
                               "or generation.")
                    .arg(package->rootPath));
        }
    } else {
        m_activePackageLabel->setText(
            QStringLiteral("Package not loaded: %1@%2 (design is read-only)")
                .arg(m_design->package.id, m_design->package.version));
        m_activePackageLabel->setToolTip(
            QStringLiteral("Install the exact Package id and version to restore editing."));
    }
    updateEndpointPalette();
    updateDomainLayerControls();
    updateDomainManager();
    updateDesignExtensionsWorkspace();
    updateUiState();
}

void FinepaperMainWindow::updateDomainLayerControls() {
    if (!m_domainLayerSelector) {
        return;
    }

    const PackageDefinition* package = packageForDesign();
    QString restoredDomainType;
    if (m_design) {
        const QString workspaceKey = workbench::designWorkspaceKey(
            m_design->package.id, m_design->package.version, m_design->id);
        const QVariantMap selections = QSettings().value(
            workbench::domainLayerSelectionsSetting).toMap();
        restoredDomainType = selections.value(workspaceKey).toString();
    }

    {
        const QSignalBlocker blocker(m_domainLayerSelector);
        m_domainLayerSelector->clear();
        m_domainLayerSelector->addItem(QStringLiteral("None"), QString());
        if (package) {
            for (const DomainTypeDefinition& type : package->domainTypes) {
                m_domainLayerSelector->addItem(
                    type.label.trimmed().isEmpty() ? type.id : type.label,
                    type.id);
            }
        }
        const int restoredIndex = m_domainLayerSelector->findData(restoredDomainType);
        m_domainLayerSelector->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
    }

    const bool hasDomainLayers = m_design && package && !package->domainTypes.isEmpty();
    m_domainLayerSelector->setEnabled(hasDomainLayers);
    if (!m_design) {
        m_domainLayerSelector->setToolTip(
            QStringLiteral("Create or open a design before selecting a Domain layer."));
    } else if (!package) {
        m_domainLayerSelector->setToolTip(
            QStringLiteral("The design Package is not loaded, so Domain layers are unavailable."));
    } else if (package->domainTypes.isEmpty()) {
        m_domainLayerSelector->setToolTip(
            QStringLiteral("This Package does not declare any Domain types."));
    } else {
        m_domainLayerSelector->setToolTip(
            QStringLiteral("Color the fixed Mesh projection by a Package-declared Domain type."));
    }

    applyDomainLayer(m_domainLayerSelector->currentData().toString());
}

void FinepaperMainWindow::applyDomainLayer(const QString& domainType) {
    const PackageDefinition* package = packageForDesign();
    if (m_nodeEditor) {
        if (!m_design || !m_resolvedDesign || !package) {
            m_nodeEditor->setDomainPresentation({});
        } else {
            m_nodeEditor->setDomainPresentation(
                buildDomainPresentationSnapshot(
                    *m_resolvedDesign, *package, domainType));
        }
    }
    if (m_domainManager) {
        m_domainManager->setCanvasDomainType(domainType);
    }
}

void FinepaperMainWindow::updateDomainManager() {
    const PackageDefinition* package = packageForDesign();
    if (m_domainManager) {
        m_domainManager->setContext(
            m_design ? &*m_design : nullptr,
            m_resolvedDesign ? &*m_resolvedDesign : nullptr,
            package,
            m_domainLayerSelector
                ? m_domainLayerSelector->currentData().toString()
                : QString());
        m_domainManager->setSelection(m_editorSelection.elements());
        m_domainManager->setBusy(m_operationBusy);
    }
    if (m_domainConfigurationWorkspace) {
        DomainConfigurationValidator validator;
        if (m_design && package) {
            const NocDesign baseDesign = *m_design;
            validator = [this, baseDesign](
                const DomainConfiguration& configuration) {
                return m_application.replaceDomainConfiguration(
                    baseDesign, configuration);
            };
        }
        m_domainConfigurationWorkspace->setContext(
            m_design ? &*m_design : nullptr,
            package,
            m_designSessionIdentity,
            std::move(validator));
        m_domainConfigurationWorkspace->setBusy(m_operationBusy);
    }
}

void FinepaperMainWindow::updateDesignExtensionsWorkspace() {
    if (!m_designExtensionsWorkspace) {
        return;
    }
    m_designExtensionsWorkspace->setContext(
        m_design ? &*m_design : nullptr,
        packageForDesign());
    m_designExtensionsWorkspace->setBusy(m_operationBusy);
}

void FinepaperMainWindow::updateEndpointPalette() {
    m_endpointPalette->clear();
    const PackageDefinition* package = packageForDesign();
    if (!package) {
        m_nodeEditor->setEndpointTypes({});
        const attachment::Policy inferredPolicy = m_design
            ? attachment::inferReadOnlyPolicy(*m_design)
            : attachment::inferReadOnlyPolicy(NocDesign{});
        m_nodeEditor->setAttachmentPolicy(inferredPolicy);
        updateUiState();
        return;
    }
    QVector<NocEndpointTypeItem> editorTypes;
    editorTypes.reserve(package->endpointTypes.size());
    for (const EndpointTypeDefinition& type : package->endpointTypes) {
        auto* item = new QListWidgetItem(type.label, m_endpointPalette);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setData(Qt::UserRole, type.id);
        item->setToolTip(QStringLiteral("%1\nDrag anywhere onto the canvas, then connect it to a Router. "
                                        "Dropping directly on a Router attaches immediately.")
                             .arg(type.id));
        editorTypes.append({type.id, type.label});
    }
    m_nodeEditor->setEndpointTypes(std::move(editorTypes));
    m_nodeEditor->setAttachmentPolicy(
        attachment::policyFromPackage(package->attachment));
    updateUiState();
}

void FinepaperMainWindow::updateUiState() {
    const bool hasDesign = m_design.has_value();
    const bool hasDesignMetadata = hasDesign && packageForDesign();
    const bool hasDesignRuntime = hasDesign && runtimePackageForDesign();
    const bool hasRunnablePackages = !m_runtimeAvailablePackageKeys.isEmpty();
    const bool hasEndpointDrafts = m_endpointConfigurationPanel
        && !m_designSessionIdentity.isEmpty()
        && m_endpointConfigurationPanel->hasUnappliedDrafts(
            m_designSessionIdentity);
    setWindowModified(m_dirty || hasEndpointDrafts);

    if (m_newAction) {
        m_newAction->setEnabled(hasRunnablePackages && !m_operationBusy);
    }
    if (m_openAction) {
        m_openAction->setEnabled(!m_operationBusy);
    }
    if (m_installAction) {
        m_installAction->setEnabled(!m_operationBusy);
    }
    if (m_reloadAction) {
        m_reloadAction->setEnabled(!m_operationBusy);
    }

    if (m_saveAction) {
        m_saveAction->setEnabled(
            hasDesign && (m_dirty || hasEndpointDrafts) && !m_operationBusy);
    }
    if (m_saveAsAction) {
        m_saveAsAction->setEnabled(hasDesign && !m_operationBusy);
    }
    if (m_validateAction) {
        m_validateAction->setEnabled(hasDesignRuntime && !m_operationBusy);
    }
    if (m_generateAction) {
        m_generateAction->setEnabled(hasDesignRuntime && !m_operationBusy);
    }
    if (m_resizeMeshAction) {
        m_resizeMeshAction->setEnabled(hasDesignMetadata && !m_operationBusy);
    }
    if (m_regularizeAction) {
        m_regularizeAction->setEnabled(hasDesign);
    }
    if (m_fitAction) {
        m_fitAction->setEnabled(hasDesign);
    }
    if (m_createDesignButton) {
        m_createDesignButton->setEnabled(hasRunnablePackages && !m_operationBusy);
        m_createDesignButton->setToolTip(
            hasRunnablePackages
                ? QStringLiteral("Choose a NoC IP and create a new design.")
                : QStringLiteral("Install or repair a runnable NoC IP Package before "
                                 "creating a design."));
    }
    if (m_designExtensionsWorkspace) {
        m_designExtensionsWorkspace->setBusy(m_operationBusy);
    }
    if (m_installPackageButton) {
        m_installPackageButton->setEnabled(!m_operationBusy);
    }
    if (m_reloadPackagesButton) {
        m_reloadPackagesButton->setEnabled(!m_operationBusy);
    }
    if (m_endpointPalette) {
        m_endpointPalette->setEnabled(
            hasDesignMetadata && !m_operationBusy && m_endpointPalette->count() > 0);
        if (!hasDesign) {
            m_endpointPalette->setToolTip(
                QStringLiteral("Create or open a design before adding Endpoints."));
        } else if (!hasDesignMetadata) {
            m_endpointPalette->setToolTip(
                QStringLiteral("The design Package is not loaded; Endpoint editing is disabled."));
        } else {
            m_endpointPalette->setToolTip({});
        }
    }
    if (m_parameterGroup) {
        m_parameterGroup->setEnabled(hasDesignMetadata && !m_operationBusy);
    }
    if (m_resizeMeshButton) {
        m_resizeMeshButton->setEnabled(hasDesignMetadata && !m_operationBusy);
        if (!hasDesign) {
            m_resizeMeshButton->setToolTip(
                QStringLiteral("Create or open a design before resizing its Mesh."));
        } else if (!hasDesignMetadata) {
            m_resizeMeshButton->setToolTip(
                QStringLiteral("The design Package is not loaded; Mesh editing is disabled."));
        } else {
            m_resizeMeshButton->setToolTip(
                QStringLiteral("Preview topology changes and configure Domain assignments for new Routers."));
        }
    }
    if (m_applyParametersButton) {
        m_applyParametersButton->setEnabled(
            hasDesignMetadata && !m_operationBusy && m_parameterForm
            && !m_parameterForm->isEmpty() && m_parameterForm->locallyValid()
            && m_parameterForm->isModified());
    }
    if (m_endpointConfigurationPanel) {
        m_endpointConfigurationPanel->setBusy(m_operationBusy);
    }
    if (m_generateButton) {
        m_generateButton->setEnabled(hasDesignRuntime && !m_operationBusy);
    }
    if (m_outputRoot) {
        m_outputRoot->setEnabled(!m_operationBusy);
    }
    if (m_browseOutputButton) {
        m_browseOutputButton->setEnabled(!m_operationBusy);
    }
    if (m_nodeEditor) {
        m_nodeEditor->setEnabled(true);
        m_nodeEditor->setEditingEnabled(hasDesignMetadata && !m_operationBusy);
        if (m_operationBusy) {
            m_nodeEditor->setToolTip(
                QStringLiteral("Read-only while validation or generation is running."));
        } else if (hasDesign && !hasDesignMetadata) {
            m_nodeEditor->setToolTip(
                QStringLiteral("Read-only: the design Package is not loaded."));
        } else {
            m_nodeEditor->setToolTip({});
        }
    }
    if (m_domainManager) {
        m_domainManager->setBusy(m_operationBusy);
    }
    if (m_domainConfigurationWorkspace) {
        m_domainConfigurationWorkspace->setBusy(m_operationBusy);
    }
    if (m_elementConfigurationPanel) {
        m_elementConfigurationPanel->setBusy(m_operationBusy);
    }
}

void FinepaperMainWindow::setOperationBusy(bool busy, const QString& message) {
    m_operationBusy = busy;
    if (m_operationProgress) {
        m_operationProgress->setVisible(busy);
    }
    if (busy && !message.isEmpty()) {
        statusBar()->showMessage(message);
    }
    updateUiState();
}

void FinepaperMainWindow::setDirty(bool dirty) {
    if (m_dirty == dirty) {
        updateUiState();
        return;
    }
    m_dirty = dirty;
    setWindowModified(m_dirty);
    updateUiState();
}

bool FinepaperMainWindow::confirmDiscardPendingDomainChanges(
    const QString& action) {
    const bool assignmentPending = m_domainManager
        && m_domainManager->hasPendingAssignmentChanges();
    const bool workspacePending = m_domainConfigurationWorkspace
        && m_domainConfigurationWorkspace->hasPendingChanges();
    if (!assignmentPending && !workspacePending) {
        return true;
    }

    QStringList pending;
    if (assignmentPending) {
        pending.append(QStringLiteral(
            "a staged quick assignment in the Domain Manager"));
    }
    if (workspacePending) {
        pending.append(QStringLiteral(
            "an unapplied five-section Domain Configuration draft"));
    }
    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Pending Domain changes"),
        QStringLiteral(
            "%1 would replace or consume the durable Design while it still "
            "has %2. Return to the Domain tools and Apply the changes, or "
            "authorize discarding them if the requested operation actually "
            "completes. Cancelling a later dialog will keep every draft.")
            .arg(action, pending.join(QStringLiteral(" and "))),
        QMessageBox::Discard | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.pendingDomainChangesConfirmation"));
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Discard) {
        return false;
    }
    return true;
}

bool FinepaperMainWindow::confirmDiscardPendingDomainWorkspace(
    const QString& action) {
    if (!m_domainConfigurationWorkspace
        || !m_domainConfigurationWorkspace->hasPendingChanges()) {
        return true;
    }

    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Unapplied Domain Configuration"),
        QStringLiteral(
            "%1 would replace the unapplied five-section Domain "
            "Configuration draft. Apply it in the Domain Configuration "
            "Workspace, or authorize discarding it if this quick edit "
            "actually succeeds.")
            .arg(action),
        QMessageBox::Discard | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(QStringLiteral(
        "finepaper.pendingDomainWorkspaceConfirmation"));
    confirmation.setDefaultButton(QMessageBox::Cancel);
    return confirmation.exec() == QMessageBox::Discard;
}

void FinepaperMainWindow::discardPendingDomainChanges() {
    if (m_domainManager) {
        m_domainManager->discardPendingAssignmentChanges();
    }
    discardPendingDomainWorkspace();
}

void FinepaperMainWindow::discardPendingDomainWorkspace() {
    if (m_domainConfigurationWorkspace) {
        m_domainConfigurationWorkspace->discardPendingChanges();
    }
}

bool FinepaperMainWindow::confirmDiscardPendingEndpointDrafts(
    const QString& action) {
    if (!m_endpointConfigurationPanel
        || m_designSessionIdentity.isEmpty()
        || !m_endpointConfigurationPanel->hasUnappliedDrafts(
            m_designSessionIdentity)) {
        return true;
    }

    const QStringList endpointIds =
        m_endpointConfigurationPanel->unappliedDraftEndpointIds(
            m_designSessionIdentity);
    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Unapplied Endpoint configuration"),
        QStringLiteral(
            "%1 would use or replace the durable Design while the following "
            "Endpoint configuration draft(s) have not been applied:\n\n%2\n\n"
            "Return to the Endpoint Inspector and Apply them, or explicitly "
            "discard the drafts now. Unapplied values are never saved, "
            "validated, or generated silently.")
            .arg(action, endpointIds.join(QStringLiteral(", "))),
        QMessageBox::Discard | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.pendingEndpointDraftConfirmation"));
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Discard) {
        return false;
    }
    return true;
}

void FinepaperMainWindow::discardPendingEndpointDrafts() {
    if (!m_endpointConfigurationPanel
        || m_designSessionIdentity.isEmpty()) {
        return;
    }
    m_endpointConfigurationPanel->clearDraftsForDesign(
        m_designSessionIdentity);
}

bool FinepaperMainWindow::canSaveDetachedEndpointDrafts() {
    const QStringList endpointIds = m_nodeEditor
        ? m_nodeEditor->detachedEndpointDraftIds() : QStringList{};
    if (endpointIds.isEmpty()) {
        return true;
    }

    QMessageBox warning(
        QMessageBox::Warning,
        QStringLiteral("Reconnect detached Endpoints before saving"),
        QStringLiteral(
            "The following Endpoint(s) are detached editing drafts and are "
            "not yet part of the durable design:\n\n%1\n\nReconnect them, "
            "or explicitly choose Delete Unattached Endpoint if they should "
            "be removed permanently. The design was not saved.")
            .arg(endpointIds.join(QStringLiteral(", "))),
        QMessageBox::Ok,
        this);
    warning.setObjectName(
        QStringLiteral("finepaper.detachedEndpointSaveBlocker"));
    warning.exec();
    return false;
}

bool FinepaperMainWindow::maybeSave() {
    if (!m_dirty || !m_design) {
        return confirmDiscardPendingDomainChanges(
                   QStringLiteral("Continuing"))
            && confirmDiscardPendingEndpointDrafts(
                   QStringLiteral("Continuing"));
    }
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        QStringLiteral("Unsaved changes"),
        QStringLiteral("Save changes to %1 before continuing?")
            .arg(m_design->name.isEmpty() ? QStringLiteral("the current design")
                                          : m_design->name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Save) {
        return saveDesign();
    }
    return confirmDiscardPendingDomainChanges(
               QStringLiteral("Continuing"))
        && confirmDiscardPendingEndpointDrafts(
               QStringLiteral("Continuing"));
}

const PackageDefinition* FinepaperMainWindow::packageByKey(const QString& key) const {
    for (const PackageDefinition& package : m_application.packages()) {
        if (package.key() == key) {
            return &package;
        }
    }
    return nullptr;
}

const PackageDefinition* FinepaperMainWindow::packageForDesign() const {
    if (!m_design) {
        return nullptr;
    }
    return packageByKey(
        QStringLiteral("%1@%2").arg(m_design->package.id, m_design->package.version));
}

const PackageDefinition* FinepaperMainWindow::runtimePackageByKey(
    const QString& key) const {
    return m_runtimeAvailablePackageKeys.contains(key) ? packageByKey(key) : nullptr;
}

const PackageDefinition* FinepaperMainWindow::runtimePackageForDesign() const {
    if (!m_design) {
        return nullptr;
    }
    return runtimePackageByKey(
        QStringLiteral("%1@%2").arg(m_design->package.id, m_design->package.version));
}

QVector<PackageDefinition> FinepaperMainWindow::runtimePackages() const {
    QVector<PackageDefinition> packages;
    for (const PackageDefinition& package : m_application.packages()) {
        if (m_runtimeAvailablePackageKeys.contains(package.key())) {
            packages.append(package);
        }
    }
    std::sort(
        packages.begin(), packages.end(),
        [](const PackageDefinition& lhs, const PackageDefinition& rhs) {
            if (lhs.id != rhs.id) {
                return lhs.id < rhs.id;
            }
            const QVersionNumber lhsVersion =
                QVersionNumber::fromString(lhs.version);
            const QVersionNumber rhsVersion =
                QVersionNumber::fromString(rhs.version);
            const int versionOrder = QVersionNumber::compare(
                lhsVersion, rhsVersion);
            if (versionOrder != 0) {
                return versionOrder > 0;
            }
            if (lhs.version != rhs.version) {
                return lhs.version > rhs.version;
            }
            return lhs.name < rhs.name;
        });
    return packages;
}

void FinepaperMainWindow::createDesign() {
    if (m_operationBusy) {
        return;
    }
    QVector<PackageDefinition> availablePackages = runtimePackages();
    if (availablePackages.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("No NoC IP Package"),
            QStringLiteral("Install a valid runtime NoC IP Package before creating a design."));
        return;
    }

    const PackageDefinition* activeRuntimePackage = runtimePackageForDesign();
    const QString preferredPackageKey = activeRuntimePackage
        ? activeRuntimePackage->key()
        : QString();
    const QString suggestedName = m_design
        ? m_design->name
        : QStringLiteral("my_noc");
    NewDesignDialog dialog(
        std::move(availablePackages), preferredPackageKey, suggestedName, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QJsonObject request = dialog.createRequest();
    if (!maybeSave()) {
        return;
    }

    DesignResult result = m_application.createDesign(request);
    if (!result.success
        && formatVersionSupportsDomains(result.design.formatVersion)
        && hasOnlyDomainConfigurationErrors(result.diagnostics)) {
        const PackageDefinition* package = packageByKey(
            QStringLiteral("%1@%2")
                .arg(result.design.package.id, result.design.package.version));
        if (package && formatVersionSupportsDomains(package->formatVersion)) {
            DomainConfigurationDialog configurationDialog(
                result.design,
                *package,
                domain_configuration::fromDesign(result.design),
                [this, request](const DomainConfiguration& configuration) {
                    QJsonObject configuredRequest = request;
                    configuredRequest.insert(
                        QStringLiteral("domainConfiguration"),
                        domain_configuration::toJson(configuration));
                    return m_application.createDesign(configuredRequest);
                },
                this);
            configurationDialog.setWindowTitle(
                QStringLiteral("Configure Domains for New Design"));
            if (configurationDialog.exec() != QDialog::Accepted) {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Design creation cancelled before its required Domain "
                        "configuration was complete."),
                    6000);
                return;
            }
            result = configurationDialog.validatedResult();
        }
    }
    if (result.success) {
        discardPendingDomainChanges();
        discardPendingEndpointDrafts();
        m_designPath.clear();
        beginDesignSession();
    }
    adoptDesignResult(result, QStringLiteral("Create Mesh"));
    selectCenterView(workbench::editorViewId);
}

void FinepaperMainWindow::resizeMesh() {
    const PackageDefinition* package = packageForDesign();
    if (m_operationBusy || !m_design || !package) {
        return;
    }
    MeshResizeDialog dialog(*m_design, *package, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Resizing the Mesh"))) {
        return;
    }
    if (!confirmDiscardPendingEndpointDrafts(
            QStringLiteral("Resizing the Mesh"))) {
        return;
    }

    const int rows = dialog.requestedRows();
    const int columns = dialog.requestedColumns();
    const DesignResult result = m_application.resizeMesh(
        *m_design,
        rows,
        columns,
        dialog.newRouterMemberships(),
        dialog.impactConfirmation());
    if (result.success) {
        discardPendingDomainChanges();
        discardPendingEndpointDrafts();
    }
    adoptDesignResult(
        result,
        QStringLiteral("Resize Mesh to %1 × %2").arg(rows).arg(columns));
}

void FinepaperMainWindow::openDesign() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open NoC design"), QString(),
        QStringLiteral("Finepaper NoC (*.fpnoc *.json)"));
    if (path.isEmpty()) {
        return;
    }
    openDesignFile(path);
}

bool FinepaperMainWindow::openDesignFile(const QString& path) {
    if (m_operationBusy || path.trimmed().isEmpty()) {
        return false;
    }
    if (!maybeSave()) {
        return false;
    }
    const DesignResult loaded = m_application.loadDesignFile(path);
    if (!loaded.success) {
        showDiagnostics(loaded.diagnostics, QStringLiteral("Open design"));
        return false;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();
    beginDesignSession();
    m_design = loaded.design;
    m_designPath = path;
    refreshDesignViews();
    setDirty(false);
    selectCenterView(workbench::editorViewId);
    appendActivity(QStringLiteral("Opened design %1.").arg(path));
    if (runtimePackageForDesign()) {
        statusBar()->showMessage(QStringLiteral("Opened %1").arg(path));
    } else if (packageForDesign()) {
        statusBar()->showMessage(
            QStringLiteral("Opened %1; Package runtime is unavailable, so validation and "
                           "generation are disabled.")
                .arg(path));
    } else {
        statusBar()->showMessage(
            QStringLiteral("Read-only design: Package %1@%2 is not loaded.")
                .arg(m_design->package.id, m_design->package.version));
    }
    showWorkspaceStatusMessage();
    return true;
}

bool FinepaperMainWindow::saveDesign() {
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Save design"),
                                 QStringLiteral("Create or open a NoC design first."));
        return false;
    }
    if (m_designPath.isEmpty()) {
        return saveDesignAs();
    }
    return saveDesignTo(m_designPath);
}

bool FinepaperMainWindow::saveDesignAs() {
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Save design"),
                                 QStringLiteral("Create or open a NoC design first."));
        return false;
    }
    if (!canSaveDetachedEndpointDrafts()) {
        return false;
    }
    const QString suggestedPath = m_designPath.isEmpty()
        ? QDir::current().filePath(m_design->id + QStringLiteral(".fpnoc"))
        : m_designPath;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save NoC design as"), suggestedPath,
        QStringLiteral("Finepaper NoC (*.fpnoc)"));
    return !path.isEmpty() && saveDesignTo(path);
}

bool FinepaperMainWindow::saveDesignTo(const QString& path) {
    if (!m_design || path.isEmpty()) {
        return false;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Saving the design"))) {
        return false;
    }
    if (!confirmDiscardPendingEndpointDrafts(
            QStringLiteral("Saving the design"))) {
        return false;
    }
    if (!canSaveDetachedEndpointDrafts()) {
        return false;
    }
    QVector<Diagnostic> diagnostics;
    if (!m_application.saveDesignFile(path, *m_design, &diagnostics)) {
        showDiagnostics(diagnostics, QStringLiteral("Save design"));
        return false;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();
    m_designPath = path;
    setDirty(false);
    appendActivity(QStringLiteral("Saved design %1.").arg(path));
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path));
    return true;
}

void FinepaperMainWindow::validateDesign() {
    if (m_operationBusy) {
        return;
    }
    if (!m_design || !runtimePackageForDesign()) {
        QMessageBox::information(this, QStringLiteral("Validate design"),
                                 QStringLiteral("The design's runtime NoC IP Package is not "
                                                "available. Reload or reinstall the exact "
                                                "Package before validation."));
        return;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Validating the design"))) {
        return;
    }
    if (!confirmDiscardPendingEndpointDrafts(
            QStringLiteral("Validating the design"))) {
        return;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();
    FinepaperApplication application = m_application;
    NocDesign design = *m_design;
    auto* watcher = new QFutureWatcher<ValidationResult>(this);
    m_validationWatcher = watcher;
    connect(watcher, &QFutureWatcher<ValidationResult>::finished,
            this, [this, watcher] {
                const ValidationResult result = watcher->result();
                if (m_validationWatcher == watcher) {
                    m_validationWatcher = nullptr;
                }
                watcher->deleteLater();
                setOperationBusy(false);
                presentValidationResult(result);
            });
    appendActivity(QStringLiteral("Starting validation and Package DRC."));
    setOperationBusy(true, QStringLiteral("Validating design…"));
    watcher->setFuture(QtConcurrent::run(
        [application = std::move(application), design = std::move(design)]() mutable {
            return application.validate(design, true);
        }));
}

void FinepaperMainWindow::presentValidationResult(const ValidationResult& result) {
    populateDiagnostics(result.diagnostics);
    m_problemReport->setPlainText(diagnosticText(result.diagnostics));
    m_resultTabs->setCurrentIndex(0);
    m_resultsDock->show();
    appendActivity(result.success
                       ? QStringLiteral("Validation completed without errors.")
                       : QStringLiteral("Validation found errors."));
    statusBar()->showMessage(result.success ? QStringLiteral("Design is valid.")
                                            : QStringLiteral("Validation found errors."));
    if (!result.success) {
        selectCenterView(workbench::problemReportViewId);
    }
}

void FinepaperMainWindow::generateDesign() {
    if (m_operationBusy) {
        return;
    }
    if (!m_design || !runtimePackageForDesign()) {
        QMessageBox::information(this, QStringLiteral("Generate RTL"),
                                 QStringLiteral("The design's runtime NoC IP Package is not "
                                                "available. Reload or reinstall the exact "
                                                "Package before generation."));
        return;
    }
    const QString root = m_outputRoot->text().trimmed();
    if (root.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Generate RTL"),
                             QStringLiteral("Choose an output root."));
        return;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Generating RTL"))) {
        return;
    }
    if (!confirmDiscardPendingEndpointDrafts(
            QStringLiteral("Generating RTL"))) {
        return;
    }
    discardPendingDomainChanges();
    discardPendingEndpointDrafts();
    appendActivity(QStringLiteral("Starting RTL generation in %1.").arg(root));
    FinepaperApplication application = m_application;
    NocDesign design = *m_design;
    auto* watcher = new QFutureWatcher<GenerationResult>(this);
    m_generationWatcher = watcher;
    connect(watcher, &QFutureWatcher<GenerationResult>::finished,
            this, [this, watcher] {
                const GenerationResult result = watcher->result();
                if (m_generationWatcher == watcher) {
                    m_generationWatcher = nullptr;
                }
                watcher->deleteLater();
                setOperationBusy(false);
                presentGenerationResult(result);
            });
    setOperationBusy(true, QStringLiteral("Generating RTL…"));
    watcher->setFuture(QtConcurrent::run(
        [application = std::move(application),
         design = std::move(design),
         root]() mutable {
            return application.generate(design, GenerationOptions{root});
        }));
}

void FinepaperMainWindow::presentGenerationResult(const GenerationResult& result) {
    populateGenerationOutputs(result);
    populateDiagnostics(result.diagnostics);
    m_resultTabs->setCurrentIndex(2);
    m_resultsDock->show();
    appendActivity(result.success
                       ? QStringLiteral("RTL generation completed: %1 artifact(s).")
                             .arg(result.artifacts.size())
                       : QStringLiteral("RTL generation failed with exit code %1.")
                             .arg(result.exitCode));
    statusBar()->showMessage(result.success ? QStringLiteral("RTL generated.")
                                            : QStringLiteral("RTL generation failed."));
    if (!result.success) {
        showDiagnostics(result.diagnostics, QStringLiteral("Generate RTL"));
    }
}

attachment::CreateEndpointResult FinepaperMainWindow::addEndpoint(
    const QString& endpointType,
    NocAttachmentTarget target) {
    if (m_operationBusy || !m_design || !packageForDesign()) {
        QMessageBox::information(this, QStringLiteral("Add Endpoint"),
                                 QStringLiteral("Create or open an editable NoC design first."));
        return {};
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Adding an Endpoint"))) {
        return {};
    }
    const AttachmentSlotChoice slotChoice = chooseAttachmentSlot(target);
    if (!slotChoice.accepted) {
        return {};
    }
    EndpointCreationDialog configuration(
        *m_design,
        *packageForDesign(),
        endpointType,
        nextEndpointId(endpointType),
        this);
    if (configuration.exec() != QDialog::Accepted) {
        return {};
    }
    const EndpointCreationDraft draft = configuration.draft();
    EndpointInstance endpoint;
    endpoint.id = draft.id;
    endpoint.type = draft.type;
    endpoint.attachment.router = target.router;
    endpoint.attachment.slot = slotChoice.slot;
    endpoint.parameters = draft.parameters;
    const DesignResult result = m_application.addEndpoint(
        *m_design, endpoint, draft.domainAssignments);
    if (result.success) {
        discardPendingDomainChanges();
    }
    adoptDesignResult(result,
                      QStringLiteral("Add Endpoint %1").arg(endpoint.id));
    return {result.success, result.success ? endpoint.id : QString()};
}

std::optional<QHash<QString, QStringList>>
FinepaperMainWindow::chooseEndpointDomainAssignments(
    const QString& endpointId,
    const QHash<QString, QStringList>& initialAssignments) {
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        return std::nullopt;
    }

    const QVector<EndpointDomainAssignmentGroup> groups =
        buildEndpointDomainAssignmentGroups(
            *m_design, *package, initialAssignments);
    if (!endpointDomainAssignmentsRequireUserDecision(
            groups,
            EndpointDomainAssignmentDecisionMode::Restore)) {
        return endpointDomainAssignmentsFromGroups(groups);
    }

    EndpointDomainAssignmentDialog dialog(
        *m_design, *package, initialAssignments, this);
    if (dialog.groups().isEmpty()) {
        return QHash<QString, QStringList>{};
    }
    dialog.setWindowTitle(
        QStringLiteral("Domain Assignments — %1").arg(endpointId));
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.assignments();
}

bool FinepaperMainWindow::moveEndpoint(const QString& endpointId,
                                       NocAttachmentTarget target) {
    if (m_operationBusy || !m_design || !packageForDesign()) {
        return false;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Moving an Endpoint"))) {
        return false;
    }
    const AttachmentSlotChoice slotChoice = chooseAttachmentSlot(target, endpointId);
    if (!slotChoice.accepted) {
        m_nodeEditor->setDesign(&*m_design);
        return false;
    }
    const DesignResult result = m_application.moveEndpoint(
        *m_design, endpointId, target.router, slotChoice.slot);
    if (result.success) {
        discardPendingDomainChanges();
    }
    adoptDesignResult(result,
                      QStringLiteral("Move Endpoint %1").arg(endpointId));
    return result.success;
}

bool FinepaperMainWindow::removeEndpoint(
    const QString& endpointId,
    bool discardConfigurationDraft) {
    if (m_operationBusy || !m_design || !packageForDesign() || endpointId.isEmpty()) {
        return false;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Removing an Endpoint"))) {
        return false;
    }
    const DesignResult result = m_application.removeEndpoint(*m_design, endpointId);
    if (result.success) {
        discardPendingDomainChanges();
        if (discardConfigurationDraft && m_endpointConfigurationPanel) {
            m_endpointConfigurationPanel->discardDraft(
                m_designSessionIdentity, endpointId);
        }
    }
    adoptDesignResult(result, QStringLiteral("Remove Endpoint %1").arg(endpointId));
    return result.success;
}

FinepaperMainWindow::AttachmentSlotChoice FinepaperMainWindow::chooseAttachmentSlot(
    NocAttachmentTarget target,
    const QString& ignoredEndpointId) {
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        return {};
    }
    const attachment::SlotResolution resolution = attachment::resolveSlot(
        *m_design,
        attachment::policyFromPackage(package->attachment),
        target,
        ignoredEndpointId);
    if (resolution.kind == attachment::SlotResolutionKind::Rejected) {
        QMessageBox::information(
            this,
            QStringLiteral("No available attachment position"),
            attachment::rejectionMessage(resolution.rejection, target.router));
        return {};
    }
    if (resolution.kind == attachment::SlotResolutionKind::Automatic) {
        return {true, std::nullopt};
    }
    if (resolution.kind == attachment::SlotResolutionKind::Exact) {
        return {true, resolution.resolvedSlot};
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Choose Endpoint attachment position"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* prompt = new QLabel(
        QStringLiteral("Router (%1, %2) slot")
            .arg(target.router.x)
            .arg(target.router.y),
        &dialog);
    auto* selector = new QComboBox(&dialog);
    selector->setObjectName(QStringLiteral("finepaper.attachmentSlotSelector"));
    prompt->setBuddy(selector);
    layout->addWidget(prompt);
    layout->addWidget(selector);
    for (const attachment::SlotChoice& choice : resolution.choices) {
        const QString label = choice.label == choice.id
            ? choice.id
            : QStringLiteral("%1 — %2")
                  .arg(choice.label)
                  .arg(choice.id);
        selector->addItem(label, choice.id);
    }
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    selector->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return {true, selector->currentData().toString()};
}

void FinepaperMainWindow::applyParameters() {
    if (m_operationBusy || !m_design || !packageForDesign()
        || !m_parameterForm || !m_parameterForm->locallyValid()
        || !m_parameterForm->isModified()) {
        return;
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Applying NoC parameters"))) {
        return;
    }
    const DesignResult result = m_application.updateParameters(
        *m_design, m_parameterForm->values());
    if (result.success) {
        discardPendingDomainChanges();
    }
    adoptDesignResult(result, QStringLiteral("Apply Parameters"));
}

void FinepaperMainWindow::updateInspector(const NocEditorSelectionSet& selection) {
    m_editorSelection = selection;
    std::optional<ElementRef> selectedElement = std::nullopt;
    if (selection.items.size() == 1) {
        selectedElement = selection.items.front().element();
    }
    const bool singleEndpoint = selection.items.size() == 1
        && selection.items.front().kind
            == NocEditorSelection::Kind::Endpoint;
    const bool singleElementConfigurationTarget =
        selectedElement
        && isElementConfigurationTargetKind(selectedElement->kind);
    if (m_endpointConfigurationGroup) {
        m_endpointConfigurationGroup->setVisible(singleEndpoint);
    }
    if (m_elementConfigurationGroup) {
        m_elementConfigurationGroup->setVisible(
            singleElementConfigurationTarget);
    }
    if (m_domainManager) {
        m_domainManager->setSelection(selection.elements());
    }
    if (m_elementConfigurationPanel) {
        m_elementConfigurationPanel->setContext(
            m_design ? &*m_design : nullptr,
            packageForDesign(),
            selectedElement,
            m_operationBusy);
    }
    if (m_endpointConfigurationPanel) {
        std::optional<QString> endpointId;
        if (selection.items.size() == 1
            && selection.items.front().kind
                == NocEditorSelection::Kind::Endpoint) {
            endpointId = selection.items.front().id;
        }
        m_endpointConfigurationPanel->setContext(
            m_design ? &*m_design : nullptr,
            packageForDesign(),
            m_designSessionIdentity,
            std::move(endpointId),
            m_operationBusy);
    }
    m_selectedRouter.reset();

    if (selection.items.isEmpty()) {
        m_selectionSummary->setText(QStringLiteral(
            "Nothing selected.<br><b>Pan</b> is the default: drag the canvas "
            "to move the viewport. Hold <b>Shift</b> and drag, or press "
            "<b>V</b>, to box-select."));
        return;
    }

    if (selection.items.size() > 1) {
        int routers = 0;
        int endpoints = 0;
        int routerLinks = 0;
        int attachments = 0;
        int pendingEndpoints = 0;
        for (const NocEditorSelection& item : selection.items) {
            switch (item.kind) {
            case NocEditorSelection::Kind::Router: ++routers; break;
            case NocEditorSelection::Kind::Endpoint: ++endpoints; break;
            case NocEditorSelection::Kind::RouterLink: ++routerLinks; break;
            case NocEditorSelection::Kind::EndpointAttachment: ++attachments; break;
            case NocEditorSelection::Kind::PendingEndpoint: ++pendingEndpoints; break;
            case NocEditorSelection::Kind::None: break;
            }
        }
        QStringList counts;
        if (routers > 0) counts.append(QStringLiteral("%1 Router(s)").arg(routers));
        if (endpoints > 0) counts.append(QStringLiteral("%1 Endpoint(s)").arg(endpoints));
        if (routerLinks > 0) counts.append(QStringLiteral("%1 Router Link(s)").arg(routerLinks));
        if (attachments > 0) {
            counts.append(QStringLiteral("%1 Endpoint Attachment(s)").arg(attachments));
        }
        if (pendingEndpoints > 0) {
            counts.append(QStringLiteral("%1 unattached Endpoint draft(s)").arg(pendingEndpoints));
        }
        const QString meshNote = routers > 0 || routerLinks > 0
            ? QStringLiteral("<br>Routers and Router Links remain fixed semantic projections "
                             "of the Mesh; selection does not expose topology creation, deletion, "
                             "or rewiring.")
            : QString();
        m_selectionSummary->setText(
            QStringLiteral("<b>%1 items selected</b><br>%2%3")
                .arg(selection.items.size())
                .arg(counts.join(QStringLiteral(" · ")))
                .arg(meshNote));
        return;
    }

    const NocEditorSelection& item = selection.items.front();
    if (item.kind == NocEditorSelection::Kind::Router) {
        std::optional<RouterPosition> position = item.router;
        if (!position) {
            position = routerPositionFromId(item.id);
        }
        if (position) {
            m_selectedRouter = position;
        }
        m_selectionSummary->setText(
            QStringLiteral("<b>Router %1</b><br>Column x: %2<br>Row y: %3<br>"
                           "Router identity and every Router-to-Router Link are derived from "
                           "the fixed Mesh. Dragging changes only this local Workspace layout; "
                           "Router creation, deletion, and manual rewiring are not exposed.")
                .arg(item.id.toHtmlEscaped())
                .arg(position ? position->x : -1)
                .arg(position ? position->y : -1));
        return;
    }
    const auto endpoint = m_design
        ? std::find_if(m_design->endpoints.cbegin(), m_design->endpoints.cend(),
                       [&item](const EndpointInstance& candidate) {
                           return candidate.id == item.id;
                       })
        : QVector<EndpointInstance>::const_iterator{};
    const bool endpointFound = m_design && endpoint != m_design->endpoints.cend();
    if (item.kind == NocEditorSelection::Kind::Endpoint && endpointFound) {
        m_selectionSummary->setText(
            QStringLiteral("<b>Endpoint %1</b><br>Type: %2<br>Router: (%3, %4)<br>"
                           "Slot: %5<br>Moving the node changes only its Workspace position; "
                           "the attachment changes only through an explicit connection action.")
                .arg(endpoint->id.toHtmlEscaped(), endpoint->type.toHtmlEscaped())
                .arg(endpoint->attachment.router.x)
                .arg(endpoint->attachment.router.y)
                .arg(endpoint->attachment.slot.value_or(QStringLiteral("automatic"))
                         .toHtmlEscaped()));
        return;
    }
    if (item.kind == NocEditorSelection::Kind::RouterLink && m_design) {
        const ElementRef edge{ElementKind::RouterLink, item.id};
        QString endpointsText;
        if (const auto endpoints = edgeEndpoints(*m_design, edge)) {
            endpointsText = QStringLiteral("<br>From: %1<br>To: %2")
                                .arg(endpoints->first.id.toHtmlEscaped(),
                                     endpoints->second.id.toHtmlEscaped());
        }
        const QString crossingText = m_nodeEditor
            ? domainCrossingInspectorHtml(
                  m_nodeEditor->domainPresentation(), edge)
            : QString();
        m_selectionSummary->setText(
            QStringLiteral("<b>Router Link %1</b>%2<br>This Link is derived from the fixed "
                           "Mesh and cannot be created, deleted, or rewired manually.%3")
                .arg(item.id.toHtmlEscaped(), endpointsText, crossingText));
        return;
    }
    if (item.kind == NocEditorSelection::Kind::EndpointAttachment) {
        const ElementRef edge{ElementKind::EndpointAttachment, item.id};
        const QString crossingText = m_nodeEditor
            ? domainCrossingInspectorHtml(
                  m_nodeEditor->domainPresentation(), edge)
            : QString();
        if (endpointFound) {
            m_selectionSummary->setText(
                QStringLiteral("<b>Endpoint Attachment %1</b><br>Router: (%2, %3)<br>"
                               "Slot: %4<br>This semantic attachment may be disconnected or "
                               "reassigned explicitly. Endpoint canvas placement remains "
                               "independent Workspace state.%5")
                    .arg(endpoint->id.toHtmlEscaped())
                    .arg(endpoint->attachment.router.x)
                    .arg(endpoint->attachment.router.y)
                    .arg(endpoint->attachment.slot.value_or(QStringLiteral("automatic"))
                             .toHtmlEscaped())
                    .arg(crossingText));
        } else {
            m_selectionSummary->setText(
                QStringLiteral("<b>Endpoint Attachment %1</b>%2")
                    .arg(item.id.toHtmlEscaped(), crossingText));
        }
        return;
    }
    if (item.kind == NocEditorSelection::Kind::PendingEndpoint) {
        m_selectionSummary->setText(
            QStringLiteral("<b>Unattached Endpoint</b><br>Type: %1<br>"
                           "Drag its EP port to a Router EP port or Router body to attach it. "
                           "Moving the node only changes its canvas position.")
                .arg(item.id.toHtmlEscaped()));
        return;
    }
    m_selectionSummary->setText(QStringLiteral("Nothing selected."));
}

void FinepaperMainWindow::adoptDesignResult(
    const DesignResult& result,
    const QString& action,
    DesignRefreshScope scope) {
    if (!result.success) {
        showDiagnostics(result.diagnostics, action);
        if (m_design && scope == DesignRefreshScope::FullProjection) {
            m_nodeEditor->setDesign(&*m_design);
        }
        return;
    }
    m_design = result.design;
    if (scope == DesignRefreshScope::DomainsOnly) {
        m_nodeEditor->syncDesignState(*m_design);
        refreshDomainViews();
    } else if (scope == DesignRefreshScope::InspectorOnly) {
        m_nodeEditor->syncDesignState(*m_design);
        m_resolvedDesign = resolveDesign(*m_design);
        updateInspector(m_editorSelection);
        updateUiState();
    } else {
        refreshDesignViews();
    }
    setDirty(true);
    appendActivity(action + QStringLiteral(" completed."));
    statusBar()->showMessage(action + QStringLiteral(" completed."));
    showWorkspaceStatusMessage();
}

void FinepaperMainWindow::adoptDomainResult(
    const DesignResult& result,
    const QString& action) {
    if (m_domainManager) {
        m_domainManager->setDiagnostics(result.diagnostics);
    }
    showDiagnostics(result.diagnostics, action, false);
    if (!result.success) {
        appendActivity(action + QStringLiteral(" failed."));
        statusBar()->showMessage(action + QStringLiteral(" failed."), 6000);
        return;
    }
    adoptDesignResult(result, action, DesignRefreshScope::DomainsOnly);
}

void FinepaperMainWindow::refreshDomainViews() {
    if (!m_design) {
        refreshDesignViews();
        return;
    }
    m_resolvedDesign = resolveDesign(*m_design);
    applyDomainLayer(
        m_domainLayerSelector
            ? m_domainLayerSelector->currentData().toString()
            : QString());
    updateDomainManager();
    updateInspector(m_editorSelection);
    updateUiState();
}

void FinepaperMainWindow::refreshDesignViews() {
    if (!m_design) {
        m_resolvedDesign.reset();
        setWindowTitle(QStringLiteral("Finepaper — NoC Workbench[*]"));
        m_nodeEditor->setDesign(nullptr);
        m_designOverview->setText(QStringLiteral("No design is open."));
        rebuildParameterEditors();
        updateInspector({});
        updatePackageControls();
        return;
    }

    m_resolvedDesign = resolveDesign(*m_design);
    const PackageDefinition* package = packageForDesign();
    const bool packageMetadataAvailable = package;
    setWindowTitle(QStringLiteral("Finepaper — %1[*]").arg(m_design->name));
    rebuildParameterEditors();
    updateInspector({});
    const attachment::Policy attachmentPolicy = package
        ? attachment::policyFromPackage(package->attachment)
        : attachment::inferReadOnlyPolicy(*m_design);
    m_nodeEditor->setDesign(&*m_design, attachmentPolicy);
    updatePackageControls();

    const bool packageRuntimeAvailable = runtimePackageForDesign();
    QString availabilityNote;
    if (!packageMetadataAvailable) {
        availabilityNote = QStringLiteral(
            "<p><b>Read-only</b><br>The design Package is not loaded. Endpoint and "
            "parameter editing, validation, and generation are disabled.</p>");
    } else if (!packageRuntimeAvailable) {
        availabilityNote = QStringLiteral(
            "<p><b>Runtime unavailable</b><br>Retained Package metadata keeps Endpoint "
            "and parameter editing available. Reload or reinstall this exact Package "
            "before validation or generation.</p>");
    }
    m_designOverview->setText(
        QStringLiteral("<h3>%1</h3><p><b>Package</b><br>%2@%3</p>"
                       "<p><b>Topology</b><br>%4 × %5 Mesh</p>"
                       "<p><b>Endpoints</b><br>%6</p>%7")
            .arg(m_design->name.toHtmlEscaped(),
                 m_design->package.id.toHtmlEscaped(),
                 m_design->package.version.toHtmlEscaped())
            .arg(m_design->topology.rows)
            .arg(m_design->topology.columns)
            .arg(m_design->endpoints.size())
            .arg(availabilityNote));
    m_performanceSummary->setText(
        QStringLiteral("NoC %1 currently contains a %2 × %3 Mesh and %4 Endpoint(s). "
                       "This view is reserved for Package or IP Engine performance results; "
                       "the NodeEditor remains the source interaction surface.")
            .arg(m_design->name.toHtmlEscaped())
            .arg(m_design->topology.rows)
            .arg(m_design->topology.columns)
            .arg(m_design->endpoints.size()));

    if (!packageMetadataAvailable) {
        statusBar()->showMessage(
            QStringLiteral("Read-only design: Package %1@%2 is not loaded.")
                .arg(m_design->package.id, m_design->package.version));
    } else if (!packageRuntimeAvailable) {
        statusBar()->showMessage(
            QStringLiteral("Package runtime unavailable: editing remains enabled, but "
                           "validation and generation require %1@%2.")
                .arg(m_design->package.id, m_design->package.version));
    }
}

void FinepaperMainWindow::rebuildParameterEditors() {
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        m_parameterForm->setSchema({}, {});
        updateUiState();
        return;
    }
    m_parameterForm->setSchema(package->parameters, m_design->parameters);
    updateUiState();
}

void FinepaperMainWindow::populateDiagnostics(const QVector<Diagnostic>& diagnostics) {
    m_drcTable->clearContents();
    m_drcTable->setRowCount(diagnostics.size());
    for (qsizetype row = 0; row < diagnostics.size(); ++row) {
        const Diagnostic& diagnostic = diagnostics.at(row);
        m_drcTable->setItem(row, 0, readOnlyItem(diagnostic.severity));
        m_drcTable->setItem(row, 1, readOnlyItem(diagnostic.code));
        m_drcTable->setItem(row, 2, readOnlyItem(diagnostic.message));
        m_drcTable->setItem(row, 3, readOnlyItem(diagnostic.path));
    }
    m_drcTable->resizeColumnsToContents();
    m_drcTable->horizontalHeader()->setStretchLastSection(true);
}

void FinepaperMainWindow::populateGenerationOutputs(const GenerationResult& result) {
    m_artifactTable->clearContents();
    m_artifactTable->setRowCount(result.artifacts.size());
    for (qsizetype row = 0; row < result.artifacts.size(); ++row) {
        const Artifact& artifact = result.artifacts.at(row);
        m_artifactTable->setItem(row, 0, readOnlyItem(artifact.id));
        m_artifactTable->setItem(row, 1, readOnlyItem(artifact.type));
        m_artifactTable->setItem(row, 2, readOnlyItem(artifact.path));
        m_artifactTable->setItem(row, 3,
                                 readOnlyItem(artifact.primary ? QStringLiteral("yes")
                                                               : QStringLiteral("no")));
    }
    m_artifactTable->resizeColumnsToContents();
    m_generationDetails->setPlainText(
        QString::fromUtf8(QJsonDocument(generationResultToJson(result))
                              .toJson(QJsonDocument::Indented)));
}

void FinepaperMainWindow::showDiagnostics(const QVector<Diagnostic>& diagnostics,
                                          const QString& title,
                                          bool modalOnError) {
    populateDiagnostics(diagnostics);
    m_problemReport->setPlainText(diagnosticText(diagnostics));
    if (modalOnError && hasErrors(diagnostics)) {
        m_resultTabs->setCurrentIndex(0);
        m_resultsDock->show();
        QMessageBox::critical(this, title, diagnosticText(diagnostics));
    }
}

void FinepaperMainWindow::appendActivity(const QString& message) {
    m_activityLog->appendPlainText(
        QStringLiteral("%1  %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                 message));
}

void FinepaperMainWindow::showWorkspaceStatusMessage() {
    if (!m_workspaceStatusMessage.isEmpty()) {
        statusBar()->showMessage(m_workspaceStatusMessage);
    }
}

void FinepaperMainWindow::selectCenterView(const QString& id) {
    if (!m_viewRegistry->select(id)) {
        m_viewRegistry->select(workbench::editorViewId);
    }
}

void FinepaperMainWindow::beginDesignSession() {
    ++m_designSessionSerial;
    m_designSessionIdentity = QStringLiteral("design-session-%1")
        .arg(m_designSessionSerial);
    m_workspaceStatusMessage.clear();
    if (m_nodeEditor) {
        m_nodeEditor->beginDocumentSession(m_designSessionIdentity);
    }
    if (m_endpointConfigurationPanel) {
        m_endpointConfigurationPanel->clearDrafts();
    }
}

QString FinepaperMainWindow::nextEndpointId(const QString& endpointType) const {
    QString base = endpointType.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    base = base.trimmed();
    if (base.isEmpty()) {
        base = QStringLiteral("endpoint");
    }
    int suffix = 0;
    while (true) {
        const QString candidate = QStringLiteral("%1_%2").arg(base).arg(suffix);
        bool exists = false;
        if (m_design) {
            for (const EndpointInstance& endpoint : m_design->endpoints) {
                if (endpoint.id == candidate) {
                    exists = true;
                    break;
                }
            }
        }
        if (!exists) {
            return candidate;
        }
        ++suffix;
    }
}

} // namespace finepaper
