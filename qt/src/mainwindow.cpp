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
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
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
      m_saveAction(nullptr) {
    setupPanels();
    setupConnections();
    setupActions();
    setCentralWidget(createCentralContent());
    setWindowTitle("SoC/NoC Node Editor");
    resize(1200, 800);
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

    auto* mainToolBar = addToolBar("Main");
    mainToolBar->setObjectName("mainToolBar");
    mainToolBar->addAction(m_saveAction);
}

QWidget* MainWindow::createCentralContent() {
    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(m_palette);
    mainSplitter->addWidget(m_nodeEditor);
    mainSplitter->addWidget(m_propertyPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 4);
    mainSplitter->setStretchFactor(2, 1);

    auto* verticalSplitter = new QSplitter(Qt::Vertical, this);
    verticalSplitter->addWidget(mainSplitter);
    verticalSplitter->addWidget(m_logPanel);
    verticalSplitter->setStretchFactor(0, 4);
    verticalSplitter->setStretchFactor(1, 1);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(verticalSplitter);
    return central;
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
            << "editor" << (m_nodeEditor ? m_nodeEditor->geometry() : QRect())
            << "palette" << (m_palette ? m_palette->geometry() : QRect())
            << "properties" << (m_propertyPanel ? m_propertyPanel->geometry() : QRect())
            << "log" << (m_logPanel ? m_logPanel->geometry() : QRect());
#endif
}
