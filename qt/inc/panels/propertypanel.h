// PropertyPanel displays and edits parameters for the selected module
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHash>
#include <QJsonValue>
#include <QPointer>
#include <QVector>

class Graph;
class IIpInstanceParameterAdapter;
class Module;
class Connection;
class CommandManager;
class EditorMutationTarget;
class ProjectStateService;
class QFormLayout;
class QPlainTextEdit;
struct IpInstanceParameterField;
struct IpInstanceParameterSection;

class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    PropertyPanel(Graph* graph,
                  ProjectStateService* stateService,
                  QVector<IIpInstanceParameterAdapter*> ipInstanceParameterAdapters,
                  CommandManager* commandManager,
                  QWidget* parent = nullptr,
                  EditorMutationTarget* editorMutationTarget = nullptr);
    PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);
    void setSelectedModule(Module* module);
    void setIpInstanceParameterAdapters(QVector<IIpInstanceParameterAdapter*> adapters);

public slots:
    void setSelectedModule(QString moduleId);

private slots:
    void onParameterChanged(const QString& name);
    void onIpInstanceParameterChanged(const QString& ipcoreId,
                                       const QString& instanceId,
                                       const QString& section,
                                       const QString& name);
    void onConnectionChanged(Connection* connection);
    void onConnectionRemoved(const QString& connectionId);

private:
    void clearPanel();
    void populatePanel();
    void queueSelectedConnectionRefresh(const QString& connectionId,
                                        bool clearSelectionWhenMissing);
    QWidget* createIpInstanceParameterWidget(const IpInstanceParameterSection& section,
                                         const IpInstanceParameterField& field,
                                         const QJsonValue& storedValue,
                                         bool editable);

    Graph* m_graph;
    ProjectStateService* m_stateService;
    QVector<IIpInstanceParameterAdapter*> m_ipInstanceParameterAdapters;
    CommandManager* m_commandManager;
    EditorMutationTarget* m_editorMutationTarget = nullptr;
    QPointer<Module> m_selectedModule;
    QVBoxLayout* m_layout;
    QPlainTextEdit* m_descriptionView;
    QFormLayout* m_formLayout;
    QHash<QString, QWidget*> m_parameterWidgets;
    QHash<QString, QWidget*> m_ipParameterWidgets;
};
