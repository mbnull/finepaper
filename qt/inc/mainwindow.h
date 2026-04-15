// MainWindow — top-level application window for the SoC/NoC node editor.
// Owns the Graph, CommandManager, and all major UI panels (palette, node editor,
// property panel, log panel, validation manager). Wires them together and
// provides load/save entry points.
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>

class Graph;
class CommandManager;
class NodeEditorWidget;
class PropertyPanel;
class Palette;
class LogPanel;
class ValidationManager;
class QAction;
class QDockWidget;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Loads a design from disk via command pipeline so the action is undoable.
    void loadGraph(const QString& jsonPath);

private slots:
    // Saves current graph into editor JSON format.
    void saveGraph();
    // Exports a framework-oriented output (Verilog generation entry point).
    void generateVerilog();
    // Runs local + framework validation and refreshes the log panel.
    void runValidation();

  private:
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
    QAction* m_saveAction;
    QAction* m_generateAction;
    QAction* m_validateAction;
    QAction* m_arrangeAction;
};

#endif // MAINWINDOW_H
