// MainWindow — constructs and connects all top-level UI components.
// Layout: horizontal splitter (IP catalog | node editor | property panel)
// inside a vertical splitter with the log panel below.
#include "app/appcontext.h"
#include "app/appsettings.h"
#include "app/capabilityregistry.h"
#include "app/extensionpointregistry.h"
#include "app/interactionids.h"
#include "app/mainwindow.h"
#include "app/pluginhost.h"
#include "app/plugininteractionregistry.h"
#include "app/serviceids.h"
#include "app/serviceregistry.h"
#include "app/staticplugincatalog.h"
#include "app/toolpipelineservice.h"
#include "app/topologypresetinteractionhandler.h"
#include "app/workbenchservice.h"
#include "commands/addipinstancecommand.h"
#include "graph/graph.h"
#include "commands/commandmanager.h"
#include "ipcraft/ipcraftmanifest.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/ipcoreruntimediagnostics.h"
#include "nodeeditor/nodeeditorwidget.h"
#include "package/packageservice.h"
#include "panels/ipcorepathsdialog.h"
#include "panels/ipcatalogpanel.h"
#include "panels/propertypanel.h"
#include "panels/logpanel.h"
#include "project/designeditingservice.h"
#include "project/ipinstanceparameteradapter.h"
#include "project/editorprojectionservice.h"
#include "project/projectipservice.h"
#include "project/projectservice.h"
#include "project/projectstateservice.h"
#include "validation/validationmanager.h"
#include "workspace/activeworkspacecontroller.h"
#include "modules/moduleregistry.h"
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
#include <QHash>
#include <QJsonValue>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QKeySequence>
#include <QLineEdit>
#include <QList>
#include <QScopedValueRollback>
#include <QSet>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtGlobal>
#include <QVBoxLayout>
#include <algorithm>
#include <optional>
#include <utility>
#include <variant>

namespace {

QString projectFileDialogSaveFilter() {
    return QStringLiteral("Finepaper Project (*.fpproj)");
}

QString projectFileDialogOpenFilter() {
    return QStringLiteral("Finepaper Project (*.fpproj);;All Files (*)");
}

bool hasMeaningfulProjectDesign(const ipcraft::core::ProjectDesign& design) {
    return !design.schema.isEmpty() ||
           !design.id.isEmpty() ||
           !design.name.isEmpty() ||
           !design.packages.isEmpty() ||
           !design.components.isEmpty() ||
           !design.interfaces.isEmpty() ||
           !design.connections.isEmpty() ||
           !design.topologies.isEmpty() ||
           !design.constraints.isEmpty() ||
           !design.views.isEmpty() ||
           !design.diagnostics.isEmpty() ||
           !design.artifacts.isEmpty() ||
           !design.extensions.isEmpty() ||
           !design.metadata.isEmpty();
}

QJsonValue parameterValueToJson(const Parameter::Value& value) {
    if (const auto* stringValue = std::get_if<QString>(&value)) {
        return *stringValue;
    }
    if (const auto* intValue = std::get_if<int>(&value)) {
        return *intValue;
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return *doubleValue;
    }
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }
    return {};
}

QString instanceScopeKey(const QString& ipcoreId, const QString& instanceId) {
    return ipcoreId + QLatin1Char('\n') + instanceId;
}

QSet<QString> activeInstanceScopes(const ProjectDocument& document) {
    QSet<QString> scopes;
    for (const ProjectIpInstanceRecord& instance : document.instances) {
        scopes.insert(instanceScopeKey(instance.package.id, instance.id));
        scopes.insert(instanceScopeKey(instance.ipcoreId, instance.instanceId));
    }
    return scopes;
}

QHash<QString, ProjectModuleRecord> activeModuleRecords(const ProjectDocument& document,
                                                        const QSet<QString>& activeScopes) {
    QHash<QString, ProjectModuleRecord> records;
    for (const ProjectModuleRecord& record : document.modules) {
        if (activeScopes.contains(instanceScopeKey(record.ipcoreId, record.instanceId))) {
            records.insert(record.id, record);
        }
    }
    return records;
}

bool connectionRecordBelongsToActiveProjection(const ProjectConnectionRecord& record,
                                               const QHash<QString, ProjectModuleRecord>& modules) {
    if (record.interfaces.isEmpty()) {
        return modules.contains(record.source.moduleId) && modules.contains(record.target.moduleId);
    }
    return std::all_of(record.interfaces.cbegin(), record.interfaces.cend(),
                       [&modules](const ProjectConnectionInterfaceRef& interfaceRef) {
                           return modules.contains(interfaceRef.instanceId);
                       });
}

QHash<QString, ProjectConnectionRecord> activeConnectionRecords(
    const ProjectDocument& document,
    const QHash<QString, ProjectModuleRecord>& modules) {
    QHash<QString, ProjectConnectionRecord> records;
    for (const ProjectConnectionRecord& record : document.connections) {
        if (connectionRecordBelongsToActiveProjection(record, modules)) {
            records.insert(record.id, record);
        }
    }
    return records;
}

bool moduleParameterMatchesDocumentRecord(const Module& module,
                                          const QString& parameterName,
                                          const Parameter::Value& runtimeValue,
                                          const ProjectModuleRecord& record) {
    if (record.parameters.contains(parameterName)) {
        return parameterValueToJson(runtimeValue) == record.parameters.value(parameterName);
    }

    const ModuleType* type = ModuleRegistry::instance().getType(module.type());
    if (!type) {
        return false;
    }
    const auto defaultIt = type->defaultParameters.constFind(parameterName);
    if (defaultIt == type->defaultParameters.constEnd()) {
        return false;
    }
    return parameterValueToJson(runtimeValue) == parameterValueToJson(defaultIt.value().value());
}

bool moduleMatchesDocumentRecord(const Module& module, const ProjectModuleRecord& record) {
    if (module.type() != record.type ||
        module.ipcoreId() != record.ipcoreId ||
        module.instanceId() != record.instanceId) {
        return false;
    }

    for (auto it = module.parameters().constBegin(); it != module.parameters().constEnd(); ++it) {
        if (!moduleParameterMatchesDocumentRecord(module, it.key(), it.value().value(), record)) {
            return false;
        }
    }
    return true;
}

bool stringListsMatch(const QStringList& left, const QStringList& right) {
    QStringList normalizedLeft = left;
    QStringList normalizedRight = right;
    normalizedLeft.sort();
    normalizedRight.sort();
    return normalizedLeft == normalizedRight;
}

bool connectionMatchesDocumentRecord(const Connection& connection,
                                     const ProjectConnectionRecord& record) {
    if (connection.connectionClassId() != record.connectionClassId ||
        connection.status() != record.status ||
        !stringListsMatch(connection.alternatives(), record.alternatives)) {
        return false;
    }

    if (record.interfaces.isEmpty()) {
        return connection.source().moduleId == record.source.moduleId &&
               connection.source().portId == record.source.portId &&
               connection.target().moduleId == record.target.moduleId &&
               connection.target().portId == record.target.portId;
    }

    const QVector<ConnectionInterfaceRef> runtimeInterfaces = connection.interfaces();
    if (runtimeInterfaces.size() != record.interfaces.size()) {
        return false;
    }
    for (qsizetype index = 0; index < runtimeInterfaces.size(); ++index) {
        if (runtimeInterfaces.at(index).instanceId != record.interfaces.at(index).instanceId ||
            runtimeInterfaces.at(index).interfaceId != record.interfaces.at(index).interfaceId) {
            return false;
        }
    }
    return true;
}

QStringList unmigratedGraphProjectionOwners(const Graph& graph,
                                            const ProjectDocument& stagedDocument) {
    QStringList owners;
    const auto addOwner = [&owners](const QString& owner) {
        if (!owners.contains(owner)) {
            owners.append(owner);
        }
    };

    const QSet<QString> activeScopes = activeInstanceScopes(stagedDocument);
    const QHash<QString, ProjectModuleRecord> moduleRecords =
        activeModuleRecords(stagedDocument, activeScopes);
    QSet<QString> liveModuleIds;
    for (const std::unique_ptr<Module>& module : graph.modules()) {
        if (!module) {
            continue;
        }
        const QString scope = instanceScopeKey(module->ipcoreId(), module->instanceId());
        if (!activeScopes.contains(scope)) {
            addOwner(QStringLiteral("Unowned graph projection module"));
            continue;
        }
        liveModuleIds.insert(module->id());
        const auto recordIt = moduleRecords.constFind(module->id());
        if (recordIt == moduleRecords.constEnd()) {
            addOwner(QStringLiteral("Unowned graph projection module record"));
            continue;
        }
        if (!moduleMatchesDocumentRecord(*module, recordIt.value())) {
            addOwner(QStringLiteral("Unowned graph projection module parameters/layout"));
        }
    }
    for (auto it = moduleRecords.constBegin(); it != moduleRecords.constEnd(); ++it) {
        if (!liveModuleIds.contains(it.key())) {
            addOwner(QStringLiteral("Unowned graph projection module removal"));
        }
    }

    const QHash<QString, ProjectConnectionRecord> connectionRecords =
        activeConnectionRecords(stagedDocument, moduleRecords);
    QSet<QString> liveConnectionIds;
    for (const std::unique_ptr<Connection>& connection : graph.connections()) {
        if (!connection) {
            continue;
        }
        const bool sourceActive = liveModuleIds.contains(connection->source().moduleId);
        const bool targetActive = liveModuleIds.contains(connection->target().moduleId);
        if (!sourceActive || !targetActive) {
            addOwner(QStringLiteral("Unowned graph projection connection"));
            continue;
        }
        liveConnectionIds.insert(connection->id());
        const auto recordIt = connectionRecords.constFind(connection->id());
        if (recordIt == connectionRecords.constEnd()) {
            addOwner(QStringLiteral("Unowned graph projection connection record"));
            continue;
        }
        if (!connectionMatchesDocumentRecord(*connection, recordIt.value())) {
            addOwner(QStringLiteral("Unowned graph projection connection metadata"));
        }
    }
    for (auto it = connectionRecords.constBegin(); it != connectionRecords.constEnd(); ++it) {
        if (!liveConnectionIds.contains(it.key())) {
            addOwner(QStringLiteral("Unowned graph projection connection removal"));
        }
    }

    owners.sort();
    return owners;
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

QString ipcraftDiagnosticLine(const IpcraftDiagnostic& diagnostic) {
    QStringList locationParts;
    if (!diagnostic.packageRootPath.trimmed().isEmpty()) {
        locationParts.append(diagnostic.packageRootPath.trimmed());
    }
    if (!diagnostic.path.trimmed().isEmpty()) {
        locationParts.append(diagnostic.path.trimmed());
    }

    const QString location = locationParts.isEmpty()
        ? QString()
        : QStringLiteral(" %1").arg(locationParts.join(QStringLiteral(" ")));
    const QString severity = diagnostic.severity.trimmed().isEmpty()
        ? QStringLiteral("error")
        : diagnostic.severity.trimmed();
    return QStringLiteral("[%1]%2: %3")
        .arg(severity, location, diagnostic.message.trimmed());
}

QStringList ipcraftDiagnosticLines(const QVector<IpcraftDiagnostic>& diagnostics) {
    QStringList lines;
    for (const IpcraftDiagnostic& diagnostic : diagnostics) {
        lines.append(ipcraftDiagnosticLine(diagnostic));
    }
    return lines;
}

QString packageIdForEntry(const IpCatalogEntry& entry) {
    return entry.packageId.trimmed().isEmpty() ? entry.id : entry.packageId;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_graph(new Graph(this)),
      m_workbenchService(std::make_unique<WorkbenchService>()),
      m_commandManager(std::make_unique<CommandManager>()),
      m_appSettings(std::make_unique<AppSettings>()),
      m_packageService(std::make_unique<PackageService>(&ModuleRegistry::instance())),
      m_ipCatalogService(std::make_unique<IpCatalogService>()),
      m_projectService(std::make_unique<ProjectService>()),
      m_designEditingService(std::make_unique<DesignEditingService>()),
      m_projectStateService(std::make_unique<ProjectStateService>()),
      m_projectIpService(std::make_unique<ProjectIpService>(m_projectStateService.get())),
      m_editorProjectionService(std::make_unique<EditorProjectionService>(
          m_graph,
          m_projectStateService.get(),
          m_projectIpService.get(),
          m_projectService.get())),
      m_toolPipelineService(std::make_unique<ToolPipelineService>()),
      m_serviceRegistry(std::make_unique<ServiceRegistry>()),
      m_extensionPointRegistry(std::make_unique<ExtensionPointRegistry>()),
      m_capabilityRegistry(std::make_unique<CapabilityRegistry>()),
      m_pluginInteractionRegistry(std::make_unique<PluginInteractionRegistry>()),
      m_pluginHost(nullptr),
      m_activeWorkspaceController(std::make_unique<ActiveWorkspaceController>(
          m_projectIpService.get(),
          m_ipCatalogService.get())),
      m_topologyPresetInteractionHandler(std::make_unique<TopologyPresetInteractionHandler>(
          this,
          m_designEditingService.get(),
          m_activeWorkspaceController.get())),
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
    m_serviceRegistry->registerService(app::serviceids::project(), m_projectService.get());
    m_serviceRegistry->registerService(app::serviceids::workbench(), m_workbenchService.get());
    m_serviceRegistry->registerService(app::serviceids::designEditing(),
                                       m_designEditingService.get());
    m_serviceRegistry->registerService(app::serviceids::package(), m_packageService.get());
    m_serviceRegistry->registerService(app::serviceids::toolPipeline(),
                                       m_toolPipelineService.get());

    connect(m_designEditingService.get(),
            &DesignEditingService::designChanged,
            this,
            [this]() {
                syncProjectServiceFromDesignEditingService();
            });

    AppContext context;
    context.services = m_serviceRegistry.get();
    context.extensionPoints = m_extensionPointRegistry.get();
    context.capabilities = m_capabilityRegistry.get();
    context.interactions = m_pluginInteractionRegistry.get();

    m_pluginHost = std::make_unique<PluginHost>(context);
    if (!registerStaticPluginInteractions(*m_pluginInteractionRegistry)) {
        qCritical().noquote() << "Static plugin interactions could not be registered.";
    }
    registerMainWindowInteractionHandlers();
    registerStaticPlugins(*m_pluginHost);
    const PluginActivationResult activationResult = m_pluginHost->activatePlugins();
    if (!activationResult.success) {
        qCritical().noquote() << "Plugin activation failed:" << activationResult.error;
    }

    // Build the window in dependency order: widgets first, then signal wiring,
    // then actions/menus that depend on those widgets.
    reloadIpcoreCatalog();
    setupPanels();
    registerBuiltinWorkbenchContributions();
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

MainWindow::~MainWindow() {
    // UI children hold raw service/adapter pointers; destroy them before member services.
    delete m_validationManager;
    m_validationManager = nullptr;

    delete m_propertyDock;
    m_propertyDock = nullptr;
    m_propertyPanel = nullptr;

    delete m_ipCatalogDock;
    m_ipCatalogDock = nullptr;
    m_ipCatalogPanel = nullptr;

    delete m_logDock;
    m_logDock = nullptr;
    m_logPanel = nullptr;

    delete takeCentralWidget();
    m_nodeEditor = nullptr;
}

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

    ProjectService stagedProject;
    const ProjectServiceResult createResult =
        stagedProject.createNew(QFileInfo(projectPath).completeBaseName());
    if (!createResult.success) {
        qWarning() << "Failed to create project document" << createResult.error;
        QMessageBox::warning(this, "Create Project Failed", createResult.error);
        return false;
    }

    const ProjectServiceResult saveResult = stagedProject.saveFile(projectPath);
    if (!saveResult.success) {
        qWarning() << "Failed to create project at" << projectPath << saveResult.error;
        QMessageBox::warning(this, "Create Project Failed", saveResult.error);
        return false;
    }

    clearDocument();
    const ProjectServiceResult adoptResult =
        m_projectService->replaceDocumentFromLoadedFile(stagedProject.document(),
                                                        stagedProject.currentPath());
    if (!adoptResult.success) {
        qWarning() << "Failed to adopt created project" << adoptResult.error;
        QMessageBox::warning(this, "Create Project Failed", adoptResult.error);
        return false;
    }
    seedDesignEditingServiceFromProjectService();

    setCurrentDocumentPath(projectPath);
    m_projectStateDirty = false;
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
    request.projectDesign = m_projectService ? &m_projectService->design() : nullptr;
    request.projectPath = m_currentDocumentPath;
    request.designName = QFileInfo(m_currentDocumentPath).completeBaseName();
    request.catalogEntries = m_ipCatalogService ? m_ipCatalogService->entries() : QList<IpCatalogEntry>{};

    const QString outputRoot =
        QFileInfo(m_currentDocumentPath).absoluteDir().filePath(QStringLiteral("generated"));
    m_logPanel->appendMessage(QString("[Generate] Start project=%1 output=%2")
                                  .arg(m_currentDocumentPath, outputRoot),
                              QColor(70, 110, 190));

    statusBar()->showMessage("Generating project IP instances...");
    QApplication::setOverrideCursor(Qt::WaitCursor);

    const ProjectGenerationResult result = m_toolPipelineService->generateProject(request);
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
    m_validationManager->runValidation(m_currentDocumentPath,
                                       QFileInfo(m_currentDocumentPath).completeBaseName());
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
    executeWorkspaceInteractionFor(workspace.ipcoreId,
                                   workspace.instanceId,
                                   action->data().toString());
}

void MainWindow::executeWorkspaceInteractionFor(const QString& ipcoreId,
                                                const QString& instanceId,
                                                const QString& interactionId) {
    if (!requireOpenProject(QStringLiteral("using workspace tools"))) {
        return;
    }
    if (!m_pluginInteractionRegistry ||
        !m_activeWorkspaceController ||
        ipcoreId.trimmed().isEmpty() ||
        instanceId.trimmed().isEmpty() ||
        interactionId.trimmed().isEmpty()) {
        qWarning().noquote() << "Ignoring incomplete workspace interaction request";
        return;
    }

    const std::optional<ActiveWorkspaceContext> context =
        m_activeWorkspaceController->activeContext();
    if (!context.has_value() ||
        context->record.ipcoreId != ipcoreId ||
        context->record.instanceId != instanceId) {
        qWarning().noquote() << "Ignoring workspace interaction request for inactive IP instance:"
                             << ipcoreId << instanceId << interactionId;
        return;
    }

    const PackageCoverageReport* coverage = m_packageService
        ? m_packageService->coverageReport(packageIdForEntry(context->entry))
        : nullptr;
    const std::optional<PluginInteractionDescriptor> interaction =
        m_pluginInteractionRegistry->interactionForWorkspace(interactionId,
                                                             context->workspace,
                                                             context->entry,
                                                             coverage);
    if (!interaction.has_value()) {
        qWarning().noquote() << "Ignoring unknown workspace interaction request:" << interactionId;
        return;
    }
    if (!interaction->enabled) {
        qWarning().noquote() << "Ignoring disabled workspace interaction request:" << interactionId;
        return;
    }

    PluginInteractionContext dispatchContext;
    dispatchContext.ipcoreId = ipcoreId;
    dispatchContext.instanceId = instanceId;
    dispatchContext.packageId = interaction->packageId;
    const PluginInteractionResult result =
        m_pluginInteractionRegistry->dispatch(*interaction, dispatchContext);
    if (!result.handled || !result.success) {
        qWarning().noquote() << "Workspace interaction failed:" << interactionId
                             << result.message;
        return;
    }
    syncDocumentStateFromHistory();
    if (!result.message.trimmed().isEmpty()) {
        statusBar()->showMessage(result.message, 5000);
    }
}

void MainWindow::registerMainWindowInteractionHandlers() {
    if (!m_pluginInteractionRegistry) {
        return;
    }

    if (!m_topologyPresetInteractionHandler ||
        !m_topologyPresetInteractionHandler->registerHandlers(*m_pluginInteractionRegistry)) {
        qCritical().noquote() << "Topology workspace interaction handler could not be registered.";
    }
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
    m_nodeEditor = new NodeEditorWidget(m_graph,
                                        m_projectStateService.get(),
                                        m_activeWorkspaceController.get(),
                                        m_commandManager.get(),
                                        this);
    m_propertyPanel = new PropertyPanel(m_graph,
                                        m_projectStateService.get(),
                                        rebuildIpInstanceParameterAdapters(),
                                        m_commandManager.get(),
                                        this);
    m_ipCatalogPanel = new IpCatalogPanel(m_ipCatalogService.get(),
                                          m_projectStateService.get(),
                                          m_projectIpService.get(),
                                          m_activeWorkspaceController.get(),
                                          m_pluginInteractionRegistry.get(),
                                          m_packageService.get(),
                                          this);
    m_logPanel = new LogPanel(this);
    m_validationManager = new ValidationManager(m_graph,
                                                m_projectStateService.get(),
                                                m_ipCatalogService.get(),
                                                m_activeWorkspaceController.get(),
                                                m_logPanel,
                                                m_projectService ? &m_projectService->design() : nullptr,
                                                this);

    m_nodeEditor->setObjectName("nodeEditorPanel");
    m_propertyPanel->setObjectName("propertyPanel");
    m_logPanel->setObjectName("logPanel");
}

void MainWindow::registerBuiltinWorkbenchContributions() {
    if (!m_workbenchService) {
        return;
    }

    WorkbenchEditorContribution editor;
    editor.id = QStringLiteral("builtin.node-editor");
    editor.title = QStringLiteral("Node Editor");
    editor.objectName = QStringLiteral("nodeEditorPanel");
    editor.factory = [this](QWidget*) { return m_nodeEditor; };
    m_workbenchService->addEditor(editor);

    WorkbenchPanelContribution catalog;
    catalog.id = QStringLiteral("builtin.ip-catalog");
    catalog.title = QStringLiteral("IP Catalog");
    catalog.objectName = QStringLiteral("ipCatalogDock");
    catalog.area = WorkbenchPanelArea::Left;
    catalog.factory = [this](QWidget*) { return m_ipCatalogPanel; };
    m_workbenchService->addPanel(catalog);

    WorkbenchPanelContribution properties;
    properties.id = QStringLiteral("builtin.properties");
    properties.title = QStringLiteral("Properties");
    properties.objectName = QStringLiteral("propertyDock");
    properties.area = WorkbenchPanelArea::Right;
    properties.factory = [this](QWidget*) { return m_propertyPanel; };
    m_workbenchService->addPanel(properties);

    WorkbenchPanelContribution log;
    log.id = QStringLiteral("builtin.activity-log");
    log.title = QStringLiteral("Activity Log");
    log.objectName = QStringLiteral("logDock");
    log.area = WorkbenchPanelArea::Bottom;
    log.factory = [this](QWidget*) { return m_logPanel; };
    m_workbenchService->addPanel(log);
}

void MainWindow::setupConnections() {
    // Keep validation-entry selection synchronized between the log, canvas, and property panel.
    connect(m_logPanel, &LogPanel::elementSelected, m_nodeEditor, &NodeEditorWidget::highlightElement);
    connect(m_logPanel,
            &LogPanel::elementSelected,
            m_propertyPanel,
            QOverload<QString>::of(&PropertyPanel::setSelectedModule));
    connect(m_nodeEditor,
            &NodeEditorWidget::moduleSelected,
            m_propertyPanel,
            QOverload<QString>::of(&PropertyPanel::setSelectedModule));
    connect(m_nodeEditor,
            &NodeEditorWidget::connectionSelected,
            m_propertyPanel,
            QOverload<QString>::of(&PropertyPanel::setSelectedModule));

    const auto trackGraphChange = [this]() {
        // Bulk load/import paths suppress tracking until the graph is fully
        // rebuilt; otherwise intermediate signals would mark a clean document dirty.
        if (m_suppressDocumentTracking) {
            return;
        }
        m_projectStateDirty = true;
        scheduleDocumentStateRefresh();
    };
    const auto appendConnectionAmbiguity = [this](Connection* connection) {
        if (m_logPanel && connection) {
            m_logPanel->appendConnectionAmbiguityWarning(*connection);
        }
    };

    connect(m_graph, &Graph::moduleAdded, this, [trackGraphChange](Module*) { trackGraphChange(); });
    connect(m_graph, &Graph::moduleRemoved, this, [trackGraphChange](const QString&) { trackGraphChange(); });
    connect(m_graph, &Graph::connectionAdded, this, [trackGraphChange, appendConnectionAmbiguity](Connection* connection) {
        trackGraphChange();
        appendConnectionAmbiguity(connection);
    });
    connect(m_graph, &Graph::connectionRemoved, this, [trackGraphChange](const QString&) { trackGraphChange(); });
    connect(m_graph, &Graph::connectionChanged, this, [trackGraphChange, appendConnectionAmbiguity](Connection* connection) {
        trackGraphChange();
        appendConnectionAmbiguity(connection);
    });
    connect(m_graph, &Graph::parameterChanged, this, [trackGraphChange](const QString&, const QString&) {
        trackGraphChange();
    });
    const auto trackProjectStateChange = [this, trackGraphChange]() {
        if (!m_suppressDocumentTracking) {
            syncProjectServiceFromProjectState();
        }
        m_projectStateDirty = true;
        trackGraphChange();
    };
    connect(m_projectStateService.get(),
            &ProjectStateService::parameterChanged,
            this,
            [trackProjectStateChange](const QString&, const QString&, const QString&, const QString&) {
                trackProjectStateChange();
            });
    connect(m_projectStateService.get(),
            &ProjectStateService::ipInstanceRecordsChanged,
            this,
            [trackProjectStateChange]() {
                trackProjectStateChange();
            });
    connect(m_projectIpService.get(),
            &ProjectIpService::ipInstancesChanged,
            this,
            [trackProjectStateChange]() {
                trackProjectStateChange();
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
                Q_UNUSED(ipcoreId);
                Q_UNUSED(instanceId);
                QMessageBox::warning(
                    this,
                    QStringLiteral("IP Catalog"),
                    QStringLiteral("Removing IP instances requires the design-level patch path."));
            });
    connect(m_ipCatalogPanel,
            &IpCatalogPanel::workspaceToolRequested,
            this,
            [this](const QString& toolId, const QString& ipcoreId, const QString& instanceId) {
                executeWorkspaceInteractionFor(ipcoreId, instanceId, toolId);
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
    auto* manageIpcorePathsAction = new QAction("IP Core Packages...", this);
    manageIpcorePathsAction->setObjectName(QStringLiteral("manageIpcorePathsAction"));
    manageIpcorePathsAction->setToolTip("Add, remove, and reload IP core package roots.");
    connect(manageIpcorePathsAction,
            &QAction::triggered,
            this,
            &MainWindow::manageIpcorePackageRoots);
    toolsMenu->addSeparator();
    toolsMenu->addAction(manageIpcorePathsAction);

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
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* editorWidget = m_nodeEditor;
    if (m_workbenchService && !m_workbenchService->editors().isEmpty()) {
        const WorkbenchEditorContribution editor = m_workbenchService->editors().first();
        if (editor.factory) {
            if (QWidget* contributedEditor = editor.factory(central)) {
                editorWidget = contributedEditor;
            }
        }
    }

    layout->addWidget(editorWidget);
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

    if (m_workbenchService) {
        for (const WorkbenchPanelContribution& panel : m_workbenchService->panels()) {
            QWidget* content = panel.factory ? panel.factory(this) : nullptr;
            if (!content) {
                continue;
            }

            QDockWidget* dock = createDock(panel.title,
                                           content,
                                           dockAreaForWorkbenchPanel(panel.area),
                                           panel.objectName);
            if (panel.id == QStringLiteral("builtin.ip-catalog")) {
                m_ipCatalogDock = dock;
            } else if (panel.id == QStringLiteral("builtin.properties")) {
                m_propertyDock = dock;
            } else if (panel.id == QStringLiteral("builtin.activity-log")) {
                m_logDock = dock;
            }
        }
    }

    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    if (m_ipCatalogDock && m_propertyDock) {
        resizeDocks({m_ipCatalogDock, m_propertyDock}, {280, 320}, Qt::Horizontal);
    }
    if (m_logDock) {
        resizeDocks({m_logDock}, {180}, Qt::Vertical);
    }

    // Register dock toggle actions under View so users can restore hidden panels.
    QMenu* viewMenu = nullptr;
    for (QAction* action : menuBar()->actions()) {
        if (action && action->menu() && action->menu()->objectName() == "viewMenu") {
            viewMenu = action->menu();
            break;
        }
    }
    if (viewMenu) {
        if (m_ipCatalogDock) {
            viewMenu->addAction(m_ipCatalogDock->toggleViewAction());
        }
        if (m_propertyDock) {
            viewMenu->addAction(m_propertyDock->toggleViewAction());
        }
        if (m_logDock) {
            viewMenu->addAction(m_logDock->toggleViewAction());
        }
    }
}

Qt::DockWidgetArea MainWindow::dockAreaForWorkbenchPanel(WorkbenchPanelArea area) const {
    switch (area) {
    case WorkbenchPanelArea::Left:
        return Qt::LeftDockWidgetArea;
    case WorkbenchPanelArea::Right:
        return Qt::RightDockWidgetArea;
    case WorkbenchPanelArea::Bottom:
        return Qt::BottomDockWidgetArea;
    }
    return Qt::LeftDockWidgetArea;
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
    if (!m_projectOpen || !m_activeWorkspaceController || !m_pluginInteractionRegistry) {
        return;
    }

    const std::optional<ActiveWorkspaceContext> context =
        m_activeWorkspaceController->activeContext();
    if (!context.has_value()) {
        return;
    }

    const PackageCoverageReport* coverage = m_packageService
        ? m_packageService->coverageReport(packageIdForEntry(context->entry))
        : nullptr;
    const QVector<PluginInteractionDescriptor> interactions =
        m_pluginInteractionRegistry->interactionsForWorkspace(context->workspace,
                                                              context->entry,
                                                              coverage);
    bool hasTopologyInteraction = false;
    for (const PluginInteractionDescriptor& interaction : interactions) {
        if (interaction.kind != app::interactionids::topologyPreset()) {
            continue;
        }

        QAction* action = m_topologyMenu->addAction(interaction.label);
        action->setData(interaction.id);
        action->setEnabled(interaction.enabled);
        connect(action, &QAction::triggered, this, &MainWindow::createTopologyPreset);
        hasTopologyInteraction = true;
    }
    m_topologyMenu->setEnabled(hasTopologyInteraction);
}

QVector<IIpInstanceParameterAdapter*> MainWindow::rebuildIpInstanceParameterAdapters() {
    m_ipInstanceParameterAdapters.clear();
    QVector<IIpInstanceParameterAdapter*> adapters;
    if (!m_ipCatalogService) {
        return adapters;
    }

    for (const IpCatalogEntry& entry : m_ipCatalogService->entries()) {
        auto adapter = std::make_unique<CatalogIpInstanceParameterAdapter>(
            entry.id,
            entry.name,
            entry.instanceParameters);
        adapters.push_back(adapter.get());
        m_ipInstanceParameterAdapters.push_back(std::move(adapter));
    }
    return adapters;
}

void MainWindow::manageIpcorePackageRoots() {
    IpcorePathsDialog dialog(this);
    const QStringList currentPaths = m_appSettings ? m_appSettings->ipcorePaths() : QStringList{};
    ModuleRegistry diagnosticsRegistry(ModuleRegistry::LoadMode::Empty);
    PackageService diagnosticsService(&diagnosticsRegistry);
    diagnosticsService.setCapabilityRegistry(m_capabilityRegistry.get());
    const PackageServiceLoadResult diagnosticsResult =
        diagnosticsService.reloadPackageRoots(currentPaths);
    dialog.setPaths(currentPaths);
    dialog.setDiagnostics(ipcraftDiagnosticLines(diagnosticsResult.diagnostics));

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (m_appSettings) {
        m_appSettings->setIpcorePaths(dialog.paths());
    }
    reloadIpcoreCatalog();
}

void MainWindow::reloadIpcoreCatalog() {
    if (!m_ipCatalogService || !m_packageService) {
        return;
    }

    const QStringList rootPaths = m_appSettings ? m_appSettings->ipcorePaths() : QStringList{};
    const PackageServiceLoadResult loadResult =
        m_packageService->reloadPackageRoots(rootPaths);
    const ModuleRegistry* moduleRegistry = m_packageService->moduleRegistry();
    *m_ipCatalogService = m_packageService->catalog();
    if (m_propertyPanel) {
        m_propertyPanel->setIpInstanceParameterAdapters(rebuildIpInstanceParameterAdapters());
    }

    if (m_logPanel) {
        m_logPanel->appendMessage(
            QStringLiteral("[IP Catalog] Reloaded %1 package root(s), %2 package(s).")
                .arg(loadResult.packageRootCount)
                .arg(loadResult.packageCount),
            QColor(70, 110, 190));
        for (const QString& line : ipcraftDiagnosticLines(loadResult.diagnostics)) {
            m_logPanel->appendMessage(QStringLiteral("[IP Catalog] %1").arg(line),
                                      QColor(220, 50, 50));
        }
        const QStringList lines = moduleRegistry
            ? IpCoreRuntimeDiagnostics::logLines(m_ipCatalogService->entries(), *moduleRegistry)
            : QStringList{};
        for (const QString& line : lines) {
            m_logPanel->appendMessage(line, QColor(70, 110, 190));
        }
    }

    if (m_ipCatalogPanel) {
        if (auto* search = m_ipCatalogPanel->findChild<QLineEdit*>(
                QStringLiteral("ipCatalogSearch"))) {
            const QString filter = search->text();
            search->setText(filter + QStringLiteral(" "));
            search->setText(filter);
        }
    }

    if (m_projectIpService) {
        const std::optional<ProjectIpInstanceRef> selected = m_projectIpService->selectedIpInstance();
        if (selected.has_value()) {
            m_projectIpService->handleIpInstanceRecordsMutated(
                std::nullopt,
                ProjectIpService::SelectionFallbackPolicy::ExactOrClear);
            m_projectIpService->selectInstance(selected->ipcoreId, selected->instanceId);
        }
    }

    rebuildTopologyMenu();
    statusBar()->showMessage(QStringLiteral("Reloaded IP core catalog"), 5000);
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

    ProjectService stagedProject;
    const ProjectServiceResult serviceLoadResult = stagedProject.loadFile(absolutePath);
    if (!serviceLoadResult.success) {
        qWarning() << "Failed to read project" << absolutePath << serviceLoadResult.error;
        QMessageBox::warning(this, "Open Failed", serviceLoadResult.error);
        return false;
    }

    // Suppress document tracking while Graph emits module/connection signals
    // for the newly loaded state.
    m_suppressDocumentTracking = true;
    const EditorProjectionResult projectionResult =
        m_editorProjectionService->rebuildProjectionFromDocument(stagedProject.document(),
                                                                 stagedProject.currentPath());
    if (!projectionResult.success) {
        m_suppressDocumentTracking = false;
        qWarning() << "Failed to rebuild editor projection" << absolutePath << projectionResult.error;
        QMessageBox::warning(this, "Open Failed", projectionResult.error);
        return false;
    }
    m_suppressDocumentTracking = false;
    if (m_propertyPanel) {
        m_propertyPanel->setSelectedModule(QString());
    }
    seedDesignEditingServiceFromProjectService();

    m_commandManager->clearHistory();
    m_projectStateDirty = false;
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

bool MainWindow::saveDocument(const QString& path) {
    const QString absolutePath = QFileInfo(pathWithProjectExtension(path)).absoluteFilePath();
    qInfo() << "Saving project to" << absolutePath;
    const bool designEditingDirty = m_designEditingDirty;
    std::optional<ipcraft::core::ProjectDesign> designBeforeProjection;
    bool reseedDesignEditingService = false;
    if ((designEditingDirty || m_projectStateDirty) &&
        m_projectOpen &&
        m_designEditingService &&
        m_projectService &&
        m_projectService->hasDocument()) {
        const ipcraft::core::ProjectDesign currentDesign = m_designEditingService->design();
        if (designEditingDirty || (m_projectStateDirty && hasMeaningfulProjectDesign(currentDesign))) {
            designBeforeProjection = currentDesign;
        }
    }

    if (!m_projectService || !m_projectService->hasDocument()) {
        const QString error = QStringLiteral("No project document is open.");
        qWarning() << "Failed to save project to" << absolutePath << error;
        QMessageBox::warning(this, "Save Failed", error);
        return false;
    }

    ProjectDocument stagedDocument = m_projectService->document();
    if (m_projectStateDirty && m_projectStateService) {
        m_projectStateService->writeToDocument(stagedDocument);
    }

    const QStringList unmigratedOwners = unmigratedGraphProjectionOwners(*m_graph, stagedDocument);
    if (!unmigratedOwners.isEmpty()) {
        const QString error =
            QStringLiteral("Save is blocked because NodeEditor graph-command edits are not "
                           "migrated to ProjectDesign ownership yet. Unmigrated mutation owners: %1")
                .arg(unmigratedOwners.join(QStringLiteral("; ")));
        qWarning().noquote() << error;
        QMessageBox::warning(this, "Save Blocked", error);
        return false;
    }

    if (m_projectStateDirty) {
        const ProjectServiceResult replaceResult =
            m_projectService->replaceDocumentPreservingPath(std::move(stagedDocument));
        if (!replaceResult.success) {
            qWarning() << "Failed to update project document before save" << replaceResult.error;
            QMessageBox::warning(this, "Save Failed", replaceResult.error);
            return false;
        }
        reseedDesignEditingService = true;
    }
    if (designBeforeProjection.has_value()) {
        if (m_projectStateDirty) {
            m_projectService->mergeDesignOnlyComponents(*designBeforeProjection);
            reseedDesignEditingService = true;
        } else {
            m_projectService->replaceDesign(*designBeforeProjection);
        }
    }

    const ProjectServiceResult saveResult = m_projectService->saveFile(absolutePath);
    if (!saveResult.success) {
        qWarning() << "Failed to save project to" << absolutePath << saveResult.error;
        QMessageBox::warning(this, "Save Failed", saveResult.error);
        return false;
    }

    if (reseedDesignEditingService) {
        seedDesignEditingServiceFromProjectService();
    }
    setCurrentDocumentPath(absolutePath);
    m_cleanStateId = m_commandManager->currentStateId();
    m_designEditingDirty = false;
    m_projectStateDirty = false;
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

void MainWindow::seedDesignEditingServiceFromProjectService() {
    if (!m_designEditingService || !m_projectService) {
        return;
    }

    QScopedValueRollback<bool> rollback(m_syncingDesignEditingService, true);
    if (m_projectService->hasDocument()) {
        m_designEditingService->replaceDesign(m_projectService->design());
        m_designEditingDirty = false;
        return;
    }

    m_designEditingService->replaceDesign(ipcraft::core::ProjectDesign{});
    m_designEditingDirty = false;
}

void MainWindow::syncProjectServiceFromDesignEditingService() {
    if (m_syncingDesignEditingService || !m_designEditingService || !m_projectService) {
        return;
    }

    m_projectService->replaceDesign(m_designEditingService->design());
    m_designEditingDirty = true;
    syncDocumentStateFromHistory();
}

bool MainWindow::syncProjectServiceFromProjectState() {
    if (m_syncingDesignEditingService ||
        !m_projectOpen ||
        !m_projectService ||
        !m_projectService->hasDocument() ||
        !m_projectStateService) {
        return false;
    }

    const bool designEditingDirty = m_designEditingDirty;
    std::optional<ipcraft::core::ProjectDesign> designBeforeProjection;
    if (designEditingDirty && m_designEditingService &&
        hasMeaningfulProjectDesign(m_designEditingService->design())) {
        designBeforeProjection = m_designEditingService->design();
    }

    ProjectDocument stagedDocument = m_projectService->document();
    m_projectStateService->writeToDocument(stagedDocument);
    const ProjectServiceResult replaceResult =
        m_projectService->replaceDocumentPreservingPath(std::move(stagedDocument));
    if (!replaceResult.success) {
        qWarning() << "Failed to refresh ProjectDesign from project state"
                   << replaceResult.error;
        return false;
    }

    if (designBeforeProjection.has_value()) {
        m_projectService->mergeDesignOnlyComponents(*designBeforeProjection);
    }

    seedDesignEditingServiceFromProjectService();
    m_designEditingDirty = designEditingDirty;
    return true;
}

void MainWindow::clearDocument() {
    m_suppressDocumentTracking = true;
    m_editorProjectionService->clearProjection();
    m_suppressDocumentTracking = false;
    seedDesignEditingServiceFromProjectService();
    if (m_propertyPanel) {
        m_propertyPanel->setSelectedModule(QString());
    }
    m_commandManager->clearHistory();
    m_cleanStateId = m_commandManager->currentStateId();
    m_projectStateDirty = false;
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
    setDocumentDirty(m_projectOpen &&
                     (m_designEditingDirty ||
                      m_projectStateDirty ||
                      m_commandManager->currentStateId() != m_cleanStateId));
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
    setWindowModified(dirty);
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
