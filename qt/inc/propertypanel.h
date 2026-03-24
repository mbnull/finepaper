#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHash>

class Graph;
class Module;
class CommandManager;
class QFormLayout;

class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);
    void setSelectedModule(Module* module);

public slots:
    void setSelectedModule(QString moduleId);

private slots:
    void onParameterChanged(const QString& name);

private:
    void clearPanel();
    void populatePanel();

    Graph* m_graph;
    CommandManager* m_commandManager;
    Module* m_selectedModule = nullptr;
    QVBoxLayout* m_layout;
    QFormLayout* m_formLayout;
    QHash<QString, QWidget*> m_parameterWidgets;
};
