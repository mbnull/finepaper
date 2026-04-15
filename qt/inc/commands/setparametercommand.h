// SetParameterCommand changes a module parameter with undo support
#pragma once

#include "commands/command.h"
#include "graph/graph.h"
#include "graph/parameter.h"

class SetParameterCommand : public Command {
public:
    // Targets one module parameter update and captures prior value for undo.
    SetParameterCommand(Graph* graph, const QString& moduleId, const QString& paramName, Parameter::Value newValue);
    // Applies the new parameter value.
    void execute() override;
    // Restores previous parameter value or removes newly introduced parameter.
    void undo() override;

private:
    Graph* m_graph;
    QString m_moduleId;
    QString m_paramName;
    Parameter::Value m_newValue;
    Parameter::Value m_oldValue;
    bool m_parameterExisted = false;
};
