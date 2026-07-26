#include "gui/main_window.h"

#include "gui/workbench_config.h"
#include "storage/json.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
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
#include <QRegularExpression>
#include <QScrollArea>
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
    void startDrag(Qt::DropActions) override {
        QListWidgetItem* item = currentItem();
        if (!item) {
            return;
        }
        const QString endpointType = item->data(Qt::UserRole).toString();
        if (endpointType.isEmpty()) {
            return;
        }
        auto* mimeData = new QMimeData;
        mimeData->setData(workbench::endpointTypeMime, endpointType.toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->setPixmap(style()->standardIcon(QStyle::SP_ArrowRight).pixmap(32, 32));
        drag->exec(Qt::CopyAction);
    }
};

FinepaperMainWindow::FinepaperMainWindow(RuntimeLocations locations, QWidget* parent)
    : QMainWindow(parent),
      m_locations(std::move(locations)) {
    loadInstalledPackageRoots();
    createUi();
    reloadPackages();
    restoreWorkbenchState();
    statusBar()->showMessage(
        QStringLiteral("Install or select a NoC Package, then create a Mesh NoC."));
}

void FinepaperMainWindow::createUi() {
    setWindowTitle(QStringLiteral("Finepaper — NoC Workbench"));
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

    resizeDocks({m_packageDock, m_inspectorDock}, {285, 330}, Qt::Horizontal);
    resizeDocks({m_resultsDock}, {250}, Qt::Vertical);
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
                                                RouterPosition router) {
        addEndpoint(endpointType, router);
    };
    m_nodeEditor->endpointMoveRequested = [this](const QString& endpointId,
                                                  RouterPosition router) {
        moveEndpoint(endpointId, router);
    };
    m_nodeEditor->endpointAttachmentRequested = [this](RouterPosition router) {
        showEndpointAttachmentMenu(router);
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
    auto* installButton = new QPushButton(QStringLiteral("Install…"));
    auto* reloadButton = new QPushButton(QStringLiteral("Reload"));
    packageButtons->addWidget(installButton);
    packageButtons->addWidget(reloadButton);
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
    auto* createButton = new QPushButton(QStringLiteral("Create / Replace Design"));
    createButton->setObjectName(QStringLiteral("finepaper.createDesign"));
    createLayout->addRow(createButton);
    layout->addWidget(createGroup);

    auto* paletteHeading = new QLabel(QStringLiteral("Endpoint Types"));
    QFont paletteFont = paletteHeading->font();
    paletteFont.setBold(true);
    paletteHeading->setFont(paletteFont);
    layout->addWidget(paletteHeading);
    layout->addWidget(new QLabel(
        QStringLiteral("Drag onto a Router, or select a Router and double-click a type.")));
    m_endpointPalette = new EndpointPaletteList;
    m_endpointPalette->setObjectName(QStringLiteral("finepaper.endpointPalette"));
    m_endpointPalette->setDragEnabled(true);
    m_endpointPalette->setSelectionMode(QAbstractItemView::SingleSelection);
    m_endpointPalette->setAlternatingRowColors(true);
    layout->addWidget(m_endpointPalette, 1);

    m_packageDock->setWidget(content);
    addDockWidget(Qt::LeftDockWidgetArea, m_packageDock);

    connect(installButton, &QPushButton::clicked, this, &FinepaperMainWindow::installPackage);
    connect(reloadButton, &QPushButton::clicked, this, &FinepaperMainWindow::reloadPackages);
    connect(createButton, &QPushButton::clicked, this, &FinepaperMainWindow::createDesign);
    connect(m_packageSelector, &QComboBox::currentIndexChanged, this, [this](int) {
        updateMeshBounds();
        updateEndpointPalette();
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
                addEndpoint(item->data(Qt::UserRole).toString(), *m_selectedRouter);
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
    auto* selectionLayout = new QVBoxLayout(selectionGroup);
    m_selectionSummary = new QLabel(QStringLiteral("Nothing selected."));
    m_selectionSummary->setWordWrap(true);
    m_selectionSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_attachEndpoint = new QPushButton(QStringLiteral("Attach Endpoint to selected Router…"));
    m_attachEndpoint->setObjectName(QStringLiteral("finepaper.attachEndpoint"));
    m_attachEndpoint->setEnabled(false);
    auto* removeEndpoint = new QPushButton(QStringLiteral("Remove selected Endpoint"));
    selectionLayout->addWidget(m_selectionSummary);
    selectionLayout->addWidget(m_attachEndpoint);
    selectionLayout->addWidget(removeEndpoint);
    layout->addWidget(selectionGroup);

    auto* parameterGroup = new QGroupBox(QStringLiteral("NoC Parameters"));
    auto* parameterGroupLayout = new QVBoxLayout(parameterGroup);
    auto* parameterContent = new QWidget;
    m_parameterForm = new QFormLayout(parameterContent);
    auto* parameterScroll = new QScrollArea;
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setWidget(parameterContent);
    auto* applyButton = new QPushButton(QStringLiteral("Apply Parameters"));
    parameterGroupLayout->addWidget(parameterScroll, 1);
    parameterGroupLayout->addWidget(applyButton);
    layout->addWidget(parameterGroup, 1);

    m_inspectorDock->setWidget(content);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    connect(removeEndpoint, &QPushButton::clicked,
            this, &FinepaperMainWindow::removeSelectedEndpoint);
    connect(m_attachEndpoint, &QPushButton::clicked, this, [this] {
        if (m_selectedRouter) {
            showEndpointAttachmentMenu(*m_selectedRouter);
        }
    });
    connect(applyButton, &QPushButton::clicked,
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
    auto* browseOutput = new QPushButton(QStringLiteral("Browse…"));
    auto* generateButton = new QPushButton(QStringLiteral("Generate RTL"));
    outputControls->addWidget(new QLabel(QStringLiteral("Output root")));
    outputControls->addWidget(m_outputRoot, 1);
    outputControls->addWidget(browseOutput);
    outputControls->addWidget(generateButton);
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

    connect(browseOutput, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Select output root"), m_outputRoot->text());
        if (!directory.isEmpty()) {
            m_outputRoot->setText(directory);
        }
    });
    connect(generateButton, &QPushButton::clicked,
            this, &FinepaperMainWindow::generateDesign);
}

void FinepaperMainWindow::createActions() {
    auto* newAction = new QAction(
        style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("New Mesh…"), this);
    newAction->setShortcut(QKeySequence::New);
    auto* openAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("Open…"), this);
    openAction->setShortcut(QKeySequence::Open);
    auto* saveAction = new QAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Save"), this);
    saveAction->setShortcut(QKeySequence::Save);
    auto* installAction = new QAction(QStringLiteral("Install Package Directory…"), this);
    auto* reloadAction = new QAction(QStringLiteral("Reload Packages"), this);
    auto* validateAction = new QAction(QStringLiteral("Validate / DRC"), this);
    validateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    auto* generateAction = new QAction(QStringLiteral("Generate RTL"), this);
    generateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    auto* fitAction = new QAction(QStringLiteral("Fit NoC in View"), this);
    fitAction->setShortcut(QKeySequence(QStringLiteral("F")));

    connect(newAction, &QAction::triggered, this, &FinepaperMainWindow::createDesign);
    connect(openAction, &QAction::triggered, this, &FinepaperMainWindow::openDesign);
    connect(saveAction, &QAction::triggered, this, &FinepaperMainWindow::saveDesign);
    connect(installAction, &QAction::triggered, this, &FinepaperMainWindow::installPackage);
    connect(reloadAction, &QAction::triggered, this, &FinepaperMainWindow::reloadPackages);
    connect(validateAction, &QAction::triggered, this, &FinepaperMainWindow::validateDesign);
    connect(generateAction, &QAction::triggered, this, &FinepaperMainWindow::generateDesign);
    connect(fitAction, &QAction::triggered, m_nodeEditor, &NocNodeEditor::zoomToFit);

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
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    QMenu* packageMenu = menuBar()->addMenu(QStringLiteral("&Package"));
    packageMenu->addAction(installAction);
    packageMenu->addAction(reloadAction);
    QMenu* runMenu = menuBar()->addMenu(QStringLiteral("&Run"));
    runMenu->addAction(validateAction);
    runMenu->addAction(generateAction);

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
    viewMenu->addAction(fitAction);
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
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction);
    toolbar->addSeparator();
    toolbar->addAction(validateAction);
    toolbar->addAction(generateAction);
    toolbar->addSeparator();
    toolbar->addAction(fitAction);

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
    const QVector<Diagnostic> diagnostics = m_application.reloadPackages(m_locations.packageRoots);
    updatePackageControls();
    showDiagnostics(diagnostics, QStringLiteral("Package discovery"), false);
    appendActivity(QStringLiteral("Loaded %1 runtime Package(s) from %2 configured root(s).")
                       .arg(m_application.packages().size())
                       .arg(m_locations.packageRoots.size()));
    if (!hasErrors(diagnostics)) {
        statusBar()->showMessage(
            QStringLiteral("Loaded %1 runtime Package(s).")
                .arg(m_application.packages().size()));
    }
}

void FinepaperMainWindow::installPackage() {
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
    }
    updateMeshBounds();
    updateEndpointPalette();
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
    const PackageDefinition* package = packageForDesign();
    if (!package) {
        package = selectedPackage();
    }
    if (!package) {
        return;
    }
    for (const EndpointTypeDefinition& type : package->endpointTypes) {
        auto* item = new QListWidgetItem(type.label, m_endpointPalette);
        item->setData(Qt::UserRole, type.id);
        item->setToolTip(QStringLiteral("%1\nDrag onto a Router, or double-click after selecting one.")
                             .arg(type.id));
    }
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
    m_designPath.clear();
    adoptDesignResult(m_application.createDesign(request), QStringLiteral("Create Mesh"));
    selectCenterView(workbench::editorViewId);
}

void FinepaperMainWindow::openDesign() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open NoC design"), QString(),
        QStringLiteral("Finepaper NoC (*.fpnoc *.json)"));
    if (path.isEmpty()) {
        return;
    }
    const DesignResult loaded = m_application.loadDesignFile(path);
    if (!loaded.success) {
        showDiagnostics(loaded.diagnostics, QStringLiteral("Open design"));
        return;
    }
    m_design = loaded.design;
    m_designPath = path;
    refreshDesignViews();
    selectCenterView(workbench::editorViewId);
    appendActivity(QStringLiteral("Opened design %1.").arg(path));
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(path));
}

void FinepaperMainWindow::saveDesign() {
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Save design"),
                                 QStringLiteral("Create or open a NoC design first."));
        return;
    }
    if (m_designPath.isEmpty()) {
        m_designPath = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save NoC design"),
            QDir::current().filePath(m_design->id + QStringLiteral(".fpnoc")),
            QStringLiteral("Finepaper NoC (*.fpnoc)"));
    }
    if (m_designPath.isEmpty()) {
        return;
    }
    QVector<Diagnostic> diagnostics;
    if (!m_application.saveDesignFile(m_designPath, *m_design, &diagnostics)) {
        showDiagnostics(diagnostics, QStringLiteral("Save design"));
        return;
    }
    appendActivity(QStringLiteral("Saved design %1.").arg(m_designPath));
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(m_designPath));
}

void FinepaperMainWindow::validateDesign() {
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Validate design"),
                                 QStringLiteral("Create or open a NoC design first."));
        return;
    }
    const ValidationResult result = m_application.validate(*m_design, true);
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
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Generate RTL"),
                                 QStringLiteral("Create or open a NoC design first."));
        return;
    }
    const QString root = m_outputRoot->text().trimmed();
    if (root.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Generate RTL"),
                             QStringLiteral("Choose an output root."));
        return;
    }
    appendActivity(QStringLiteral("Starting RTL generation in %1.").arg(root));
    const GenerationResult result = m_application.generate(*m_design, GenerationOptions{root});
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

void FinepaperMainWindow::addEndpoint(const QString& endpointType, RouterPosition router) {
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Add Endpoint"),
                                 QStringLiteral("Create or open a NoC design first."));
        return;
    }
    EndpointInstance endpoint;
    endpoint.id = nextEndpointId(endpointType);
    endpoint.type = endpointType;
    endpoint.attachment.router = router;
    adoptDesignResult(m_application.addEndpoint(*m_design, endpoint),
                      QStringLiteral("Add Endpoint %1").arg(endpoint.id));
}

void FinepaperMainWindow::showEndpointAttachmentMenu(RouterPosition router) {
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        statusBar()->showMessage(
            QStringLiteral("Create a design with an available NoC Package first."), 5000);
        return;
    }

    QMenu menu(this);
    menu.setTitle(QStringLiteral("Attach Endpoint"));
    for (const EndpointTypeDefinition& type : package->endpointTypes) {
        QAction* action = menu.addAction(type.label);
        action->setToolTip(type.id);
        connect(action, &QAction::triggered, this, [this, type, router] {
            addEndpoint(type.id, router);
        });
    }
    if (menu.actions().isEmpty()) {
        statusBar()->showMessage(
            QStringLiteral("This NoC Package does not declare any Endpoint types."), 5000);
        return;
    }
    menu.exec(QCursor::pos());
}

void FinepaperMainWindow::moveEndpoint(const QString& endpointId, RouterPosition router) {
    if (!m_design) {
        return;
    }
    adoptDesignResult(m_application.moveEndpoint(*m_design, endpointId, router),
                      QStringLiteral("Move Endpoint %1").arg(endpointId));
}

void FinepaperMainWindow::removeSelectedEndpoint() {
    if (!m_design || m_selectedEndpointId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Remove Endpoint"),
                                 QStringLiteral("Select an Endpoint in the NoC Editor first."));
        return;
    }
    const QString endpointId = m_selectedEndpointId;
    adoptDesignResult(m_application.removeEndpoint(*m_design, endpointId),
                      QStringLiteral("Remove Endpoint %1").arg(endpointId));
}

QJsonValue FinepaperMainWindow::valueFromControl(const ParameterControl& control) const {
    if (const auto* spin = qobject_cast<QSpinBox*>(control.editor)) {
        return spin->value();
    }
    if (const auto* check = qobject_cast<QCheckBox*>(control.editor)) {
        return check->isChecked();
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
    if (!m_design) {
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
    m_selectedEndpointId.clear();
    m_selectedRouter.reset();
    m_attachEndpoint->setEnabled(false);
    if (selection.kind == NocEditorSelection::Kind::Router && selection.router) {
        m_selectedRouter = selection.router;
        m_attachEndpoint->setEnabled(true);
        m_selectionSummary->setText(
            QStringLiteral("<b>Router %1</b><br>Column x: %2<br>Row y: %3<br>"
                           "Drag to arrange this workspace. Use +/− to collapse or expand. "
                           "Right-click to attach an Endpoint. Router identity and links "
                           "remain derived from the Mesh.")
                .arg(selection.id)
                .arg(selection.router->x)
                .arg(selection.router->y));
        return;
    }
    if (selection.kind == NocEditorSelection::Kind::Endpoint && m_design) {
        for (const EndpointInstance& endpoint : m_design->endpoints) {
            if (endpoint.id == selection.id) {
                m_selectedEndpointId = endpoint.id;
                m_selectionSummary->setText(
                    QStringLiteral("<b>Endpoint %1</b><br>Type: %2<br>Router: (%3, %4)<br>"
                                   "Drag this node onto another Router to move it.")
                        .arg(endpoint.id, endpoint.type)
                        .arg(endpoint.attachment.router.x)
                        .arg(endpoint.attachment.router.y));
                return;
            }
        }
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
    appendActivity(action + QStringLiteral(" completed."));
    statusBar()->showMessage(action + QStringLiteral(" completed."));
}

void FinepaperMainWindow::refreshDesignViews() {
    if (!m_design) {
        m_nodeEditor->setDesign(nullptr);
        m_designOverview->setText(QStringLiteral("No design is open."));
        return;
    }

    setWindowTitle(QStringLiteral("Finepaper — %1").arg(m_design->name));
    m_designOverview->setText(
        QStringLiteral("<h3>%1</h3><p><b>Package</b><br>%2@%3</p>"
                       "<p><b>Topology</b><br>%4 × %5 Mesh</p>"
                       "<p><b>Endpoints</b><br>%6</p>")
            .arg(m_design->name.toHtmlEscaped(),
                 m_design->package.id.toHtmlEscaped(),
                 m_design->package.version.toHtmlEscaped())
            .arg(m_design->topology.rows)
            .arg(m_design->topology.columns)
            .arg(m_design->endpoints.size()));
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
    if (packageIndex >= 0) {
        const QSignalBlocker blocker(m_packageSelector);
        m_packageSelector->setCurrentIndex(packageIndex);
    }
    m_designName->setText(m_design->name);
    updateMeshBounds();
    updateEndpointPalette();
    rebuildParameterEditors();
    updateInspector({});
    m_nodeEditor->setDesign(&*m_design);
}

void FinepaperMainWindow::rebuildParameterEditors() {
    while (QLayoutItem* item = m_parameterForm->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_parameterControls.clear();
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        return;
    }
    for (const ParameterDefinition& definition : package->parameters) {
        const QJsonValue value = m_design->parameters.value(definition.id);
        QWidget* editor = nullptr;
        if (definition.type == QStringLiteral("integer")) {
            auto* spin = new QSpinBox;
            spin->setRange(definition.minimum.value_or(-1000000000),
                           definition.maximum.value_or(1000000000));
            spin->setValue(value.toInt());
            editor = spin;
        } else if (definition.type == QStringLiteral("boolean")) {
            auto* check = new QCheckBox;
            check->setChecked(value.toBool());
            editor = check;
        } else if (definition.type == QStringLiteral("enum")) {
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
        m_parameterForm->addRow(definition.label, editor);
        m_parameterControls.append(ParameterControl{definition, editor});
    }
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
