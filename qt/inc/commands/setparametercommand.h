#pragma once

#include "command.h"
#include "graph.h"
#include "parameter.h"

class SetParameterCommand : public Command {
public:
    SetParameterCommand(Graph* graph, const QString& moduleId, const QString& paramName, Parameter::Value newValue);
    void execute() override;
    void undo() override;

private:
    Graph* m_graph;
    QString m_moduleId;
    QString m_paramName;
    Parameter::Value m_newValue;
    Parameter::Value m_oldValue;
    bool m_parameterExisted = false;
};
