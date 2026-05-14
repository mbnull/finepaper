// MainWindow — constructs and connects all top-level UI components.
// Layout: horizontal splitter (IP catalog | node editor | property panel)
// inside a vertical splitter with the log panel below.
#include "app/appsettings.h"
#include "app/mainwindow.h"
#include "app/projectgenerationrunner.h"
#include "commands/addipinstancecommand.h"
#include "commands/removeipinstancecommand.h"
#include "commands/topologypresetcommand.h"
#include "graph/graph.h"
#include "commands/commandmanager.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcoreruntimediagnostics.h"
#include "nodeeditor/nodeeditorwidget.h"
#include "panels/ipcatalogpanel.h"
#include "panels/propertypanel.h"
#include "panels/logpanel.h"
#include "project/ipinstanceparameteradapter.h"
#include "project/graphprojectserializer.h"
#include "project/projectipservice.h"
#include "project/projectreader.h"
#include "project/projectstateservice.h"
#include "project/projectwriter.h"
#include "topology/topologypresetbuilder.h"
#include "validation/validationmanager.h"
#include "workspace/activeworkspacecontroller.h"
#include "modules/moduleregistry.h"
#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QKeySequence>
#include <QList>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtGlobal>
#include <QVBoxLayout>
#include <optional>

namespace {

QString projectFileDialogSaveFilter() {
    return QStringLiteral("Finepaper Project (*.fpproj)");
}

QString projectFileDialogOpenFilter() {
    return QStringLiteral("Finepaper Project (*.fpproj);;All Files (*)");
}

QString pathWithProjectExtension(QString path) {
    return QFileInfo(path).suffix().isEmpty() ? path + QStringLiteral(".fpproj") : path;
}

QString documentDisplayName(const QString& path) {
    return path.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path).fileName();
}

void updateRecentProjectState(AppSettings* settings, const QString& path) {
    if (!settings || path.trimmed().isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    settings->addRecentProject(info.absoluteFilePath());
    settings->setLastDirectory(info.absolutePath());
}

void appendLogLines(LogPanel* logPanel,
                    const QString& text,
                    const QColor& color,
                    const QString& prefix) {
    if (!logPanel) {
        return;
    }

    const QStringList lines = text.split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        logPanel->appendMessage(prefix + trimmed, color);
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_graph(new Graph(this)),
      m_commandManager(std::make_unique<CommandManager>()),
      m_appSettings(std::make_unique<AppSettings>()),
      m_ipCatalogService(std::make_unique<IpCatalogService>(IpCatalogService::fromRuntimeRegistries())),
      m_projectStateService(std::make_unique<ProjectStateService>()),
      m_projectIpService(std::make_unique<ProjectIpService>(m_projectStateService.get())),
      m_activeWorkspaceController(std::make_unique<ActiveWorkspaceController>(
          m_projectIpService.get(),
          m_ipCatalogService.get())),
      m_nodeEditor(nullptr),
      m_propertyPanel(nullptr),
      m_ipCatalogPanel(nullptr),
      m_logPanel(nullptr),
      m_validationManager(nullptr),
      m_ipCatalogDock(nullptr),
      m_propertyDock(nullptr),
      m_logDock(nullptr),
      m_newAction(nullptr),
      m_openAction(nullptr),
      m_saveAction(nullptr),
      m_saveAsAction(nullptr),
      m_undoAction(nullptr),
      m_redoAction(nullptr),
      m_generateAction(nullptr),
      m_validateAction(nullptr),
      m_arrangeAction(nullptr),
      m_topologyMenu(nullptr) {
    // Build the window in dependency order: widgets first, then signal wiring,
    // then actions/menus that depend on those widgets.
    setupPanels();
    setupConnections();
    setupActions();
    setCentralWidget(createCentralContent());
    setupDocks();
    const QByteArray geometry = m_appSettings ? m_appSettings->mainWindowGeometry() : QByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(1920, 1080);
    }
    const QByteArray state = m_appSettings ? m_appSettings->mainWindowState() : QByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    setProjectOpen(false);
    appendStartupLog();
    scheduleStartupLayoutLog();
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadGraph(const QString& path) {
    return loadDocument(path);
}

bool MainWindow::hasOpenProject() const {
    return m_projectOpen;
}

bool MainWindow::createProjectAt(const QString& path) {
    const QString projectPath = path.trimmed().isEmpty()
        ? QString()
        : QFileInfo(pathWithProjectExtension(path)).absoluteFilePath();
    if (projectPath.isEmpty()) {
        return false;
    }

    Graph emptyGraph;
    ProjectDocument document =
        GraphProjectSerializer::toProject(emptyGraph, QFileInfo(projectPath).completeBaseName());
    const ProjectWriteResult result = ProjectWriter::writeFile(projectPath, document);
    if (!result.success) {
        qWarning() << "Failed to create project at" << projectPath << result.error;
        QMessageBox::warning(this, "Create Project Failed", result.error);
        return false;
    }

    clearDocument();
    setCurrentDocumentPath(projectPath);
    m_cleanStateId = m_commandManager->currentStateId();
    setProjectOpen(true);
    syncDocumentStateFromHistory();
    updateRecentProjectState(m_appSettings.get(), projectPath);
    statusBar()->showMessage("Created " + QFileInfo(projectPath).fileName(), 5000);
    qInfo() << "Created project at" << projectPath;
    return true;
}

void MainWindow::saveGraph() {
    if (!requireOpenProject(QStringLiteral("saving the project"))) {
        return;
    }

    if (!m_currentDocumentPath.isEmpty()) {
        saveDocument(m_currentDocumentPath);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save Project",
                                                      defaultDocumentPath(),
                                                      projectFileDialogSaveFilter());
    if (path.isEmpty()) {
        return;
    }

    saveDocument(pathWithProjectExtension(path));
}

void MainWindow::saveGraphAs() {
    if (!requireOpenProject(QStringLiteral("saving the project"))) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(this,
                                                "Save Project As",
                                                defaultDocumentPath(),
                                                projectFileDialogSaveFilter());
    if (path.isEmpty()) {
        return;
    }

    saveDocument(pathWithProjectExtension(path));
}

void MainWindow::newGraph() {
    if (!maybeSaveChanges(QStringLiteral("creating a new project"))) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Create Project",
                                                      defaultProjectDirectoryPath(),
                                                      projectFileDialogSaveFilter());
    if (path.isEmpty()) {
        return;
    }

    createProjectAt(pathWithProjectExtension(path));
}

void MainWindow::openGraph() {
    if (!maybeSaveChanges(QStringLiteral("opening another project"))) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this,
                                                      "Open Project",
                                                      defaultDocumentPath(),
                                                      projectFileDialogOpenFilter());
    if (path.isEmpty()) {
        return;
    }

    loadDocument(path);
}

void MainWindow::generateVerilog() {
    if (!requireOpenProject(QStringLiteral("generating Verilog"))) {
        return;
    }

    if (m_currentDocumentPath.trimmed().isEmpty()) {
        const QString error = QStringLiteral("Save the project before generation.");
        m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
        statusBar()->showMessage(error, 5000);
        QMessageBox::warning(this, "Generate Failed", error);
        return;
    }

    ProjectGenerationRequest request;
    request.graph = m_graph;
    request.projectPath = m_currentDocumentPath;
    request.designName = QFileInfo(m_currentDocumentPath).completeBaseName();
    request.catalogEntries = m_ipCatalogService ? m_ipCatalogService->entries() : QList<IpCatalogEntry>{};
    request.instances = m_projectStateService
        ? m_projectStateService->ipInstanceRecords()
        : QVector<ProjectIpInstanceRecord>{};

    const QString outputRoot =
        QFileInfo(m_currentDocumentPath).absoluteDir().filePath(QStringLiteral("generated"));
    m_logPanel->appendMessage(QString("[Generate] Start project=%1 output=%2")
                                  .arg(m_currentDocumentPath, outputRoot),
                              QColor(70, 110, 190));

    statusBar()->showMessage("Generating project IP instances...");
    QApplication::setOverrideCursor(Qt::WaitCursor);

    ProjectGenerationRunner runner;
    const ProjectGenerationResult result = runner.generate(request);
    QApplication::restoreOverrideCursor();

    for (const ProjectGenerationInstanceResult& instance : result.instances) {
        const QString instanceId = instance.instanceId.isEmpty()
            ? QStringLiteral("<unknown>")
            : instance.instanceId;
        const QColor statusColor = instance.success ? QColor(40, 140, 80) : QColor(220, 50, 50);
        m_logPanel->appendMessage(QString("[Generate][%1] output=%2")
                                      .arg(instanceId, instance.outputDirectory),
                                  QColor(70, 110, 190));
        m_logPanel->appendMessage(QString("[Generate][%1] input=%2")
                                      .arg(instanceId, instance.inputPath),
                                  QColor(70, 110, 190));
        m_logPanel->appendMessage(QString("[Generate][%1] manifest=%2")
                                      .arg(instanceId, instance.manifestPath),
                                  QColor(70, 110, 190));
        m_logPanel->appendMessage(QString("[Generate][%1] %2")
                                      .arg(instanceId,
                                           instance.success ? QStringLiteral("complete")
                                                            : instance.error),
                                  statusColor);
        appendLogLines(m_logPanel,
                       instance.standardOutput,
                       instance.success ? QColor(40, 140, 80) : QColor(80, 120, 180),
                       QString("[Generate][%1][stdout] ").arg(instanceId));
        appendLogLines(m_logPanel,
                       instance.standardError,
                       instance.success ? QColor(200, 150, 50) : QColor(220, 50, 50),
                       QString("[Generate][%1][stderr] ").arg(instanceId));
    }

    if (!result.snapshotPath.isEmpty()) {
        m_logPanel->appendMessage(QString("[Generate] Project snapshot=%1").arg(result.snapshotPath),
                                  QColor(70, 110, 190));
    }

    if (!result.success) {
        const QString error = result.error.isEmpty()
            ? QStringLiteral("Generation failed.")
            : result.error;
        qWarning().noquote() << error;
        m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
        statusBar()->showMessage(error, 5000);
        QMessageBox::warning(this, "Generate Failed", error);
        return;
    }

    const QString successMessage =
        QStringLiteral("Generated project IP instances in %1").arg(result.outputRoot);
    qInfo().noquote() << successMessage;
    m_logPanel->appendMessage("[Generate] " + successMessage, QColor(40, 140, 80));
    statusBar()->showMessage(successMessage, 5000);
}

void MainWindow::runValidation() {
    if (!requireOpenProject(QStringLiteral("running validation"))) {
        return;
    }

    if (!m_validationManager) {
        qCritical() << "Validation manager not initialized, cannot run validation";
        return;
    }

    qInfo() << "Validation requested by user";
    m_validationManager->runValidation();
}

void MainWindow::undo() {
    if (!requireOpenProject(QStringLiteral("undoing changes"))) {
        return;
    }
    m_commandManager->undo();
    syncDocumentStateFromHistory();
}

void MainWindow::redo() {
    if (!requireOpenProject(QStringLiteral("redoing changes"))) {
        return;
    }
    m_commandManager->redo();
    syncDocumentStateFromHistory();
}

void MainWindow::createTopologyPreset() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action || !m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
        return;
    }

    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
    createTopologyPresetFor(workspace.ipcoreId, workspace.instanceId, action->data().toString());
}

void MainWindow::createTopologyPresetFor(const QString& ipcoreId,
                                         const QString& instanceId,
                                         const QString& presetId) {
    if (!requireOpenProject(QStringLiteral("creating topology"))) {
        return;
    }
    if (!m_activeWorkspaceController ||
        ipcoreId.trimmed().isEmpty() ||
        instanceId.trimmed().isEmpty() ||
        presetId.trimmed().isEmpty()) {
        qWarning().noquote() << "Ignoring incomplete topology preset request";
        return;
    }

    const std::optional<ActiveWorkspaceContext> context =
        m_activeWorkspaceController->activeContext();
    if (!context.has_value() ||
        context->record.ipcoreId != ipcoreId ||
        context->record.instanceId != instanceId) {
        qWarning().noquote() << "Ignoring topology preset request for inactive IP instance:"
                             << ipcoreId << instanceId;
        return;
    }

    // Presets are IP-core-owned; look them up by ID at trigger time so rebuilt
    // menus cannot leave stale QAction pointers to deleted descriptors.
    const IpCatalogEntry& entry = context->entry;
    auto presetIt = std::find_if(entry.topologyPresets.cbegin(),
                                 entry.topologyPresets.cend(),
                                 [&](const TopologyPresetDescriptor& preset) {
                                     return preset.id == presetId;
                                 });
    if (presetIt == entry.topologyPresets.cend()) {
        qWarning().noquote() << "Ignoring unknown topology preset request:" << presetId;
        return;
    }

    TopologyPresetRequest request;
    request.ipcoreId = ipcoreId;
    request.instanceId = instanceId;
    request.preset = *presetIt;

    QStringList parameterNames = presetIt->parameters.keys();
    parameterNames.sort();
    for (const QString& name : parameterNames) {
        // Deterministic prompt ordering makes repeated preset creation and UI
        // tests stable even though descriptor parameters are stored in a hash.
        const TopologyPresetParameterDescriptor parameter = presetIt->parameters.value(name);
        bool ok = false;
        const int value = QInputDialog::getInt(this,
                                               presetIt->label,
                                               parameter.label,
                                               parameter.defaultValue,
                                               parameter.minimumValue,
                                               parameter.maximumValue,
                                               1,
                                               &ok);
        if (!ok) {
            return;
        }
        request.parameters.insert(name, value);
    }

    const std::optional<ActiveWorkspaceContext> contextBeforeExecute =
        m_activeWorkspaceController->activeContext();
    if (!contextBeforeExecute.has_value() ||
        contextBeforeExecute->record.ipcoreId != ipcoreId ||
        contextBeforeExecute->record.instanceId != instanceId) {
        qWarning().noquote() << "Ignoring topology preset request after active IP changed:"
                             << ipcoreId << instanceId;
        return;
    }

    std::unique_ptr<Command> rejected =
        m_commandManager->executeCommand(std::make_unique<TopologyPresetCommand>(
            m_graph,
            &ModuleRegistry::instance(),
            request));
    if (auto* failed = dynamic_cast<TopologyPresetCommand*>(rejected.get())) {
        QMessageBox::warning(this, "Topology", failed->result().error);
        return;
    }

    syncDocumentStateFromHistory();
    statusBar()->showMessage(QString("Created %1 topology").arg(presetIt->label), 5000);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (maybeSaveChanges(QStringLiteral("closing the window"))) {
        if (m_appSettings) {
            m_appSettings->setMainWindowGeometry(saveGeometry());
            m_appSettings->setMainWindowState(saveState());
        }
        event->accept();
        return;
    }

    event->ignore();
}

void MainWindow::setupPanels() {
    m_ipInstanceParameterAdapters.clear();
    QVector<IIpInstanceParameterAdapter*> ipInstanceParameterAdapters;
    for (const IpCatalogEntry& entry : m_ipCatalogService->entries()) {
        auto adapter = std::make_unique<CatalogIpInstanceParameterAdapter>(
            entry.id,
            entry.name,
            entry.instanceParameters);
        ipInstanceParameterAdapters.push_back(adapter.get());
        m_ipInstanceParameterAdapters.push_back(std::move(adapter));
    }

    m_nodeEditor = new NodeEditorWidget(m_graph,
                                        m_projectStateService.get(),
                                        m_activeWorkspaceController.get(),
                                        m_commandManager.get(),
                                        this);
    m_propertyPanel = new PropertyPanel(m_graph,
                                        m_projectStateService.get(),
                                        ipInstanceParameterAdapters,
                                        m_commandManager.get(),
                                        this);
    m_ipCatalogPanel = new IpCatalogPanel(m_ipCatalogService.get(),
                                          m_projectStateService.get(),
                                          m_projectIpService.get(),
                                          m_activeWorkspaceController.get(),
                                          this);
    m_logPanel = new LogPanel(this);
    m_validationManager = new ValidationManager(m_graph,
                                                m_projectStateService.get(),
                                                m_ipCatalogService.get(),
                                                m_activeWorkspaceController.get(),
                                                m_logPanel,
                                                this);

    m_nodeEditor->setObjectName("nodeEditorPanel");
    m_propertyPanel->setObjectName("propertyPanel");
    m_logPanel->setObjectName("logPanel");
}

void MainWindow::setupConnections() {
    // Keep validation-entry selection synchronized between the log, canvas, and property panel.
    connect(m_logPanel, &LogPanel::elementSelected, m_nodeEditor, &NodeEditorWidget::highlightElement);
    connect(m_nodeEditor,
            &NodeEditorWidget::moduleSelected,
            m_propertyPanel,
            QOverload<QString>::of(&PropertyPanel::setSelectedModule));

    const auto trackGraphChange = [this]() {
        // Bulk load/import paths suppress tracking until the graph is fully
        // rebuilt; otherwise intermediate signals would mark a clean document dirty.
        if (m_suppressDocumentTracking) {
            return;
        }
        scheduleDocumentStateRefresh();
    };

    connect(m_graph, &Graph::moduleAdded, this, [trackGraphChange](Module*) { trackGraphChange(); });
    connect(m_graph, &Graph::moduleRemoved, this, [trackGraphChange](const QString&) { trackGraphChange(); });
    connect(m_graph, &Graph::connectionAdded, this, [trackGraphChange](Connection*) { trackGraphChange(); });
    connect(m_graph, &Graph::connectionRemoved, this, [trackGraphChange](const QString&) { trackGraphChange(); });
    connect(m_graph, &Graph::parameterChanged, this, [trackGraphChange](const QString&, const QString&) {
        trackGraphChange();
    });
    connect(m_projectStateService.get(),
            &ProjectStateService::parameterChanged,
            this,
            [trackGraphChange](const QString&, const QString&, const QString&, const QString&) {
                trackGraphChange();
            });
    connect(m_projectIpService.get(),
            &ProjectIpService::ipInstancesChanged,
            this,
            [trackGraphChange]() {
                trackGraphChange();
            });
    connect(m_activeWorkspaceController.get(),
            &ActiveWorkspaceController::activeWorkspaceChanged,
            this,
            &MainWindow::rebuildTopologyMenu);
    connect(m_ipCatalogPanel,
            &IpCatalogPanel::addIpcoreRequested,
            this,
            [this](const QString& ipcoreId) {
                if (!requireOpenProject(QStringLiteral("editing the IP catalog"))) {
                    return;
                }
                const std::optional<IpCatalogEntry> entry = m_ipCatalogService->entry(ipcoreId);
                if (!entry.has_value()) {
                    return;
                }
                std::unique_ptr<Command> rejected = m_commandManager->executeCommand(
                    std::make_unique<AddIpInstanceCommand>(m_projectStateService.get(),
                                                           m_projectIpService.get(),
                                                           *entry));
                if (rejected) {
                    QMessageBox::warning(this,
                                         "IP Catalog",
                                         "IP instance could not be created.");
                    return;
                }
                syncDocumentStateFromHistory();
            });
    connect(m_ipCatalogPanel,
            &IpCatalogPanel::selectIpInstanceRequested,
            this,
            [this](const QString& ipcoreId, const QString& instanceId) {
                m_projectIpService->selectInstance(ipcoreId, instanceId);
            });
    connect(m_ipCatalogPanel,
            &IpCatalogPanel::removeIpInstanceRequested,
            this,
            [this](const QString& ipcoreId, const QString& instanceId) {
                if (!requireOpenProject(QStringLiteral("editing the IP catalog"))) {
                    return;
                }
                std::unique_ptr<Command> rejected = m_commandManager->executeCommand(
                    std::make_unique<RemoveIpInstanceCommand>(m_graph,
                                                              m_projectStateService.get(),
                                                              m_projectIpService.get(),
                                                              ipcoreId,
                                                              instanceId));
                if (rejected) {
                    QMessageBox::warning(this,
                                         "IP Catalog",
                                         QStringLiteral("Could not remove the selected IP instance."));
                    return;
                }
                syncDocumentStateFromHistory();
            });
    connect(m_ipCatalogPanel,
            &IpCatalogPanel::workspaceToolRequested,
            this,
            [this](const QString& toolId, const QString& ipcoreId, const QString& instanceId) {
                static const QString topologyPrefix = QStringLiteral("topology:");
                if (!toolId.startsWith(topologyPrefix)) {
                    qWarning().noquote() << "Ignoring unsupported workspace tool request:" << toolId;
                    return;
                }

                createTopologyPresetFor(ipcoreId, instanceId, toolId.mid(topologyPrefix.size()));
            });
}

void MainWindow::setupActions() {
    // Menu and toolbar share the same QAction instances to keep enabled/check
    // states synchronized automatically.
    m_newAction = new QAction("New", this);
    m_newAction->setObjectName(QStringLiteral("newAction"));
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newGraph);

    m_openAction = new QAction("Open...", this);
    m_openAction->setObjectName(QStringLiteral("openAction"));
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openGraph);

    m_saveAction = new QAction("Save", this);
    m_saveAction->setObjectName(QStringLiteral("saveAction"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveGraph);

    m_saveAsAction = new QAction("Save As...", this);
    m_saveAsAction->setObjectName(QStringLiteral("saveAsAction"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveGraphAs);

    m_undoAction = new QAction("Undo", this);
    m_undoAction->setObjectName(QStringLiteral("undoAction"));
    m_undoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Undo));
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

    m_redoAction = new QAction("Redo", this);
    m_redoAction->setObjectName(QStringLiteral("redoAction"));
    QList<QKeySequence> redoShortcuts = QKeySequence::keyBindings(QKeySequence::Redo);
    if (!redoShortcuts.contains(QKeySequence(QStringLiteral("Ctrl+Shift+Z")))) {
        redoShortcuts.push_back(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    }
    if (!redoShortcuts.contains(QKeySequence(QStringLiteral("Ctrl+Y")))) {
        redoShortcuts.push_back(QKeySequence(QStringLiteral("Ctrl+Y")));
    }
    m_redoAction->setShortcuts(redoShortcuts);
    m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

    // Register shortcuts on the main window directly so they still fire when
    // focus sits inside child widgets like the graphics view viewport.
    addAction(m_newAction);
    addAction(m_openAction);
    addAction(m_saveAction);
    addAction(m_saveAsAction);
    addAction(m_undoAction);
    addAction(m_redoAction);

    m_generateAction = new QAction("Generate Verilog", this);
    m_generateAction->setObjectName(QStringLiteral("generateAction"));
    m_generateAction->setToolTip("Generate every project IP instance under the project generated directory.");
    connect(m_generateAction, &QAction::triggered, this, &MainWindow::generateVerilog);

    m_validateAction = new QAction("Validate", this);
    m_validateAction->setObjectName(QStringLiteral("validateAction"));
    m_validateAction->setToolTip("Run project validation across all IP instances.");
    connect(m_validateAction, &QAction::triggered, this, &MainWindow::runValidation);

    m_arrangeAction = new QAction("Arrange", this);
    m_arrangeAction->setObjectName(QStringLiteral("arrangeAction"));
    m_arrangeAction->setCheckable(true);
    m_arrangeAction->setToolTip("Arrange the graph once into a mesh-style layout.");
    connect(m_arrangeAction, &QAction::toggled, m_nodeEditor, &NodeEditorWidget::setArrangeEnabled);
    connect(m_arrangeAction, &QAction::toggled, this, [this](bool enabled) {
        if (!enabled || !m_arrangeAction) {
            return;
        }
        // Keep the existing toggle pipeline, but make Arrange behave like a one-shot click.
        QTimer::singleShot(0, this, [this]() {
            if (m_arrangeAction && m_arrangeAction->isChecked()) {
                m_arrangeAction->setChecked(false);
            }
        });
    });

    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);

    auto* editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);

    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction(m_generateAction);
    toolsMenu->addAction(m_validateAction);

    auto* layoutMenu = menuBar()->addMenu("&Layout");
    layoutMenu->addAction(m_arrangeAction);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->setObjectName("viewMenu");

    auto* mainToolBar = addToolBar("Main");
    mainToolBar->setObjectName("mainToolBar");
    mainToolBar->addAction(m_newAction);
    mainToolBar->addAction(m_openAction);
    mainToolBar->addAction(m_saveAction);
    mainToolBar->addAction(m_undoAction);
    mainToolBar->addAction(m_redoAction);
    mainToolBar->addAction(m_generateAction);
    mainToolBar->addAction(m_validateAction);
    mainToolBar->addAction(m_arrangeAction);

    m_topologyMenu = new QMenu("Topology", this);
    auto* topologyButton = new QToolButton(this);
    topologyButton->setText("Topology");
    topologyButton->setPopupMode(QToolButton::InstantPopup);
    topologyButton->setMenu(m_topologyMenu);
    mainToolBar->addWidget(topologyButton);
}

QWidget* MainWindow::createCentralContent() {
    // Main editing surface stays in the center; auxiliary tools are docked.
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_nodeEditor);
    return central;
}

void MainWindow::setupDocks() {
    QMainWindow::DockOptions dockOptions = QMainWindow::AnimatedDocks |
                                           QMainWindow::AllowNestedDocks |
                                           QMainWindow::AllowTabbedDocks;
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    dockOptions |= QMainWindow::GroupedDragging;
#endif
    setDockOptions(dockOptions);
    setDockNestingEnabled(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    m_ipCatalogDock = createDock("IP Catalog", m_ipCatalogPanel, Qt::LeftDockWidgetArea, "ipCatalogDock");
    m_propertyDock = createDock("Properties", m_propertyPanel, Qt::RightDockWidgetArea, "propertyDock");
    m_logDock = createDock("Activity Log", m_logPanel, Qt::BottomDockWidgetArea, "logDock");

    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    resizeDocks({m_ipCatalogDock, m_propertyDock}, {280, 320}, Qt::Horizontal);
    resizeDocks({m_logDock}, {180}, Qt::Vertical);

    // Register dock toggle actions under View so users can restore hidden panels.
    QMenu* viewMenu = nullptr;
    for (QAction* action : menuBar()->actions()) {
        if (action && action->menu() && action->menu()->objectName() == "viewMenu") {
            viewMenu = action->menu();
            break;
        }
    }
    if (viewMenu) {
        viewMenu->addAction(m_ipCatalogDock->toggleViewAction());
        viewMenu->addAction(m_propertyDock->toggleViewAction());
        viewMenu->addAction(m_logDock->toggleViewAction());
    }
}

QDockWidget* MainWindow::createDock(const QString& title,
                                    QWidget* content,
                                    Qt::DockWidgetArea area,
                                    const QString& objectName) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(objectName);
    dock->setWidget(content);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    addDockWidget(area, dock);
    return dock;
}

void MainWindow::appendStartupLog() const {
    if (!m_logPanel) {
        return;
    }

    const QStringList lines =
        IpCoreRuntimeDiagnostics::logLines(m_ipCatalogService->entries(), ModuleRegistry::instance());
    for (const QString& line : lines) {
        qInfo().noquote() << line;
        m_logPanel->appendMessage(line, QColor(70, 110, 190));
    }
}

void MainWindow::scheduleStartupLayoutLog() {
#if !defined(QT_NO_DEBUG)
    if (!qEnvironmentVariableIsSet("FINEPAPER_DEBUG_LAYOUT")) {
        return;
    }

    QTimer::singleShot(0, this, [this]() { logStartupLayout(); });
#endif
}

void MainWindow::logStartupLayout() const {
#if !defined(QT_NO_DEBUG)
    const QWidget* central = centralWidget();
    qInfo() << "MainWindow geometry" << geometry()
            << "frame" << frameGeometry()
            << "visible" << isVisible();
    qInfo() << "Central widget" << (central ? central->geometry() : QRect())
            << "editor" << (m_nodeEditor ? m_nodeEditor->geometry() : QRect());
    qInfo() << "IP catalog dock" << (m_ipCatalogDock ? m_ipCatalogDock->geometry() : QRect())
            << "floating" << (m_ipCatalogDock ? m_ipCatalogDock->isFloating() : false)
            << "visible" << (m_ipCatalogDock ? m_ipCatalogDock->isVisible() : false);
    qInfo() << "Property dock" << (m_propertyDock ? m_propertyDock->geometry() : QRect())
            << "floating" << (m_propertyDock ? m_propertyDock->isFloating() : false)
            << "visible" << (m_propertyDock ? m_propertyDock->isVisible() : false);
    qInfo() << "Log dock" << (m_logDock ? m_logDock->geometry() : QRect())
            << "floating" << (m_logDock ? m_logDock->isFloating() : false)
            << "visible" << (m_logDock ? m_logDock->isVisible() : false);
#endif
}

void MainWindow::rebuildTopologyMenu() {
    if (!m_topologyMenu) {
        return;
    }

    m_topologyMenu->clear();
    m_topologyMenu->setEnabled(false);
    if (!m_projectOpen || !m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
        return;
    }

    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
    for (const TopologyPresetDescriptor& preset : workspace.topologyPresets) {
        QAction* action = m_topologyMenu->addAction(preset.label);
        action->setData(preset.id);
        connect(action, &QAction::triggered, this, &MainWindow::createTopologyPreset);
    }
    m_topologyMenu->setEnabled(!workspace.topologyPresets.isEmpty());
}

bool MainWindow::maybeSaveChanges(const QString& actionDescription) {
    if (!m_projectOpen || !m_documentDirty) {
        return true;
    }

    // All destructive document transitions funnel through this prompt so close,
    // open, and new preserve the same save/discard/cancel semantics.
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        "Unsaved Changes",
        QString("Save changes before %1?").arg(actionDescription),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Discard) {
        return true;
    }

    if (!m_currentDocumentPath.isEmpty()) {
        return saveDocument(m_currentDocumentPath);
    }

    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save Project",
                                                      defaultDocumentPath(),
                                                      projectFileDialogSaveFilter());
    if (path.isEmpty()) {
        return false;
    }

    return saveDocument(pathWithProjectExtension(path));
}

bool MainWindow::loadDocument(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    qInfo() << "Loading document from" << absolutePath;

    // Format detection is content-based instead of extension-based so renamed
    // project files still load through the correct path.
    const ProjectFileKind kind = ProjectReader::detectKind(absolutePath);
    if (kind == ProjectFileKind::Project) {
        // Project files carry IP-core ownership and typed parameters, so load
        // through the serializer.
        const ProjectReadResult readResult = ProjectReader::readFile(absolutePath);
        if (!readResult.success) {
            qWarning() << "Failed to read project" << absolutePath << readResult.error;
            QMessageBox::warning(this, "Open Failed", readResult.error);
            return false;
        }

        // Suppress document tracking while Graph emits module/connection signals
        // for the newly loaded state.
        m_suppressDocumentTracking = true;
        const GraphProjectLoadResult loadResult =
            GraphProjectSerializer::loadProject(readResult.document, *m_graph);
        if (!loadResult.success) {
            m_suppressDocumentTracking = false;
            qWarning() << "Failed to load project graph" << absolutePath << loadResult.error;
            QMessageBox::warning(this, "Open Failed", loadResult.error);
            return false;
        }
        m_projectIpService->loadFromDocument(readResult.document);
        m_suppressDocumentTracking = false;
        if (m_propertyPanel) {
            m_propertyPanel->setSelectedModule(QString());
        }

        m_commandManager->clearHistory();
        // After a successful project load, the current command state becomes
        // the clean baseline for window title and save action enablement.
        m_cleanStateId = m_commandManager->currentStateId();
        setCurrentDocumentPath(absolutePath);
        setProjectOpen(true);
        syncDocumentStateFromHistory();
        updateRecentProjectState(m_appSettings.get(), absolutePath);
        statusBar()->showMessage("Opened " + QFileInfo(absolutePath).fileName(), 5000);
        qInfo() << "Project load finished for" << absolutePath
                << "modules" << m_graph->modules().size()
                << "connections" << m_graph->connections().size();
        return true;
    }

    qWarning() << "Unsupported document format" << absolutePath;
    // Unknown files leave the existing design untouched.
    QMessageBox::warning(this, "Open Failed", "Unsupported document format: " + absolutePath);
    return false;
}

bool MainWindow::saveDocument(const QString& path) {
    const QString absolutePath = QFileInfo(pathWithProjectExtension(path)).absoluteFilePath();
    qInfo() << "Saving project to" << absolutePath;
    ProjectDocument document =
        GraphProjectSerializer::toProject(*m_graph, QFileInfo(absolutePath).completeBaseName());
    m_projectStateService->writeToDocument(document);
    const ProjectWriteResult result = ProjectWriter::writeFile(absolutePath, document);
    if (!result.success) {
        qWarning() << "Failed to save project to" << absolutePath << result.error;
        QMessageBox::warning(this, "Save Failed", result.error);
        return false;
    }

    setCurrentDocumentPath(absolutePath);
    m_cleanStateId = m_commandManager->currentStateId();
    setProjectOpen(true);
    syncDocumentStateFromHistory();
    updateRecentProjectState(m_appSettings.get(), absolutePath);
    statusBar()->showMessage("Saved " + QFileInfo(absolutePath).fileName(), 5000);
    qInfo() << "Saved project to" << absolutePath;
    return true;
}

QString MainWindow::defaultDocumentPath() const {
    if (!m_currentDocumentPath.isEmpty()) {
        return m_currentDocumentPath;
    }
    if (m_appSettings && !m_appSettings->lastDirectory().isEmpty()) {
        return m_appSettings->lastDirectory();
    }
    return QDir::currentPath();
}

QString MainWindow::defaultProjectDirectoryPath() const {
    if (!m_currentDocumentPath.isEmpty()) {
        return QFileInfo(m_currentDocumentPath).absolutePath();
    }
    if (m_appSettings && !m_appSettings->lastDirectory().isEmpty()) {
        return m_appSettings->lastDirectory();
    }
    return QDir::currentPath();
}

void MainWindow::clearDocument() {
    m_suppressDocumentTracking = true;
    m_graph->clear();
    m_projectIpService->clear();
    m_suppressDocumentTracking = false;
    if (m_propertyPanel) {
        m_propertyPanel->setSelectedModule(QString());
    }
    m_commandManager->clearHistory();
    m_cleanStateId = m_commandManager->currentStateId();
    setCurrentDocumentPath(QString());
    setProjectOpen(false);
    syncDocumentStateFromHistory();
}

void MainWindow::scheduleDocumentStateRefresh() {
    if (m_documentStateRefreshPending) {
        return;
    }

    // Coalesce bursts of graph signals from one logical command into one dirty
    // state refresh on the next event-loop turn.
    m_documentStateRefreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_documentStateRefreshPending = false;
        syncDocumentStateFromHistory();
    });
}

void MainWindow::syncDocumentStateFromHistory() {
    setDocumentDirty(m_projectOpen && m_commandManager->currentStateId() != m_cleanStateId);
    updateCommandActions();
}

void MainWindow::setCurrentDocumentPath(const QString& path) {
    m_currentDocumentPath = path;
    updateWindowTitle();
}

void MainWindow::setDocumentDirty(bool dirty) {
    if (m_documentDirty == dirty) {
        return;
    }

    m_documentDirty = dirty;
    updateWindowTitle();
}

void MainWindow::setProjectOpen(bool open) {
    m_projectOpen = open;
    if (m_nodeEditor) {
        m_nodeEditor->setEnabled(open);
    }
    if (m_propertyPanel) {
        m_propertyPanel->setEnabled(open);
    }
    if (m_ipCatalogPanel) {
        m_ipCatalogPanel->setEnabled(open);
    }
    rebuildTopologyMenu();
    updateWindowTitle();
    updateCommandActions();
}

bool MainWindow::requireOpenProject(const QString& actionName) {
    if (m_projectOpen) {
        return true;
    }

    const QString message = QString("Create or open a project before %1.").arg(actionName);
    statusBar()->showMessage(message, 5000);
    qWarning().noquote() << message;
    return false;
}

void MainWindow::updateWindowTitle() {
    const QString title = !m_projectOpen
        ? QStringLiteral("Finepaper - SoC/NoC Node Editor")
        : QString("%1%2 - SoC/NoC Node Editor")
              .arg(documentDisplayName(m_currentDocumentPath),
                   m_documentDirty ? QStringLiteral("*") : QString());
    setWindowTitle(title);
}

void MainWindow::updateCommandActions() {
    if (m_saveAction) {
        m_saveAction->setEnabled(m_projectOpen && (m_documentDirty || !m_currentDocumentPath.isEmpty()));
    }
    if (m_saveAsAction) {
        m_saveAsAction->setEnabled(m_projectOpen);
    }
    if (m_undoAction) {
        m_undoAction->setEnabled(m_projectOpen && m_commandManager->canUndo());
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(m_projectOpen && m_commandManager->canRedo());
    }
    if (m_generateAction) {
        m_generateAction->setEnabled(m_projectOpen);
    }
    if (m_validateAction) {
        m_validateAction->setEnabled(m_projectOpen);
    }
    if (m_arrangeAction) {
        m_arrangeAction->setEnabled(m_projectOpen);
    }
}
