// RemoveConnectionCommand removes a connection with undo support
#pragma once

#include "commands/command.h"
#include "graph/graph.h"
#include <memory>

class EditorMutationTarget;

class RemoveConnectionCommand : public Command {
public:
    // Targets a specific connection ID for deletion.
    RemoveConnectionCommand(Graph* graph,
                            const QString& connectionId,
                            EditorMutationTarget* editorMutationTarget = nullptr);
    // Removes the connection and stores it for undo.
    void execute() override;
    // Re-inserts the removed connection.
    void undo() override;

private:
    Graph* m_graph;
    QString m_connectionId;
    std::unique_ptr<Connection> m_connection;
    EditorMutationTarget* m_editorMutationTarget = nullptr;
};
