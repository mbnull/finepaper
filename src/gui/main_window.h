#pragma once

#include "application/application.h"
#include "application/runtime_settings.h"
#include "features/operations/design_run_state.h"
#include "features/package_library/runtime_package_cache.h"
#include "features/topology/noc_node_editor.h"
#include "features/endpoint_configuration/package_parameter_form.h"
#include "gui/workbench_view_registry.h"

#include <QJsonValue>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSet>
#include <QVector>

#include <optional>

class QAction;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedLayout;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolBar;
class QToolButton;
class QWidget;

namespace finepaper {

struct FinepaperMainWindowSmokeAccess;
class DomainConfigurationWorkspace;
class DomainManagerPanel;
class DesignExtensionsWorkspace;
class ElementConfigurationPanel;
class EndpointConfigurationPanel;
class PackageParameterForm;
class PackageLibraryPanel;
namespace ui {
class EmptyState;
class InspectorDesignSettings;
class InspectorSummaryPanel;
class OperationTaskStrip;
class WorkbenchLayoutController;
class WorkbenchPanelNavigator;
enum class WorkbenchPanelRole;
enum class WorkbenchPanelId;
enum class WorkbenchPanelIntent;
enum class WorkbenchWidthMode;
}

class FinepaperMainWindow final : public QMainWindow {
public:
    explicit FinepaperMainWindow(RuntimeLocations locations, QWidget* parent = nullptr);
    ~FinepaperMainWindow() override;
    bool openDesignFile(const QString& path);
    bool installPackageDirectory(const QString& directory);
    bool operationBusy() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    friend struct FinepaperMainWindowSmokeAccess;

    enum class DesignRefreshScope {
        FullProjection,
        DomainsOnly,
        InspectorOnly,
    };

    enum class GenerationPublicationKind {
        None,
        Artifacts,
        FailedAttempt,
    };

    struct AttachmentSlotChoice {
        bool accepted = false;
        std::optional<QString> slot = std::nullopt;
    };

    struct ParameterDraftState {
        QString designIdentity;
        QJsonObject sourceValues;
        QString sourceSchemaIdentity;
        PackageParameterDraft editorState;
    };

    struct ActiveOperation final {
        operations::RunTicket ticket;
        CancellationSource cancellation;
        bool closeWhenFinished = false;
    };

    struct OperationCompletion final {
        operations::CompletionDisposition disposition =
            operations::CompletionDisposition::Superseded;
        bool finishedActiveRun = false;
        bool closeWhenFinished = false;
    };

    void createUi();
    void createActions();
    void createCentralViews();
    void createPackageDock();
    void createInspectorDock();
    void createDomainDock();
    void createResultsDock();
    void createCleanupRecoveryBanner();
    void requestResultsDockReadabilityUpdate();
    void ensureResultsDockReadable();
    [[nodiscard]] int preferredResultsDockHeight() const;
    void resetWorkbenchLayout();
    void restoreWorkbenchState();
    void loadInstalledPackageRoots();
    void reloadPackages();
    void installPackage();
    RuntimePackageRefreshResult refreshRuntimePackageAvailability(
        RuntimePackageRefreshPolicy policy =
            RuntimePackageRefreshPolicy::IfCatalogChanged);
    [[nodiscard]] bool revalidateRuntimePackage(const QString& key);
    void updatePackageControls();
    void refreshPackageLibraryView();
    void updateEditorEmptyState();
    void updateInspectorContextActions();
    void addEndpointFromLibrary(const QString& endpointType);
    void updateDomainLayerControls();
    void applyDomainLayer(const QString& domainType);
    void updateDomainManager();
    void updateDesignExtensionsWorkspace();
    void updateEndpointPalette();
    void detachPackageBorrowingPanels();
    void updateUiState();
    void setOperationBusy(bool busy);
    [[nodiscard]] CancellationToken beginOperation(
        const operations::RunTicket& ticket,
        const QString& operationName,
        const QString& cancelAccessibleName);
    [[nodiscard]] bool requestOperationCancellation(
        bool closeWhenFinished = false);
    [[nodiscard]] OperationCompletion finishOperation(
        const operations::RunTicket& ticket,
        bool processCleanupUnresolved = false);
    void presentOperationCancellation(
        const OperationCompletion& completion,
        bool cleanupUnresolved,
        bool processCleanupUnresolved,
        const QVector<Diagnostic>& cleanupDiagnostics,
        const QStringList& retainedRuntimePaths,
        const QString& operationName,
        const QString& preservedStatus,
        const QString& preservedActivity,
        const QVector<QLabel*>& statusLabels);
    [[nodiscard]] QString runtimeFileCleanupDetails(
        const QString& operationName,
        const QVector<Diagnostic>& cleanupDiagnostics,
        const QStringList& retainedRuntimePaths) const;
    void presentProcessCleanupFailure(
        const QString& operationName,
        const QVector<Diagnostic>& cleanupDiagnostics,
        const QStringList& retainedRuntimePaths,
        const QVector<QLabel*>& statusLabels);
    [[nodiscard]] QString processCleanupDetails(
        const QString& operationName,
        const QVector<Diagnostic>& cleanupDiagnostics,
        const QStringList& retainedRuntimePaths) const;
    void reviewProcessCleanupDetails();
    void queueDeferredClose();
    void setDirty(bool dirty);
    void captureParameterDraft();
    void discardParameterDraft();
    [[nodiscard]] bool hasPendingInspectorDrafts() const;
    [[nodiscard]] QStringList pendingInspectorDraftDescriptions() const;
    bool confirmDiscardPendingInspectorDrafts(const QString& action);
    void discardPendingInspectorDrafts();
    bool confirmDiscardElementDrafts(const QString& action,
                                     const ElementRef& element);
    bool confirmDiscardPendingDomainChanges(const QString& action);
    bool confirmDiscardPendingDomainWorkspace(const QString& action);
    void discardPendingDomainChanges();
    void discardPendingDomainWorkspace();
    bool ensureEndpointCanvasDraftsResolved(const QString& operation);
    bool confirmDiscardEndpointCanvasDrafts();
    bool maybeSave();
    void createDesign();
    void createDesignWithPreferredPackage(
        const QString& preferredPackageKey);
    void openDesign();
    bool saveDesign();
    bool saveDesignAs();
    bool saveDesignTo(const QString& path);
    void validateDesign();
    void generateDesign();
    void resizeMesh();
    void presentValidationResult(const ValidationResult& result,
                                 const operations::DesignStamp& stamp);
    void presentGenerationResult(const GenerationResult& result,
                                 const operations::DesignStamp& stamp);
    attachment::CreateEndpointResult addEndpoint(
        const QString& endpointType,
        NocAttachmentTarget target);
    bool moveEndpoint(const QString& endpointId, NocAttachmentTarget target);
    bool removeEndpoint(const QString& endpointId,
                        bool discardConfigurationDraft = true);
    void discardEndpointLifecycleDrafts(const QString& endpointId);
    std::optional<QHash<QString, QStringList>> chooseEndpointDomainAssignments(
        const QString& endpointId,
        const QHash<QString, QStringList>& initialAssignments = {});
    void applyParameters();
    void updateInspector(const NocEditorSelectionSet& selection);
    void adoptDesignResult(
        const DesignResult& result,
        const QString& action,
        DesignRefreshScope scope = DesignRefreshScope::FullProjection);
    void adoptDomainResult(const DesignResult& result, const QString& action);
    void refreshDesignViews();
    void refreshDomainViews();
    void rebuildParameterEditors();
    void populateDiagnostics(
        const QVector<Diagnostic>& diagnostics,
        const QString& source = {},
        std::optional<operations::DesignStamp> stamp = std::nullopt);
    void populateGenerationOutputs(
        const GenerationResult& result,
        const operations::DesignStamp& stamp);
    void showDiagnostics(const QVector<Diagnostic>& diagnostics,
                         const QString& title,
                         bool modalOnError = true);
    void appendActivity(const QString& message);
    void showWorkspaceStatusMessage();
    void showResultsDock();
    void showWorkbenchPanel(QDockWidget* dock);
    void activateWorkbenchPanel(
        ui::WorkbenchPanelId id,
        ui::WorkbenchPanelIntent intent);
    void beginDomainAssignmentTask();
    void endDomainAssignmentTask(bool restoreLayout = true);
    [[nodiscard]] std::optional<ui::WorkbenchPanelRole>
        panelRole(QDockWidget* dock) const;
    void setCanvasFocusMode(bool enabled);
    void updateCanvasFocusActionPresentation(bool enabled);
    void updateCommandBarPresentation();
    void updateResponsiveWorkbenchPresentation(
        ui::WorkbenchWidthMode mode);
    void updateEndpointDraftTaskFocus();
    void updateWorkspaceNavigationPresentation();
    void updateCanvasControlsPresentation();
    void rebuildCompactDomainLayerMenu();
    void focusCanvasWorkspace();
    void focusCurrentCenterView();
    void selectCenterView(const QString& id);
    void beginDesignSession(const QString& designName);
    [[nodiscard]] operations::DesignStamp currentDesignStamp() const;
    void advanceDesignRevision();
    void advanceCatalogRevision();
    void clearPublishedResults();
    void updateResultFreshness();

    const PackageDefinition* packageByKey(const QString& key) const;
    const PackageDefinition* packageForDesign() const;
    const PackageDefinition* runtimePackageByKey(const QString& key) const;
    const PackageDefinition* runtimePackageForDesign() const;
    [[nodiscard]] QSet<QString> endpointIdsReservedByCanvasDrafts() const;
    [[nodiscard]] QSet<QString> unavailableEndpointIds() const;
    QString nextEndpointId(const QString& endpointType) const;
    AttachmentSlotChoice chooseAttachmentSlot(
        NocAttachmentTarget target,
        const QString& ignoredEndpointId = {});

    FinepaperApplication m_application;
    RuntimeLocations m_locations;
    std::optional<NocDesign> m_design = std::nullopt;
    std::optional<ResolvedDesign> m_resolvedDesign = std::nullopt;
    QString m_designPath;
    QString m_workspaceStatusMessage;
    bool m_dirty = false;
    bool m_operationBusy = false;
    bool m_processCleanupUnresolved = false;
    QString m_processCleanupDetails;
    quint64 m_designSessionSerial = 0;
    QString m_designSessionIdentity;
    operations::DesignRunState m_runState;
    std::optional<ParameterDraftState> m_parameterDraft = std::nullopt;
    bool m_updatingParameterForm = false;
    bool m_parameterDraftConflict = false;
    bool m_batchingInspectorDraftChanges = false;
    std::optional<operations::DesignStamp> m_diagnosticsStamp = std::nullopt;
    std::optional<operations::DesignStamp> m_generationStamp = std::nullopt;
    GenerationPublicationKind m_generationPublicationKind =
        GenerationPublicationKind::None;
    QString m_diagnosticsSource;
    RuntimePackageCache m_runtimePackageCache;
    std::optional<RouterPosition> m_selectedRouter = std::nullopt;
    NocEditorSelectionSet m_editorSelection;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_validateAction = nullptr;
    QAction* m_generateAction = nullptr;
    QAction* m_resizeMeshAction = nullptr;
    QAction* m_regularizeAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_selectCanvasAction = nullptr;
    QAction* m_panCanvasAction = nullptr;
    QAction* m_canvasFocusAction = nullptr;
    QAction* m_reduceMotionAction = nullptr;
    QAction* m_resetWorkbenchLayoutAction = nullptr;
    QAction* m_installAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_domainLayerSeparator = nullptr;
    QAction* m_domainLayerLabelAction = nullptr;
    QAction* m_domainLayerSelectorAction = nullptr;
    QAction* m_runControlsAction = nullptr;
    QAction* m_wideCanvasSeparatorAction = nullptr;
    QAction* m_wideCanvasControlsAction = nullptr;
    QAction* m_compactCanvasControlsAction = nullptr;
    QAction* m_panelNavigationSeparatorAction = nullptr;
    QAction* m_packagePanelAction = nullptr;
    QAction* m_inspectorPanelAction = nullptr;
    QAction* m_domainManagerPanelAction = nullptr;
    QLabel* m_domainLayerLabel = nullptr;
    QComboBox* m_domainLayerSelector = nullptr;
    QMenu* m_compactDomainLayerMenu = nullptr;
    QToolBar* m_mainToolbar = nullptr;
    QToolBar* m_workspaceToolbar = nullptr;
    QToolButton* m_canvasControlsButton = nullptr;
    QComboBox* m_workspaceSelector = nullptr;
    QLabel* m_workspaceLabel = nullptr;
    bool m_compactWorkbenchPresentation = false;
    bool m_endpointDraftTaskFocusApplied = false;
    std::optional<quint64> m_endpointDraftCanvasFocusGeneration = std::nullopt;

    QFutureWatcher<ValidationResult>* m_validationWatcher = nullptr;
    QFutureWatcher<GenerationResult>* m_generationWatcher = nullptr;
    std::optional<ActiveOperation> m_activeOperation = std::nullopt;

    QTabWidget* m_centerViews = nullptr;
    std::optional<WorkbenchViewRegistry> m_viewRegistry = std::nullopt;
    NocNodeEditor* m_nodeEditor = nullptr;
    QWidget* m_editorPage = nullptr;
    QStackedLayout* m_editorStack = nullptr;
    QWidget* m_editorEmptyStateOverlay = nullptr;
    ui::EmptyState* m_editorEmptyState = nullptr;
    QPushButton* m_emptyCreateButton = nullptr;
    QPushButton* m_emptyOpenButton = nullptr;
    QPushButton* m_emptyInstallButton = nullptr;
    DomainConfigurationWorkspace* m_domainConfigurationWorkspace = nullptr;
    DesignExtensionsWorkspace* m_designExtensionsWorkspace = nullptr;
    QLabel* m_performanceSummary = nullptr;
    QLabel* m_problemReportStatus = nullptr;
    QPlainTextEdit* m_problemReport = nullptr;

    QDockWidget* m_packageDock = nullptr;
    PackageLibraryPanel* m_packageLibraryPanel = nullptr;

    QDockWidget* m_inspectorDock = nullptr;
    QScrollArea* m_inspectorScroll = nullptr;
    ui::InspectorSummaryPanel* m_inspectorSummaryPanel = nullptr;
    ui::InspectorDesignSettings* m_inspectorDesignSettings = nullptr;
    QWidget* m_topologyGroup = nullptr;
    QPushButton* m_resizeMeshButton = nullptr;
    QWidget* m_endpointConfigurationGroup = nullptr;
    EndpointConfigurationPanel* m_endpointConfigurationPanel = nullptr;
    QWidget* m_elementConfigurationGroup = nullptr;
    QWidget* m_parameterGroup = nullptr;
    PackageParameterForm* m_parameterForm = nullptr;
    QPushButton* m_applyParametersButton = nullptr;
    QPushButton* m_discardParametersButton = nullptr;
    ElementConfigurationPanel* m_elementConfigurationPanel = nullptr;
    QString m_inspectorSelectionKey;

    QDockWidget* m_domainDock = nullptr;
    DomainManagerPanel* m_domainManager = nullptr;

    QDockWidget* m_resultsDock = nullptr;
    ui::WorkbenchLayoutController* m_workbenchLayoutController = nullptr;
    ui::WorkbenchPanelNavigator* m_panelNavigator = nullptr;
    QString m_canvasFocusRestoreCenterViewId;
    quint64 m_canvasFocusGeneration = 0;
    QTabWidget* m_resultTabs = nullptr;
    QWidget* m_diagnosticsResultsPage = nullptr;
    QWidget* m_generationResultsPage = nullptr;
    QWidget* m_generationControls = nullptr;
    QSplitter* m_outputSplitter = nullptr;
    QLabel* m_diagnosticsStatus = nullptr;
    QTableWidget* m_drcTable = nullptr;
    QPlainTextEdit* m_activityLog = nullptr;
    QLineEdit* m_outputRoot = nullptr;
    bool m_outputRootEmpty = true;
    QPushButton* m_browseOutputButton = nullptr;
    QPushButton* m_generateButton = nullptr;
    ui::OperationTaskStrip* m_operationTaskStrip = nullptr;
    QWidget* m_cleanupRecoveryBanner = nullptr;
    QLabel* m_cleanupRecoveryLabel = nullptr;
    QPushButton* m_reviewCleanupButton = nullptr;
    QLabel* m_generationStatus = nullptr;
    QTableWidget* m_artifactTable = nullptr;
    QPlainTextEdit* m_generationDetails = nullptr;
    QTimer* m_resultsLayoutTimer = nullptr;
    bool m_resultsDockResizeFollowUpPending = false;
};

} // namespace finepaper
