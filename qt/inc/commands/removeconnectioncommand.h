#pragma once

#include "command.h"
#include "graph.h"
#include <memory>

class RemoveConnectionCommand : public Command {
public:
    RemoveConnectionCommand(Graph* graph, const QString& connectionId);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    QString m_connectionId;
    std::unique_ptr<Connection> m_connection;
};
