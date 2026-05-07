// SetIpInstanceParameterCommand changes the graph IP instance parameter with undo support.
#pragma once

#include "commands/command.h"
#include "graph/graph.h"
#include "graph/parameter.h"

class SetIpInstanceParameterCommand : public Command {
public:
    SetIpInstanceParameterCommand(Graph* graph, const QString& paramName, Parameter::Value newValue);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    QString m_paramName;
    Parameter::Value m_newValue;
    Parameter::Value m_oldValue;
};
