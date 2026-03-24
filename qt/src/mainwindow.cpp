// MainWindow — constructs and connects all top-level UI components.
// Layout: horizontal splitter (palette | node editor | property panel)
// inside a vertical splitter with the log panel below.
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "graph.h"
#include "commandmanager.h"
#include "nodeeditorwidget.h"
#include "propertypanel.h"
#include "palette.h"
#include "logpanel.h"
#include "validationmanager.h"
#include "commands/loadgraphcommand.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    m_graph = new Graph(this);
    m_commandManager = new CommandManager();

    m_nodeEditor = new NodeEditorWidget(m_graph, m_commandManager, this);
    m_propertyPanel = new PropertyPanel(m_graph, m_commandManager, this);
    m_palette = new Palette(m_graph, m_commandManager, this);
    m_logPanel = new LogPanel(this);
    m_validationManager = new ValidationManager(m_graph, m_logPanel, this);

    connect(m_logPanel, &LogPanel::elementSelected, m_nodeEditor, &NodeEditorWidget::highlightElement);
    connect(m_nodeEditor, &NodeEditorWidget::moduleSelected, m_propertyPanel, QOverload<QString>::of(&PropertyPanel::setSelectedModule));

    QPushButton* saveBtn = new QPushButton("Save JSON", this);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveGraph);

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(m_palette);
    mainSplitter->addWidget(m_nodeEditor);
    mainSplitter->addWidget(m_propertyPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 4);
    mainSplitter->setStretchFactor(2, 1);

    QSplitter* verticalSplitter = new QSplitter(Qt::Vertical, this);
    verticalSplitter->addWidget(mainSplitter);
    verticalSplitter->addWidget(m_logPanel);
    verticalSplitter->setStretchFactor(0, 4);
    verticalSplitter->setStretchFactor(1, 1);

    QWidget* central = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->addStretch();
    toolbar->addWidget(saveBtn);
    layout->addLayout(toolbar);
    layout->addWidget(verticalSplitter);

    setCentralWidget(central);
    setWindowTitle("SoC/NoC Node Editor");
    resize(1200, 800);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::loadGraph(const QString& jsonPath) {
    m_commandManager->executeCommand(std::make_unique<LoadGraphCommand>(m_graph, jsonPath));
}

void MainWindow::saveGraph() {
    QString path = QFileDialog::getSaveFileName(this, "Save Graph", "", "JSON Files (*.json)");
    if (path.isEmpty()) return;
    if (!m_graph->saveToJson(path))
        QMessageBox::warning(this, "Save Failed", "Could not write to " + path);
}
