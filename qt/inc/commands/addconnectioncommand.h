// AddConnectionCommand adds a connection between two ports with undo support
#pragma once

#include "commands/command.h"
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include <memory>

class AddConnectionCommand : public Command {
public:
    // Takes ownership of a prepared connection to be inserted into the graph.
    AddConnectionCommand(Graph* graph,
                         ConnectionRuleService* ruleService,
                         std::unique_ptr<Connection> connection);
    // Inserts the connection if it passes graph validation rules.
    void execute() override;
    // Removes the inserted connection and restores local ownership.
    void undo() override;

private:
    Graph* m_graph = nullptr;
    ConnectionRuleService* m_ruleService = nullptr;
    std::unique_ptr<Connection> m_connection;
    QString m_connectionId;
};
