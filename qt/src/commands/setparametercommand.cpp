#include "commands/setparametercommand.h"

SetParameterCommand::SetParameterCommand(Graph* graph, const QString& moduleId,
                                         const QString& paramName, Parameter::Value newValue)
    : m_graph(graph), m_moduleId(moduleId), m_paramName(paramName), m_newValue(newValue) {}

// Set parameter value, storing old value for undo
void SetParameterCommand::execute() {
    Module* module = m_graph->getModule(m_moduleId);
    if (!module) return;
    const auto& params = module->parameters();
    auto it = params.find(m_paramName);
    m_parameterExisted = (it != params.end());
    if (m_parameterExisted) {
        m_oldValue = it.value().value();
    }
    module->setParameter(m_paramName, m_newValue);
    m_executed = true;
}

// Restore old parameter value or remove if it didn't exist
void SetParameterCommand::undo() {
    Module* module = m_graph->getModule(m_moduleId);
    if (!module) return;
    if (m_parameterExisted) {
        module->setParameter(m_paramName, m_oldValue);
    } else {
        module->removeParameter(m_paramName);
    }
}
