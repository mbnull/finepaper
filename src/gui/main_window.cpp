#include "gui/main_window.h"

#include "gui/workbench_config.h"
#include "storage/json.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QInputDialog>
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
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <limits>
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
            QStringLiteral("Install or select a NoC Package, then create a Mesh NoC."));
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
    createResultsDock();
    createActions();

    m_operationProgress = new QProgressBar(this);
    m_operationProgress->setObjectName(QStringLiteral("finepaper.operationProgress"));
    m_operationProgress->setRange(0, 0);
    m_operationProgress->setMaximumWidth(180);
    m_operationProgress->setTextVisible(false);
    m_operationProgress->hide();
    statusBar()->addPermanentWidget(m_operationProgress);

    resizeDocks({m_packageDock, m_inspectorDock}, {285, 330}, Qt::Horizontal);
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
    m_nodeEditor->detachedEndpointDropped = [this](const EndpointInstance& detached,
                                                    NocAttachmentTarget target) {
        if (!m_design || !packageForDesign()) {
            return false;
        }
        const AttachmentSlotChoice slotChoice = chooseAttachmentSlot(target);
        if (!slotChoice.accepted) {
            return false;
        }
        EndpointInstance endpoint = detached;
        endpoint.attachment.router = target.router;
        endpoint.attachment.slot = slotChoice.slot;
        const DesignResult result = m_application.addEndpoint(*m_design, endpoint);
        adoptDesignResult(result,
                          QStringLiteral("Reconnect Endpoint %1").arg(endpoint.id));
        return result.success;
    };
    m_nodeEditor->endpointRemovalRequested = [this](const QString& endpointId) {
        return removeEndpoint(endpointId);
    };
    m_nodeEditor->selectionChanged = [this](const NocEditorSelection& selection) {
        updateInspector(selection);
    };
}

void FinepaperMainWindow::createPackageDock() {
    m_packageDock = new QDockWidget(QStringLiteral("NoC Package & Endpoint Library"), this);
    m_packageDock->setObjectName(workbench::packageDockName);
    m_packageDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 10, 10, 10);

    auto* packageGroup = new QGroupBox(QStringLiteral("Runtime Package"));
    auto* packageLayout = new QVBoxLayout(packageGroup);
    m_packageSelector = new QComboBox;
    m_packageSelector->setObjectName(QStringLiteral("finepaper.packageSelector"));
    m_packageSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    packageLayout->addWidget(m_packageSelector);
    auto* packageButtons = new QHBoxLayout;
    m_installPackageButton = new QPushButton(QStringLiteral("Install…"));
    m_reloadPackagesButton = new QPushButton(QStringLiteral("Reload"));
    packageButtons->addWidget(m_installPackageButton);
    packageButtons->addWidget(m_reloadPackagesButton);
    packageLayout->addLayout(packageButtons);
    layout->addWidget(packageGroup);

    auto* createGroup = new QGroupBox(QStringLiteral("Create Mesh NoC"));
    auto* createLayout = new QFormLayout(createGroup);
    m_designName = new QLineEdit(QStringLiteral("my_noc"));
    m_designName->setObjectName(QStringLiteral("finepaper.designName"));
    m_rows = new QSpinBox;
    m_rows->setObjectName(QStringLiteral("finepaper.meshRows"));
    m_columns = new QSpinBox;
    m_columns->setObjectName(QStringLiteral("finepaper.meshColumns"));
    m_rows->setRange(1, 1024);
    m_columns->setRange(1, 1024);
    m_rows->setValue(2);
    m_columns->setValue(2);
    createLayout->addRow(QStringLiteral("Name"), m_designName);
    createLayout->addRow(QStringLiteral("Rows"), m_rows);
    createLayout->addRow(QStringLiteral("Columns"), m_columns);
    m_createDesignButton = new QPushButton(QStringLiteral("Create / Replace Design"));
    m_createDesignButton->setObjectName(QStringLiteral("finepaper.createDesign"));
    createLayout->addRow(m_createDesignButton);
    layout->addWidget(createGroup);

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
    connect(m_packageSelector, &QComboBox::currentIndexChanged, this, [this](int) {
        updateMeshBounds();
        updateEndpointPalette();
        updateUiState();
    });
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

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 10, 10, 10);
    m_designOverview = new QLabel(QStringLiteral("No design is open."));
    m_designOverview->setWordWrap(true);
    m_designOverview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_designOverview);

    auto* selectionGroup = new QGroupBox(QStringLiteral("Selection"));
    selectionGroup->setObjectName(workbench::selectionInspectorName);
    auto* selectionLayout = new QVBoxLayout(selectionGroup);
    m_selectionSummary = new QLabel(QStringLiteral("Nothing selected."));
    m_selectionSummary->setWordWrap(true);
    m_selectionSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    selectionLayout->addWidget(m_selectionSummary);
    layout->addWidget(selectionGroup);

    m_parameterGroup = new QGroupBox(QStringLiteral("NoC Parameters"));
    m_parameterGroup->setObjectName(QStringLiteral("finepaper.parameterGroup"));
    auto* parameterGroupLayout = new QVBoxLayout(m_parameterGroup);
    auto* parameterContent = new QWidget;
    m_parameterForm = new QFormLayout(parameterContent);
    auto* parameterScroll = new QScrollArea;
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setWidget(parameterContent);
    m_applyParametersButton = new QPushButton(QStringLiteral("Apply Parameters"));
    m_applyParametersButton->setObjectName(QStringLiteral("finepaper.applyParameters"));
    parameterGroupLayout->addWidget(parameterScroll, 1);
    parameterGroupLayout->addWidget(m_applyParametersButton);
    layout->addWidget(m_parameterGroup, 1);

    m_inspectorDock->setWidget(content);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    connect(m_applyParametersButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::applyParameters);
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
        style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("New Mesh…"), this);
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
    m_nodeEditor->addAction(m_regularizeAction);
    m_nodeEditor->addAction(m_fitAction);

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
    connect(m_regularizeAction, &QAction::triggered,
            m_nodeEditor, &NocNodeEditor::regularizeLayout);
    connect(m_fitAction, &QAction::triggered, m_nodeEditor, &NocNodeEditor::zoomToFit);

    QAction* packagePanelAction = m_packageDock->toggleViewAction();
    packagePanelAction->setObjectName(workbench::packageToggleActionName);
    packagePanelAction->setText(QStringLiteral("Package && Endpoint Library"));
    packagePanelAction->setIcon(QIcon::fromTheme(
        QStringLiteral("folder-symbolic"), style()->standardIcon(QStyle::SP_DirIcon)));
    packagePanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    packagePanelAction->setStatusTip(QStringLiteral("Show or hide the left Package panel"));

    QAction* inspectorPanelAction = m_inspectorDock->toggleViewAction();
    inspectorPanelAction->setObjectName(workbench::inspectorToggleActionName);
    inspectorPanelAction->setText(QStringLiteral("Inspector"));
    inspectorPanelAction->setIcon(QIcon::fromTheme(
        QStringLiteral("view-list-details-symbolic"),
        style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    inspectorPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    inspectorPanelAction->setStatusTip(QStringLiteral("Show or hide the right Inspector panel"));

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
    panelsMenu->addAction(resultsPanelAction);
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

    QToolBar* activityBar = new QToolBar(QStringLiteral("Workbench Panels"), this);
    activityBar->setObjectName(workbench::activityBarName);
    activityBar->setMovable(false);
    activityBar->setFloatable(false);
    activityBar->setOrientation(Qt::Vertical);
    activityBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    activityBar->setIconSize(QSize(24, 24));
    activityBar->addAction(packagePanelAction);
    activityBar->addAction(inspectorPanelAction);
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
    const QVector<Diagnostic> diagnostics = m_application.reloadPackages(m_locations.packageRoots);
    updatePackageControls();
    if (m_design) {
        refreshDesignViews();
    }
    showDiagnostics(diagnostics, QStringLiteral("Package discovery"), false);
    appendActivity(QStringLiteral("Loaded %1 runtime Package(s) from %2 configured root(s).")
                       .arg(m_application.packages().size())
                       .arg(m_locations.packageRoots.size()));
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
        statusBar()->showMessage(
            QStringLiteral("Read-only design: Package %1@%2 is not loaded.")
                .arg(m_design->package.id, m_design->package.version));
    } else {
        statusBar()->showMessage(
            QStringLiteral("Loaded %1 runtime Package(s).")
                .arg(m_application.packages().size()));
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

    const PackageLoadResult package = loadPackage(directory);
    if (!package.success || !package.package) {
        showDiagnostics(package.diagnostics, QStringLiteral("Install Package"));
        return;
    }

    appendPackageRoots(m_locations, QStringList{directory});
    QSettings settings;
    QStringList installed = settings.value(workbench::packageRootsSetting).toStringList();
    if (!installed.contains(directory)) {
        installed.append(directory);
        settings.setValue(workbench::packageRootsSetting, installed);
    }

    reloadPackages();
    const int packageIndex = m_packageSelector->findData(package.package->key());
    if (packageIndex >= 0) {
        m_packageSelector->setCurrentIndex(packageIndex);
    }
    appendActivity(QStringLiteral("Installed runtime Package %1 from %2.")
                       .arg(package.package->key(), directory));
    statusBar()->showMessage(
        QStringLiteral("Installed Package %1.").arg(package.package->key()));
}

void FinepaperMainWindow::updatePackageControls() {
    const QString previous = m_packageSelector->currentData().toString();
    const QSignalBlocker blocker(m_packageSelector);
    m_packageSelector->clear();
    for (const PackageDefinition& package : m_application.packages()) {
        m_packageSelector->addItem(
            QStringLiteral("%1 — %2").arg(package.name, package.version), package.key());
    }
    QString desired = previous;
    if (m_design) {
        desired = QStringLiteral("%1@%2").arg(m_design->package.id, m_design->package.version);
    }
    const int desiredIndex = m_packageSelector->findData(desired);
    if (desiredIndex >= 0) {
        m_packageSelector->setCurrentIndex(desiredIndex);
    } else if (m_design) {
        m_packageSelector->setCurrentIndex(-1);
    }
    updateMeshBounds();
    updateEndpointPalette();
    updateUiState();
}

void FinepaperMainWindow::updateMeshBounds() {
    const PackageDefinition* package = selectedPackage();
    if (!package) {
        m_rows->setRange(1, 1);
        m_columns->setRange(1, 1);
        return;
    }
    const QSignalBlocker rowsBlocker(m_rows);
    const QSignalBlocker columnsBlocker(m_columns);
    m_rows->setRange(package->mesh.minimumRows, package->mesh.maximumRows);
    m_columns->setRange(package->mesh.minimumColumns, package->mesh.maximumColumns);
    if (m_design && packageForDesign() == package) {
        m_rows->setValue(m_design->topology.rows);
        m_columns->setValue(m_design->topology.columns);
    } else {
        m_rows->setValue(package->mesh.defaultRows);
        m_columns->setValue(package->mesh.defaultColumns);
    }
}

void FinepaperMainWindow::updateEndpointPalette() {
    m_endpointPalette->clear();
    const PackageDefinition* package = m_design ? packageForDesign() : selectedPackage();
    if (!package) {
        m_nodeEditor->setEndpointTypes({});
        QVector<NocRouterAttachmentPortItem> readOnlyPorts;
        if (m_design) {
            QSet<QString> knownSlots;
            QHash<QString, int> endpointsPerRouter;
            int maximumAttachments = 0;
            for (const EndpointInstance& endpoint : m_design->endpoints) {
                const QString router = QStringLiteral("%1,%2")
                                           .arg(endpoint.attachment.router.x)
                                           .arg(endpoint.attachment.router.y);
                maximumAttachments = std::max(
                    maximumAttachments, ++endpointsPerRouter[router]);
                if (endpoint.attachment.slot
                    && !endpoint.attachment.slot->isEmpty()
                    && !knownSlots.contains(*endpoint.attachment.slot)) {
                    knownSlots.insert(*endpoint.attachment.slot);
                    readOnlyPorts.append({*endpoint.attachment.slot,
                                          *endpoint.attachment.slot,
                                          std::nullopt});
                }
            }
            int fallbackIndex = 0;
            while (readOnlyPorts.size() < maximumAttachments) {
                QString id;
                do {
                    id = QStringLiteral("__view_%1").arg(fallbackIndex++);
                } while (knownSlots.contains(id));
                knownSlots.insert(id);
                readOnlyPorts.append({id,
                                      QStringLiteral("EP%1").arg(readOnlyPorts.size()),
                                      std::nullopt});
            }
        }
        m_nodeEditor->setRouterAttachmentPorts(std::move(readOnlyPorts));
        updateUiState();
        return;
    }
    QVector<NocEndpointTypeItem> editorTypes;
    QVector<NocRouterAttachmentPortItem> attachmentPorts;
    if (package->attachment.slotMode == AttachmentSlotMode::Explicit) {
        QVector<AttachmentSlotDefinition> declaredSlots = package->attachment.positions;
        if (declaredSlots.isEmpty()) {
            declaredSlots.reserve(package->attachment.maxPerRouter);
            for (int index = 0; index < package->attachment.maxPerRouter; ++index) {
                const QString id = QString::number(index);
                declaredSlots.append({id, QStringLiteral("Local port %1").arg(index)});
            }
        }
        attachmentPorts.reserve(declaredSlots.size());
        for (const AttachmentSlotDefinition& slot : declaredSlots) {
            attachmentPorts.append({slot.id,
                                    slot.label.trimmed().isEmpty() ? slot.id : slot.label,
                                    slot.id});
        }
    } else {
        attachmentPorts.reserve(package->attachment.maxPerRouter);
        for (int index = 0; index < package->attachment.maxPerRouter; ++index) {
            const QString id = QString::number(index);
            attachmentPorts.append({id,
                                    package->attachment.maxPerRouter == 1
                                        ? QStringLiteral("EP")
                                        : QStringLiteral("EP%1").arg(index),
                                    std::nullopt});
        }
    }
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
    m_nodeEditor->setRouterAttachmentPorts(std::move(attachmentPorts));
    updateUiState();
}

void FinepaperMainWindow::updateUiState() {
    const bool hasDesign = m_design.has_value();
    const bool hasDesignPackage = hasDesign && packageForDesign();
    const bool hasSelectedPackage = selectedPackage();

    if (m_newAction) {
        m_newAction->setEnabled(!m_operationBusy);
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
        m_saveAction->setEnabled(hasDesign && m_dirty);
    }
    if (m_saveAsAction) {
        m_saveAsAction->setEnabled(hasDesign);
    }
    if (m_validateAction) {
        m_validateAction->setEnabled(hasDesignPackage && !m_operationBusy);
    }
    if (m_generateAction) {
        m_generateAction->setEnabled(hasDesignPackage && !m_operationBusy);
    }
    if (m_regularizeAction) {
        m_regularizeAction->setEnabled(hasDesign);
    }
    if (m_fitAction) {
        m_fitAction->setEnabled(hasDesign);
    }
    if (m_createDesignButton) {
        m_createDesignButton->setEnabled(hasSelectedPackage && !m_operationBusy);
    }
    if (m_installPackageButton) {
        m_installPackageButton->setEnabled(!m_operationBusy);
    }
    if (m_reloadPackagesButton) {
        m_reloadPackagesButton->setEnabled(!m_operationBusy);
    }
    if (m_packageSelector) {
        m_packageSelector->setEnabled(!m_operationBusy);
    }
    if (m_designName) {
        m_designName->setEnabled(!m_operationBusy);
    }
    if (m_rows) {
        m_rows->setEnabled(!m_operationBusy);
    }
    if (m_columns) {
        m_columns->setEnabled(!m_operationBusy);
    }
    if (m_endpointPalette) {
        m_endpointPalette->setEnabled(
            hasDesignPackage && !m_operationBusy && m_endpointPalette->count() > 0);
        if (!hasDesign) {
            m_endpointPalette->setToolTip(
                QStringLiteral("Create or open a design before adding Endpoints."));
        } else if (!hasDesignPackage) {
            m_endpointPalette->setToolTip(
                QStringLiteral("The design Package is not loaded; Endpoint editing is disabled."));
        } else {
            m_endpointPalette->setToolTip({});
        }
    }
    if (m_parameterGroup) {
        m_parameterGroup->setEnabled(hasDesignPackage && !m_operationBusy);
    }
    if (m_applyParametersButton) {
        m_applyParametersButton->setEnabled(
            hasDesignPackage && !m_operationBusy && !m_parameterControls.isEmpty());
    }
    if (m_generateButton) {
        m_generateButton->setEnabled(hasDesignPackage && !m_operationBusy);
    }
    if (m_outputRoot) {
        m_outputRoot->setEnabled(!m_operationBusy);
    }
    if (m_browseOutputButton) {
        m_browseOutputButton->setEnabled(!m_operationBusy);
    }
    if (m_nodeEditor) {
        m_nodeEditor->setEnabled(true);
        m_nodeEditor->setEditingEnabled(hasDesignPackage && !m_operationBusy);
        if (m_operationBusy) {
            m_nodeEditor->setToolTip(
                QStringLiteral("Read-only while validation or generation is running."));
        } else if (hasDesign && !hasDesignPackage) {
            m_nodeEditor->setToolTip(
                QStringLiteral("Read-only: the design Package is not loaded."));
        } else {
            m_nodeEditor->setToolTip({});
        }
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

bool FinepaperMainWindow::maybeSave() {
    if (!m_dirty || !m_design) {
        return true;
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
    return true;
}

const PackageDefinition* FinepaperMainWindow::selectedPackage() const {
    const QString key = m_packageSelector->currentData().toString();
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
    for (const PackageDefinition& package : m_application.packages()) {
        if (package.id == m_design->package.id
            && package.version == m_design->package.version) {
            return &package;
        }
    }
    return nullptr;
}

void FinepaperMainWindow::createDesign() {
    if (m_operationBusy) {
        return;
    }
    if (!maybeSave()) {
        return;
    }
    const PackageDefinition* package = selectedPackage();
    if (!package) {
        QMessageBox::warning(this, QStringLiteral("No Package"),
                             QStringLiteral("Install or select a valid runtime NoC Package first."));
        return;
    }
    const QJsonObject request{
        {QStringLiteral("name"), m_designName->text().trimmed()},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), package->id},
            {QStringLiteral("version"), package->version}}},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), m_rows->value()},
            {QStringLiteral("columns"), m_columns->value()}}}
    };
    const DesignResult result = m_application.createDesign(request);
    if (result.success) {
        m_designPath.clear();
    }
    adoptDesignResult(result, QStringLiteral("Create Mesh"));
    selectCenterView(workbench::editorViewId);
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
    m_designPath = path;
    refreshDesignViews();
    setDirty(false);
    selectCenterView(workbench::editorViewId);
    appendActivity(QStringLiteral("Opened design %1.").arg(path));
    if (packageForDesign()) {
        statusBar()->showMessage(QStringLiteral("Opened %1").arg(path));
    } else {
        statusBar()->showMessage(
            QStringLiteral("Read-only design: Package %1@%2 is not loaded.")
                .arg(m_design->package.id, m_design->package.version));
    }
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
    QVector<Diagnostic> diagnostics;
    if (!m_application.saveDesignFile(path, *m_design, &diagnostics)) {
        showDiagnostics(diagnostics, QStringLiteral("Save design"));
        return false;
    }
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
    if (!m_design || !packageForDesign()) {
        QMessageBox::information(this, QStringLiteral("Validate design"),
                                 QStringLiteral("Create or open an editable NoC design first."));
        return;
    }
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
    if (!m_design || !packageForDesign()) {
        QMessageBox::information(this, QStringLiteral("Generate RTL"),
                                 QStringLiteral("Create or open an editable NoC design first."));
        return;
    }
    const QString root = m_outputRoot->text().trimmed();
    if (root.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Generate RTL"),
                             QStringLiteral("Choose an output root."));
        return;
    }
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

bool FinepaperMainWindow::addEndpoint(const QString& endpointType,
                                      NocAttachmentTarget target) {
    if (m_operationBusy || !m_design || !packageForDesign()) {
        QMessageBox::information(this, QStringLiteral("Add Endpoint"),
                                 QStringLiteral("Create or open an editable NoC design first."));
        return false;
    }
    const AttachmentSlotChoice slotChoice = chooseAttachmentSlot(target);
    if (!slotChoice.accepted) {
        return false;
    }
    EndpointInstance endpoint;
    endpoint.id = nextEndpointId(endpointType);
    endpoint.type = endpointType;
    endpoint.attachment.router = target.router;
    endpoint.attachment.slot = slotChoice.slot;
    const DesignResult result = m_application.addEndpoint(*m_design, endpoint);
    adoptDesignResult(result,
                      QStringLiteral("Add Endpoint %1").arg(endpoint.id));
    return result.success;
}

bool FinepaperMainWindow::moveEndpoint(const QString& endpointId,
                                       NocAttachmentTarget target) {
    if (m_operationBusy || !m_design || !packageForDesign()) {
        return false;
    }
    const AttachmentSlotChoice slotChoice = chooseAttachmentSlot(target, endpointId);
    if (!slotChoice.accepted) {
        m_nodeEditor->setDesign(&*m_design);
        return false;
    }
    const DesignResult result = m_application.moveEndpoint(
        *m_design, endpointId, target.router, slotChoice.slot);
    adoptDesignResult(result,
                      QStringLiteral("Move Endpoint %1").arg(endpointId));
    return result.success;
}

bool FinepaperMainWindow::removeEndpoint(const QString& endpointId) {
    if (m_operationBusy || !m_design || !packageForDesign() || endpointId.isEmpty()) {
        return false;
    }
    const DesignResult result = m_application.removeEndpoint(*m_design, endpointId);
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

    QSet<QString> occupiedSlots;
    int attachedEndpointCount = 0;
    for (const EndpointInstance& endpoint : m_design->endpoints) {
        if (endpoint.id == ignoredEndpointId
            || endpoint.attachment.router != target.router) {
            continue;
        }
        ++attachedEndpointCount;
        if (endpoint.attachment.slot) {
            occupiedSlots.insert(*endpoint.attachment.slot);
        }
    }
    if (attachedEndpointCount >= package->attachment.maxPerRouter) {
        QMessageBox::information(
            this,
            QStringLiteral("No available attachment position"),
            QStringLiteral("Router (%1, %2) has reached its Endpoint capacity.")
                .arg(target.router.x)
                .arg(target.router.y));
        return {};
    }
    if (package->attachment.slotMode == AttachmentSlotMode::Automatic) {
        return {true, std::nullopt};
    }

    QVector<AttachmentSlotDefinition> declaredSlots = package->attachment.positions;
    if (declaredSlots.isEmpty()) {
        declaredSlots.reserve(package->attachment.maxPerRouter);
        for (int index = 0; index < package->attachment.maxPerRouter; ++index) {
            const QString slot = QString::number(index);
            declaredSlots.append({slot, QStringLiteral("Local port %1").arg(index)});
        }
    }

    QStringList labels;
    QVector<QString> slotIds;
    for (const AttachmentSlotDefinition& slot : declaredSlots) {
        if (occupiedSlots.contains(slot.id)) {
            continue;
        }
        labels.append(slot.label == slot.id
                          ? slot.id
                          : QStringLiteral("%1 — %2").arg(slot.label, slot.id));
        slotIds.append(slot.id);
    }
    if (slotIds.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("No available attachment position"),
            QStringLiteral("Router (%1, %2) has no free explicit Endpoint slot.")
                .arg(target.router.x)
                .arg(target.router.y));
        return {};
    }

    if (target.exactSlot) {
        const int exactIndex = slotIds.indexOf(*target.exactSlot);
        if (exactIndex < 0) {
            return {};
        }
        return {true, slotIds.at(exactIndex)};
    }

    bool accepted = false;
    const QString selected = QInputDialog::getItem(
        this,
        QStringLiteral("Choose Endpoint attachment position"),
        QStringLiteral("Router (%1, %2) slot")
            .arg(target.router.x)
            .arg(target.router.y),
        labels,
        0,
        false,
        &accepted);
    const int selectedIndex = labels.indexOf(selected);
    if (!accepted || selectedIndex < 0) {
        return {};
    }
    return {true, slotIds.at(selectedIndex)};
}

QJsonValue FinepaperMainWindow::valueFromControl(const ParameterControl& control) const {
    if (const auto* spin = qobject_cast<QSpinBox*>(control.editor)) {
        return spin->value();
    }
    if (const auto* check = qobject_cast<QCheckBox*>(control.editor)) {
        return check->isChecked();
    }
    if (const auto* spin = qobject_cast<QDoubleSpinBox*>(control.editor)) {
        return spin->value();
    }
    if (const auto* combo = qobject_cast<QComboBox*>(control.editor)) {
        return combo->currentData().isValid()
                   ? combo->currentData().toString()
                   : combo->currentText();
    }
    if (const auto* lineEdit = qobject_cast<QLineEdit*>(control.editor)) {
        return lineEdit->text();
    }
    return {};
}

void FinepaperMainWindow::applyParameters() {
    if (m_operationBusy || !m_design || !packageForDesign()) {
        return;
    }
    QJsonObject parameters;
    for (const ParameterControl& control : m_parameterControls) {
        parameters.insert(control.definition.id, valueFromControl(control));
    }
    adoptDesignResult(m_application.updateParameters(*m_design, parameters),
                      QStringLiteral("Apply Parameters"));
}

void FinepaperMainWindow::updateInspector(const NocEditorSelection& selection) {
    m_selectedRouter.reset();
    if (selection.kind == NocEditorSelection::Kind::Router && selection.router) {
        m_selectedRouter = selection.router;
        m_selectionSummary->setText(
            QStringLiteral("<b>Router %1</b><br>Column x: %2<br>Row y: %3<br>"
                           "Drag to arrange this workspace. Router identity and links "
                           "remain derived from the Mesh.")
                .arg(selection.id)
                .arg(selection.router->x)
                .arg(selection.router->y));
        return;
    }
    if (selection.kind == NocEditorSelection::Kind::Endpoint && m_design) {
        for (const EndpointInstance& endpoint : m_design->endpoints) {
            if (endpoint.id == selection.id) {
                m_selectionSummary->setText(
                    QStringLiteral("<b>Endpoint %1</b><br>Type: %2<br>Router: (%3, %4)<br>"
                                   "Slot: %5")
                        .arg(endpoint.id, endpoint.type)
                        .arg(endpoint.attachment.router.x)
                        .arg(endpoint.attachment.router.y)
                        .arg(endpoint.attachment.slot.value_or(QStringLiteral("automatic"))));
                return;
            }
        }
    }
    if (selection.kind == NocEditorSelection::Kind::PendingEndpoint) {
        m_selectionSummary->setText(
            QStringLiteral("<b>Unattached Endpoint</b><br>Type: %1<br>"
                           "Drag the node onto a Router to attach it.")
                .arg(selection.id));
        return;
    }
    m_selectionSummary->setText(QStringLiteral("Nothing selected."));
}

void FinepaperMainWindow::adoptDesignResult(const DesignResult& result, const QString& action) {
    if (!result.success) {
        showDiagnostics(result.diagnostics, action);
        if (m_design) {
            m_nodeEditor->setDesign(&*m_design);
        }
        return;
    }
    m_design = result.design;
    refreshDesignViews();
    setDirty(true);
    appendActivity(action + QStringLiteral(" completed."));
    statusBar()->showMessage(action + QStringLiteral(" completed."));
}

void FinepaperMainWindow::refreshDesignViews() {
    if (!m_design) {
        setWindowTitle(QStringLiteral("Finepaper — NoC Workbench[*]"));
        m_nodeEditor->setDesign(nullptr);
        m_designOverview->setText(QStringLiteral("No design is open."));
        rebuildParameterEditors();
        updateInspector({});
        updateUiState();
        return;
    }

    const bool packageAvailable = packageForDesign();
    setWindowTitle(QStringLiteral("Finepaper — %1[*]").arg(m_design->name));
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
            .arg(packageAvailable
                     ? QString()
                     : QStringLiteral("<p><b>Read-only</b><br>The design Package is not "
                                      "loaded. Endpoint and parameter editing, validation, "
                                      "and generation are disabled.</p>")));
    m_performanceSummary->setText(
        QStringLiteral("NoC %1 currently contains a %2 × %3 Mesh and %4 Endpoint(s). "
                       "This view is reserved for Package or IP Engine performance results; "
                       "the NodeEditor remains the source interaction surface.")
            .arg(m_design->name.toHtmlEscaped())
            .arg(m_design->topology.rows)
            .arg(m_design->topology.columns)
            .arg(m_design->endpoints.size()));

    const QString packageKey = QStringLiteral("%1@%2")
                                   .arg(m_design->package.id, m_design->package.version);
    const int packageIndex = m_packageSelector->findData(packageKey);
    {
        const QSignalBlocker blocker(m_packageSelector);
        if (packageIndex >= 0) {
            m_packageSelector->setCurrentIndex(packageIndex);
        } else {
            m_packageSelector->setCurrentIndex(-1);
        }
    }
    m_designName->setText(m_design->name);
    updateMeshBounds();
    updateEndpointPalette();
    rebuildParameterEditors();
    updateInspector({});
    m_nodeEditor->setDesign(&*m_design);
    updateUiState();
    if (!packageAvailable) {
        statusBar()->showMessage(
            QStringLiteral("Read-only design: Package %1@%2 is not loaded.")
                .arg(m_design->package.id, m_design->package.version));
    }
}

void FinepaperMainWindow::rebuildParameterEditors() {
    while (QLayoutItem* item = m_parameterForm->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_parameterControls.clear();
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        updateUiState();
        return;
    }
    for (const ParameterDefinition& definition : package->parameters) {
        const QJsonValue value = m_design->parameters.value(definition.id);
        QWidget* editor = nullptr;
        if (definition.type == ParameterType::Integer) {
            auto* spin = new QSpinBox;
            spin->setRange(definition.minimum.value_or(-1000000000),
                           definition.maximum.value_or(1000000000));
            spin->setValue(value.toInt());
            editor = spin;
        } else if (definition.type == ParameterType::Number) {
            auto* spin = new QDoubleSpinBox;
            spin->setDecimals(12);
            spin->setRange(
                definition.minimum.value_or(std::numeric_limits<double>::lowest()),
                definition.maximum.value_or(std::numeric_limits<double>::max()));
            spin->setSingleStep(0.1);
            spin->setValue(value.toDouble());
            editor = spin;
        } else if (definition.type == ParameterType::Boolean) {
            auto* check = new QCheckBox;
            check->setChecked(value.toBool());
            editor = check;
        } else if (definition.type == ParameterType::Enumeration) {
            auto* combo = new QComboBox;
            for (const QString& entry : definition.values) {
                combo->addItem(entry, entry);
            }
            const int index = combo->findData(value.toString());
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
            editor = combo;
        } else {
            editor = new QLineEdit(value.toString());
        }
        editor->setObjectName(
            QStringLiteral("finepaper.parameter.%1").arg(definition.id));
        m_parameterForm->addRow(definition.label, editor);
        m_parameterControls.append(ParameterControl{definition, editor});
    }
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
    if (!diagnostics.isEmpty()) {
        populateDiagnostics(diagnostics);
        m_problemReport->setPlainText(diagnosticText(diagnostics));
    }
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

void FinepaperMainWindow::selectCenterView(const QString& id) {
    if (!m_viewRegistry->select(id)) {
        m_viewRegistry->select(workbench::editorViewId);
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
