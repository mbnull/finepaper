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
#include "commands/loadgraphcommand.h"
#include <QAction>
#include <QApplication>
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
      m_saveAction(nullptr),
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
    setWindowTitle("SoC/NoC Node Editor");
    resize(1920, 1080);
    scheduleStartupLayoutLog();
}

MainWindow::~MainWindow() = default;

void MainWindow::loadGraph(const QString& jsonPath) {
    qInfo() << "Loading graph from" << jsonPath;
    // Route loading through command history so users can undo graph imports.
    m_commandManager->executeCommand(std::make_unique<LoadGraphCommand>(m_graph, jsonPath));
    qInfo() << "Graph load command finished for" << jsonPath
            << "modules" << m_graph->modules().size()
            << "connections" << m_graph->connections().size();
    if (m_arrangeAction && m_arrangeAction->isChecked()) {
        m_nodeEditor->setArrangeEnabled(true);
    }
}

void MainWindow::saveGraph() {
    QString path = QFileDialog::getSaveFileName(this,
                                                "Save Graph",
                                                "",
                                                "JSON Files (*.json);;XML Files (*.xml)");
    if (path.isEmpty()) return;
    qInfo() << "Saving graph to" << path;
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool saveSucceeded = suffix == QStringLiteral("xml")
        ? m_graph->saveToXml(path)
        : m_graph->saveToJson(path);
    if (!saveSucceeded) {
        qWarning() << "Failed to save graph to" << path;
        QMessageBox::warning(this, "Save Failed", "Could not write to " + path);
        return;
    }

    qInfo() << "Saved graph to" << path;
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
    // Keep selection synchronized between validation log, canvas, and property panel.
    connect(m_logPanel, &LogPanel::elementSelected, m_nodeEditor, &NodeEditorWidget::highlightElement);
    connect(m_nodeEditor,
            &NodeEditorWidget::moduleSelected,
            m_propertyPanel,
            QOverload<QString>::of(&PropertyPanel::setSelectedModule));
}

void MainWindow::setupActions() {
    // Menu and toolbar share the same QAction instances to keep enabled/check
    // states synchronized automatically.
    m_saveAction = new QAction("Save JSON", this);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveGraph);

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
    fileMenu->addAction(m_saveAction);

    auto* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction(m_generateAction);
    toolsMenu->addAction(m_validateAction);

    auto* layoutMenu = menuBar()->addMenu("&Layout");
    layoutMenu->addAction(m_arrangeAction);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->setObjectName("viewMenu");

    auto* mainToolBar = addToolBar("Main");
    mainToolBar->setObjectName("mainToolBar");
    mainToolBar->addAction(m_saveAction);
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
    m_logDock = createDock("Validation Log", m_logPanel, Qt::BottomDockWidgetArea, "logDock");

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
