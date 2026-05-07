// PropertyPanel displays and edits parameters for the selected module
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHash>
#include <QJsonValue>
#include <QPointer>
#include <QVector>

class Graph;
class IPluginProjectAdapter;
class Module;
class CommandManager;
class ProjectStateService;
class QFormLayout;
class QPlainTextEdit;
struct PluginParameterField;
struct PluginParameterSection;

class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    PropertyPanel(Graph* graph,
                  ProjectStateService* stateService,
                  QVector<IPluginProjectAdapter*> pluginAdapters,
                  CommandManager* commandManager,
                  QWidget* parent = nullptr);
    PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);
    void setSelectedModule(Module* module);

public slots:
    void setSelectedModule(QString moduleId);

private slots:
    void onParameterChanged(const QString& name);

private:
    void clearPanel();
    void populatePanel();
    QWidget* createPluginParameterWidget(const PluginParameterSection& section,
                                         const PluginParameterField& field,
                                         const QJsonValue& storedValue);

    Graph* m_graph;
    ProjectStateService* m_stateService;
    QVector<IPluginProjectAdapter*> m_pluginAdapters;
    CommandManager* m_commandManager;
    QPointer<Module> m_selectedModule;
    QVBoxLayout* m_layout;
    QPlainTextEdit* m_descriptionView;
    QFormLayout* m_formLayout;
    QHash<QString, QWidget*> m_parameterWidgets;
    QHash<QString, QWidget*> m_ipParameterWidgets;
};
