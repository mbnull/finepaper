// MainWindow — top-level application window for the SoC/NoC node editor.
// Owns the Graph, CommandManager, and all major UI panels (palette, node editor,
// property panel, log panel, validation manager). Wires them together and
// provides load/save entry points.
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <memory>

class Graph;
class CommandManager;
class NodeEditorWidget;
class PropertyPanel;
class Palette;
class LogPanel;
class ValidationManager;
class QAction;
class QCloseEvent;
class QDockWidget;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Loads a design from disk as the active document.
    void loadGraph(const QString& jsonPath);

private slots:
    // Creates a new empty document.
    void newGraph();
    // Opens an existing editor JSON document.
    void openGraph();
    // Saves current document to the current path or prompts for one.
    void saveGraph();
    // Prompts for a new destination and saves there.
    void saveGraphAs();
    // Exports a framework-oriented output (Verilog generation entry point).
    void generateVerilog();
    // Runs local + framework validation and refreshes the log panel.
    void runValidation();
    // Executes one undo/redo step in the command history.
    void undo();
    void redo();

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
    void scheduleStartupLayoutLog();
    void logStartupLayout() const;
    bool maybeSaveChanges(const QString& actionDescription);
    bool loadDocument(const QString& jsonPath);
    bool saveDocument(const QString& path);
    QString defaultDocumentPath() const;
    void clearDocument();
    void scheduleDocumentStateRefresh();
    void syncDocumentStateFromHistory();
    void setCurrentDocumentPath(const QString& path);
    void setDocumentDirty(bool dirty);
    void updateWindowTitle();
    void updateCommandActions();

    Graph* m_graph;
    std::unique_ptr<CommandManager> m_commandManager;
    NodeEditorWidget* m_nodeEditor;
    PropertyPanel* m_propertyPanel;
    Palette* m_palette;
    LogPanel* m_logPanel;
    ValidationManager* m_validationManager;
    QDockWidget* m_paletteDock;
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
    QString m_currentDocumentPath;
    int m_cleanStateId = 0;
    bool m_documentDirty = false;
    bool m_documentStateRefreshPending = false;
    bool m_suppressDocumentTracking = false;
};

#endif // MAINWINDOW_H
