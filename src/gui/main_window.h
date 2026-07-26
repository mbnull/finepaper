#pragma once

#include "application/application.h"
#include "application/runtime_settings.h"
#include "gui/workspace_pages.h"

#include <QJsonValue>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <optional>

class QComboBox;
class QFormLayout;
class QGraphicsScene;
class QGraphicsView;
class QLineEdit;
class QListWidget;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTextEdit;
class QWidget;

namespace finepaper {

class FinepaperMainWindow final : public QMainWindow {
public:
    explicit FinepaperMainWindow(RuntimeLocations locations, QWidget* parent = nullptr);

private:
    struct ParameterControl {
        ParameterDefinition definition;
        QWidget* editor = nullptr;
    };

    void createUi();
    void reloadPackages();
    void updateStartPackages();
    void updateStartMeshBounds();
    void showPage(WorkspacePage page);
    void createDesign();
    void openDesign();
    void saveDesign();
    void validateDesign();
    void generateDesign();
    void addEndpoint();
    void moveSelectedEndpoint();
    void removeSelectedEndpoint();
    void applyParameters();
    void updateEndpointInputsFromSelection();
    void updateSelectedRouterFromTopology();
    void adoptDesignResult(const DesignResult& result, const QString& action);
    void refreshDesignViews();
    void refreshTopology();
    void refreshEndpointTable();
    void rebuildParameterEditors();
    void showDiagnostics(const QVector<Diagnostic>& diagnostics,
                         const QString& title,
                         bool modalOnError = true);

    const PackageDefinition* selectedStartPackage() const;
    const PackageDefinition* packageForDesign() const;
    QJsonValue valueFromControl(const ParameterControl& control) const;

    FinepaperApplication m_application;
    RuntimeLocations m_locations;
    std::optional<NocDesign> m_design;
    QString m_designPath;
    QVector<ParameterControl> m_parameterControls;

    QListWidget* m_navigation = nullptr;
    QStackedWidget* m_pages = nullptr;
    QComboBox* m_startPackage = nullptr;
    QLineEdit* m_startName = nullptr;
    QSpinBox* m_startRows = nullptr;
    QSpinBox* m_startColumns = nullptr;
    QLabel* m_overview = nullptr;
    QGraphicsScene* m_topologyScene = nullptr;
    QGraphicsView* m_topologyView = nullptr;
    QTableWidget* m_endpoints = nullptr;
    QLineEdit* m_endpointId = nullptr;
    QComboBox* m_endpointType = nullptr;
    QSpinBox* m_endpointX = nullptr;
    QSpinBox* m_endpointY = nullptr;
    QFormLayout* m_parameterForm = nullptr;
    QTextEdit* m_validationReport = nullptr;
    QLineEdit* m_outputRoot = nullptr;
    QTextEdit* m_generationReport = nullptr;
};

} // namespace finepaper
