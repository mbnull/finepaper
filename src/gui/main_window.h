#pragma once

#include "application/application.h"
#include "application/runtime_settings.h"
#include "features/topology/noc_node_editor.h"
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
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QWidget;

namespace finepaper {

class EndpointPaletteList;
class DomainConfigurationWorkspace;
class DomainManagerPanel;
class ElementConfigurationPanel;
class EndpointConfigurationPanel;
class PackageParameterForm;

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
    enum class DesignRefreshScope {
        FullProjection,
        DomainsOnly,
        InspectorOnly
    };

    struct AttachmentSlotChoice {
        bool accepted = false;
        std::optional<QString> slot;
    };

    void createUi();
    void createActions();
    void createCentralViews();
    void createPackageDock();
    void createInspectorDock();
    void createDomainDock();
    void createResultsDock();
    void restoreWorkbenchState();
    void loadInstalledPackageRoots();
    void reloadPackages();
    void installPackage();
    void updatePackageControls();
    void updateDomainLayerControls();
    void applyDomainLayer(const QString& domainType);
    void updateDomainManager();
    void updateEndpointPalette();
    void updateUiState();
    void setOperationBusy(bool busy, const QString& message = {});
    void setDirty(bool dirty);
    bool confirmDiscardPendingDomainChanges(const QString& action);
    bool confirmDiscardPendingDomainWorkspace(const QString& action);
    void discardPendingDomainChanges();
    void discardPendingDomainWorkspace();
    bool confirmDiscardPendingEndpointDrafts(const QString& action);
    void discardPendingEndpointDrafts();
    bool canSaveDetachedEndpointDrafts();
    bool maybeSave();
    void createDesign();
    void openDesign();
    bool saveDesign();
    bool saveDesignAs();
    bool saveDesignTo(const QString& path);
    void validateDesign();
    void generateDesign();
    void resizeMesh();
    void presentValidationResult(const ValidationResult& result);
    void presentGenerationResult(const GenerationResult& result);
    bool addEndpoint(const QString& endpointType, NocAttachmentTarget target);
    bool moveEndpoint(const QString& endpointId, NocAttachmentTarget target);
    bool removeEndpoint(const QString& endpointId,
                        bool discardConfigurationDraft = true);
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
    void populateDiagnostics(const QVector<Diagnostic>& diagnostics);
    void populateGenerationOutputs(const GenerationResult& result);
    void showDiagnostics(const QVector<Diagnostic>& diagnostics,
                         const QString& title,
                         bool modalOnError = true);
    void appendActivity(const QString& message);
    void showWorkspaceStatusMessage();
    void selectCenterView(const QString& id);
    void beginDesignSession();

    const PackageDefinition* packageByKey(const QString& key) const;
    const PackageDefinition* packageForDesign() const;
    const PackageDefinition* runtimePackageByKey(const QString& key) const;
    const PackageDefinition* runtimePackageForDesign() const;
    QVector<PackageDefinition> runtimePackages() const;
    QString nextEndpointId(const QString& endpointType) const;
    AttachmentSlotChoice chooseAttachmentSlot(
        NocAttachmentTarget target,
        const QString& ignoredEndpointId = {});

    FinepaperApplication m_application;
    RuntimeLocations m_locations;
    std::optional<NocDesign> m_design;
    std::optional<ResolvedDesign> m_resolvedDesign;
    QString m_designPath;
    QString m_workspaceStatusMessage;
    bool m_dirty = false;
    bool m_operationBusy = false;
    quint64 m_designSessionSerial = 0;
    QString m_designSessionIdentity;
    QSet<QString> m_runtimeAvailablePackageKeys;
    std::optional<RouterPosition> m_selectedRouter;
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
    QAction* m_installAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QComboBox* m_domainLayerSelector = nullptr;

    QFutureWatcher<ValidationResult>* m_validationWatcher = nullptr;
    QFutureWatcher<GenerationResult>* m_generationWatcher = nullptr;

    QTabWidget* m_centerViews = nullptr;
    std::optional<WorkbenchViewRegistry> m_viewRegistry;
    NocNodeEditor* m_nodeEditor = nullptr;
    DomainConfigurationWorkspace* m_domainConfigurationWorkspace = nullptr;
    QLabel* m_performanceSummary = nullptr;
    QPlainTextEdit* m_problemReport = nullptr;

    QDockWidget* m_packageDock = nullptr;
    QLabel* m_activePackageLabel = nullptr;
    QLabel* m_availablePackagesLabel = nullptr;
    QPushButton* m_createDesignButton = nullptr;
    QPushButton* m_installPackageButton = nullptr;
    QPushButton* m_reloadPackagesButton = nullptr;
    EndpointPaletteList* m_endpointPalette = nullptr;

    QDockWidget* m_inspectorDock = nullptr;
    QLabel* m_designOverview = nullptr;
    QPushButton* m_resizeMeshButton = nullptr;
    QLabel* m_selectionSummary = nullptr;
    EndpointConfigurationPanel* m_endpointConfigurationPanel = nullptr;
    QGroupBox* m_parameterGroup = nullptr;
    PackageParameterForm* m_parameterForm = nullptr;
    QPushButton* m_applyParametersButton = nullptr;
    ElementConfigurationPanel* m_elementConfigurationPanel = nullptr;

    QDockWidget* m_domainDock = nullptr;
    DomainManagerPanel* m_domainManager = nullptr;

    QDockWidget* m_resultsDock = nullptr;
    QTabWidget* m_resultTabs = nullptr;
    QTableWidget* m_drcTable = nullptr;
    QPlainTextEdit* m_activityLog = nullptr;
    QLineEdit* m_outputRoot = nullptr;
    QPushButton* m_browseOutputButton = nullptr;
    QPushButton* m_generateButton = nullptr;
    QProgressBar* m_operationProgress = nullptr;
    QTableWidget* m_artifactTable = nullptr;
    QPlainTextEdit* m_generationDetails = nullptr;
};

} // namespace finepaper
