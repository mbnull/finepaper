#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <unordered_map>

class Graph;
class Module;
class CommandManager;

class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);
    void setSelectedModule(Module* module);

private slots:
    void onParameterChanged(const QString& name);

private:
    void clearPanel();
    void populatePanel();

    Graph* m_graph;
    CommandManager* m_commandManager;
    Module* m_selectedModule = nullptr;
    QVBoxLayout* m_layout;
    std::unordered_map<QString, QWidget*> m_parameterWidgets;
};
