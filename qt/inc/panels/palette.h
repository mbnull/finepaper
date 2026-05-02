// Palette widget displays available module types for drag-and-drop
#pragma once

#include <QWidget>
#include <QListWidget>

class Graph;
class CommandManager;

class Palette : public QWidget {
    Q_OBJECT

public:
    Palette(Graph* graph, CommandManager* commandManager, QWidget* parent = nullptr);
    void setActivePluginId(const QString& pluginId);
    QString activePluginId() const { return m_activePluginId; }

private:
    void setupUI();
    void populateModuleTypes();

    QListWidget* m_listWidget;
    Graph* m_graph;
    CommandManager* m_commandManager;
    QString m_activePluginId;
};
