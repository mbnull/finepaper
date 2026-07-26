#pragma once

#include "application/application.h"
#include "application/runtime_settings.h"
#include "gui/noc_node_editor.h"
#include "gui/workbench_view_registry.h"

#include <QJsonValue>
#include <QMainWindow>
#include <QVector>

#include <optional>

class QCloseEvent;
class QComboBox;
class QDockWidget;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
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
    void createDesign();
    void openDesign();
    void saveDesign();
    void validateDesign();
    void generateDesign();
    bool addEndpoint(const QString& endpointType, RouterPosition router);
    void showEndpointAttachmentMenu(RouterPosition router);
    bool moveEndpoint(const QString& endpointId, RouterPosition router);
    void removeEndpoint(const QString& endpointId);
    void removeSelectedEndpoint();
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
        RouterPosition router,
        const QString& ignoredEndpointId = {});

    FinepaperApplication m_application;
    RuntimeLocations m_locations;
    std::optional<NocDesign> m_design;
    QString m_designPath;
    QString m_selectedEndpointId;
    std::optional<RouterPosition> m_selectedRouter;
    QVector<ParameterControl> m_parameterControls;

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
    EndpointPaletteList* m_endpointPalette = nullptr;

    QDockWidget* m_inspectorDock = nullptr;
    QLabel* m_designOverview = nullptr;
    QLabel* m_selectionSummary = nullptr;
    QPushButton* m_attachEndpoint = nullptr;
    QFormLayout* m_parameterForm = nullptr;

    QDockWidget* m_resultsDock = nullptr;
    QTabWidget* m_resultTabs = nullptr;
    QTableWidget* m_drcTable = nullptr;
    QPlainTextEdit* m_activityLog = nullptr;
    QLineEdit* m_outputRoot = nullptr;
    QTableWidget* m_artifactTable = nullptr;
    QPlainTextEdit* m_generationDetails = nullptr;
};

} // namespace finepaper
