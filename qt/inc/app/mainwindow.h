// MainWindow — top-level application window for the SoC/NoC node editor.
// Owns the Graph, CommandManager, and all major UI panels (catalog, node editor,
// property panel, log panel, validation manager). Wires them together and
// provides load/save entry points.
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <memory>
#include <vector>

class Graph;
class AppSettings;
class CommandManager;
class IIpInstanceParameterAdapter;
class NodeEditorWidget;
class PropertyPanel;
class IpCatalogPanel;
class IpCatalogService;
class ProjectIpService;
class ProjectStateService;
class ActiveWorkspaceController;
class LogPanel;
class ValidationManager;
class QAction;
class QCloseEvent;
class QDockWidget;
class QMenu;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Loads a Finepaper project from disk as the active document.
    bool loadGraph(const QString& path);
    bool hasOpenProject() const;
    bool createProjectAt(const QString& path);

private slots:
    // Prompts for a project path and creates a saved empty project.
    void newGraph();
    // Opens an existing project.
    void openGraph();
    // Saves current project to the current path or prompts for one.
    void saveGraph();
    // Prompts for a new destination and saves there.
    void saveGraphAs();
    // Exports active IP-core input and runs the Verilog generation entry point.
    void generateVerilog();
    // Runs local + IP-core validation and refreshes the log panel.
    void runValidation();
    // Executes one undo/redo step in the command history.
    void undo();
    void redo();
    void createTopologyPreset();

  private:
    void closeEvent(QCloseEvent* event) override;
    // Builds the three-pane editor layout and log area.
    void setupPanels();
    // Wires cross-widget signals/slots.
    void setupConnections();
    // Creates menus/tool actions and binds callbacks.
    void setupActions();
    QWidget* createCentralContent();
    void setupDocks();
    QDockWidget* createDock(const QString& title,
                            QWidget* content,
                            Qt::DockWidgetArea area,
                            const QString& objectName);
    void appendStartupLog() const;
    void scheduleStartupLayoutLog();
    void logStartupLayout() const;
    void rebuildTopologyMenu();
    bool maybeSaveChanges(const QString& actionDescription);
    bool loadDocument(const QString& path);
    bool saveDocument(const QString& path);
    QString defaultDocumentPath() const;
    QString defaultProjectDirectoryPath() const;
    void clearDocument();
    void scheduleDocumentStateRefresh();
    void syncDocumentStateFromHistory();
    void setCurrentDocumentPath(const QString& path);
    void setDocumentDirty(bool dirty);
    void setProjectOpen(bool open);
    bool requireOpenProject(const QString& actionName);
    void updateWindowTitle();
    void updateCommandActions();

    Graph* m_graph;
    std::unique_ptr<CommandManager> m_commandManager;
    std::unique_ptr<AppSettings> m_appSettings;
    std::unique_ptr<IpCatalogService> m_ipCatalogService;
    std::unique_ptr<ProjectStateService> m_projectStateService;
    std::unique_ptr<ProjectIpService> m_projectIpService;
    std::unique_ptr<ActiveWorkspaceController> m_activeWorkspaceController;
    std::vector<std::unique_ptr<IIpInstanceParameterAdapter>> m_ipInstanceParameterAdapters;
    NodeEditorWidget* m_nodeEditor;
    PropertyPanel* m_propertyPanel;
    IpCatalogPanel* m_ipCatalogPanel;
    LogPanel* m_logPanel;
    ValidationManager* m_validationManager;
    QDockWidget* m_ipCatalogDock;
    QDockWidget* m_propertyDock;
    QDockWidget* m_logDock;
    QAction* m_newAction;
    QAction* m_openAction;
    QAction* m_saveAction;
    QAction* m_saveAsAction;
    QAction* m_undoAction;
    QAction* m_redoAction;
    QAction* m_generateAction;
    QAction* m_validateAction;
    QAction* m_arrangeAction;
    QMenu* m_topologyMenu;
    QString m_currentDocumentPath;
    int m_cleanStateId = 0;
    bool m_documentDirty = false;
    bool m_documentStateRefreshPending = false;
    bool m_projectOpen = false;
    bool m_suppressDocumentTracking = false;
};

#endif // MAINWINDOW_H
