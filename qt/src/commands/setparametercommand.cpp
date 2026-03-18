#include "commands/setparametercommand.h"

SetParameterCommand::SetParameterCommand(Graph* graph, const QString& moduleId,
                                         const QString& paramName, Parameter::Value newValue)
    : m_graph(graph), m_moduleId(moduleId), m_paramName(paramName), m_newValue(newValue) {}

void SetParameterCommand::execute() {
    Module* module = m_graph->getModule(m_moduleId);
    if (!module) return;
    const auto& params = module->parameters();
    auto it = params.find(m_paramName);
    if (it != params.end()) {
        m_oldValue = it->second.value();
    }
    module->setParameter(m_paramName, m_newValue);
}

void SetParameterCommand::undo() {
    Module* module = m_graph->getModule(m_moduleId);
    if (!module) return;
    module->setParameter(m_paramName, m_oldValue);
}
