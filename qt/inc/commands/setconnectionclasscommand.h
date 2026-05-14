// SetConnectionClassCommand resolves an ambiguous connection class selection.
#pragma once

#include "commands/command.h"

#include <QString>
#include <QStringList>

class Graph;

class SetConnectionClassCommand final : public Command {
public:
    SetConnectionClassCommand(Graph* graph,
                              QString connectionId,
                              QString connectionClassId);

    void execute() override;
    void undo() override;

private:
    Graph* m_graph = nullptr;
    QString m_connectionId;
    QString m_newConnectionClassId;
    QString m_oldConnectionClassId;
    QString m_oldStatus;
    QStringList m_oldAlternatives;
};
