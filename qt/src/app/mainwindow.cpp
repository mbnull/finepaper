// MainWindow — constructs and connects all top-level UI components.
// Layout: horizontal splitter (IP catalog | node editor | property panel)
// inside a vertical splitter with the log panel below.
#include "app/mainwindow.h"
#include "app/generationartifacts.h"
#include "commands/topologypresetcommand.h"
#include "graph/graph.h"
#include "commands/commandmanager.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcoregraphexporter.h"
#include "nodeeditor/nodeeditorwidget.h"
#include "panels/ipcatalogpanel.h"
#include "panels/propertypanel.h"
#include "panels/logpanel.h"
#include "plugins/generatorrunner.h"
#include "project/ipinstanceparameteradapter.h"
#include "plugins/pluginregistry.h"
#include "plugins/startupdiagnostics.h"
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
#include <QProcess>
#include <QKeySequence>
#include <QList>
#include <QRegularExpression>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtGlobal>
#include <QVBoxLayout>
#include <optional>

namespace {

QString sanitizedDesignName(const QString& directoryPath) {
    QString designName = QFileInfo(directoryPath).fileName().trimmed().toLower();
    designName.replace(QRegularExpression("[^a-z0-9_]+"), "_");
    designName.remove(QRegularExpression("^_+|_+$"));
    return designName.isEmpty() ? QStringLiteral("design") : designName;
}

QString trimmedProcessOutput(const QString& text) {
    return text.trimmed().isEmpty() ? QStringLiteral("(no process output)") : text.trimmed();
}

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
    updateWindowTitle();
    updateCommandActions();
    resize(1920, 1080);
    appendStartupLog();
    scheduleStartupLayoutLog();
}

MainWindow::~MainWindow() = default;

void MainWindow::loadGraph(const QString& path) {
    loadDocument(path);
}

void MainWindow::saveGraph() {
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
    if (!maybeSaveChanges(QStringLiteral("creating a new design"))) {
        return;
    }

    clearDocument();
}

void MainWindow::openGraph() {
    if (!maybeSaveChanges(QStringLiteral("opening another design"))) {
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
    // Generation starts from a user-chosen directory because the IP-core command
    // may emit multiple RTL/support files beside the JSON handoff.
    const QString outputDirectory = QFileDialog::getExistingDirectory(
        this,
        "Select Verilog Output Folder",
        QDir::currentPath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (outputDirectory.isEmpty()) {
        return;
    }

    QDir outputDir(outputDirectory);
    // QFileDialog normally returns an existing folder, but this check keeps the
    // workflow robust for platform-specific symlink or permission edge cases.
    if (!outputDir.mkpath(".")) {
        m_logPanel->appendMessage("[Generate] Could not create output folder: " + outputDirectory,
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "Output Folder Error",
                             "Could not create or access " + outputDirectory);
        return;
    }

    // Persist the active IP-core generator input, then call generator.
    const QString designName = sanitizedDesignName(outputDirectory);
    const QString jsonPath = outputDir.filePath(designName + ".json");
    const std::optional<ActiveWorkspaceContext> context =
        m_activeWorkspaceController ? m_activeWorkspaceController->activeContext() : std::nullopt;
    if (!context.has_value()) {
        const QString error = QStringLiteral("Select an IP core instance before generating.");
        m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
        QMessageBox::warning(this, "Generator Not Available", error);
        return;
    }

    // Resolve the command before writing artifacts so IP-core ownership and
    // command availability fail early with a user-facing message.
    const GeneratorCommand generatorCommand =
        GeneratorRunner::resolveForIpcore(context->entry, jsonPath, outputDirectory);
    if (!generatorCommand.valid) {
        m_logPanel->appendMessage("[Generate] " + generatorCommand.errorMessage,
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "Generator Not Available",
                             generatorCommand.errorMessage);
        return;
    }

    const IpCoreGraphExportResult exportResult =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            m_graph,
            context->entry,
            context->record,
            designName,
            nullptr
        });
    if (!exportResult.success) {
        m_logPanel->appendMessage("[Generate] " + exportResult.error, QColor(220, 50, 50));
        QMessageBox::warning(this, "JSON Export Failed", exportResult.error);
        return;
    }

    QFile jsonFile(jsonPath);
    // JSON is the contract boundary between the editor and the external IP-core
    // process, so fail before launching the generator if the handoff cannot be written.
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_logPanel->appendMessage("[Generate] Could not write JSON: " + jsonPath,
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "JSON Export Failed",
                             "Could not write " + jsonPath);
        return;
    }
    jsonFile.write(exportResult.document.toJson());
    jsonFile.close();

    // Keep a Finepaper project snapshot next to generated RTL so the produced
    // files can be traced back to an editor-loadable design.
    const GeneratedProjectSnapshotResult projectSnapshot =
        writeGeneratedProjectSnapshot(*m_graph,
                                      outputDirectory,
                                      designName,
                                      m_projectStateService->ipInstanceRecords());
    if (!projectSnapshot.success) {
        m_logPanel->appendMessage("[Generate] Could not write project: " + projectSnapshot.error,
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "Project Snapshot Failed",
                             projectSnapshot.error);
        return;
    }

    // Log artifact paths before process launch; if the generator fails, users still
    // know exactly which intermediate files were produced.
    m_logPanel->appendMessage(QString("[Generate] Start output=%1").arg(outputDirectory),
                              QColor(70, 110, 190));
    m_logPanel->appendMessage(QString("[Generate] JSON=%1").arg(jsonPath),
                              QColor(70, 110, 190));
    m_logPanel->appendMessage(QString("[Generate] Project=%1").arg(projectSnapshot.path),
                              QColor(70, 110, 190));
    m_logPanel->appendMessage(QString("[Generate] IP core=%1").arg(generatorCommand.ipcoreId),
                              QColor(70, 110, 190));

    statusBar()->showMessage("Generating Verilog...");
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Run IP-core generator synchronously so UI status/logging reflects one
    // complete operation from start to finish.
    QProcess proc;
    proc.setWorkingDirectory(generatorCommand.workingDirectory);
    proc.start(generatorCommand.command, generatorCommand.arguments);

    // The cursor is restored immediately after process completion/start failure
    // so all later message boxes and status updates use the normal UI cursor.
    const bool started = proc.waitForStarted();
    const bool finished = started && proc.waitForFinished(-1);
    QApplication::restoreOverrideCursor();

    if (!started) {
        const QString error = "Failed to start IP core generator: " + proc.errorString();
        qWarning() << error;
        m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
        statusBar()->showMessage(error, 5000);
        QMessageBox::warning(this, "Generate Failed", error);
        return;
    }

    const QString standardOutput = QString::fromUtf8(proc.readAllStandardOutput());
    const QString standardError = QString::fromUtf8(proc.readAllStandardError());
    // Generators are external tools; preserve stdout/stderr separately in the
    // activity log so IP-core authors can diagnose failures without rerunning.
    if (!finished || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString detail = trimmedProcessOutput(standardError.isEmpty() ? standardOutput : standardError);
        const QString error = !finished
            ? QStringLiteral("Generator timed out while producing Verilog.")
            : QString("Generator failed (exit code %1).").arg(proc.exitCode());
        qWarning().noquote() << error << detail;
        m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
        appendLogLines(m_logPanel, standardOutput, QColor(80, 120, 180), "[Generate][stdout] ");
        appendLogLines(m_logPanel, standardError, QColor(220, 50, 50), "[Generate][stderr] ");
        statusBar()->showMessage(error, 5000);
        QMessageBox::warning(this,
                             "Generate Failed",
                             error + "\n\n" + detail);
        return;
    }

    const QString successMessage = "Generated Verilog and JSON in " + outputDirectory;
    // Successful stderr is logged as a warning-colored channel because many
    // command-line tools use stderr for progress even when exit code is zero.
    qInfo().noquote() << successMessage;
    m_logPanel->appendMessage("[Generate] " + successMessage, QColor(40, 140, 80));
    appendLogLines(m_logPanel, standardOutput, QColor(40, 140, 80), "[Generate][stdout] ");
    appendLogLines(m_logPanel, standardError, QColor(200, 150, 50), "[Generate][stderr] ");
    statusBar()->showMessage(successMessage, 5000);
    QMessageBox::information(this,
                             "Generate Complete",
                             successMessage + "\n\nJSON: " + jsonPath + "\n\n" +
                                 trimmedProcessOutput(standardOutput));
}

void MainWindow::runValidation() {
    if (!m_validationManager) {
        qCritical() << "Validation manager not initialized, cannot run validation";
        return;
    }

    qInfo() << "Validation requested by user";
    m_validationManager->runValidation();
}

void MainWindow::undo() {
    m_commandManager->undo();
    syncDocumentStateFromHistory();
}

void MainWindow::redo() {
    m_commandManager->redo();
    syncDocumentStateFromHistory();
}

void MainWindow::createTopologyPreset() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action || !m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
        return;
    }

    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
    const QString presetId = action->data().toString();
    // Presets are IP-core-owned; look them up by ID at trigger time so rebuilt
    // menus cannot leave stale QAction pointers to deleted descriptors.
    auto presetIt = std::find_if(workspace.topologyPresets.cbegin(),
                                 workspace.topologyPresets.cend(),
                                 [&](const TopologyPresetDescriptor& preset) {
                                     return preset.id == presetId;
                                 });
    if (presetIt == workspace.topologyPresets.cend()) {
        return;
    }

    TopologyPresetRequest request;
    request.ipcoreId = workspace.ipcoreId;
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
        event->accept();
        return;
    }

    event->ignore();
}

void MainWindow::setupPanels() {
    m_ipInstanceParameterAdapters.clear();
    QVector<IIpInstanceParameterAdapter*> ipInstanceParameterAdapters;
    for (const PluginDescriptor& plugin : PluginRegistry::instance().plugins()) {
        auto adapter = std::make_unique<ManifestIpInstanceParameterAdapter>(plugin);
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
                const std::optional<IpCatalogEntry> entry = m_ipCatalogService->entry(ipcoreId);
                if (!entry.has_value()) {
                    return;
                }
                const ProjectIpServiceResult result = m_projectIpService->ensureInstanceForIpcore(*entry);
                if (!result.success) {
                    QMessageBox::warning(this, "IP Catalog", result.error);
                    return;
                }
            });
    connect(m_ipCatalogPanel,
            &IpCatalogPanel::selectIpInstanceRequested,
            this,
            [this](const QString& ipcoreId, const QString& instanceId) {
                m_projectIpService->selectInstance(ipcoreId, instanceId);
            });
}

void MainWindow::setupActions() {
    // Menu and toolbar share the same QAction instances to keep enabled/check
    // states synchronized automatically.
    m_newAction = new QAction("New", this);
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newGraph);

    m_openAction = new QAction("Open...", this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openGraph);

    m_saveAction = new QAction("Save", this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveGraph);

    m_saveAsAction = new QAction("Save As...", this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveGraphAs);

    m_undoAction = new QAction("Undo", this);
    m_undoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Undo));
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

    m_redoAction = new QAction("Redo", this);
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
    m_generateAction->setToolTip("Write IP-core graph input and a project snapshot, then generate Verilog in a selected folder.");
    connect(m_generateAction, &QAction::triggered, this, &MainWindow::generateVerilog);

    m_validateAction = new QAction("Validate", this);
    m_validateAction->setToolTip("Run validation for the current graph.");
    connect(m_validateAction, &QAction::triggered, this, &MainWindow::runValidation);

    m_arrangeAction = new QAction("Arrange", this);
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
        StartupDiagnostics::logLines(PluginRegistry::instance().plugins(),
                                     ModuleRegistry::instance());
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
    if (!m_activeWorkspaceController || !m_activeWorkspaceController->state().hasActiveIp) {
        return;
    }

    const ActiveWorkspaceState& workspace = m_activeWorkspaceController->state();
    for (const TopologyPresetDescriptor& preset : workspace.topologyPresets) {
        QAction* action = m_topologyMenu->addAction(preset.label);
        action->setData(preset.id);
        connect(action, &QAction::triggered, this, &MainWindow::createTopologyPreset);
    }
}

bool MainWindow::maybeSaveChanges(const QString& actionDescription) {
    if (!m_documentDirty) {
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
    qInfo() << "Loading document from" << path;

    // Format detection is content-based instead of extension-based so renamed
    // project files still load through the correct path.
    const ProjectFileKind kind = ProjectReader::detectKind(path);
    if (kind == ProjectFileKind::Project) {
        // Project files carry IP-core ownership and typed parameters, so load
        // through the serializer.
        const ProjectReadResult readResult = ProjectReader::readFile(path);
        if (!readResult.success) {
            qWarning() << "Failed to read project" << path << readResult.error;
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
            qWarning() << "Failed to load project graph" << path << loadResult.error;
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
        setCurrentDocumentPath(path);
        syncDocumentStateFromHistory();
        statusBar()->showMessage("Opened " + QFileInfo(path).fileName(), 5000);
        qInfo() << "Project load finished for" << path
                << "modules" << m_graph->modules().size()
                << "connections" << m_graph->connections().size();
        return true;
    }

    qWarning() << "Unsupported document format" << path;
    // Unknown files leave the existing design untouched.
    QMessageBox::warning(this, "Open Failed", "Unsupported document format: " + path);
    return false;
}

bool MainWindow::saveDocument(const QString& path) {
    qInfo() << "Saving project to" << path;
    ProjectDocument document =
        GraphProjectSerializer::toProject(*m_graph, QFileInfo(path).completeBaseName());
    m_projectStateService->writeToDocument(document);
    const ProjectWriteResult result = ProjectWriter::writeFile(path, document);
    if (!result.success) {
        qWarning() << "Failed to save project to" << path << result.error;
        QMessageBox::warning(this, "Save Failed", result.error);
        return false;
    }

    setCurrentDocumentPath(path);
    m_cleanStateId = m_commandManager->currentStateId();
    syncDocumentStateFromHistory();
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 5000);
    qInfo() << "Saved project to" << path;
    return true;
}

QString MainWindow::defaultDocumentPath() const {
    return m_currentDocumentPath.isEmpty() ? QDir::currentPath() : m_currentDocumentPath;
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
    syncDocumentStateFromHistory();
    statusBar()->showMessage("Started a new design", 5000);
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
    setDocumentDirty(m_commandManager->currentStateId() != m_cleanStateId);
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

void MainWindow::updateWindowTitle() {
    const QString title = QString("%1%2 - SoC/NoC Node Editor")
                              .arg(documentDisplayName(m_currentDocumentPath),
                                   m_documentDirty ? QStringLiteral("*") : QString());
    setWindowTitle(title);
}

void MainWindow::updateCommandActions() {
    if (m_saveAction) {
        m_saveAction->setEnabled(m_documentDirty || !m_currentDocumentPath.isEmpty());
    }
    if (m_undoAction) {
        m_undoAction->setEnabled(m_commandManager->canUndo());
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(m_commandManager->canRedo());
    }
}
