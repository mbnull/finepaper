#include "gui/main_window.h"

#include "storage/json.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
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
    return lines.isEmpty() ? QStringLiteral("No diagnostics.") : lines.join(QLatin1Char('\n'));
}

bool containsErrors(const QVector<Diagnostic>& diagnostics) {
    return hasErrors(diagnostics);
}

QTableWidgetItem* readOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

FinepaperMainWindow::FinepaperMainWindow(QStringList packageRoots, QWidget* parent)
    : QMainWindow(parent),
      m_packageRoots(std::move(packageRoots)) {
    createUi();
    reloadPackages();
    statusBar()->showMessage(QStringLiteral("Choose a Package and create a Mesh NoC."));
}

void FinepaperMainWindow::createUi() {
    setWindowTitle(QStringLiteral("Finepaper — NoC Designer"));
    resize(1280, 820);

    auto* newAction = new QAction(QStringLiteral("New Mesh…"), this);
    auto* openAction = new QAction(QStringLiteral("Open…"), this);
    auto* saveAction = new QAction(QStringLiteral("Save"), this);
    auto* reloadAction = new QAction(QStringLiteral("Reload Packages"), this);
    auto* validateAction = new QAction(QStringLiteral("Validate"), this);
    auto* generateAction = new QAction(QStringLiteral("Generate RTL"), this);
    connect(newAction, &QAction::triggered, this, &FinepaperMainWindow::createDesign);
    connect(openAction, &QAction::triggered, this, &FinepaperMainWindow::openDesign);
    connect(saveAction, &QAction::triggered, this, &FinepaperMainWindow::saveDesign);
    connect(reloadAction, &QAction::triggered, this, &FinepaperMainWindow::reloadPackages);
    connect(validateAction, &QAction::triggered, this, &FinepaperMainWindow::validateDesign);
    connect(generateAction, &QAction::triggered, this, &FinepaperMainWindow::generateDesign);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    QMenu* packageMenu = menuBar()->addMenu(QStringLiteral("&Package"));
    packageMenu->addAction(reloadAction);
    QMenu* runMenu = menuBar()->addMenu(QStringLiteral("&Run"));
    runMenu->addAction(validateAction);
    runMenu->addAction(generateAction);

    auto* splitter = new QSplitter(this);
    m_navigation = new QListWidget(splitter);
    m_navigation->setFixedWidth(170);
    m_navigation->addItems({
        QStringLiteral("Start"),
        QStringLiteral("Overview"),
        QStringLiteral("Topology"),
        QStringLiteral("Parameters"),
        QStringLiteral("Validate"),
        QStringLiteral("Generate")
    });
    m_pages = new QStackedWidget(splitter);
    splitter->addWidget(m_navigation);
    splitter->addWidget(m_pages);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);
    connect(m_navigation, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);

    auto* startPage = new QWidget;
    auto* startLayout = new QVBoxLayout(startPage);
    auto* startHeading = new QLabel(QStringLiteral("Create a NoC from a runtime Package"));
    QFont headingFont = startHeading->font();
    headingFont.setPointSize(18);
    headingFont.setBold(true);
    startHeading->setFont(headingFont);
    startLayout->addWidget(startHeading);
    startLayout->addWidget(new QLabel(
        QStringLiteral("Finepaper stores design intent only. Routers, links and visual geometry are derived from the Mesh.")));
    auto* startForm = new QFormLayout;
    m_startPackage = new QComboBox;
    m_startName = new QLineEdit(QStringLiteral("my_noc"));
    m_startRows = new QSpinBox;
    m_startColumns = new QSpinBox;
    m_startRows->setRange(1, 1024);
    m_startColumns->setRange(1, 1024);
    m_startRows->setValue(2);
    m_startColumns->setValue(2);
    startForm->addRow(QStringLiteral("NoC Package"), m_startPackage);
    startForm->addRow(QStringLiteral("Design name"), m_startName);
    startForm->addRow(QStringLiteral("Mesh rows"), m_startRows);
    startForm->addRow(QStringLiteral("Mesh columns"), m_startColumns);
    startLayout->addLayout(startForm);
    auto* createButton = new QPushButton(QStringLiteral("Create Mesh NoC"));
    createButton->setDefault(true);
    connect(createButton, &QPushButton::clicked, this, &FinepaperMainWindow::createDesign);
    startLayout->addWidget(createButton, 0, Qt::AlignLeft);
    startLayout->addStretch();
    m_pages->addWidget(startPage);

    auto* overviewPage = new QWidget;
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    m_overview = new QLabel(QStringLiteral("No design is open."));
    m_overview->setWordWrap(true);
    m_overview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overviewLayout->addWidget(m_overview);
    overviewLayout->addStretch();
    m_pages->addWidget(overviewPage);

    auto* topologyPage = new QWidget;
    auto* topologyLayout = new QVBoxLayout(topologyPage);
    topologyLayout->addWidget(new QLabel(
        QStringLiteral("Topology is a projection. Select a Router here, then add or move an Endpoint below.")));
    m_topologyScene = new QGraphicsScene(this);
    m_topologyView = new QGraphicsView(m_topologyScene);
    m_topologyView->setRenderHint(QPainter::Antialiasing);
    topologyLayout->addWidget(m_topologyView, 1);

    auto* endpointBox = new QWidget;
    auto* endpointLayout = new QGridLayout(endpointBox);
    m_endpointId = new QLineEdit;
    m_endpointType = new QComboBox;
    m_endpointX = new QSpinBox;
    m_endpointY = new QSpinBox;
    m_endpointX->setRange(0, 1023);
    m_endpointY->setRange(0, 1023);
    auto* addEndpointButton = new QPushButton(QStringLiteral("Add Endpoint"));
    auto* moveEndpointButton = new QPushButton(QStringLiteral("Move Selected"));
    auto* removeEndpointButton = new QPushButton(QStringLiteral("Remove Selected"));
    endpointLayout->addWidget(new QLabel(QStringLiteral("Endpoint id")), 0, 0);
    endpointLayout->addWidget(m_endpointId, 0, 1);
    endpointLayout->addWidget(new QLabel(QStringLiteral("Type")), 0, 2);
    endpointLayout->addWidget(m_endpointType, 0, 3);
    endpointLayout->addWidget(new QLabel(QStringLiteral("Router x")), 1, 0);
    endpointLayout->addWidget(m_endpointX, 1, 1);
    endpointLayout->addWidget(new QLabel(QStringLiteral("Router y")), 1, 2);
    endpointLayout->addWidget(m_endpointY, 1, 3);
    endpointLayout->addWidget(addEndpointButton, 0, 4);
    endpointLayout->addWidget(moveEndpointButton, 1, 4);
    endpointLayout->addWidget(removeEndpointButton, 1, 5);
    topologyLayout->addWidget(endpointBox);

    m_endpoints = new QTableWidget;
    m_endpoints->setColumnCount(5);
    m_endpoints->setHorizontalHeaderLabels({
        QStringLiteral("Id"), QStringLiteral("Type"), QStringLiteral("Router x"),
        QStringLiteral("Router y"), QStringLiteral("Derived slot")
    });
    m_endpoints->horizontalHeader()->setStretchLastSection(true);
    m_endpoints->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_endpoints->setSelectionMode(QAbstractItemView::SingleSelection);
    m_endpoints->setEditTriggers(QAbstractItemView::NoEditTriggers);
    topologyLayout->addWidget(m_endpoints, 1);
    connect(addEndpointButton, &QPushButton::clicked, this, &FinepaperMainWindow::addEndpoint);
    connect(moveEndpointButton, &QPushButton::clicked, this, &FinepaperMainWindow::moveSelectedEndpoint);
    connect(removeEndpointButton, &QPushButton::clicked, this, &FinepaperMainWindow::removeSelectedEndpoint);
    connect(m_endpoints, &QTableWidget::itemSelectionChanged,
            this, &FinepaperMainWindow::updateEndpointInputsFromSelection);
    connect(m_topologyScene, &QGraphicsScene::selectionChanged,
            this, &FinepaperMainWindow::updateSelectedRouterFromTopology);
    m_pages->addWidget(topologyPage);

    auto* parametersPage = new QWidget;
    auto* parametersLayout = new QVBoxLayout(parametersPage);
    parametersLayout->addWidget(new QLabel(
        QStringLiteral("These fields come from the selected Package. Finepaper does not interpret Package-specific semantics.")));
    auto* parametersContent = new QWidget;
    m_parameterForm = new QFormLayout(parametersContent);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(parametersContent);
    parametersLayout->addWidget(scroll, 1);
    auto* applyParametersButton = new QPushButton(QStringLiteral("Apply Parameters"));
    connect(applyParametersButton, &QPushButton::clicked, this, &FinepaperMainWindow::applyParameters);
    parametersLayout->addWidget(applyParametersButton, 0, Qt::AlignLeft);
    m_pages->addWidget(parametersPage);

    auto* validatePage = new QWidget;
    auto* validateLayout = new QVBoxLayout(validatePage);
    auto* validateButton = new QPushButton(QStringLiteral("Validate current design"));
    m_validationReport = new QTextEdit;
    m_validationReport->setReadOnly(true);
    validateLayout->addWidget(validateButton, 0, Qt::AlignLeft);
    validateLayout->addWidget(m_validationReport, 1);
    connect(validateButton, &QPushButton::clicked, this, &FinepaperMainWindow::validateDesign);
    m_pages->addWidget(validatePage);

    auto* generatePage = new QWidget;
    auto* generateLayout = new QVBoxLayout(generatePage);
    auto* outputLayout = new QHBoxLayout;
    m_outputRoot = new QLineEdit(QDir::current().filePath(QStringLiteral("output")));
    auto* browseOutput = new QPushButton(QStringLiteral("Choose output…"));
    auto* generateButton = new QPushButton(QStringLiteral("Validate and generate RTL"));
    outputLayout->addWidget(new QLabel(QStringLiteral("Output root")));
    outputLayout->addWidget(m_outputRoot, 1);
    outputLayout->addWidget(browseOutput);
    m_generationReport = new QTextEdit;
    m_generationReport->setReadOnly(true);
    generateLayout->addLayout(outputLayout);
    generateLayout->addWidget(generateButton, 0, Qt::AlignLeft);
    generateLayout->addWidget(m_generationReport, 1);
    connect(browseOutput, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Select output root"), m_outputRoot->text());
        if (!directory.isEmpty()) {
            m_outputRoot->setText(directory);
        }
    });
    connect(generateButton, &QPushButton::clicked, this, &FinepaperMainWindow::generateDesign);
    m_pages->addWidget(generatePage);

    m_navigation->setCurrentRow(0);
}

void FinepaperMainWindow::reloadPackages() {
    const QVector<Diagnostic> diagnostics = m_application.reloadPackages(m_packageRoots);
    updateStartPackages();
    showDiagnostics(diagnostics, QStringLiteral("Package discovery"), false);
    if (!containsErrors(diagnostics)) {
        statusBar()->showMessage(QStringLiteral("Loaded %1 runtime Package(s).").arg(m_application.packages().size()));
    }
}

void FinepaperMainWindow::updateStartPackages() {
    const QString previous = m_startPackage->currentData().toString();
    m_startPackage->clear();
    for (const PackageDefinition& package : m_application.packages()) {
        m_startPackage->addItem(QStringLiteral("%1 — %2").arg(package.key(), package.name), package.key());
    }
    const int previousIndex = m_startPackage->findData(previous);
    if (previousIndex >= 0) {
        m_startPackage->setCurrentIndex(previousIndex);
    }
}

const PackageDefinition* FinepaperMainWindow::selectedStartPackage() const {
    const QString key = m_startPackage->currentData().toString();
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
        if (package.id == m_design->package.id && package.version == m_design->package.version) {
            return &package;
        }
    }
    return nullptr;
}

void FinepaperMainWindow::createDesign() {
    const PackageDefinition* package = selectedStartPackage();
    if (!package) {
        QMessageBox::warning(this, QStringLiteral("No Package"),
                             QStringLiteral("No valid runtime Package is loaded."));
        return;
    }
    const QJsonObject request{
        {QStringLiteral("name"), m_startName->text().trimmed()},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), package->id},
            {QStringLiteral("version"), package->version}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), m_startRows->value()},
            {QStringLiteral("columns"), m_startColumns->value()}
        }}
    };
    adoptDesignResult(m_application.createDesign(request), QStringLiteral("Create Mesh"));
}

void FinepaperMainWindow::openDesign() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open NoC design"), QString(), QStringLiteral("Finepaper NoC (*.fpnoc *.json)"));
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
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(m_designPath));
}

void FinepaperMainWindow::validateDesign() {
    if (!m_design) {
        QMessageBox::information(this, QStringLiteral("Validate design"),
                                 QStringLiteral("Create or open a NoC design first."));
        return;
    }
    const ValidationResult result = m_application.validate(*m_design, true);
    m_validationReport->setPlainText(diagnosticText(result.diagnostics));
    m_navigation->setCurrentRow(4);
    statusBar()->showMessage(result.success ? QStringLiteral("Design is valid.")
                                            : QStringLiteral("Validation found errors."));
    if (!result.success) {
        showDiagnostics(result.diagnostics, QStringLiteral("Validate design"));
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
    const GenerationResult result = m_application.generate(*m_design, GenerationOptions{root});
    m_generationReport->setPlainText(QJsonDocument(generationResultToJson(result))
                                         .toJson(QJsonDocument::Indented));
    m_navigation->setCurrentRow(5);
    statusBar()->showMessage(result.success ? QStringLiteral("RTL generated.")
                                            : QStringLiteral("RTL generation failed."));
    if (!result.success) {
        showDiagnostics(result.diagnostics, QStringLiteral("Generate RTL"));
    }
}

void FinepaperMainWindow::addEndpoint() {
    if (!m_design) {
        return;
    }
    EndpointInstance endpoint;
    endpoint.id = m_endpointId->text();
    endpoint.type = m_endpointType->currentData().toString();
    endpoint.attachment.router = RouterPosition{m_endpointX->value(), m_endpointY->value()};
    adoptDesignResult(m_application.addEndpoint(*m_design, endpoint), QStringLiteral("Add Endpoint"));
}

void FinepaperMainWindow::moveSelectedEndpoint() {
    if (!m_design || m_endpoints->currentRow() < 0) {
        QMessageBox::information(this, QStringLiteral("Move Endpoint"),
                                 QStringLiteral("Select an Endpoint in the table first."));
        return;
    }
    const QString id = m_endpoints->item(m_endpoints->currentRow(), 0)->text();
    adoptDesignResult(m_application.moveEndpoint(
        *m_design, id, RouterPosition{m_endpointX->value(), m_endpointY->value()}),
        QStringLiteral("Move Endpoint"));
}

void FinepaperMainWindow::removeSelectedEndpoint() {
    if (!m_design || m_endpoints->currentRow() < 0) {
        QMessageBox::information(this, QStringLiteral("Remove Endpoint"),
                                 QStringLiteral("Select an Endpoint in the table first."));
        return;
    }
    const QString id = m_endpoints->item(m_endpoints->currentRow(), 0)->text();
    adoptDesignResult(m_application.removeEndpoint(*m_design, id), QStringLiteral("Remove Endpoint"));
}

QJsonValue FinepaperMainWindow::valueFromControl(const ParameterControl& control) const {
    if (const auto* spin = qobject_cast<QSpinBox*>(control.editor)) {
        return spin->value();
    }
    if (const auto* check = qobject_cast<QCheckBox*>(control.editor)) {
        return check->isChecked();
    }
    if (const auto* combo = qobject_cast<QComboBox*>(control.editor)) {
        return combo->currentData().isValid() ? combo->currentData().toString() : combo->currentText();
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

void FinepaperMainWindow::updateEndpointInputsFromSelection() {
    if (!m_design || m_endpoints->currentRow() < 0) {
        return;
    }
    const EndpointInstance& endpoint = m_design->endpoints.at(m_endpoints->currentRow());
    m_endpointId->setText(endpoint.id);
    const int typeIndex = m_endpointType->findData(endpoint.type);
    if (typeIndex >= 0) {
        m_endpointType->setCurrentIndex(typeIndex);
    }
    m_endpointX->setValue(endpoint.attachment.router.x);
    m_endpointY->setValue(endpoint.attachment.router.y);
}

void FinepaperMainWindow::updateSelectedRouterFromTopology() {
    const QList<QGraphicsItem*> items = m_topologyScene->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    const QVariant x = items.first()->data(0);
    const QVariant y = items.first()->data(1);
    if (x.isValid() && y.isValid()) {
        m_endpointX->setValue(x.toInt());
        m_endpointY->setValue(y.toInt());
    }
}

void FinepaperMainWindow::adoptDesignResult(const DesignResult& result, const QString& action) {
    if (!result.success) {
        showDiagnostics(result.diagnostics, action);
        return;
    }
    m_design = result.design;
    refreshDesignViews();
    statusBar()->showMessage(action + QStringLiteral(" completed."));
}

void FinepaperMainWindow::refreshDesignViews() {
    if (!m_design) {
        return;
    }
    setWindowTitle(QStringLiteral("Finepaper — %1").arg(m_design->name));
    m_overview->setText(QStringLiteral(
        "<h2>%1</h2><p><b>Package:</b> %2@%3</p><p><b>Topology:</b> %4 × %5 Mesh</p>"
        "<p><b>Endpoints:</b> %6</p><p>Routers and links are derived. Endpoints attach to Routers; "
        "there is no arbitrary IP connection graph.</p>")
                            .arg(m_design->name.toHtmlEscaped(),
                                 m_design->package.id.toHtmlEscaped(),
                                 m_design->package.version.toHtmlEscaped())
                            .arg(m_design->topology.rows)
                            .arg(m_design->topology.columns)
                            .arg(m_design->endpoints.size()));
    const int maxX = qMax(0, m_design->topology.columns - 1);
    const int maxY = qMax(0, m_design->topology.rows - 1);
    m_endpointX->setMaximum(maxX);
    m_endpointY->setMaximum(maxY);
    refreshTopology();
    refreshEndpointTable();
    rebuildParameterEditors();
}

void FinepaperMainWindow::refreshTopology() {
    m_topologyScene->clear();
    if (!m_design) {
        return;
    }
    const TopologyProjection projection = projectTopology(*m_design);
    constexpr qreal tile = 145.0;
    constexpr qreal routerSize = 76.0;
    QHash<QString, QPointF> centers;
    for (const RouterView& router : projection.routers) {
        centers.insert(router.id, QPointF(router.position.x * tile + routerSize / 2.0,
                                          router.position.y * tile + routerSize / 2.0));
    }
    for (const LinkView& link : projection.links) {
        m_topologyScene->addLine(QLineF(centers.value(link.fromRouter), centers.value(link.toRouter)),
                                 QPen(QColor(QStringLiteral("#4f6d7a")), 3));
    }
    QHash<QString, QStringList> endpoints;
    for (const EndpointView& endpoint : projection.endpoints) {
        endpoints[endpoint.routerId].append(endpoint.id + QStringLiteral(" [slot %1]").arg(endpoint.slot));
    }
    for (const RouterView& router : projection.routers) {
        const QPointF topLeft(router.position.x * tile, router.position.y * tile);
        auto* item = m_topologyScene->addRect(QRectF(topLeft, QSizeF(routerSize, routerSize)),
                                              QPen(QColor(QStringLiteral("#1d3557")), 2),
                                              QBrush(QColor(QStringLiteral("#dceef7"))));
        item->setFlag(QGraphicsItem::ItemIsSelectable, true);
        item->setData(0, router.position.x);
        item->setData(1, router.position.y);
        auto* title = m_topologyScene->addSimpleText(router.id);
        title->setPos(topLeft + QPointF(7, 7));
        auto* attached = m_topologyScene->addSimpleText(endpoints.value(router.id).join(QLatin1Char('\n')));
        attached->setBrush(QBrush(QColor(QStringLiteral("#264653"))));
        attached->setPos(topLeft + QPointF(7, routerSize + 6));
    }
    m_topologyScene->setSceneRect(m_topologyScene->itemsBoundingRect().adjusted(-30, -30, 80, 70));
    m_topologyView->fitInView(m_topologyScene->sceneRect(), Qt::KeepAspectRatio);
}

void FinepaperMainWindow::refreshEndpointTable() {
    m_endpoints->clearContents();
    m_endpoints->setRowCount(0);
    if (!m_design) {
        return;
    }
    const NocDesign resolved = withResolvedAutomaticSlots(*m_design);
    m_endpoints->setRowCount(resolved.endpoints.size());
    for (qsizetype row = 0; row < resolved.endpoints.size(); ++row) {
        const EndpointInstance& endpoint = resolved.endpoints.at(row);
        m_endpoints->setItem(row, 0, readOnlyItem(endpoint.id));
        m_endpoints->setItem(row, 1, readOnlyItem(endpoint.type));
        m_endpoints->setItem(row, 2, readOnlyItem(QString::number(endpoint.attachment.router.x)));
        m_endpoints->setItem(row, 3, readOnlyItem(QString::number(endpoint.attachment.router.y)));
        m_endpoints->setItem(row, 4, readOnlyItem(endpoint.attachment.slot.value_or(QStringLiteral("—"))));
    }
    m_endpoints->resizeColumnsToContents();
}

void FinepaperMainWindow::rebuildParameterEditors() {
    while (QLayoutItem* item = m_parameterForm->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_parameterControls.clear();
    m_endpointType->clear();
    const PackageDefinition* package = packageForDesign();
    if (!m_design || !package) {
        return;
    }
    for (const EndpointTypeDefinition& type : package->endpointTypes) {
        m_endpointType->addItem(type.label, type.id);
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
            auto* lineEdit = new QLineEdit(value.toString());
            editor = lineEdit;
        }
        m_parameterForm->addRow(definition.label, editor);
        m_parameterControls.append(ParameterControl{definition, editor});
    }
}

void FinepaperMainWindow::showDiagnostics(const QVector<Diagnostic>& diagnostics,
                                          const QString& title,
                                          bool modalOnError) {
    const QString text = diagnosticText(diagnostics);
    if (m_validationReport && !diagnostics.isEmpty()) {
        m_validationReport->setPlainText(text);
    }
    if (modalOnError && containsErrors(diagnostics)) {
        QMessageBox::critical(this, title, text);
    }
}

} // namespace finepaper
