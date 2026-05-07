// SetIpInstanceParameterCommand updates one IP instance parameter and restores it on undo.
#include "commands/setipinstanceparametercommand.h"

SetIpInstanceParameterCommand::SetIpInstanceParameterCommand(Graph* graph,
                                                             const QString& paramName,
                                                             Parameter::Value newValue)
    : m_graph(graph), m_paramName(paramName), m_newValue(newValue) {}

void SetIpInstanceParameterCommand::execute() {
    if (!m_graph->ipInstance().has_value()) return;

    const auto it = m_graph->ipInstance()->parameters.find(m_paramName);
    if (it == m_graph->ipInstance()->parameters.end()) return;

    m_oldValue = it.value().value();
    if (m_oldValue == m_newValue) {
        return;
    }

    if (m_graph->setIpInstanceParameter(m_paramName, m_newValue)) {
        m_executed = true;
    }
}

void SetIpInstanceParameterCommand::undo() {
    m_graph->setIpInstanceParameter(m_paramName, m_oldValue);
}
