// MainWindow — top-level application window for the SoC/NoC node editor.
// Owns the Graph, CommandManager, and all major UI panels (palette, node editor,
// property panel, log panel, validation manager). Wires them together and
// provides load/save entry points.
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class Graph;
class CommandManager;
class NodeEditorWidget;
class PropertyPanel;
class Palette;
class LogPanel;
class ValidationManager;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void loadGraph(const QString& jsonPath);

private slots:
    void saveGraph();

  private:
    Ui::MainWindow *ui;
    Graph* m_graph;
    CommandManager* m_commandManager;
    NodeEditorWidget* m_nodeEditor;
    PropertyPanel* m_propertyPanel;
    Palette* m_palette;
    LogPanel* m_logPanel;
    ValidationManager* m_validationManager;
};

#endif // MAINWINDOW_H
