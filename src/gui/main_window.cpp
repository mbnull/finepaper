#include "gui/main_window.h"

#include "features/domain/domain_configuration_dialog.h"
#include "features/domain/domain_configuration_workspace.h"
#include "features/design_extensions/design_extensions_workspace.h"
#include "features/domain/domain_manager_panel.h"
#include "features/domain/presentation/domain_text.h"
#include "features/attachment/endpoint_attachment_rules.h"
#include "gui/element_configuration_panel.h"
#include "gui/endpoint_configuration_panel.h"
#include "features/domain/endpoint_domain_assignment_dialog.h"
#include "features/topology/mesh_resize_dialog.h"
#include "gui/package_parameter_form.h"
#include "ui/components/empty_state.h"
#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"
#include "ui/workbench/inspector_design_settings.h"
#include "ui/workbench/inspector_summary_panel.h"
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
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
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

QString domainSetText(const DomainPresentationSnapshot& snapshot,
                      const QStringList& domainIds) {
    if (domainIds.isEmpty()) {
        return QStringLiteral("No Domains");
    }
    QStringList assignments;
    assignments.reserve(domainIds.size());
    for (const QString& domainId : domainIds) {
        const auto entry = std::find_if(
            snapshot.legend.cbegin(), snapshot.legend.cend(),
            [&](const DomainLegendEntry& candidate) {
                return candidate.id == domainId;
            });
        assignments.append(domain_text::domainInstanceDisplayText(
            domainId,
            entry == snapshot.legend.cend() ? QString() : entry->name));
    }
    return assignments.join(QStringLiteral(", "));
}

QString propertiesText(const QJsonObject& properties) {
    return QString::fromUtf8(
        QJsonDocument(properties).toJson(QJsonDocument::Compact));
}

QString domainCrossingInspectorText(
    const DomainPresentationSnapshot& snapshot,
    const ElementRef& edge) {
    const DomainCrossingPresentation* crossing = snapshot.crossing(edge);
    if (!crossing || snapshot.activeDomainType.isEmpty()) {
        return {};
    }

    const QString layer = domain_text::domainTypeDisplayText(
        snapshot.activeDomainType, snapshot.domainTypeLabel);

    QStringList lines = {
        QStringLiteral("Color-by Domain crossing — %1").arg(layer),
        QStringLiteral("From set: %1").arg(
            domainSetText(snapshot, crossing->fromDomainIds)),
        QStringLiteral("To set: %1").arg(
            domainSetText(snapshot, crossing->toDomainIds))
    };
    if (crossing->defaultPolicy) {
        lines.append(
            QStringLiteral("Default policy: %1")
                .arg(*crossing->defaultPolicy));
        lines.append(
            QStringLiteral("Default properties: %1")
                .arg(propertiesText(crossing->defaultProperties)));
    } else {
        const bool singleton = crossing->fromDomainIds.size() == 1
            && crossing->toDomainIds.size() == 1;
        lines.append(
            singleton
                ? QStringLiteral(
                      "Default policy: none resolved for this exact "
                      "canonical boundary orientation")
                : QStringLiteral(
                      "Default policy: unavailable for a set-valued "
                      "crossing"));
    }

    if (crossing->overridePolicy || !crossing->overrideProperties.isEmpty()) {
        lines.append(
            QStringLiteral("Edge override policy: %1")
                .arg(crossing->overridePolicy
                         ? *crossing->overridePolicy
                         : QStringLiteral("not specified")));
        lines.append(
            QStringLiteral("Override properties: %1")
                .arg(propertiesText(crossing->overrideProperties)));
    } else {
        lines.append(crossing->defaultPolicy
            ? QStringLiteral("Edge override: none; the default applies unchanged")
            : QStringLiteral("Edge override: none"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString domainElementInspectorText(
    const DomainPresentationSnapshot& snapshot,
    const PackageDefinition* package,
    const NocDesign& design,
    const ElementRef& element) {
    const DomainElementPresentation* presentation = snapshot.element(element);
    if (!presentation || snapshot.activeDomainType.isEmpty()) {
        return {};
    }

    const DomainTypeDefinition* type = package
        ? package->domainType(snapshot.activeDomainType) : nullptr;
    const QString typeText = type
        ? domain_text::domainTypeDisplayText(*type)
        : domain_text::domainTypeDisplayText(
              snapshot.activeDomainType, snapshot.domainTypeLabel);
    const QString constraintText = type
        ? domain_text::domainAssignmentConstraintText(*type, element.kind)
        : QStringLiteral("Package assignment rule unavailable.");
    const QString assignmentText = type
        ? domain_text::domainAssignmentText(
              *type, element.kind, design.domains, presentation->domainIds)
        : domain_text::domainAssignmentListText(
              design.domains,
              snapshot.activeDomainType,
              presentation->domainIds);

    return QStringLiteral(
        "Active Domain layer\nType: %1\nConstraint: %2\nAssignment: %3")
        .arg(typeText, constraintText, assignmentText);
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

void setStatusLabel(QLabel* label,
                    const QString& text,
                    const QString& semanticRole) {
    if (!label) {
        return;
    }
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    if (label->property("finepaperRole").toString() == semanticRole) {
        return;
    }
    label->setProperty("finepaperRole", semanticRole);
    if (QStyle* style = label->style()) {
        style->unpolish(label);
        style->polish(label);
    }
    label->update();
}

QString ignoredRunReason(
    operations::CompletionDisposition disposition) {
    switch (disposition) {
    case operations::CompletionDisposition::StaleRevision:
        return QStringLiteral("the design has a newer revision");
    case operations::CompletionDisposition::DifferentSession:
        return QStringLiteral("a different design session is open");
    case operations::CompletionDisposition::StaleCatalog:
        return QStringLiteral("the Package catalog was reloaded");
    case operations::CompletionDisposition::Superseded:
        return QStringLiteral("a newer operation superseded it");
    case operations::CompletionDisposition::Current:
        break;
    }
    return QStringLiteral("it no longer matches the current workbench state");
}

QString designRevisionText(const operations::DesignStamp& stamp) {
    return QString::fromUtf8("“") + stamp.designName
        + QStringLiteral("”, revision ")
        + QString::number(stamp.revision);
}

bool packageDisplayLess(const PackageDefinition& lhs,
                        const PackageDefinition& rhs) {
    if (lhs.id != rhs.id) {
        return lhs.id < rhs.id;
    }
    const QVersionNumber lhsVersion = QVersionNumber::fromString(lhs.version);
    const QVersionNumber rhsVersion = QVersionNumber::fromString(rhs.version);
    const int versionOrder = QVersionNumber::compare(lhsVersion, rhsVersion);
    if (versionOrder != 0) {
        return versionOrder > 0;
    }
    if (lhs.version != rhs.version) {
        return lhs.version > rhs.version;
    }
    return lhs.name < rhs.name;
}

QWidget* placeholderPage(const QString& title, const QString& description, QLabel** summary) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(
        ui::UiMetrics::spacing32, ui::UiMetrics::spacing32,
        ui::UiMetrics::spacing32, ui::UiMetrics::spacing32);
    auto* heading = new QLabel(title);
    heading->setProperty("finepaperRole", QStringLiteral("title"));
    heading->setFont(
        ui::fontForRole(ui::UiFontRole::Title, heading->font()));
    layout->addWidget(heading);
    auto* text = new QLabel(description);
    text->setTextFormat(Qt::PlainText);
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
        m_packageDetails->setTextFormat(Qt::RichText);
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

    QString selectedPackageKey() const {
        const PackageDefinition* package = selectedPackage();
        return package ? package->key() : QString();
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
                     package->version.toHtmlEscaped(),
                     QString::number(package->mesh.minimumRows),
                     QString::number(package->mesh.maximumRows),
                     QString::number(package->mesh.minimumColumns),
                     QString::number(package->mesh.maximumColumns)));
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
        m_domainManager->draftStateChanged = {};
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
        m_elementConfigurationPanel->draftStateChanged = {};
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
    setObjectName(QStringLiteral("finepaper.workbenchWindow"));
    setWindowTitle(QStringLiteral("Finepaper — NoC Workbench[*]"));
    resize(workbench::defaultWindowWidth, workbench::defaultWindowHeight);
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
    m_operationProgress->setAccessibleName(
        QStringLiteral("Background operation in progress"));
    m_operationProgress->hide();
    statusBar()->addPermanentWidget(m_operationProgress);

    resetWorkbenchLayout();
    updateUiState();
}

void FinepaperMainWindow::resetWorkbenchLayout() {
    if (!m_packageDock || !m_inspectorDock || !m_domainDock
        || !m_resultsDock) {
        return;
    }
    for (QDockWidget* dock : {
             m_packageDock, m_inspectorDock, m_domainDock, m_resultsDock}) {
        dock->setFloating(false);
    }
    addDockWidget(Qt::LeftDockWidgetArea, m_packageDock);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    addDockWidget(Qt::RightDockWidgetArea, m_domainDock);
    tabifyDockWidget(m_inspectorDock, m_domainDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_resultsDock);
    m_packageDock->show();
    m_inspectorDock->show();
    m_domainDock->show();
    m_inspectorDock->raise();
    m_resultsDock->hide();
    resizeDocks(
        {m_packageDock, m_inspectorDock},
        {workbench::defaultPackageDockWidth,
         workbench::defaultInspectorDockWidth},
        Qt::Horizontal);
    resizeDocks(
        {m_resultsDock}, {workbench::defaultResultsDockHeight}, Qt::Vertical);
    if (m_centerViews) {
        selectCenterView(workbench::editorViewId);
    }
}

void FinepaperMainWindow::createCentralViews() {
    m_centerViews = new QTabWidget(this);
    m_centerViews->setObjectName(QStringLiteral("finepaper.centerViews"));
    m_centerViews->setAccessibleName(QStringLiteral("Workbench views"));
    m_centerViews->setDocumentMode(true);
    m_centerViews->setMovable(true);
    m_centerViews->setTabsClosable(false);
    m_centerViews->setElideMode(Qt::ElideRight);
    setCentralWidget(m_centerViews);
    m_viewRegistry.emplace(m_centerViews);

    m_editorPage = new QWidget(m_centerViews);
    m_editorPage->setObjectName(QStringLiteral("finepaper.editorPage"));
    m_editorStack = new QStackedLayout(m_editorPage);
    m_editorStack->setContentsMargins(0, 0, 0, 0);
    m_editorStack->setStackingMode(QStackedLayout::StackAll);

    m_nodeEditor = new NocNodeEditor(m_editorPage);
    m_nodeEditor->setObjectName(QStringLiteral("finepaper.nodeEditor"));
    m_editorStack->addWidget(m_nodeEditor);

    m_editorEmptyStateOverlay = new QWidget(m_editorPage);
    m_editorEmptyStateOverlay->setObjectName(
        QStringLiteral("finepaper.canvasEmptyState"));
    m_editorEmptyStateOverlay->setAutoFillBackground(true);
    auto* emptyLayout = new QVBoxLayout(m_editorEmptyStateOverlay);
    emptyLayout->setContentsMargins(
        ui::UiMetrics::spacing32, ui::UiMetrics::spacing32,
        ui::UiMetrics::spacing32, ui::UiMetrics::spacing32);
    emptyLayout->addStretch(1);
    m_editorEmptyState = new ui::EmptyState(m_editorEmptyStateOverlay);
    m_editorEmptyState->setEyebrow(QStringLiteral("NO DESIGN OPEN"));
    m_emptyCreateButton = m_editorEmptyState->addActionButton(
        QStringLiteral("Create NoC Design"), QStringLiteral("primary"));
    m_emptyCreateButton->setObjectName(
        QStringLiteral("finepaper.emptyStateCreate"));
    m_emptyOpenButton = m_editorEmptyState->addActionButton(
        QStringLiteral("Open Design…"), QStringLiteral("quiet"));
    m_emptyOpenButton->setObjectName(
        QStringLiteral("finepaper.emptyStateOpen"));
    m_emptyInstallButton = m_editorEmptyState->addActionButton(
        QStringLiteral("Install NoC IP…"), QStringLiteral("primary"));
    m_emptyInstallButton->setObjectName(
        QStringLiteral("finepaper.emptyStateInstall"));
    emptyLayout->addWidget(m_editorEmptyState, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(2);
    m_editorStack->addWidget(m_editorEmptyStateOverlay);

    connect(m_emptyCreateButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::createDesign);
    connect(m_emptyOpenButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::openDesign);
    connect(m_emptyInstallButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::installPackage);
    m_viewRegistry->addView(
        {workbench::editorViewId,
         workbench::editorViewTitle,
         workbench::editorViewTabTitle},
        m_editorPage);

    m_domainConfigurationWorkspace = new DomainConfigurationWorkspace(
        m_centerViews);
    m_viewRegistry->addView(
        {workbench::domainConfigurationViewId,
         workbench::domainConfigurationViewTitle,
         workbench::domainConfigurationViewTabTitle},
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
         workbench::designExtensionsViewTitle,
         workbench::designExtensionsViewTabTitle},
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
        {workbench::performanceViewId,
         workbench::performanceViewTitle,
         workbench::performanceViewTabTitle},
        performance);

    auto* problemPage = new QWidget;
    auto* problemLayout = new QVBoxLayout(problemPage);
    problemLayout->setContentsMargins(
        ui::UiMetrics::spacing16, ui::UiMetrics::spacing16,
        ui::UiMetrics::spacing16, ui::UiMetrics::spacing16);
    auto* problemHeading = new QLabel(QStringLiteral("Problem Report"));
    problemHeading->setProperty("finepaperRole", QStringLiteral("title"));
    problemHeading->setFont(
        ui::fontForRole(ui::UiFontRole::Title, problemHeading->font()));
    m_problemReportStatus = new QLabel;
    m_problemReportStatus->setObjectName(
        QStringLiteral("finepaper.problemReportStatus"));
    m_problemReportStatus->setWordWrap(true);
    m_problemReportStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setStatusLabel(
        m_problemReportStatus,
        QStringLiteral("Run Validate to create a report for the current design."),
        QStringLiteral("muted"));
    m_problemReport = new QPlainTextEdit;
    m_problemReport->setReadOnly(true);
    m_problemReport->setPlaceholderText(
        QStringLiteral("Run validation to create a readable problem report."));
    problemLayout->addWidget(problemHeading);
    problemLayout->addWidget(m_problemReportStatus);
    problemLayout->addWidget(m_problemReport, 1);
    m_viewRegistry->addView(
        {workbench::problemReportViewId,
         workbench::problemReportViewTitle,
         workbench::problemReportViewTabTitle},
        problemPage);

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
    m_packageDock = new QDockWidget(QStringLiteral("NoC Library"), this);
    m_packageDock->setObjectName(workbench::packageDockName);
    m_packageDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("finepaper.packageLibraryContent"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(
        ui::UiMetrics::spacing12, ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing12, ui::UiMetrics::spacing12);

    m_currentDesignGroup = new QGroupBox(QStringLiteral("Current Design"));
    m_currentDesignGroup->setObjectName(
        QStringLiteral("finepaper.currentDesignSection"));
    m_currentDesignGroup->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* currentDesignLayout = new QVBoxLayout(m_currentDesignGroup);
    m_activePackageLabel = new QLabel(QStringLiteral("No design is open."));
    m_activePackageLabel->setObjectName(QStringLiteral("finepaper.activePackage"));
    m_activePackageLabel->setWordWrap(true);
    m_activePackageLabel->setTextFormat(Qt::PlainText);
    m_activePackageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_activePackageLabel->setProperty(
        "finepaperRole", QStringLiteral("subtitle"));
    currentDesignLayout->addWidget(m_activePackageLabel);

    m_activePackageAvailability = new QLabel(m_currentDesignGroup);
    m_activePackageAvailability->setObjectName(
        QStringLiteral("finepaper.activePackageAvailability"));
    m_activePackageAvailability->setTextFormat(Qt::PlainText);
    m_activePackageAvailability->setWordWrap(true);
    m_activePackageAvailability->setMinimumWidth(0);
    m_activePackageAvailability->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_activePackageAvailability->setProperty(
        "finepaperRole", QStringLiteral("warning"));
    m_activePackageAvailability->hide();
    currentDesignLayout->addWidget(m_activePackageAvailability);
    layout->addWidget(m_currentDesignGroup);
    layout->setAlignment(m_currentDesignGroup, Qt::AlignTop);

    auto* packageGroup = new QGroupBox(QStringLiteral("NoC IP Packages"));
    packageGroup->setObjectName(
        QStringLiteral("finepaper.packageLibrarySection"));
    packageGroup->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* packageLayout = new QVBoxLayout(packageGroup);
    m_availablePackagesLabel = new QLabel;
    m_availablePackagesLabel->setObjectName(
        QStringLiteral("finepaper.availablePackages"));
    m_availablePackagesLabel->setWordWrap(true);
    m_availablePackagesLabel->setTextFormat(Qt::PlainText);
    m_availablePackagesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_availablePackagesLabel->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    packageLayout->addWidget(m_availablePackagesLabel);

    auto* creationPackageLabel = new QLabel(
        QStringLiteral("Package for new design"), packageGroup);
    m_creationPackageSelector = new QComboBox(packageGroup);
    m_creationPackageSelector->setObjectName(
        QStringLiteral("finepaper.packageSelector"));
    m_creationPackageSelector->setAccessibleName(
        QStringLiteral("Package for new design"));
    m_creationPackageSelector->setAccessibleDescription(QStringLiteral(
        "Sets the initial Package in the New NoC Design dialog. It does not "
        "change the Package bound to the open design."));
    m_creationPackageSelector->setMinimumContentsLength(18);
    m_creationPackageSelector->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_creationPackageSelector->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    creationPackageLabel->setBuddy(m_creationPackageSelector);
    packageLayout->addWidget(creationPackageLabel);
    packageLayout->addWidget(m_creationPackageSelector);

    m_creationPackageDetails = new QLabel(packageGroup);
    m_creationPackageDetails->setObjectName(
        QStringLiteral("finepaper.creationPackageDetails"));
    m_creationPackageDetails->setTextFormat(Qt::PlainText);
    m_creationPackageDetails->setWordWrap(true);
    m_creationPackageDetails->setMinimumWidth(0);
    m_creationPackageDetails->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_creationPackageDetails->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    packageLayout->addWidget(m_creationPackageDetails);

    m_createDesignButton = new QPushButton(
        QStringLiteral("Create Another Design…"), packageGroup);
    m_createDesignButton->setObjectName(QStringLiteral("finepaper.createDesign"));
    m_createDesignButton->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    packageLayout->addWidget(m_createDesignButton);

    auto* packageButtons = new ui::ResponsiveActionLayout;
    packageButtons->setSpacing(ui::UiMetrics::spacing8);
    m_installPackageButton = new QPushButton(QStringLiteral("Install Package…"));
    m_installPackageButton->setObjectName(
        QStringLiteral("finepaper.installPackage"));
    m_reloadPackagesButton = new QPushButton(QStringLiteral("Reload Packages"));
    m_reloadPackagesButton->setObjectName(
        QStringLiteral("finepaper.reloadPackages"));
    m_reloadPackagesButton->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    packageButtons->addWidget(m_installPackageButton);
    packageButtons->addWidget(m_reloadPackagesButton);
    packageLayout->addLayout(packageButtons);

    m_endpointLibraryGroup = new QGroupBox(QStringLiteral("Endpoint Types"));
    m_endpointLibraryGroup->setObjectName(
        QStringLiteral("finepaper.endpointLibrarySection"));
    auto* endpointLayout = new QVBoxLayout(m_endpointLibraryGroup);
    auto* paletteHelp = new QLabel(
        QStringLiteral("Drag onto the canvas, or select a Router and activate "
                       "an Endpoint type with Enter."));
    paletteHelp->setWordWrap(true);
    paletteHelp->setProperty("finepaperRole", QStringLiteral("muted"));
    endpointLayout->addWidget(paletteHelp);
    m_endpointFilter = new QLineEdit;
    m_endpointFilter->setObjectName(
        QStringLiteral("finepaper.endpointPaletteFilter"));
    m_endpointFilter->setPlaceholderText(
        QStringLiteral("Filter Endpoint types…"));
    m_endpointFilter->setClearButtonEnabled(true);
    m_endpointFilter->setAccessibleName(
        QStringLiteral("Filter Endpoint types"));
    endpointLayout->addWidget(m_endpointFilter);
    m_endpointPalette = new EndpointPaletteList;
    m_endpointPalette->setObjectName(QStringLiteral("finepaper.endpointPalette"));
    m_endpointPalette->setAccessibleName(
        QStringLiteral("Endpoint types"));
    m_endpointPalette->setAccessibleDescription(QStringLiteral(
        "Drag a type to the canvas, or press Enter to add it to the "
        "selected Router."));
    m_endpointPalette->setDragEnabled(true);
    m_endpointPalette->setDragDropMode(QAbstractItemView::DragOnly);
    m_endpointPalette->setDefaultDropAction(Qt::CopyAction);
    m_endpointPalette->setSelectionMode(QAbstractItemView::SingleSelection);
    m_endpointPalette->setAlternatingRowColors(true);
    endpointLayout->addWidget(m_endpointPalette, 1);

    m_endpointPaletteHint = new QLabel;
    m_endpointPaletteHint->setObjectName(
        QStringLiteral("finepaper.endpointPaletteHint"));
    m_endpointPaletteHint->setWordWrap(true);
    m_endpointPaletteHint->setTextFormat(Qt::PlainText);
    m_endpointPaletteHint->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    endpointLayout->addWidget(m_endpointPaletteHint);
    m_addEndpointButton = new QPushButton(
        QStringLiteral("Add to selected Router"));
    m_addEndpointButton->setObjectName(
        QStringLiteral("finepaper.addEndpointToRouter"));
    m_addEndpointButton->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    endpointLayout->addWidget(m_addEndpointButton);
    layout->addWidget(m_endpointLibraryGroup, 1);
    layout->addWidget(packageGroup);
    layout->setAlignment(packageGroup, Qt::AlignTop);

    auto* packageScroll = new QScrollArea(m_packageDock);
    packageScroll->setObjectName(
        QStringLiteral("finepaper.packageLibraryScroll"));
    packageScroll->setAccessibleName(
        QStringLiteral("NoC IP and Endpoint library content"));
    packageScroll->setWidgetResizable(true);
    packageScroll->setFrameShape(QFrame::NoFrame);
    packageScroll->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    packageScroll->setWidget(content);
    m_packageDock->setWidget(packageScroll);
    addDockWidget(Qt::LeftDockWidgetArea, m_packageDock);

    connect(m_installPackageButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::installPackage);
    connect(m_reloadPackagesButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::reloadPackages);
    connect(m_createDesignButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::createDesign);
    connect(m_creationPackageSelector, &QComboBox::currentIndexChanged,
            this, [this] {
                updateCreationPackageDetails();
                updateEditorEmptyState();
            });
    connect(m_endpointPalette, &QListWidget::itemActivated,
            this, [this](QListWidgetItem* item) {
                addEndpointFromPalette(item);
            });
    connect(m_endpointPalette, &QListWidget::itemSelectionChanged,
            this, &FinepaperMainWindow::updateEndpointQuickAddState);
    connect(m_addEndpointButton, &QPushButton::clicked,
            this, [this] { addEndpointFromPalette(); });
    connect(m_endpointFilter, &QLineEdit::textChanged,
            this, &FinepaperMainWindow::filterEndpointPalette);
}

void FinepaperMainWindow::filterEndpointPalette(const QString& text) {
    if (!m_endpointPalette) {
        return;
    }
    const QString filter = text.trimmed();
    for (int row = 0; row < m_endpointPalette->count(); ++row) {
        QListWidgetItem* item = m_endpointPalette->item(row);
        const bool matches = filter.isEmpty()
            || item->text().contains(filter, Qt::CaseInsensitive)
            || item->data(Qt::UserRole).toString().contains(
                filter, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
    updateEndpointQuickAddState();
}

void FinepaperMainWindow::addEndpointFromPalette(QListWidgetItem* item) {
    QListWidgetItem* selectedItem = item ? item : m_endpointPalette->currentItem();
    if (!selectedItem || selectedItem->isHidden()) {
        statusBar()->showMessage(
            QStringLiteral("Choose an Endpoint type first."), 4000);
        return;
    }
    if (!m_selectedRouter) {
        statusBar()->showMessage(
            QStringLiteral(
                "Select a Router before adding an Endpoint from the library."),
            5000);
        return;
    }
    const attachment::CreateEndpointResult created = addEndpoint(
        selectedItem->data(Qt::UserRole).toString(),
        NocAttachmentTarget{*m_selectedRouter, std::nullopt});
    if (created.success && !created.endpointId.trimmed().isEmpty()) {
        m_nodeEditor->selectElements(
            {{ElementKind::Endpoint, created.endpointId}});
    }
}

void FinepaperMainWindow::updateEndpointQuickAddState() {
    if (!m_addEndpointButton || !m_endpointPaletteHint
        || !m_endpointPalette) {
        return;
    }
    QListWidgetItem* item = m_endpointPalette->currentItem();
    if (item && item->isHidden()) {
        item = nullptr;
    }
    const bool hasEditableDesign = m_design && packageForDesign()
        && !m_operationBusy;
    attachment::SlotResolution slotResolution;
    if (hasEditableDesign && m_selectedRouter && m_nodeEditor) {
        slotResolution = m_nodeEditor->endpointAttachmentAvailability(
            *m_selectedRouter);
    }
    const bool attachmentAvailable = !m_selectedRouter
        || slotResolution.kind != attachment::SlotResolutionKind::Rejected;
    const bool canAdd = hasEditableDesign && m_selectedRouter && item
        && attachmentAvailable;
    m_addEndpointButton->setEnabled(canAdd);

    if (!m_design) {
        m_endpointPaletteHint->setText(
            QStringLiteral("Create or open a design to add Endpoints."));
    } else if (!packageForDesign()) {
        m_endpointPaletteHint->setText(QStringLiteral(
            "Endpoint editing is unavailable until the design Package is loaded."));
    } else if (m_operationBusy) {
        m_endpointPaletteHint->setText(
            QStringLiteral("Wait for the current operation to finish."));
    } else if (!m_selectedRouter) {
        m_endpointPaletteHint->setText(
            QStringLiteral("Select a Router to enable keyboard quick-add."));
    } else if (!item) {
        m_endpointPaletteHint->setText(
            QStringLiteral("Choose an Endpoint type for %1.")
                .arg(routerId(*m_selectedRouter)));
    } else if (!attachmentAvailable) {
        m_endpointPaletteHint->setText(
            attachment::rejectionMessage(
                slotResolution.rejection, *m_selectedRouter));
    } else {
        m_endpointPaletteHint->setText(
            QStringLiteral("Ready to add %1 to %2.")
                .arg(item->text(), routerId(*m_selectedRouter)));
    }
}

void FinepaperMainWindow::createInspectorDock() {
    m_inspectorDock = new QDockWidget(QStringLiteral("Inspector"), this);
    m_inspectorDock->setObjectName(workbench::inspectorDockName);
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_inspectorScroll = new QScrollArea(m_inspectorDock);
    m_inspectorScroll->setObjectName(
        QStringLiteral("finepaper.inspectorScroll"));
    m_inspectorScroll->setWidgetResizable(true);
    m_inspectorScroll->setFrameShape(QFrame::NoFrame);
    m_inspectorScroll->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(m_inspectorScroll);
    content->setObjectName(QStringLiteral("finepaper.inspectorContent"));
    content->setMinimumWidth(0);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(
        ui::UiMetrics::spacing12, ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing12, ui::UiMetrics::spacing12);
    m_inspectorSummaryPanel = new ui::InspectorSummaryPanel(content);
    m_inspectorSummaryPanel->setDesignSummary({
        QStringLiteral("No design open"),
        QStringLiteral("Create or open a design to inspect it."),
        {}});
    layout->addWidget(m_inspectorSummaryPanel);

    m_endpointConfigurationGroup = new QWidget(content);
    m_endpointConfigurationGroup->setObjectName(
        QStringLiteral("finepaper.endpointConfigurationGroup"));
    m_endpointConfigurationGroup->setMinimumWidth(0);
    auto* endpointConfigurationLayout =
        new QVBoxLayout(m_endpointConfigurationGroup);
    endpointConfigurationLayout->setContentsMargins(0, 0, 0, 0);
    m_endpointConfigurationPanel = new EndpointConfigurationPanel(
        m_endpointConfigurationGroup);
    endpointConfigurationLayout->addWidget(m_endpointConfigurationPanel);
    m_endpointConfigurationGroup->setVisible(false);
    layout->addWidget(m_endpointConfigurationGroup);

    m_elementConfigurationGroup = new QWidget(content);
    m_elementConfigurationGroup->setObjectName(
        QStringLiteral("finepaper.elementConfigurationGroup"));
    m_elementConfigurationGroup->setMinimumWidth(0);
    auto* elementConfigurationLayout =
        new QVBoxLayout(m_elementConfigurationGroup);
    elementConfigurationLayout->setContentsMargins(0, 0, 0, 0);
    m_elementConfigurationPanel = new ElementConfigurationPanel(
        m_elementConfigurationGroup);
    elementConfigurationLayout->addWidget(m_elementConfigurationPanel);
    m_elementConfigurationGroup->setVisible(false);
    layout->addWidget(m_elementConfigurationGroup);

    m_inspectorDesignSettings = new ui::InspectorDesignSettings(content);

    m_topologyGroup = new QWidget(m_inspectorDesignSettings);
    m_topologyGroup->setObjectName(
        QStringLiteral("finepaper.meshTopologyGroup"));
    auto* topologyLayout = new QVBoxLayout(m_topologyGroup);
    topologyLayout->setContentsMargins(0, 0, 0, 0);
    topologyLayout->setSpacing(ui::UiMetrics::spacing8);
    auto* topologyTitle = new QLabel(
        QStringLiteral("Mesh topology"), m_topologyGroup);
    topologyTitle->setProperty(
        "finepaperRole", QStringLiteral("subtitle"));
    auto* topologyNote = new QLabel(
        QStringLiteral(
            "Routers and Router links are Mesh-managed. Resize the complete "
            "topology here."),
        m_topologyGroup);
    topologyNote->setObjectName(
        QStringLiteral("finepaper.inspectorMeshSummary"));
    topologyNote->setProperty("finepaperRole", QStringLiteral("muted"));
    topologyNote->setWordWrap(true);
    m_resizeMeshButton = new QPushButton(
        QStringLiteral("Resize Mesh…"), m_topologyGroup);
    m_resizeMeshButton->setObjectName(QStringLiteral("finepaper.resizeMesh"));
    m_resizeMeshButton->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    topologyLayout->addWidget(topologyTitle);
    topologyLayout->addWidget(topologyNote);
    topologyLayout->addWidget(m_resizeMeshButton);
    m_inspectorDesignSettings->addSection(m_topologyGroup);

    m_parameterGroup = new QWidget(m_inspectorDesignSettings);
    m_parameterGroup->setObjectName(QStringLiteral("finepaper.parameterGroup"));
    auto* parameterGroupLayout = new QVBoxLayout(m_parameterGroup);
    parameterGroupLayout->setContentsMargins(0, 0, 0, 0);
    parameterGroupLayout->setSpacing(ui::UiMetrics::spacing8);
    auto* parameterTitle = new QLabel(
        QStringLiteral("NoC parameters"), m_parameterGroup);
    parameterTitle->setProperty(
        "finepaperRole", QStringLiteral("subtitle"));
    parameterGroupLayout->addWidget(parameterTitle);
    m_parameterForm = new PackageParameterForm(
        QStringLiteral("finepaper.parameter"), m_parameterGroup);
    m_applyParametersButton = new QPushButton(QStringLiteral("Apply Parameters"));
    m_applyParametersButton->setObjectName(QStringLiteral("finepaper.applyParameters"));
    m_applyParametersButton->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    m_discardParametersButton = new QPushButton(
        QStringLiteral("Discard Unapplied Changes"));
    m_discardParametersButton->setObjectName(
        QStringLiteral("finepaper.discardParameters"));
    m_discardParametersButton->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    m_discardParametersButton->hide();
    auto* parameterButtons = new QVBoxLayout;
    parameterButtons->setSpacing(ui::UiMetrics::spacing8);
    parameterButtons->addWidget(m_applyParametersButton);
    parameterButtons->addWidget(m_discardParametersButton);
    parameterGroupLayout->addWidget(m_parameterForm);
    parameterGroupLayout->addLayout(parameterButtons);
    m_inspectorDesignSettings->addSection(m_parameterGroup);
    m_inspectorDesignSettings->setVisible(false);
    layout->addWidget(m_inspectorDesignSettings);
    layout->addStretch(1);

    m_inspectorScroll->setWidget(content);
    m_inspectorDock->setWidget(m_inspectorScroll);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    connect(m_applyParametersButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::applyParameters);
    connect(m_discardParametersButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::discardParameterDraft);
    m_parameterForm->valueChanged = [this] {
        if (m_updatingParameterForm) {
            return;
        }
        captureParameterDraft();
        updateUiState();
    };
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
            element,
            propertySet,
            effectiveValues);
        if (result.success) {
            discardPendingDomainChanges();
            m_elementConfigurationPanel->discardDraft(
                m_designSessionIdentity, element, propertySet);
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
            *m_design, element, propertySet);
        if (result.success) {
            discardPendingDomainChanges();
            m_elementConfigurationPanel->discardDraft(
                m_designSessionIdentity, element, propertySet);
        }
        adoptDesignResult(
            result,
            QStringLiteral("Reset Element Configuration"),
            DesignRefreshScope::InspectorOnly);
    };
    m_elementConfigurationPanel->draftStateChanged = [this] {
        if (!m_batchingInspectorDraftChanges) {
            updateUiState();
        }
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
        if (!m_batchingInspectorDraftChanges) {
            updateUiState();
        }
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
        const ElementRef attachment{
            ElementKind::EndpointAttachment, endpointId};
        if (!confirmDiscardElementDrafts(
                QStringLiteral("Changing the Endpoint type"), attachment)) {
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
            m_elementConfigurationPanel->discardDraftsForElement(
                m_designSessionIdentity, attachment);
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

    auto* domainScroll = new QScrollArea(m_domainDock);
    domainScroll->setObjectName(
        QStringLiteral("finepaper.domainManagerScroll"));
    domainScroll->setAccessibleName(QStringLiteral("Domain Manager content"));
    domainScroll->setWidgetResizable(true);
    domainScroll->setFrameShape(QFrame::NoFrame);
    domainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_domainManager = new DomainManagerPanel;
    domainScroll->setWidget(m_domainManager);
    m_domainDock->setWidget(domainScroll);
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
    m_domainManager->draftStateChanged = [this] {
        updateUiState();
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
    m_resultTabs->setObjectName(QStringLiteral("finepaper.resultTabs"));
    m_resultTabs->setAccessibleName(
        QStringLiteral("Diagnostics and generation results"));
    m_resultTabs->setDocumentMode(true);
    m_resultTabs->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);

    auto* diagnosticsPage = new QWidget;
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
    diagnosticsLayout->setContentsMargins(
        ui::UiMetrics::spacing8, ui::UiMetrics::spacing8,
        ui::UiMetrics::spacing8, ui::UiMetrics::spacing8);
    m_diagnosticsStatus = new QLabel;
    m_diagnosticsStatus->setObjectName(
        QStringLiteral("finepaper.diagnosticsStatus"));
    m_diagnosticsStatus->setAccessibleName(
        QStringLiteral("Diagnostic result status"));
    m_diagnosticsStatus->setWordWrap(true);
    m_diagnosticsStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setStatusLabel(
        m_diagnosticsStatus,
        QStringLiteral("No diagnostics have been published for this design."),
        QStringLiteral("muted"));
    diagnosticsLayout->addWidget(m_diagnosticsStatus);

    m_drcTable = new QTableWidget;
    m_drcTable->setObjectName(QStringLiteral("finepaper.drcTable"));
    m_drcTable->setAccessibleName(QStringLiteral("Design rule diagnostics"));
    m_drcTable->setColumnCount(4);
    m_drcTable->setHorizontalHeaderLabels({
        QStringLiteral("Severity"), QStringLiteral("Code"),
        QStringLiteral("Message"), QStringLiteral("Location")});
    m_drcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_drcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_drcTable->setMinimumHeight(0);
    m_drcTable->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_drcTable->horizontalHeader()->setStretchLastSection(true);
    diagnosticsLayout->addWidget(m_drcTable, 1);
    m_resultTabs->addTab(diagnosticsPage, workbench::drcTabTitle);

    m_activityLog = new QPlainTextEdit;
    m_activityLog->setObjectName(QStringLiteral("finepaper.activityLog"));
    m_activityLog->setAccessibleName(QStringLiteral("Workbench activity log"));
    m_activityLog->setReadOnly(true);
    m_activityLog->setMaximumBlockCount(5000);
    m_activityLog->setMinimumHeight(0);
    m_activityLog->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_resultTabs->addTab(m_activityLog, workbench::activityTabTitle);

    auto* outputPage = new QWidget;
    auto* outputLayout = new QVBoxLayout(outputPage);
    outputLayout->setContentsMargins(
        ui::UiMetrics::spacing8, ui::UiMetrics::spacing8,
        ui::UiMetrics::spacing8, ui::UiMetrics::spacing8);
    m_generationStatus = new QLabel;
    m_generationStatus->setObjectName(
        QStringLiteral("finepaper.generationStatus"));
    m_generationStatus->setAccessibleName(
        QStringLiteral("RTL generation result status"));
    m_generationStatus->setWordWrap(true);
    m_generationStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setStatusLabel(
        m_generationStatus,
        QStringLiteral("No RTL has been generated for this design revision."),
        QStringLiteral("muted"));
    outputLayout->addWidget(m_generationStatus);
    auto* outputControls = new QHBoxLayout;
    m_outputRoot = new QLineEdit(m_locations.defaultOutputRoot);
    m_outputRoot->setObjectName(QStringLiteral("finepaper.outputRoot"));
    m_browseOutputButton = new QPushButton(QStringLiteral("Browse…"));
    m_generateButton = new QPushButton(QStringLiteral("Generate RTL"));
    m_generateButton->setObjectName(QStringLiteral("finepaper.generateButton"));
    m_generateButton->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    auto* outputRootLabel = new QLabel(QStringLiteral("Output root"));
    outputRootLabel->setBuddy(m_outputRoot);
    outputControls->addWidget(outputRootLabel);
    outputControls->addWidget(m_outputRoot, 1);
    outputControls->addWidget(m_browseOutputButton);
    outputControls->addWidget(m_generateButton);
    outputLayout->addLayout(outputControls);

    auto* outputSplitter = new QSplitter(Qt::Vertical);
    m_artifactTable = new QTableWidget;
    m_artifactTable->setObjectName(QStringLiteral("finepaper.artifactTable"));
    m_artifactTable->setAccessibleName(
        QStringLiteral("Generated RTL artifacts"));
    m_artifactTable->setColumnCount(4);
    m_artifactTable->setHorizontalHeaderLabels({
        QStringLiteral("Artifact"), QStringLiteral("Type"),
        QStringLiteral("Path"), QStringLiteral("Primary")});
    m_artifactTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_artifactTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_artifactTable->setMinimumHeight(0);
    m_artifactTable->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_artifactTable->horizontalHeader()->setStretchLastSection(true);
    m_generationDetails = new QPlainTextEdit;
    m_generationDetails->setReadOnly(true);
    m_generationDetails->setMinimumHeight(0);
    m_generationDetails->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Ignored);
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
    m_newAction = new QAction(QStringLiteral("New NoC Design…"), this);
    m_newAction->setIconText(QStringLiteral("New Design"));
    m_newAction->setShortcut(QKeySequence::New);
    m_openAction = new QAction(QStringLiteral("Open…"), this);
    m_openAction->setIconText(QStringLiteral("Open"));
    m_openAction->setShortcut(QKeySequence::Open);
    m_saveAction = new QAction(QStringLiteral("Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAsAction = new QAction(QStringLiteral("Save As…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_installAction = new QAction(QStringLiteral("Install Package Directory…"), this);
    m_reloadAction = new QAction(QStringLiteral("Reload Packages"), this);
    m_validateAction = new QAction(QStringLiteral("Validate / DRC"), this);
    m_validateAction->setIconText(QStringLiteral("Validate"));
    m_validateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    m_generateAction = new QAction(QStringLiteral("Generate RTL"), this);
    m_generateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_resizeMeshAction = new QAction(QStringLiteral("Resize Mesh…"), this);
    m_resizeMeshAction->setObjectName(
        QStringLiteral("finepaper.resizeMeshAction"));
    m_resizeMeshAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    m_regularizeAction = new QAction(QStringLiteral("Regularize Layout"), this);
    m_regularizeAction->setObjectName(workbench::regularizeActionName);
    m_regularizeAction->setShortcut(QKeySequence(QStringLiteral("R")));
    m_regularizeAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_regularizeAction->setStatusTip(
        QStringLiteral("Restore Router and Endpoint positions to the topology layout"));
    m_fitAction = new QAction(QStringLiteral("Fit NoC in View"), this);
    m_fitAction->setIconText(QStringLiteral("Fit View"));
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
    m_reduceMotionAction = new QAction(
        QStringLiteral("Reduce Motion"), this);
    m_reduceMotionAction->setObjectName(
        workbench::reducedMotionActionName);
    m_reduceMotionAction->setCheckable(true);
    m_reduceMotionAction->setChecked(
        QSettings().value(workbench::reducedMotionSetting, false).toBool());
    m_nodeEditor->setReducedMotion(m_reduceMotionAction->isChecked());
    QMainWindow::DockOptions initialDockOptions = dockOptions();
    initialDockOptions.setFlag(
        QMainWindow::AnimatedDocks,
        !m_reduceMotionAction->isChecked());
    setDockOptions(initialDockOptions);
    m_reduceMotionAction->setStatusTip(QStringLiteral(
        "Replace canvas pulses, fades, and Dock transitions with static changes"));
    m_resetWorkbenchLayoutAction = new QAction(
        QStringLiteral("Reset Workbench Layout"), this);
    m_resetWorkbenchLayoutAction->setObjectName(
        workbench::resetWorkbenchLayoutActionName);
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
    connect(m_regularizeAction, &QAction::triggered, this, [this] {
        QMessageBox confirmation(
            QMessageBox::Warning,
            QStringLiteral("Regularize canvas layout?"),
            QStringLiteral(
                "This resets every custom Router and Endpoint position in the "
                "current canvas workspace. The NoC design itself is unchanged."),
            QMessageBox::Yes | QMessageBox::Cancel,
            this);
        confirmation.setObjectName(
            QStringLiteral("finepaper.regularizeLayoutConfirmation"));
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmation.setEscapeButton(QMessageBox::Cancel);
        if (QPushButton* regularizeButton = qobject_cast<QPushButton*>(
                confirmation.button(QMessageBox::Yes))) {
            regularizeButton->setText(QStringLiteral("Regularize Layout"));
            regularizeButton->setProperty(
                "finepaperRole", QStringLiteral("danger"));
        }
        if (confirmation.exec() == QMessageBox::Yes) {
            const TopologyWorkspaceRegularizeResult result =
                m_nodeEditor->regularizeLayout();
            if (result == TopologyWorkspaceRegularizeResult::Saved) {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Canvas positions restored to the topology layout."),
                    5000);
            } else if (result
                       == TopologyWorkspaceRegularizeResult::SaveFailed) {
                showWorkspaceStatusMessage();
            }
        }
    });
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
    connect(m_reduceMotionAction, &QAction::toggled,
            this, [this](bool reduced) {
                QSettings().setValue(
                    workbench::reducedMotionSetting, reduced);
                m_nodeEditor->setReducedMotion(reduced);
                QMainWindow::DockOptions options = dockOptions();
                options.setFlag(QMainWindow::AnimatedDocks, !reduced);
                setDockOptions(options);
                statusBar()->showMessage(
                    reduced
                        ? QStringLiteral("Reduced motion enabled.")
                        : QStringLiteral("Motion feedback enabled."),
                    4000);
            });
    connect(m_resetWorkbenchLayoutAction, &QAction::triggered,
            this, [this] {
                resetWorkbenchLayout();
                statusBar()->showMessage(
                    QStringLiteral(
                        "Workbench layout restored. Diagnostics open when needed."),
                    4000);
            });

    QAction* packagePanelAction = m_packageDock->toggleViewAction();
    packagePanelAction->setObjectName(workbench::packageToggleActionName);
    packagePanelAction->setText(QStringLiteral("NoC IP && Endpoint Library"));
    packagePanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    packagePanelAction->setStatusTip(QStringLiteral("Show or hide the NoC IP and Endpoint panel"));
    connect(packagePanelAction, &QAction::triggered,
            this, [this](bool visible) {
                if (!visible) {
                    return;
                }
                QTimer::singleShot(0, m_packageDock, [this] {
                    QWidget* focusTarget = m_creationPackageSelector
                            && m_creationPackageSelector->isEnabled()
                        ? static_cast<QWidget*>(m_creationPackageSelector)
                        : static_cast<QWidget*>(m_installPackageButton);
                    if (focusTarget && focusTarget->isEnabled()
                        && focusTarget->isVisibleTo(m_packageDock)) {
                        focusTarget->setFocus(Qt::ShortcutFocusReason);
                    }
                });
            });

    QAction* inspectorPanelAction = m_inspectorDock->toggleViewAction();
    inspectorPanelAction->setObjectName(workbench::inspectorToggleActionName);
    inspectorPanelAction->setText(QStringLiteral("Inspector"));
    inspectorPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    inspectorPanelAction->setStatusTip(QStringLiteral("Show or hide the right Inspector panel"));

    QAction* domainManagerPanelAction = m_domainDock->toggleViewAction();
    domainManagerPanelAction->setObjectName(
        workbench::domainManagerToggleActionName);
    domainManagerPanelAction->setText(QStringLiteral("Domain Manager"));
    domainManagerPanelAction->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    domainManagerPanelAction->setStatusTip(
        QStringLiteral("Show or hide the Package-driven Domain Manager"));

    QAction* resultsPanelAction = m_resultsDock->toggleViewAction();
    resultsPanelAction->setObjectName(workbench::resultsToggleActionName);
    resultsPanelAction->setText(QStringLiteral("Diagnostics && Output"));
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
    designMenu->addSeparator();
    designMenu->addAction(m_regularizeAction);
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
    viewMenu->addAction(m_fitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_reduceMotionAction);
    viewMenu->addAction(m_resetWorkbenchLayoutAction);
    connect(m_centerViews, &QTabWidget::currentChanged, this, [this, viewGroup](int index) {
        Q_UNUSED(index);
        const QString id = m_viewRegistry->currentViewId();
        for (QAction* action : viewGroup->actions()) {
            action->setChecked(action->data().toString() == id);
        }
    });

    QToolBar* toolbar = addToolBar(QStringLiteral("NoC Workbench"));
    toolbar->setObjectName(workbench::mainToolbarName);
    toolbar->setAccessibleName(QStringLiteral("Design and canvas actions"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(m_validateAction);
    toolbar->addAction(m_generateAction);
    toolbar->addSeparator();
    toolbar->addAction(m_selectCanvasAction);
    toolbar->addAction(m_panCanvasAction);
    toolbar->addAction(m_fitAction);
    if (auto* button = qobject_cast<QToolButton*>(
            toolbar->widgetForAction(m_generateAction))) {
        button->setProperty("finepaperRole", QStringLiteral("primary"));
    }
    if (auto* button = qobject_cast<QToolButton*>(
            toolbar->widgetForAction(m_selectCanvasAction))) {
        button->setProperty("finepaperRole", QStringLiteral("canvasMode"));
    }
    if (auto* button = qobject_cast<QToolButton*>(
            toolbar->widgetForAction(m_panCanvasAction))) {
        button->setProperty("finepaperRole", QStringLiteral("canvasMode"));
    }
    m_domainLayerSeparator = toolbar->addSeparator();
    m_domainLayerLabel = new QLabel(QStringLiteral("Domain layer"), toolbar);
    m_domainLayerLabel->setObjectName(
        QStringLiteral("finepaper.domainLayerLabel"));
    m_domainLayerLabel->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    m_domainLayerLabelAction = toolbar->addWidget(m_domainLayerLabel);
    m_domainLayerSelector = new QComboBox(toolbar);
    m_domainLayerSelector->setObjectName(workbench::domainLayerSelectorName);
    m_domainLayerSelector->setMinimumContentsLength(12);
    m_domainLayerSelector->setMaximumWidth(240);
    m_domainLayerSelector->addItem(QStringLiteral("None"), QString());
    m_domainLayerSelector->setEnabled(false);
    m_domainLayerSelector->setAccessibleName(QStringLiteral("Canvas Domain layer"));
    m_domainLayerLabel->setBuddy(m_domainLayerSelector);
    m_domainLayerSelectorAction = toolbar->addWidget(m_domainLayerSelector);
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

    auto* panelNavigationButton = new QToolButton(toolbar);
    panelNavigationButton->setObjectName(workbench::panelNavigationButtonName);
    panelNavigationButton->setText(QStringLiteral("Panels"));
    panelNavigationButton->setAccessibleName(
        QStringLiteral("Open a workbench panel"));
    panelNavigationButton->setPopupMode(QToolButton::InstantPopup);
    auto* panelNavigationMenu = new QMenu(
        QStringLiteral("Workbench Panels"), panelNavigationButton);
    panelNavigationButton->setMenu(panelNavigationMenu);
    const auto addPanelNavigation = [panelNavigationMenu](
        const QString& objectName,
        const QString& text,
        QDockWidget* dock,
        QWidget* primaryFocusTarget,
        QWidget* fallbackFocusTarget) {
        auto* action = new QAction(text, panelNavigationMenu);
        action->setObjectName(objectName);
        panelNavigationMenu->addAction(action);
        QObject::connect(
            action, &QAction::triggered, dock,
            [dock, primaryFocusTarget, fallbackFocusTarget] {
                dock->show();
                dock->raise();
                QWidget* focusTarget = primaryFocusTarget
                        && primaryFocusTarget->isEnabled()
                        && primaryFocusTarget->isVisibleTo(dock)
                    ? primaryFocusTarget : fallbackFocusTarget;
                if (focusTarget && focusTarget->isEnabled()) {
                    focusTarget->setFocus(Qt::ShortcutFocusReason);
                }
            });
    };
    addPanelNavigation(
        workbench::packageNavigationActionName,
        QStringLiteral("Package Library"), m_packageDock,
        m_creationPackageSelector, m_installPackageButton);
    addPanelNavigation(
        workbench::inspectorNavigationActionName,
        QStringLiteral("Inspector"), m_inspectorDock,
        m_resizeMeshButton, m_inspectorDock->widget());
    addPanelNavigation(
        workbench::domainNavigationActionName,
        QStringLiteral("Domain Manager"), m_domainDock,
        m_domainManager->findChild<QComboBox*>(
            QStringLiteral("finepaper.domainManager.typeSelector")),
        m_domainManager);
    panelNavigationMenu->addSeparator();
    addPanelNavigation(
        workbench::resultsNavigationActionName,
        QStringLiteral("Diagnostics && Output"), m_resultsDock,
        m_resultTabs, m_resultsDock->widget());
    toolbar->addSeparator();
    toolbar->addWidget(panelNavigationButton);
}

void FinepaperMainWindow::restoreWorkbenchState() {
    QSettings settings;
    const QByteArray geometry = settings.value(workbench::geometrySetting).toByteArray();
    const QByteArray state = settings.value(workbench::windowStateSetting).toByteArray();
    if (!geometry.isEmpty() && !restoreGeometry(geometry)) {
        settings.remove(workbench::geometrySetting);
        resize(workbench::defaultWindowWidth, workbench::defaultWindowHeight);
    }
    if (!state.isEmpty() && !restoreState(state)) {
        settings.remove(workbench::windowStateSetting);
        resetWorkbenchLayout();
        statusBar()->showMessage(
            QStringLiteral(
                "The saved workbench layout was invalid and has been reset."),
            6000);
    }
    selectCenterView(settings.value(workbench::centerViewSetting,
                                    workbench::editorViewId).toString());
    m_resultTabs->setCurrentIndex(std::clamp(
        settings.value(workbench::resultTabSetting, 0).toInt(),
        0,
        (std::max)(0, m_resultTabs->count() - 1)));
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
    discardPendingInspectorDrafts();
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
    FinepaperApplication candidateApplication = m_application;
    const PackageCatalogReloadResult reload =
        candidateApplication.reloadPackages(m_locations.packageRoots);
    const QVector<Diagnostic>& diagnostics = reload.diagnostics;
    if (reload.committed()) {
        if (!confirmDiscardPendingDomainChanges(
                QStringLiteral("Reloading Packages"))) {
            return;
        }
        if (!confirmDiscardPendingInspectorDrafts(
                QStringLiteral("Reloading Packages"))) {
            return;
        }
        // Panels borrow PackageDefinition objects from the current catalog.
        // Consume the authorized drafts while those borrowed contexts are
        // still valid, detach every borrowed pointer, then replace the catalog
        // as one transaction.
        discardPendingDomainChanges();
        discardPendingInspectorDrafts();
        detachPackageBorrowingPanels();
        m_application = std::move(candidateApplication);
        advanceCatalogRevision();
    }
    // Even a rejected reload must re-probe the retained snapshot's files so
    // stale runtime availability is never presented as executable.
    if (m_design) {
        refreshDesignViews();
    } else {
        updatePackageControls();
    }
    if (!diagnostics.isEmpty()) {
        showDiagnostics(
            diagnostics, QStringLiteral("Package discovery"), false);
    } else if (!m_diagnosticsStamp
               && m_diagnosticsSource == QStringLiteral("Package discovery")) {
        populateDiagnostics({}, QStringLiteral("Package discovery"));
    }
    if (reload.catalogFatal()) {
        m_resultTabs->setCurrentIndex(0);
        showResultsDock();
        QString summary = QStringLiteral("Package reload failed.");
        for (const Diagnostic& diagnostic : diagnostics) {
            if (diagnostic.severity == QStringLiteral("error")) {
                summary = QStringLiteral("Package reload failed: %1").arg(diagnostic.message);
                break;
            }
        }
        appendActivity(summary);
        statusBar()->showMessage(summary);
    } else if (!reload.committed()) {
        m_resultTabs->setCurrentIndex(0);
        showResultsDock();
        const QString summary = QStringLiteral(
            "Package reload kept the previous catalog because all %1 discovered %2 were rejected.")
            .arg(QString::number(reload.rejectedCount),
                 reload.rejectedCount == 1
                     ? QStringLiteral("candidate")
                     : QStringLiteral("candidates"));
        appendActivity(summary);
        statusBar()->showMessage(summary);
    } else if (m_design && !packageForDesign()) {
        const qsizetype packageCount = runtimePackages().size();
        appendActivity(
            QStringLiteral("Reloaded %1 NoC IP %2; the current design Package is missing.")
                .arg(QString::number(packageCount),
                     packageCount == 1
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
        const QString summary = reload.rejectedCount == 0
            ? QStringLiteral("Loaded %1 NoC IP %2.")
                  .arg(QString::number(packageCount), packageNoun)
            : QStringLiteral("Loaded %1 NoC IP %2; isolated %3 rejected %4.")
                  .arg(QString::number(packageCount),
                       packageNoun,
                       QString::number(reload.rejectedCount),
                       reload.rejectedCount == 1
                           ? QStringLiteral("candidate")
                           : QStringLiteral("candidates"));
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
    const PackageCatalogReloadResult reload = candidateApplication.reloadPackages(
        candidateLocations.packageRoots);
    const QVector<Diagnostic>& diagnostics = reload.diagnostics;
    if (!reload.committed()) {
        showDiagnostics(reload.diagnostics, QStringLiteral("Install Package"));
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
    if (!installedPackage
        || normalizedAbsolutePath(installedPackage->rootPath)
            != normalizedAbsolutePath(package.package->rootPath)) {
        if (!reload.diagnostics.isEmpty()) {
            showDiagnostics(
                reload.diagnostics, QStringLiteral("Install Package"), false);
        }
        QMessageBox::warning(
            this,
            QStringLiteral("NoC IP Package was not installed"),
            QStringLiteral("The catalog did not accept %1 from:\n%2\n\n"
                           "Resolve the reported Package conflict or validation "
                           "errors, then try again. No Package roots were changed.")
                .arg(package.package->key(), package.package->rootPath));
        appendActivity(
            QStringLiteral("Did not install NoC IP Package %1; its selected source was rejected.")
                .arg(package.package->key()));
        statusBar()->showMessage(
            QStringLiteral("NoC IP Package was not installed."));
        return false;
    }

    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Installing a Package"))) {
        return false;
    }
    if (!confirmDiscardPendingInspectorDrafts(
            QStringLiteral("Installing a Package"))) {
        return false;
    }
    discardPendingDomainChanges();
    discardPendingInspectorDrafts();
    detachPackageBorrowingPanels();

    m_application = std::move(candidateApplication);
    m_locations = std::move(candidateLocations);
    advanceCatalogRevision();

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
    if (!diagnostics.isEmpty()) {
        showDiagnostics(diagnostics, QStringLiteral("Install Package"), false);
    } else if (!m_diagnosticsStamp
               && (m_diagnosticsSource == QStringLiteral("Package discovery")
                   || m_diagnosticsSource == QStringLiteral("Install Package"))) {
        populateDiagnostics({}, QStringLiteral("Install Package"));
    }

    appendActivity(QStringLiteral("Installed runtime Package %1 from %2.")
                       .arg(package.package->key(), package.package->rootPath));
    statusBar()->showMessage(
        QStringLiteral("Installed NoC IP %1.").arg(package.package->key()));
    return true;
}

void FinepaperMainWindow::updatePackageControls() {
    const QString previouslySelectedPackage = m_creationPackageSelector
        ? m_creationPackageSelector->currentData().toString()
        : QString();
    m_runtimeAvailablePackageKeys.clear();
    QVector<const PackageDefinition*> runnablePackages;
    runnablePackages.reserve(m_application.packages().size());
    for (const PackageDefinition& package : m_application.packages()) {
        const PackageLoadResult loaded = loadPackage(package.rootPath);
        if (!loaded.success || !loaded.package
            || loaded.package->key() != package.key()
            || normalizedAbsolutePath(loaded.package->rootPath)
                != normalizedAbsolutePath(package.rootPath)) {
            continue;
        }
        m_runtimeAvailablePackageKeys.insert(package.key());
        runnablePackages.append(&package);
    }
    const auto packagePointerDisplayLess = [](
        const PackageDefinition* lhs,
        const PackageDefinition* rhs) {
        return packageDisplayLess(*lhs, *rhs);
    };
    std::sort(
        runnablePackages.begin(), runnablePackages.end(),
        packagePointerDisplayLess);

    QStringList availablePackages;
    availablePackages.reserve(runnablePackages.size());
    for (const PackageDefinition* package : runnablePackages) {
        availablePackages.append(
            QStringLiteral("%1 — %2")
                .arg(package->name)
                .arg(package->key()));
    }
    if (availablePackages.isEmpty()) {
        m_availablePackagesLabel->setText(
            QStringLiteral("No runnable NoC IP Package is available."));
        m_availablePackagesLabel->setToolTip(
            QStringLiteral("Use Install to add or repair a runtime NoC IP Package."));
    } else {
        m_availablePackagesLabel->setText(
            availablePackages.size() == 1
                ? QStringLiteral("1 NoC IP Package available.")
                : QStringLiteral("%1 NoC IP Packages available.")
                      .arg(availablePackages.size()));
        m_availablePackagesLabel->setToolTip(availablePackages.join(QLatin1Char('\n')));
    }

    if (m_creationPackageSelector) {
        const PackageDefinition* activeRuntimePackage = runtimePackageForDesign();
        QString preferredPackage = previouslySelectedPackage;
        if (!m_runtimeAvailablePackageKeys.contains(preferredPackage)) {
            preferredPackage = activeRuntimePackage
                ? activeRuntimePackage->key()
                : QString();
        }
        const QSignalBlocker blocker(m_creationPackageSelector);
        m_creationPackageSelector->clear();
        for (const PackageDefinition* package : runnablePackages) {
            m_creationPackageSelector->addItem(
                package->name,
                package->key());
            m_creationPackageSelector->setItemData(
                m_creationPackageSelector->count() - 1,
                QStringLiteral("%1 — %2")
                    .arg(package->name)
                    .arg(package->key()),
                Qt::ToolTipRole);
            m_creationPackageSelector->setItemData(
                m_creationPackageSelector->count() - 1,
                QStringLiteral("%1, %2")
                    .arg(package->name, package->key()),
                Qt::AccessibleTextRole);
        }
        if (m_creationPackageSelector->count() == 0) {
            m_creationPackageSelector->addItem(
                QStringLiteral("No runnable Package"), QString());
        } else {
            const int preferredIndex =
                m_creationPackageSelector->findData(preferredPackage);
            m_creationPackageSelector->setCurrentIndex(
                preferredIndex >= 0 ? preferredIndex : 0);
        }
        m_creationPackageSelector->setEnabled(
            !m_runtimeAvailablePackageKeys.isEmpty() && !m_operationBusy);
        updateCreationPackageDetails();
    }

    if (!m_design) {
        m_activePackageLabel->setText(QStringLiteral("No design is open."));
        m_activePackageLabel->setToolTip({});
        m_activePackageAvailability->clear();
        m_activePackageAvailability->hide();
    } else if (const PackageDefinition* package = packageForDesign()) {
        if (runtimePackageByKey(package->key())) {
            m_activePackageLabel->setText(
                QStringLiteral("%1 — %2").arg(package->name, package->key()));
            m_activePackageLabel->setToolTip(package->rootPath);
            m_activePackageAvailability->clear();
            m_activePackageAvailability->hide();
        } else {
            m_activePackageLabel->setText(
                QStringLiteral("%1 — %2 (runtime unavailable)")
                    .arg(package->name, package->key()));
            const QString recovery = QStringLiteral(
                "Runtime unavailable. Reload or reinstall this exact Package "
                "before Validate or Generate RTL.");
            m_activePackageAvailability->setText(recovery);
            m_activePackageAvailability->setAccessibleDescription(recovery);
            m_activePackageAvailability->show();
            m_activePackageLabel->setToolTip(QStringLiteral(
                "Retained metadata from %1 keeps editing available.")
                .arg(package->rootPath));
        }
    } else {
        m_activePackageLabel->setText(
            QStringLiteral("Package not loaded: %1@%2 (design is read-only)")
                .arg(m_design->package.id, m_design->package.version));
        const QString recovery = QStringLiteral(
            "Install this exact Package ID and version to restore editing.");
        m_activePackageAvailability->setText(recovery);
        m_activePackageAvailability->setAccessibleDescription(recovery);
        m_activePackageAvailability->show();
        m_activePackageLabel->setToolTip(recovery);
    }
    updateEndpointPalette();
    updateDomainLayerControls();
    updateDomainManager();
    updateDesignExtensionsWorkspace();
    updateUiState();
}

void FinepaperMainWindow::updateCreationPackageDetails() {
    if (!m_creationPackageSelector || !m_creationPackageDetails) {
        return;
    }
    const PackageDefinition* package = runtimePackageByKey(
        m_creationPackageSelector->currentData().toString());
    if (!package) {
        const QString unavailable = QStringLiteral(
            "Install or repair a runnable Package before creating a design.");
        m_creationPackageDetails->setText(unavailable);
        m_creationPackageDetails->setAccessibleDescription(unavailable);
        m_creationPackageSelector->setToolTip(unavailable);
        return;
    }

    const QString details = QStringLiteral(
        "%1\nMesh %2–%3 rows × %4–%5 columns")
        .arg(package->key(),
             QString::number(package->mesh.minimumRows),
             QString::number(package->mesh.maximumRows),
             QString::number(package->mesh.minimumColumns),
             QString::number(package->mesh.maximumColumns));
    m_creationPackageDetails->setText(details);
    m_creationPackageDetails->setAccessibleDescription(
        QStringLiteral("Selected Package: %1. %2")
            .arg(package->name, details));
    m_creationPackageSelector->setToolTip(
        QStringLiteral(
            "%1\n%2\nSets the initial choice for New NoC Design. The open "
            "design keeps its own bound Package.")
            .arg(package->name, details));
}

void FinepaperMainWindow::detachPackageBorrowingPanels() {
    // These panels deliberately borrow catalog objects to avoid duplicating
    // large schemas. Their contexts must not span replacement of
    // m_application, which owns the catalog storage.
    if (m_domainManager) {
        m_domainManager->setContext(nullptr, nullptr, nullptr, {});
    }
    if (m_elementConfigurationPanel) {
        m_elementConfigurationPanel->setContext(
            nullptr, nullptr, std::nullopt, {}, m_operationBusy);
    }
    if (m_endpointConfigurationPanel) {
        m_endpointConfigurationPanel->setContext(
            nullptr, nullptr, {}, std::nullopt, m_operationBusy,
            currentDesignStamp().catalogRevision);
    }
}

void FinepaperMainWindow::updateEditorEmptyState() {
    if (!m_editorEmptyStateOverlay || !m_editorEmptyState) {
        return;
    }
    const bool hasDesign = m_design.has_value();
    const bool hasRunnablePackages = !m_runtimeAvailablePackageKeys.isEmpty();
    m_editorEmptyStateOverlay->setVisible(!hasDesign);
    if (m_editorStack) {
        m_editorStack->setCurrentWidget(
            hasDesign ? static_cast<QWidget*>(m_nodeEditor)
                      : m_editorEmptyStateOverlay);
    }
    if (hasDesign) {
        return;
    }
    if (hasRunnablePackages) {
        m_editorEmptyState->setTitle(QStringLiteral("Start a NoC design"));
        const QString selectedPackage = m_creationPackageSelector
            ? m_creationPackageSelector->currentData().toString()
            : QString();
        m_editorEmptyState->setDescription(
            QStringLiteral(
                "New designs will start with %1. Confirm the Package and Mesh "
                "in the creation dialog, or open an existing design. Package "
                "rules remain the source of truth for Endpoint types, "
                "parameters, and Domains.")
                .arg(selectedPackage));
    } else {
        m_editorEmptyState->setTitle(
            QStringLiteral("Install a NoC IP Package first"));
        m_editorEmptyState->setDescription(QStringLiteral(
            "Finepaper needs a runnable Package before it can create a new "
            "design. You can still open an existing design for inspection, or "
            "install a Package directory now."));
    }
    if (m_emptyCreateButton) {
        m_emptyCreateButton->setVisible(hasRunnablePackages);
        m_emptyCreateButton->setEnabled(
            hasRunnablePackages && !m_operationBusy);
    }
    if (m_emptyInstallButton) {
        m_emptyInstallButton->setVisible(!hasRunnablePackages);
        m_emptyInstallButton->setEnabled(!m_operationBusy);
    }
    if (m_emptyOpenButton) {
        m_emptyOpenButton->setEnabled(!m_operationBusy);
    }
    m_editorEmptyStateOverlay->raise();
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
                    domain_text::domainTypeDisplayText(type),
                    type.id);
            }
        }
        const int restoredIndex = m_domainLayerSelector->findData(restoredDomainType);
        m_domainLayerSelector->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
    }

    const bool hasDomainLayers = m_design && package && !package->domainTypes.isEmpty();
    m_domainLayerSelector->setEnabled(hasDomainLayers);
    m_domainLayerSelector->setVisible(hasDomainLayers);
    if (m_domainLayerLabel) {
        m_domainLayerLabel->setVisible(hasDomainLayers);
    }
    if (m_domainLayerSeparator) {
        m_domainLayerSeparator->setVisible(hasDomainLayers);
    }
    if (m_domainLayerLabelAction) {
        m_domainLayerLabelAction->setVisible(hasDomainLayers);
    }
    if (m_domainLayerSelectorAction) {
        m_domainLayerSelectorAction->setVisible(hasDomainLayers);
    }
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
    filterEndpointPalette(
        m_endpointFilter ? m_endpointFilter->text() : QString());
    updateUiState();
}

void FinepaperMainWindow::updateUiState() {
    const bool hasDesign = m_design.has_value();
    const bool hasDesignMetadata = hasDesign && packageForDesign();
    const bool hasDesignRuntime = hasDesign && runtimePackageForDesign();
    const bool hasRunnablePackages = !m_runtimeAvailablePackageKeys.isEmpty();
    const bool hasInspectorDrafts = hasPendingInspectorDrafts();
    const bool hasDomainDrafts =
        (m_domainManager
         && m_domainManager->hasPendingAssignmentChanges())
        || (m_domainConfigurationWorkspace
            && m_domainConfigurationWorkspace->hasPendingChanges());
    const bool hasPendingDrafts = hasInspectorDrafts || hasDomainDrafts;
    const bool hasParameterDraft = m_parameterDraft
        && m_parameterDraft->designIdentity == m_designSessionIdentity;
    setWindowModified(m_dirty || hasPendingDrafts);
    updateEditorEmptyState();

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
            hasDesign && (m_dirty || hasPendingDrafts) && !m_operationBusy);
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
    if (m_selectCanvasAction) {
        m_selectCanvasAction->setEnabled(hasDesign && !m_operationBusy);
    }
    if (m_panCanvasAction) {
        m_panCanvasAction->setEnabled(hasDesign && !m_operationBusy);
    }
    if (!hasDesign && m_selectCanvasAction && m_panCanvasAction) {
        m_selectCanvasAction->setChecked(false);
        m_panCanvasAction->setChecked(false);
    } else if (hasDesign && m_selectCanvasAction && m_panCanvasAction
               && !m_selectCanvasAction->isChecked()
               && !m_panCanvasAction->isChecked()) {
        m_panCanvasAction->setChecked(true);
    }
    if (m_createDesignButton) {
        m_createDesignButton->setEnabled(hasRunnablePackages && !m_operationBusy);
        m_createDesignButton->setVisible(hasDesign);
        m_createDesignButton->setToolTip(
            hasRunnablePackages
                ? QStringLiteral("Choose a NoC IP and create a new design.")
                : QStringLiteral("Install or repair a runnable NoC IP Package before "
                                 "creating a design."));
    }
    if (m_currentDesignGroup) {
        m_currentDesignGroup->setVisible(hasDesign);
    }
    if (m_creationPackageSelector) {
        m_creationPackageSelector->setEnabled(
            hasRunnablePackages && !m_operationBusy);
    }
    if (m_endpointLibraryGroup) {
        m_endpointLibraryGroup->setVisible(hasDesignMetadata);
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
        m_parameterGroup->setVisible(
            hasDesignMetadata && m_parameterForm
            && !m_parameterForm->isEmpty());
        m_parameterGroup->setEnabled(hasDesignMetadata);
    }
    if (m_parameterForm) {
        m_parameterForm->setEnabled(
            hasDesignMetadata && !m_operationBusy
            && !m_parameterDraftConflict);
    }
    if (m_topologyGroup) {
        m_topologyGroup->setVisible(hasDesign);
    }
    if (m_inspectorDesignSettings) {
        m_inspectorDesignSettings->setVisible(hasDesign);
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
            && m_parameterForm->isModified() && !m_parameterDraftConflict);
    }
    if (m_discardParametersButton) {
        m_discardParametersButton->setVisible(hasParameterDraft);
        m_discardParametersButton->setEnabled(
            hasParameterDraft && !m_operationBusy);
    }
    if (m_inspectorDesignSettings) {
        const QString draftNotice = !hasParameterDraft
            ? QString()
            : m_parameterDraftConflict
            ? QStringLiteral(
                  "NoC parameter draft conflict. Discard the preserved draft "
                  "before editing the current values.")
            : m_parameterForm && m_parameterForm->locallyValid()
            ? QStringLiteral(
                  "Unapplied NoC parameter changes. Apply or discard them "
                  "before Save, Validate, or Generate RTL.")
            : QStringLiteral(
                  "The NoC parameter draft has validation errors. Correct or "
                  "discard it before continuing.");
        m_inspectorDesignSettings->setDraftNotice(
            draftNotice,
            m_parameterDraftConflict
                ? QStringLiteral("error")
                : QStringLiteral("warning"));
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
    updateEndpointQuickAddState();
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

void FinepaperMainWindow::captureParameterDraft() {
    if (m_updatingParameterForm || !m_parameterForm || !m_design
        || m_designSessionIdentity.isEmpty()) {
        return;
    }
    if (!m_parameterForm->isModified()) {
        if (m_parameterDraft
            && m_parameterDraft->designIdentity
                == m_designSessionIdentity) {
            m_parameterDraft.reset();
        }
        return;
    }
    m_parameterDraft = ParameterDraftState{
        m_designSessionIdentity,
        m_design->parameters,
        m_parameterForm->schemaIdentity(),
        m_parameterForm->draftValues(),
    };
}

void FinepaperMainWindow::discardParameterDraft() {
    m_parameterDraft.reset();
    m_parameterDraftConflict = false;
    if (m_parameterForm) {
        m_updatingParameterForm = true;
        m_parameterForm->setValues(
            m_design ? m_design->parameters : QJsonObject{});
        m_updatingParameterForm = false;
    }
    if (!m_batchingInspectorDraftChanges) {
        updateUiState();
    }
}

bool FinepaperMainWindow::hasPendingInspectorDrafts() const {
    const bool parameterPending = m_parameterDraft
        && m_parameterDraft->designIdentity == m_designSessionIdentity;
    const bool endpointPending = m_endpointConfigurationPanel
        && !m_designSessionIdentity.isEmpty()
        && m_endpointConfigurationPanel->hasUnappliedDrafts(
            m_designSessionIdentity);
    const bool elementPending = m_elementConfigurationPanel
        && !m_designSessionIdentity.isEmpty()
        && m_elementConfigurationPanel->hasUnappliedDrafts(
            m_designSessionIdentity);
    return parameterPending || endpointPending || elementPending;
}

QStringList FinepaperMainWindow::pendingInspectorDraftDescriptions() const {
    QStringList pending;
    if (m_parameterDraft
        && m_parameterDraft->designIdentity == m_designSessionIdentity) {
        pending.append(QStringLiteral("NoC parameters"));
    }
    if (m_elementConfigurationPanel
        && !m_designSessionIdentity.isEmpty()) {
        for (const QString& description
             : m_elementConfigurationPanel->unappliedDraftDescriptions(
                   m_designSessionIdentity)) {
            pending.append(QStringLiteral("Element: %1").arg(description));
        }
    }
    if (m_endpointConfigurationPanel
        && !m_designSessionIdentity.isEmpty()) {
        for (const QString& endpointId
             : m_endpointConfigurationPanel->unappliedDraftEndpointIds(
                   m_designSessionIdentity)) {
            pending.append(
                QStringLiteral("Endpoint: %1").arg(endpointId));
        }
    }
    return pending;
}

bool FinepaperMainWindow::confirmDiscardPendingInspectorDrafts(
    const QString& action) {
    const QStringList pending = pendingInspectorDraftDescriptions();
    if (pending.isEmpty()) {
        return true;
    }
    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Unapplied Inspector changes"),
        QStringLiteral(
            "%1 would use or replace the durable Design while these "
            "Inspector drafts have not been applied:\n\n• %2\n\n"
            "Return to the Inspector and Apply them, or explicitly discard "
            "the listed drafts. Unapplied values are never saved, validated, "
            "or generated silently.")
            .arg(action, pending.join(QStringLiteral("\n• "))),
        QMessageBox::Discard | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.pendingInspectorDraftConfirmation"));
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.setEscapeButton(QMessageBox::Cancel);
    return confirmation.exec() == QMessageBox::Discard;
}

void FinepaperMainWindow::discardPendingInspectorDrafts() {
    {
        QScopedValueRollback<bool> batching(
            m_batchingInspectorDraftChanges, true);
        discardParameterDraft();
        if (m_endpointConfigurationPanel
            && !m_designSessionIdentity.isEmpty()) {
            m_endpointConfigurationPanel->clearDraftsForDesign(
                m_designSessionIdentity);
        }
        if (m_elementConfigurationPanel
            && !m_designSessionIdentity.isEmpty()) {
            m_elementConfigurationPanel->clearDraftsForDesign(
                m_designSessionIdentity);
        }
    }
    updateUiState();
}

bool FinepaperMainWindow::confirmDiscardElementDrafts(
    const QString& action,
    const ElementRef& element) {
    if (!m_elementConfigurationPanel
        || m_designSessionIdentity.isEmpty()
        || !m_elementConfigurationPanel->hasUnappliedDraft(
            m_designSessionIdentity, element)) {
        return true;
    }
    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Unapplied element configuration"),
        QStringLiteral(
            "%1 would invalidate an unapplied configuration draft for %2. "
            "Return to the Element Configuration Inspector and Apply it, or "
            "explicitly discard it if this operation succeeds.")
            .arg(action, element.id),
        QMessageBox::Discard | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.pendingElementDraftConfirmation"));
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.setEscapeButton(QMessageBox::Cancel);
    return confirmation.exec() == QMessageBox::Discard;
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
    confirmation.setTextFormat(Qt::PlainText);
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
    confirmation.setTextFormat(Qt::PlainText);
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
            && confirmDiscardPendingInspectorDrafts(
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
        && confirmDiscardPendingInspectorDrafts(
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
        packageDisplayLess);
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

    QString preferredPackageKey = m_creationPackageSelector
        ? m_creationPackageSelector->currentData().toString()
        : QString();
    if (!m_runtimeAvailablePackageKeys.contains(preferredPackageKey)) {
        const PackageDefinition* activeRuntimePackage = runtimePackageForDesign();
        preferredPackageKey = activeRuntimePackage
            ? activeRuntimePackage->key()
            : QString();
    }
    const QString suggestedName = m_design
        ? m_design->name
        : QStringLiteral("my_noc");
    NewDesignDialog dialog(
        std::move(availablePackages), preferredPackageKey, suggestedName, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString selectedPackageKey = dialog.selectedPackageKey();
    if (m_creationPackageSelector) {
        const int selectedIndex =
            m_creationPackageSelector->findData(selectedPackageKey);
        if (selectedIndex >= 0) {
            m_creationPackageSelector->setCurrentIndex(selectedIndex);
        }
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
        discardPendingInspectorDrafts();
        m_designPath.clear();
        beginDesignSession(result.design.name);
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
    if (!confirmDiscardPendingInspectorDrafts(
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
        discardPendingInspectorDrafts();
    }
    adoptDesignResult(
        result,
        QStringLiteral("Resize Mesh to %1 × %2")
            .arg(QString::number(rows), QString::number(columns)));
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
    m_design = loaded.design;
    discardPendingDomainChanges();
    discardPendingInspectorDrafts();
    beginDesignSession(m_design->name);
    advanceDesignRevision();
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
    if (!confirmDiscardPendingInspectorDrafts(
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
    discardPendingInspectorDrafts();
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
    if (!confirmDiscardPendingInspectorDrafts(
            QStringLiteral("Validating the design"))) {
        return;
    }
    FinepaperApplication application = m_application;
    NocDesign design = *m_design;
    const operations::RunTicket ticket = m_runState.beginRun(
        operations::RunKind::Validation);
    auto* watcher = new QFutureWatcher<ValidationResult>(this);
    m_validationWatcher = watcher;
    connect(watcher, &QFutureWatcher<ValidationResult>::finished,
            this, [this, watcher, ticket] {
                const ValidationResult result = watcher->result();
                const operations::CompletionDisposition disposition =
                    m_runState.disposition(ticket);
                const bool finishedActiveRun = m_runState.finishRun(ticket);
                if (m_validationWatcher == watcher) {
                    m_validationWatcher = nullptr;
                }
                watcher->deleteLater();
                if (finishedActiveRun) {
                    setOperationBusy(false);
                }
                if (disposition != operations::CompletionDisposition::Current) {
                    const QString reason = ignoredRunReason(disposition);
                    appendActivity(
                        QStringLiteral("Ignored validation result for ")
                        + designRevisionText(ticket.input)
                        + QStringLiteral(": ") + reason
                        + QStringLiteral("."));
                    if (finishedActiveRun) {
                        const QString status =
                            QStringLiteral(
                                "Result not published — validation for ")
                            + designRevisionText(ticket.input)
                            + QStringLiteral(" finished after ") + reason
                            + QStringLiteral(". Run Validate again.");
                        setStatusLabel(
                            m_diagnosticsStatus,
                            status,
                            QStringLiteral("warning"));
                        setStatusLabel(
                            m_problemReportStatus,
                            status,
                            QStringLiteral("warning"));
                    }
                    return;
                }
                presentValidationResult(result, ticket.input);
            });
    appendActivity(QStringLiteral("Starting validation and Package DRC."));
    setOperationBusy(true, QStringLiteral("Validating design…"));
    setStatusLabel(
        m_diagnosticsStatus,
        QStringLiteral("Running — validating ")
            + designRevisionText(ticket.input) + QStringLiteral("."),
        QStringLiteral("warning"));
    setStatusLabel(
        m_problemReportStatus,
        QStringLiteral(
            "Validation is running for ")
            + designRevisionText(ticket.input)
            + QStringLiteral(
                ". Any report below belongs to the previous completed run "
                "until this one finishes."),
        QStringLiteral("warning"));
    watcher->setFuture(QtConcurrent::run(
        [application = std::move(application), design = std::move(design)]() mutable {
            return application.validate(design, true);
        }));
    discardPendingDomainChanges();
    discardPendingInspectorDrafts();
}

void FinepaperMainWindow::presentValidationResult(
    const ValidationResult& result,
    const operations::DesignStamp& stamp) {
    populateDiagnostics(
        result.diagnostics, QStringLiteral("Validation"), stamp);
    m_resultTabs->setCurrentIndex(0);
    showResultsDock();
    appendActivity(result.success
                       ? QStringLiteral("Validation completed without errors.")
                       : QStringLiteral("Validation found errors."));
    statusBar()->showMessage(result.success ? QStringLiteral("Design is valid.")
                                            : QStringLiteral("Validation found errors."));
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
    if (!confirmDiscardPendingInspectorDrafts(
            QStringLiteral("Generating RTL"))) {
        return;
    }
    appendActivity(QStringLiteral("Starting RTL generation in %1.").arg(root));
    FinepaperApplication application = m_application;
    NocDesign design = *m_design;
    const operations::RunTicket ticket = m_runState.beginRun(
        operations::RunKind::Generation, root);
    auto* watcher = new QFutureWatcher<GenerationResult>(this);
    m_generationWatcher = watcher;
    connect(watcher, &QFutureWatcher<GenerationResult>::finished,
            this, [this, watcher, ticket] {
                const GenerationResult result = watcher->result();
                const operations::CompletionDisposition disposition =
                    m_runState.disposition(ticket);
                const bool finishedActiveRun = m_runState.finishRun(ticket);
                if (m_generationWatcher == watcher) {
                    m_generationWatcher = nullptr;
                }
                watcher->deleteLater();
                if (finishedActiveRun) {
                    setOperationBusy(false);
                }
                if (disposition != operations::CompletionDisposition::Current) {
                    const QString reason = ignoredRunReason(disposition);
                    appendActivity(
                        QStringLiteral("Ignored RTL generation result for ")
                        + designRevisionText(ticket.input)
                        + QStringLiteral(": ") + reason
                        + QStringLiteral(
                            ". The requested output root was ")
                        + ticket.outputRoot + QStringLiteral("."));
                    if (finishedActiveRun) {
                        setStatusLabel(
                            m_generationStatus,
                            QStringLiteral(
                                "Result not published — RTL generation for ")
                                + designRevisionText(ticket.input)
                                + QStringLiteral(" finished after ") + reason
                                + QStringLiteral(
                                    ". Generate RTL again."),
                            QStringLiteral("warning"));
                    }
                    return;
                }
                presentGenerationResult(result, ticket.input);
            });
    setOperationBusy(true, QStringLiteral("Generating RTL…"));
    setStatusLabel(
        m_generationStatus,
        QStringLiteral("Running — generating RTL for ")
            + designRevisionText(ticket.input) + QStringLiteral(", in ")
            + ticket.outputRoot + QStringLiteral("."),
        QStringLiteral("warning"));
    watcher->setFuture(QtConcurrent::run(
        [application = std::move(application),
         design = std::move(design),
         root]() mutable {
            return application.generate(design, GenerationOptions{root});
        }));
    discardPendingDomainChanges();
    discardPendingInspectorDrafts();
}

void FinepaperMainWindow::presentGenerationResult(
    const GenerationResult& result,
    const operations::DesignStamp& stamp) {
    populateGenerationOutputs(result, stamp);
    populateDiagnostics(
        result.diagnostics, QStringLiteral("RTL generation"), stamp);
    m_resultTabs->setCurrentIndex(2);
    showResultsDock();
    appendActivity(result.success
                       ? QStringLiteral("RTL generation completed: %1 artifact(s).")
                             .arg(result.artifacts.size())
                       : QStringLiteral("RTL generation failed with exit code %1.")
                             .arg(result.exitCode));
    statusBar()->showMessage(result.success ? QStringLiteral("RTL generated.")
                                            : QStringLiteral("RTL generation failed."));
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
    if (discardConfigurationDraft) {
        QMessageBox confirmation(
            QMessageBox::Warning,
            QStringLiteral("Delete Endpoint?"),
            QStringLiteral(
                "Delete Endpoint %1 and its attachment from this design? "
                "Unapplied Endpoint configuration for it will also be discarded.")
                .arg(endpointId),
            QMessageBox::Yes | QMessageBox::Cancel,
            this);
        confirmation.setObjectName(
            QStringLiteral("finepaper.deleteEndpointConfirmation"));
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmation.setEscapeButton(QMessageBox::Cancel);
        if (QPushButton* deleteButton = qobject_cast<QPushButton*>(
                confirmation.button(QMessageBox::Yes))) {
            deleteButton->setText(QStringLiteral("Delete Endpoint"));
            deleteButton->setProperty(
                "finepaperRole", QStringLiteral("danger"));
        }
        if (confirmation.exec() != QMessageBox::Yes) {
            return false;
        }
    }
    if (!confirmDiscardPendingDomainChanges(
            QStringLiteral("Removing an Endpoint"))) {
        return false;
    }
    const ElementRef attachment{
        ElementKind::EndpointAttachment, endpointId};
    if (discardConfigurationDraft
        && !confirmDiscardElementDrafts(
            QStringLiteral("Deleting the Endpoint"), attachment)) {
        return false;
    }
    const DesignResult result = m_application.removeEndpoint(*m_design, endpointId);
    if (result.success) {
        discardPendingDomainChanges();
        if (discardConfigurationDraft && m_endpointConfigurationPanel) {
            m_endpointConfigurationPanel->discardDraft(
                m_designSessionIdentity, endpointId);
            m_elementConfigurationPanel->discardDraftsForElement(
                m_designSessionIdentity, attachment);
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
            .arg(QString::number(target.router.x),
                 QString::number(target.router.y)),
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
                  .arg(choice.label, choice.id);
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
        || !m_parameterForm->isModified() || m_parameterDraftConflict) {
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
        // Commit the accepted editor state before adoptDesignResult() rebuilds
        // the Inspector. Otherwise captureParameterDraft() observes the old
        // baseline during refresh and recreates a ghost draft for the value
        // that has just become durable.
        m_updatingParameterForm = true;
        m_parameterForm->setValues(result.design.parameters);
        m_updatingParameterForm = false;
        m_parameterDraft.reset();
        m_parameterDraftConflict = false;
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
    if (m_domainManager) {
        m_domainManager->setSelection(selection.elements());
    }
    ElementConfigurationPanelState elementConfigurationState =
        ElementConfigurationPanelState::NoSelection;
    if (m_elementConfigurationPanel) {
        const operations::DesignStamp stamp = currentDesignStamp();
        m_elementConfigurationPanel->setContext(
            m_design ? &*m_design : nullptr,
            packageForDesign(),
            selectedElement,
            {.designIdentity = m_designSessionIdentity,
             .designRevision = stamp.revision,
             .packageCatalogRevision = stamp.catalogRevision},
            m_operationBusy);
        elementConfigurationState =
            m_elementConfigurationPanel->projectionState();
    }
    if (m_endpointConfigurationPanel) {
        std::optional<QString> endpointId = std::nullopt;
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
            m_operationBusy,
            currentDesignStamp().catalogRevision);
    }
    const bool selectedElementHasDraft = selectedElement
        && m_elementConfigurationPanel
        && m_elementConfigurationPanel->hasUnappliedDraft(
            m_designSessionIdentity, *selectedElement);
    if (m_endpointConfigurationGroup) {
        m_endpointConfigurationGroup->setVisible(singleEndpoint);
    }
    if (m_elementConfigurationGroup) {
        m_elementConfigurationGroup->setVisible(
            singleElementConfigurationTarget
            && (elementConfigurationState
                    == ElementConfigurationPanelState::Ready
                || selectedElementHasDraft));
    }
    m_selectedRouter.reset();
    if (selection.items.size() == 1
        && selection.items.front().kind
            == NocEditorSelection::Kind::Router) {
        m_selectedRouter = selection.items.front().router;
        if (!m_selectedRouter) {
            m_selectedRouter = routerPositionFromId(
                selection.items.front().id);
        }
    }
    updateEndpointQuickAddState();

    QStringList selectionKeyParts;
    selectionKeyParts.reserve(selection.items.size());
    for (const NocEditorSelection& item : selection.items) {
        selectionKeyParts.append(
            QStringLiteral("%1:%2")
                .arg(QString::number(static_cast<int>(item.kind)), item.id));
    }
    const QString selectionKey = !m_design
        ? QStringLiteral("no-design")
        : selectionKeyParts.isEmpty()
        ? QStringLiteral("none")
        : selectionKeyParts.join(QLatin1Char('|'));
    const QString previousSelectionKey = m_inspectorSelectionKey;
    const bool selectionChanged = selectionKey != previousSelectionKey;
    m_inspectorSelectionKey = selectionKey;
    if (selectionChanged && m_inspectorDesignSettings) {
        if (selection.items.isEmpty()) {
            m_inspectorDesignSettings->setExpanded(true);
        } else if (previousSelectionKey.isEmpty()
                   || previousSelectionKey == QStringLiteral("none")
                   || previousSelectionKey == QStringLiteral("no-design")) {
            m_inspectorDesignSettings->setExpanded(false);
        }
    }
    if (selectionChanged && m_inspectorScroll) {
        QTimer::singleShot(0, m_inspectorScroll, [scroll = m_inspectorScroll] {
            scroll->verticalScrollBar()->setValue(0);
        });
    }

    if (!m_inspectorSummaryPanel) {
        return;
    }
    if (!m_design) {
        m_inspectorSummaryPanel->setSelectionSummary(std::nullopt);
        return;
    }

    ui::InspectorSelectionSummary summary;
    const auto appendDetail = [&summary](const QString& detail) {
        if (detail.trimmed().isEmpty()) {
            return;
        }
        if (!summary.detail.isEmpty()) {
            summary.detail += QStringLiteral("\n\n");
        }
        summary.detail += detail.trimmed();
    };
    const auto appendElementConfigurationState = [&] {
        if (!singleElementConfigurationTarget
            || elementConfigurationState
                == ElementConfigurationPanelState::Ready) {
            return;
        }
        switch (elementConfigurationState) {
        case ElementConfigurationPanelState::PackageUnavailable:
            appendDetail(QStringLiteral(
                "Element properties are unavailable until this design's "
                "Package is loaded."));
            break;
        case ElementConfigurationPanelState::UnsupportedFormat:
            appendDetail(QStringLiteral(
                "Element properties require Design and Package formatVersion 3."));
            break;
        case ElementConfigurationPanelState::MissingElement:
            appendDetail(QStringLiteral(
                "This element is no longer present in the current Mesh."));
            break;
        case ElementConfigurationPanelState::NoApplicablePropertySets:
            appendDetail(QStringLiteral(
                "The Package declares no properties for this element."));
            break;
        case ElementConfigurationPanelState::UnsupportedSelection:
        case ElementConfigurationPanelState::NoSelection:
        case ElementConfigurationPanelState::NoDesign:
        case ElementConfigurationPanelState::Ready:
            break;
        }
    };

    if (selection.items.isEmpty()) {
        summary.title = QStringLiteral("Nothing selected");
        summary.metadata = QStringLiteral(
            "Drag canvas to pan · Shift-drag to select");
        m_inspectorSummaryPanel->setSelectionSummary(summary);
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
        if (routers > 0) counts.append(QStringLiteral("%1 Router").arg(routers));
        if (endpoints > 0) counts.append(QStringLiteral("%1 Endpoint").arg(endpoints));
        if (routerLinks > 0) counts.append(QStringLiteral("%1 Router Link").arg(routerLinks));
        if (attachments > 0) {
            counts.append(QStringLiteral("%1 Attachment").arg(attachments));
        }
        if (pendingEndpoints > 0) {
            counts.append(QStringLiteral("%1 unattached Endpoint").arg(pendingEndpoints));
        }
        summary.title = QStringLiteral("%1 items selected")
            .arg(selection.items.size());
        summary.metadata = counts.join(QStringLiteral(" · "));
        if (routers > 0 || routerLinks > 0) {
            appendDetail(QStringLiteral(
                "Router topology remains fixed by the Mesh."));
        }
        m_inspectorSummaryPanel->setSelectionSummary(summary);
        return;
    }

    const NocEditorSelection& item = selection.items.front();
    if (item.kind == NocEditorSelection::Kind::Router) {
        const std::optional<RouterPosition> position = m_selectedRouter;
        summary.title = QStringLiteral("Router %1").arg(item.id);
        summary.metadata = QStringLiteral("Column %1 · Row %2 · fixed Mesh")
            .arg(position ? position->x : -1)
            .arg(position ? position->y : -1);
        appendDetail(QStringLiteral(
            "Moving this node changes only its Workspace layout."));
        appendElementConfigurationState();
        if (m_nodeEditor && m_design) {
            appendDetail(domainElementInspectorText(
                m_nodeEditor->domainPresentation(),
                packageForDesign(),
                *m_design,
                ElementRef{ElementKind::Router, item.id}));
        }
        m_inspectorSummaryPanel->setSelectionSummary(summary);
        return;
    }
    const auto endpoint = std::find_if(
        m_design->endpoints.cbegin(), m_design->endpoints.cend(),
        [&item](const EndpointInstance& candidate) {
            return candidate.id == item.id;
        });
    const bool endpointFound = endpoint != m_design->endpoints.cend();
    if (item.kind == NocEditorSelection::Kind::Endpoint && endpointFound) {
        summary.title = QStringLiteral("Endpoint %1").arg(endpoint->id);
        summary.metadata = QStringLiteral("%1 · Router %2 · Slot %3")
            .arg(endpoint->type,
                 routerId(endpoint->attachment.router),
                 endpoint->attachment.slot.value_or(
                     QStringLiteral("automatic")));
        appendDetail(QStringLiteral(
            "Move freely on the canvas. Reconnect the EP port to change its "
            "Router attachment."));
        if (m_nodeEditor && m_design) {
            appendDetail(domainElementInspectorText(
                m_nodeEditor->domainPresentation(),
                packageForDesign(),
                *m_design,
                ElementRef{ElementKind::Endpoint, endpoint->id}));
        }
        m_inspectorSummaryPanel->setSelectionSummary(summary);
        return;
    }
    if (item.kind == NocEditorSelection::Kind::RouterLink && m_design) {
        const ElementRef edge{ElementKind::RouterLink, item.id};
        if (const auto endpoints = edgeEndpoints(*m_design, edge)) {
            summary.metadata = QStringLiteral("%1 → %2 · fixed Mesh")
                .arg(endpoints->first.id, endpoints->second.id);
        } else {
            summary.metadata = QStringLiteral("fixed Mesh");
        }
        summary.title = QStringLiteral("Router Link %1").arg(item.id);
        appendDetail(QStringLiteral(
            "Mesh-managed; manual creation, deletion, and rewiring are unavailable."));
        appendElementConfigurationState();
        if (m_nodeEditor) {
            appendDetail(domainCrossingInspectorText(
                m_nodeEditor->domainPresentation(), edge));
        }
        m_inspectorSummaryPanel->setSelectionSummary(summary);
        return;
    }
    if (item.kind == NocEditorSelection::Kind::EndpointAttachment) {
        const ElementRef edge{ElementKind::EndpointAttachment, item.id};
        summary.title = QStringLiteral("Endpoint Attachment %1").arg(item.id);
        if (endpointFound) {
            summary.metadata = QStringLiteral("Router %1 · Slot %2")
                .arg(routerId(endpoint->attachment.router),
                     endpoint->attachment.slot.value_or(
                         QStringLiteral("automatic")));
        } else {
            summary.metadata = QStringLiteral("Attachment target unavailable");
        }
        appendDetail(QStringLiteral(
            "Disconnect or reconnect this line to change the attachment."));
        appendElementConfigurationState();
        if (m_nodeEditor) {
            appendDetail(domainCrossingInspectorText(
                m_nodeEditor->domainPresentation(), edge));
        }
        m_inspectorSummaryPanel->setSelectionSummary(summary);
        return;
    }
    if (item.kind == NocEditorSelection::Kind::PendingEndpoint) {
        summary.title = QStringLiteral("Unattached Endpoint");
        summary.metadata = QStringLiteral("Type %1").arg(item.id);
        appendDetail(QStringLiteral(
            "Drag its EP port to a Router EP port or Router body to attach it."));
        m_inspectorSummaryPanel->setSelectionSummary(summary);
        return;
    }
    summary.title = QStringLiteral("Nothing selected");
    m_inspectorSummaryPanel->setSelectionSummary(summary);
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
    advanceDesignRevision();
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
    if (!result.diagnostics.isEmpty()) {
        showDiagnostics(result.diagnostics, action, false);
    }
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
        if (m_inspectorSummaryPanel) {
            m_inspectorSummaryPanel->setDesignSummary({
                QStringLiteral("No design open"),
                QStringLiteral("Create or open a design to inspect it."),
                {}});
        }
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
            "Read-only — the design Package is not loaded. Endpoint and "
            "parameter editing, validation, and generation are disabled.");
    } else if (!packageRuntimeAvailable) {
        availabilityNote = QStringLiteral(
            "Runtime unavailable — retained Package metadata keeps Endpoint "
            "and parameter editing available. Reload or reinstall this exact Package "
            "before validation or generation.");
    }
    if (m_inspectorSummaryPanel) {
        m_inspectorSummaryPanel->setDesignSummary({
            m_design->name,
            QStringLiteral("%1@%2 · %3 × %4 Mesh · %5 Endpoint(s)")
                .arg(m_design->package.id,
                     m_design->package.version,
                     QString::number(m_design->topology.rows),
                     QString::number(m_design->topology.columns),
                     QString::number(m_design->endpoints.size())),
            availabilityNote});
    }
    m_performanceSummary->setText(
        QStringLiteral("NoC %1 currently contains a %2 × %3 Mesh and %4 Endpoint(s). "
                       "This view is reserved for Package or IP Engine performance results; "
                       "the NodeEditor remains the source interaction surface.")
            .arg(m_design->name,
                 QString::number(m_design->topology.rows),
                 QString::number(m_design->topology.columns),
                 QString::number(m_design->endpoints.size())));

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
    captureParameterDraft();
    const PackageDefinition* package = packageForDesign();
    m_updatingParameterForm = true;
    if (!m_design || !package) {
        m_parameterForm->setSchema({}, {});
        m_parameterDraftConflict = m_parameterDraft.has_value();
        m_updatingParameterForm = false;
        updateUiState();
        return;
    }
    m_parameterForm->setSchema(package->parameters, m_design->parameters);
    m_parameterDraftConflict = false;
    if (m_parameterDraft
        && m_parameterDraft->designIdentity == m_designSessionIdentity) {
        const PackageParameterDraft baseline = m_parameterForm->draftValues();
        bool schemaCompatible =
            m_parameterDraft->sourceSchemaIdentity
                == m_parameterForm->schemaIdentity()
            && baseline.size() == m_parameterDraft->editorState.size();
        if (schemaCompatible) {
            for (auto value = baseline.cbegin(); value != baseline.cend(); ++value) {
                if (!m_parameterDraft->editorState.contains(value.key())) {
                    schemaCompatible = false;
                    break;
                }
            }
        }
        const bool sourceCompatible =
            m_parameterDraft->sourceValues == m_design->parameters;
        if (schemaCompatible) {
            m_parameterForm->setDraftValues(
                m_parameterDraft->editorState);
        }
        m_parameterDraftConflict = !schemaCompatible || !sourceCompatible;
    }
    m_updatingParameterForm = false;
    updateUiState();
}

void FinepaperMainWindow::populateDiagnostics(
    const QVector<Diagnostic>& diagnostics,
    const QString& source,
    std::optional<operations::DesignStamp> stamp) {
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
    m_diagnosticsSource = source.trimmed().isEmpty()
        ? QStringLiteral("Diagnostics") : source.trimmed();
    m_diagnosticsStamp = std::move(stamp);
    m_problemReport->setPlainText(diagnosticText(diagnostics));
    m_resultTabs->setTabText(0, workbench::drcTabTitle);

    QString status;
    QString role = QStringLiteral("muted");
    if (m_diagnosticsStamp) {
        const qsizetype errorCount = std::count_if(
            diagnostics.cbegin(), diagnostics.cend(),
            [](const Diagnostic& diagnostic) {
                return diagnostic.severity == QStringLiteral("error");
            });
        if (diagnostics.isEmpty()) {
            status = QStringLiteral("Current result — ")
                + m_diagnosticsSource + QStringLiteral(" passed for ")
                + designRevisionText(*m_diagnosticsStamp)
                + QStringLiteral(", with 0 problems.");
            role = QStringLiteral("success");
        } else {
            status = QStringLiteral("Current result — ")
                + m_diagnosticsSource + QStringLiteral(" for ")
                + designRevisionText(*m_diagnosticsStamp)
                + QStringLiteral(": ")
                + QString::number(diagnostics.size())
                + QStringLiteral(" diagnostic(s), ")
                + QString::number(errorCount)
                + QStringLiteral(" error(s).");
            role = errorCount > 0
                ? QStringLiteral("error") : QStringLiteral("warning");
        }
    } else {
        status = m_diagnosticsSource + QStringLiteral(" published ")
            + QString::number(diagnostics.size())
            + QStringLiteral(
                " workspace diagnostic(s); this report is not bound to a "
                "design revision.");
    }
    setStatusLabel(m_diagnosticsStatus, status, role);
    setStatusLabel(m_problemReportStatus, status, role);
    updateResultFreshness();
}

void FinepaperMainWindow::populateGenerationOutputs(
    const GenerationResult& result,
    const operations::DesignStamp& stamp) {
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
    m_generationStamp = stamp;
    m_generationPublicationKind = result.success
        ? GenerationPublicationKind::Artifacts
        : GenerationPublicationKind::FailedAttempt;
    m_resultTabs->setTabText(2, workbench::generationTabTitle);
    setStatusLabel(
        m_generationStatus,
        result.success
            ? QStringLiteral("Current artifacts — generated for ")
                  + designRevisionText(stamp) + QStringLiteral(": ")
                  + QString::number(result.artifacts.size())
                  + QStringLiteral(" artifact(s).")
            : QStringLiteral("Current generation attempt — ")
                  + designRevisionText(stamp)
                  + QStringLiteral(" failed with exit code ")
                  + QString::number(result.exitCode) + QStringLiteral("."),
        result.success ? QStringLiteral("success") : QStringLiteral("error"));
    updateResultFreshness();
}

void FinepaperMainWindow::showDiagnostics(const QVector<Diagnostic>& diagnostics,
                                          const QString& title,
                                          bool modalOnError) {
    populateDiagnostics(diagnostics, title);
    if (modalOnError && hasErrors(diagnostics)) {
        m_resultTabs->setCurrentIndex(0);
        showResultsDock();
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

void FinepaperMainWindow::showResultsDock() {
    if (!m_resultsDock) {
        return;
    }
    m_resultsDock->show();
    resizeDocks(
        {m_resultsDock},
        {workbench::defaultResultsDockHeight},
        Qt::Vertical);
}

void FinepaperMainWindow::selectCenterView(const QString& id) {
    if (!m_viewRegistry->select(id)) {
        m_viewRegistry->select(workbench::editorViewId);
    }
}

void FinepaperMainWindow::beginDesignSession(const QString& designName) {
    ++m_designSessionSerial;
    m_designSessionIdentity = QStringLiteral("design-session-%1")
        .arg(m_designSessionSerial);
    m_workspaceStatusMessage.clear();
    m_runState.beginSession(designName);
    m_parameterDraft.reset();
    m_parameterDraftConflict = false;
    if (m_nodeEditor) {
        m_nodeEditor->beginDocumentSession(m_designSessionIdentity);
    }
    if (m_endpointConfigurationPanel) {
        m_endpointConfigurationPanel->clearDrafts();
    }
    if (m_elementConfigurationPanel) {
        m_elementConfigurationPanel->clearDrafts();
    }
    clearPublishedResults();
}

operations::DesignStamp FinepaperMainWindow::currentDesignStamp() const {
    return m_runState.currentStamp();
}

void FinepaperMainWindow::advanceDesignRevision() {
    m_runState.advanceDesignRevision(
        m_design ? m_design->name : QString());
    updateResultFreshness();
}

void FinepaperMainWindow::advanceCatalogRevision() {
    m_runState.advanceCatalogRevision();
    updateResultFreshness();
}

void FinepaperMainWindow::clearPublishedResults() {
    m_diagnosticsStamp.reset();
    m_generationStamp.reset();
    m_generationPublicationKind = GenerationPublicationKind::None;
    m_diagnosticsSource.clear();
    if (m_drcTable) {
        m_drcTable->clearContents();
        m_drcTable->setRowCount(0);
    }
    if (m_artifactTable) {
        m_artifactTable->clearContents();
        m_artifactTable->setRowCount(0);
    }
    if (m_generationDetails) {
        m_generationDetails->clear();
    }
    if (m_problemReport) {
        m_problemReport->clear();
    }
    if (m_resultTabs) {
        m_resultTabs->setTabText(0, workbench::drcTabTitle);
        m_resultTabs->setTabText(2, workbench::generationTabTitle);
    }
    setStatusLabel(
        m_diagnosticsStatus,
        QStringLiteral("No diagnostics have been published for this design."),
        QStringLiteral("muted"));
    setStatusLabel(
        m_problemReportStatus,
        QStringLiteral("Run Validate to create a report for the current design."),
        QStringLiteral("muted"));
    setStatusLabel(
        m_generationStatus,
        QStringLiteral("No RTL has been generated for this design revision."),
        QStringLiteral("muted"));
}

void FinepaperMainWindow::updateResultFreshness() {
    const operations::DesignStamp current = currentDesignStamp();
    if (m_diagnosticsStamp && *m_diagnosticsStamp != current) {
        const QString status = QStringLiteral("Out of date — ")
            + m_diagnosticsSource + QStringLiteral(" belongs to ")
            + designRevisionText(*m_diagnosticsStamp)
            + QStringLiteral("; the current design is revision ")
            + QString::number(current.revision)
            + QStringLiteral(
                " or uses a reloaded Package runtime. Run Validate again "
                "before relying on this report.");
        setStatusLabel(
            m_diagnosticsStatus, status, QStringLiteral("warning"));
        setStatusLabel(
            m_problemReportStatus, status, QStringLiteral("warning"));
        if (m_resultTabs) {
            m_resultTabs->setTabText(
                0, QStringLiteral("%1 (out of date)")
                       .arg(workbench::drcTabTitle));
        }
    }
    if (m_generationStamp && *m_generationStamp != current) {
        const bool failedAttempt =
            m_generationPublicationKind
            == GenerationPublicationKind::FailedAttempt;
        const QString status = failedAttempt
            ? QStringLiteral(
                  "Out of date — a failed generation attempt belongs to ")
                  + designRevisionText(*m_generationStamp)
                  + QStringLiteral("; the current design is revision ")
                  + QString::number(current.revision)
                  + QStringLiteral(
                      " or uses a reloaded Package runtime. Generate RTL "
                      "again after resolving the failure.")
            : QStringLiteral(
                  "Out of date — these artifacts were generated for ")
                  + designRevisionText(*m_generationStamp)
                  + QStringLiteral("; the current design is revision ")
                  + QString::number(current.revision)
                  + QStringLiteral(
                      " or uses a reloaded Package runtime. Generate RTL "
                      "again before delivery.");
        setStatusLabel(
            m_generationStatus,
            status,
            failedAttempt ? QStringLiteral("error")
                          : QStringLiteral("warning"));
        if (m_resultTabs) {
            m_resultTabs->setTabText(
                2, QStringLiteral("%1 (out of date)")
                       .arg(workbench::generationTabTitle));
        }
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
        const QString candidate = QStringLiteral("%1_%2")
            .arg(base, QString::number(suffix));
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
