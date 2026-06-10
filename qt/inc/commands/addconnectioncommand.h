// AddConnectionCommand adds a connection between two ports with undo support
#pragma once

#include "commands/command.h"
#include "connection/connectionruleservice.h"
#include "graph/graph.h"
#include <functional>
#include <memory>

class AddConnectionCommand : public Command {
public:
    using PackageManifestsProvider = std::function<QVector<IpcraftPackageManifest>()>;

    // Takes ownership of a prepared connection to be inserted into the graph.
    AddConnectionCommand(Graph* graph,
                         std::unique_ptr<Connection> connection,
                         PackageManifestsProvider packageManifestsProvider = {});
    // Inserts the connection if it passes graph validation rules.
    void execute() override;
    // Removes the inserted connection and restores local ownership.
    void undo() override;

private:
    Graph* m_graph = nullptr;
    PackageManifestsProvider m_packageManifestsProvider;
    std::unique_ptr<Connection> m_connection;
    QString m_connectionId;
};
