// AddConnectionCommand adds a connection between two ports with undo support
#pragma once

#include "command.h"
#include "graph.h"
#include <memory>

class AddConnectionCommand : public Command {
public:
    AddConnectionCommand(Graph* graph, std::unique_ptr<Connection> connection);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    std::unique_ptr<Connection> m_connection;
    QString m_connectionId;
};
