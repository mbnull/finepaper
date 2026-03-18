#include "commands/setparametercommand.h"

SetParameterCommand::SetParameterCommand(Graph* graph, const QString& moduleId,
                                         const QString& paramName, Parameter::Value newValue)
    : m_graph(graph), m_moduleId(moduleId), m_paramName(paramName), m_newValue(newValue) {}

void SetParameterCommand::execute() {
    Module* module = m_graph->getModule(m_moduleId);
    m_oldValue = module->parameters().at(m_paramName).value();
    module->setParameter(m_paramName, m_newValue);
}

void SetParameterCommand::undo() {
    Module* module = m_graph->getModule(m_moduleId);
    module->setParameter(m_paramName, m_oldValue);
}
