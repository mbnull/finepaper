#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class Graph;
class CommandManager;
class NodeEditorWidget;
class PropertyPanel;
class Palette;
class LogPanel;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

  private:
    Ui::MainWindow *ui;
    Graph* m_graph;
    CommandManager* m_commandManager;
    NodeEditorWidget* m_nodeEditor;
    PropertyPanel* m_propertyPanel;
    Palette* m_palette;
    LogPanel* m_logPanel;
};

#endif // MAINWINDOW_H
