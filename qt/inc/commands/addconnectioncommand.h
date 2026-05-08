// AddConnectionCommand adds a connection between two ports with undo support
#pragma once

#include "commands/command.h"
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include <functional>
#include <memory>

class AddConnectionCommand : public Command {
public:
    using PluginStatesProvider = std::function<QVector<ProjectPluginStateRecord>()>;

    // Takes ownership of a prepared connection to be inserted into the graph.
    AddConnectionCommand(Graph* graph,
                         PluginStatesProvider pluginStatesProvider,
                         std::unique_ptr<Connection> connection);
    // Inserts the connection if it passes graph validation rules.
    void execute() override;
    // Removes the inserted connection and restores local ownership.
    void undo() override;

private:
    Graph* m_graph = nullptr;
    PluginStatesProvider m_pluginStatesProvider;
    std::unique_ptr<Connection> m_connection;
    QString m_connectionId;
};
