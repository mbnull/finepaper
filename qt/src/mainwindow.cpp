// MainWindow — constructs and connects all top-level UI components.
// Layout: horizontal splitter (palette | node editor | property panel)
// inside a vertical splitter with the log panel below.
#include "mainwindow.h"
#include "graph.h"
#include "commandmanager.h"
#include "nodeeditorwidget.h"
#include "propertypanel.h"
#include "palette.h"
#include "logpanel.h"
#include "validationmanager.h"
#include "commands/loadgraphcommand.h"
#include <QAction>
#include <QDebug>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

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
      m_saveAction(nullptr) {
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
    m_commandManager->executeCommand(std::make_unique<LoadGraphCommand>(m_graph, jsonPath));
}

void MainWindow::saveGraph() {
    QString path = QFileDialog::getSaveFileName(this, "Save Graph", "", "JSON Files (*.json)");
    if (path.isEmpty()) return;
    if (!m_graph->saveToJson(path))
        QMessageBox::warning(this, "Save Failed", "Could not write to " + path);
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
    connect(m_logPanel, &LogPanel::elementSelected, m_nodeEditor, &NodeEditorWidget::highlightElement);
    connect(m_nodeEditor,
            &NodeEditorWidget::moduleSelected,
            m_propertyPanel,
            QOverload<QString>::of(&PropertyPanel::setSelectedModule));
}

void MainWindow::setupActions() {
    m_saveAction = new QAction("Save JSON", this);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveGraph);

    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_saveAction);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->setObjectName("viewMenu");

    auto* mainToolBar = addToolBar("Main");
    mainToolBar->setObjectName("mainToolBar");
    mainToolBar->addAction(m_saveAction);
}

QWidget* MainWindow::createCentralContent() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_nodeEditor);
    return central;
}

void MainWindow::setupDocks() {
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks);

    m_paletteDock = createDock("Palette", m_palette, Qt::LeftDockWidgetArea, "paletteDock");
    m_propertyDock = createDock("Properties", m_propertyPanel, Qt::RightDockWidgetArea, "propertyDock");
    m_logDock = createDock("Validation Log", m_logPanel, Qt::BottomDockWidgetArea, "logDock");

    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    resizeDocks({m_paletteDock, m_propertyDock}, {260, 320}, Qt::Horizontal);
    resizeDocks({m_logDock}, {180}, Qt::Vertical);

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
