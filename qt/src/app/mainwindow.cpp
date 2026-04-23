// MainWindow — constructs and connects all top-level UI components.
// Layout: horizontal splitter (palette | node editor | property panel)
// inside a vertical splitter with the log panel below.
#include "app/mainwindow.h"
#include "common/frameworkpaths.h"
#include "graph/graph.h"
#include "commands/commandmanager.h"
#include "nodeeditor/nodeeditorwidget.h"
#include "panels/propertypanel.h"
#include "panels/palette.h"
#include "panels/logpanel.h"
#include "validation/validationmanager.h"
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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QKeySequence>
#include <QList>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QtGlobal>
#include <QVBoxLayout>

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

QString fileDialogSaveFilter() {
    return QStringLiteral("JSON Files (*.json);;XML Files (*.xml)");
}

QString jsonFileDialogSaveFilter() {
    return QStringLiteral("JSON Files (*.json)");
}

QString jsonFileDialogOpenFilter() {
    return QStringLiteral("JSON Files (*.json);;All Files (*)");
}

QString pathWithSelectedExtension(QString path, const QString& selectedFilter) {
    if (!QFileInfo(path).suffix().isEmpty()) {
        return path;
    }

    return path + (selectedFilter.startsWith(QStringLiteral("XML")) ? QStringLiteral(".xml")
                                                                    : QStringLiteral(".json"));
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
      m_nodeEditor(nullptr),
      m_propertyPanel(nullptr),
      m_palette(nullptr),
      m_logPanel(nullptr),
      m_validationManager(nullptr),
      m_paletteDock(nullptr),
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
      m_arrangeAction(nullptr) {
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
    scheduleStartupLayoutLog();
}

MainWindow::~MainWindow() = default;

void MainWindow::loadGraph(const QString& jsonPath) {
    loadDocument(jsonPath);
}

void MainWindow::saveGraph() {
    if (!m_currentDocumentPath.isEmpty()) {
        saveDocument(m_currentDocumentPath);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this,
                                                      "Save Graph",
                                                      defaultDocumentPath(),
                                                      jsonFileDialogSaveFilter());
    if (path.isEmpty()) {
        return;
    }

    saveDocument(pathWithSelectedExtension(path, jsonFileDialogSaveFilter()));
}

void MainWindow::saveGraphAs() {
    QString selectedFilter = QStringLiteral("JSON Files (*.json)");
    QString path = QFileDialog::getSaveFileName(this,
                                                "Save Graph",
                                                defaultDocumentPath(),
                                                fileDialogSaveFilter(),
                                                &selectedFilter);
    if (path.isEmpty()) {
        return;
    }

    saveDocument(pathWithSelectedExtension(path, selectedFilter));
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
                                                      "Open Graph",
                                                      defaultDocumentPath(),
                                                      jsonFileDialogOpenFilter());
    if (path.isEmpty()) {
        return;
    }

    loadDocument(path);
}

void MainWindow::generateVerilog() {
    const QString outputDirectory = QFileDialog::getExistingDirectory(
        this,
        "Select Verilog Output Folder",
        QDir::currentPath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (outputDirectory.isEmpty()) {
        return;
    }

    const QString frameworkPath = FrameworkPaths::resolveFrameworkPath();
    const QString templatePath = FrameworkPaths::resolveTemplatePath();
    if (frameworkPath.isEmpty() || templatePath.isEmpty()) {
        m_logPanel->appendMessage("[Generate] Framework not found for ../framework/bin/generate or ../framework/template.",
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "Framework Not Found",
                             "Could not find ../framework. Set FRAMEWORK_PATH or keep framework/ next to this repository.");
        return;
    }

    QDir outputDir(outputDirectory);
    if (!outputDir.mkpath(".")) {
        m_logPanel->appendMessage("[Generate] Could not create output folder: " + outputDirectory,
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "Output Folder Error",
                             "Could not create or access " + outputDirectory);
        return;
    }

    // Persist the editor graph as framework-flavored JSON, then call generator.
    const QString designName = sanitizedDesignName(outputDirectory);
    const QString jsonPath = outputDir.filePath(designName + ".json");
    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_logPanel->appendMessage("[Generate] Could not write JSON: " + jsonPath,
                                  QColor(220, 50, 50));
        QMessageBox::warning(this,
                             "JSON Export Failed",
                             "Could not write " + jsonPath);
        return;
    }
    jsonFile.write(m_graph->toJsonDocument(designName, GraphJsonFlavor::Framework).toJson());
    jsonFile.close();

    m_logPanel->appendMessage(QString("[Generate] Start output=%1").arg(outputDirectory),
                              QColor(70, 110, 190));
    m_logPanel->appendMessage(QString("[Generate] JSON=%1").arg(jsonPath),
                              QColor(70, 110, 190));

    statusBar()->showMessage("Generating Verilog...");
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Run framework generator synchronously so UI status/logging reflects one
    // complete operation from start to finish.
    QProcess proc;
    proc.setWorkingDirectory(frameworkPath);
    proc.start("ruby", {
        "bin/generate",
        "-i", jsonPath,
        "-o", outputDirectory,
        "-t", templatePath
    });

    const bool started = proc.waitForStarted();
    const bool finished = started && proc.waitForFinished(-1);
    QApplication::restoreOverrideCursor();

    if (!started) {
        const QString error = "Failed to start framework generator: " + proc.errorString();
        qWarning() << error;
        m_logPanel->appendMessage("[Generate] " + error, QColor(220, 50, 50));
        statusBar()->showMessage(error, 5000);
        QMessageBox::warning(this, "Generate Failed", error);
        return;
    }

    const QString standardOutput = QString::fromUtf8(proc.readAllStandardOutput());
    const QString standardError = QString::fromUtf8(proc.readAllStandardError());
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

void MainWindow::closeEvent(QCloseEvent* event) {
    if (maybeSaveChanges(QStringLiteral("closing the window"))) {
        event->accept();
        return;
    }

    event->ignore();
}

void MainWindow::setupPanels() {
    m_nodeEditor = new NodeEditorWidget(m_graph, m_commandManager.get(), this);
    m_propertyPanel = new PropertyPanel(m_graph, m_commandManager.get(), this);
    m_palette = new Palette(m_graph, m_commandManager.get(), this);
    m_logPanel = new LogPanel(this);
    m_validationManager = new ValidationManager(m_graph, m_logPanel, this);

    m_nodeEditor->setObjectName("nodeEditorPanel");
    m_propertyPanel->setObjectName("propertyPanel");
    m_palette->setObjectName("palettePanel");
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
    m_generateAction->setToolTip("Export the current graph as framework JSON and generate Verilog in a selected folder.");
    connect(m_generateAction, &QAction::triggered, this, &MainWindow::generateVerilog);

    m_validateAction = new QAction("Validate", this);
    m_validateAction->setToolTip("Run validation for the current graph.");
    connect(m_validateAction, &QAction::triggered, this, &MainWindow::runValidation);

    m_arrangeAction = new QAction("Arrange", this);
    m_arrangeAction->setCheckable(true);
    m_arrangeAction->setToolTip("Arrange the graph once into a mesh-style layout.");
    connect(m_arrangeAction, &QAction::toggled, m_nodeEditor, &NodeEditorWidget::setArrangeEnabled);
    connect(m_arrangeAction, &QAction::toggled, m_palette, [this](bool enabled) {
        m_palette->setEnabled(!enabled);
    });
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

    m_paletteDock = createDock("Palette", m_palette, Qt::LeftDockWidgetArea, "paletteDock");
    m_propertyDock = createDock("Properties", m_propertyPanel, Qt::RightDockWidgetArea, "propertyDock");
    m_logDock = createDock("Activity Log", m_logPanel, Qt::BottomDockWidgetArea, "logDock");

    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    resizeDocks({m_paletteDock, m_propertyDock}, {260, 320}, Qt::Horizontal);
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
        viewMenu->addAction(m_paletteDock->toggleViewAction());
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
    qInfo() << "Palette dock" << (m_paletteDock ? m_paletteDock->geometry() : QRect())
            << "floating" << (m_paletteDock ? m_paletteDock->isFloating() : false)
            << "visible" << (m_paletteDock ? m_paletteDock->isVisible() : false);
    qInfo() << "Property dock" << (m_propertyDock ? m_propertyDock->geometry() : QRect())
            << "floating" << (m_propertyDock ? m_propertyDock->isFloating() : false)
            << "visible" << (m_propertyDock ? m_propertyDock->isVisible() : false);
    qInfo() << "Log dock" << (m_logDock ? m_logDock->geometry() : QRect())
            << "floating" << (m_logDock ? m_logDock->isFloating() : false)
            << "visible" << (m_logDock ? m_logDock->isVisible() : false);
#endif
}

bool MainWindow::maybeSaveChanges(const QString& actionDescription) {
    if (!m_documentDirty) {
        return true;
    }

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
                                                      "Save Graph",
                                                      defaultDocumentPath(),
                                                      jsonFileDialogSaveFilter());
    if (path.isEmpty()) {
        return false;
    }

    return saveDocument(pathWithSelectedExtension(path, jsonFileDialogSaveFilter()));
}

bool MainWindow::loadDocument(const QString& jsonPath) {
    qInfo() << "Loading graph from" << jsonPath;

    m_suppressDocumentTracking = true;
    const bool loadSucceeded = m_graph->loadFromJson(jsonPath);
    m_suppressDocumentTracking = false;

    if (!loadSucceeded) {
        QMessageBox::warning(this, "Open Failed", "Could not load " + jsonPath);
        return false;
    }

    m_commandManager->clearHistory();
    m_cleanStateId = m_commandManager->currentStateId();
    setCurrentDocumentPath(jsonPath);
    syncDocumentStateFromHistory();
    statusBar()->showMessage("Opened " + QFileInfo(jsonPath).fileName(), 5000);
    qInfo() << "Graph load finished for" << jsonPath
            << "modules" << m_graph->modules().size()
            << "connections" << m_graph->connections().size();
    return true;
}

bool MainWindow::saveDocument(const QString& path) {
    qInfo() << "Saving graph to" << path;
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool savingXml = suffix == QStringLiteral("xml");
    const bool saveSucceeded = suffix == QStringLiteral("xml")
        ? m_graph->saveToXml(path)
        : m_graph->saveToJson(path);
    if (!saveSucceeded) {
        qWarning() << "Failed to save graph to" << path;
        QMessageBox::warning(this, "Save Failed", "Could not write to " + path);
        return false;
    }

    if (!savingXml) {
        setCurrentDocumentPath(path);
        m_cleanStateId = m_commandManager->currentStateId();
    }
    syncDocumentStateFromHistory();
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 5000);
    qInfo() << "Saved graph to" << path;
    return true;
}

QString MainWindow::defaultDocumentPath() const {
    return m_currentDocumentPath.isEmpty() ? QDir::currentPath() : m_currentDocumentPath;
}

void MainWindow::clearDocument() {
    m_suppressDocumentTracking = true;
    m_graph->clear();
    m_suppressDocumentTracking = false;
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
