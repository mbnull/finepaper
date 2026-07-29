#pragma once

#include "application/application.h"
#include "application/runtime_settings.h"
#include "gui/noc_node_editor.h"
#include "gui/workbench_view_registry.h"

#include <QJsonValue>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QVector>

#include <optional>

class QAction;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QWidget;

namespace finepaper {

class EndpointPaletteList;

class FinepaperMainWindow final : public QMainWindow {
public:
    explicit FinepaperMainWindow(RuntimeLocations locations, QWidget* parent = nullptr);
    bool openDesignFile(const QString& path);
    bool operationBusy() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct AttachmentSlotChoice {
        bool accepted = false;
        std::optional<QString> slot;
    };

    struct ParameterControl {
        ParameterDefinition definition;
        QWidget* editor = nullptr;
    };

    void createUi();
    void createActions();
    void createCentralViews();
    void createPackageDock();
    void createInspectorDock();
    void createResultsDock();
    void restoreWorkbenchState();
    void loadInstalledPackageRoots();
    void reloadPackages();
    void installPackage();
    void updatePackageControls();
    void updateMeshBounds();
    void updateEndpointPalette();
    void updateUiState();
    void setOperationBusy(bool busy, const QString& message = {});
    void setDirty(bool dirty);
    bool maybeSave();
    void createDesign();
    void openDesign();
    bool saveDesign();
    bool saveDesignAs();
    bool saveDesignTo(const QString& path);
    void validateDesign();
    void generateDesign();
    void presentValidationResult(const ValidationResult& result);
    void presentGenerationResult(const GenerationResult& result);
    bool addEndpoint(const QString& endpointType, NocAttachmentTarget target);
    bool moveEndpoint(const QString& endpointId, NocAttachmentTarget target);
    bool removeEndpoint(const QString& endpointId);
    void applyParameters();
    void updateInspector(const NocEditorSelection& selection);
    void adoptDesignResult(const DesignResult& result, const QString& action);
    void refreshDesignViews();
    void rebuildParameterEditors();
    void populateDiagnostics(const QVector<Diagnostic>& diagnostics);
    void populateGenerationOutputs(const GenerationResult& result);
    void showDiagnostics(const QVector<Diagnostic>& diagnostics,
                         const QString& title,
                         bool modalOnError = true);
    void appendActivity(const QString& message);
    void selectCenterView(const QString& id);

    const PackageDefinition* selectedPackage() const;
    const PackageDefinition* packageForDesign() const;
    QJsonValue valueFromControl(const ParameterControl& control) const;
    QString nextEndpointId(const QString& endpointType) const;
    AttachmentSlotChoice chooseAttachmentSlot(
        NocAttachmentTarget target,
        const QString& ignoredEndpointId = {});

    FinepaperApplication m_application;
    RuntimeLocations m_locations;
    std::optional<NocDesign> m_design;
    QString m_designPath;
    bool m_dirty = false;
    bool m_operationBusy = false;
    std::optional<RouterPosition> m_selectedRouter;
    QVector<ParameterControl> m_parameterControls;

    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_validateAction = nullptr;
    QAction* m_generateAction = nullptr;
    QAction* m_regularizeAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_installAction = nullptr;
    QAction* m_reloadAction = nullptr;

    QFutureWatcher<ValidationResult>* m_validationWatcher = nullptr;
    QFutureWatcher<GenerationResult>* m_generationWatcher = nullptr;

    QTabWidget* m_centerViews = nullptr;
    std::optional<WorkbenchViewRegistry> m_viewRegistry;
    NocNodeEditor* m_nodeEditor = nullptr;
    QLabel* m_performanceSummary = nullptr;
    QPlainTextEdit* m_problemReport = nullptr;

    QDockWidget* m_packageDock = nullptr;
    QComboBox* m_packageSelector = nullptr;
    QLineEdit* m_designName = nullptr;
    QSpinBox* m_rows = nullptr;
    QSpinBox* m_columns = nullptr;
    QPushButton* m_createDesignButton = nullptr;
    QPushButton* m_installPackageButton = nullptr;
    QPushButton* m_reloadPackagesButton = nullptr;
    EndpointPaletteList* m_endpointPalette = nullptr;

    QDockWidget* m_inspectorDock = nullptr;
    QLabel* m_designOverview = nullptr;
    QLabel* m_selectionSummary = nullptr;
    QGroupBox* m_parameterGroup = nullptr;
    QFormLayout* m_parameterForm = nullptr;
    QPushButton* m_applyParametersButton = nullptr;

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
