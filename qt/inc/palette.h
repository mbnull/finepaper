#pragma once

#include <QWidget>
#include <QListWidget>

class Graph;
class CommandManager;

class Palette : public QWidget {
    Q_OBJECT

public:
    Palette(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);

private:
    void setupUI();
    void populateModuleTypes();

    QListWidget* m_listWidget;
    Graph* m_graph;
    CommandManager* m_commandManager;
};
