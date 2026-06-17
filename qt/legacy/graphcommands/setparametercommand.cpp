// SetParameterCommand updates one module parameter and restores old state on undo.
#include "legacy/graphcommands/setparametercommand.h"

#include "legacy/graphcommands/editormutationtarget.h"

namespace {

void applyParameterState(Module* module,
                         const QString& paramName,
                         bool parameterExisted,
                         const Parameter::Value& value) {
    if (parameterExisted) {
        module->setParameter(paramName, value);
    } else {
        module->removeParameter(paramName);
    }
}

} // namespace

SetParameterCommand::SetParameterCommand(Graph* graph, const QString& moduleId,
                                         const QString& paramName,
                                         Parameter::Value newValue,
                                         EditorMutationTarget* editorMutationTarget)
    : m_graph(graph),
      m_moduleId(moduleId),
      m_paramName(paramName),
      m_newValue(newValue),
      m_editorMutationTarget(editorMutationTarget) {}

// Set parameter value, storing old value for undo
void SetParameterCommand::execute() {
    m_executed = false;
    m_undone = false;
    if (!m_graph) {
        return;
    }
    Module* module = m_graph->getModule(m_moduleId);
    if (!module) return;
    const auto& params = module->parameters();
    auto it = params.find(m_paramName);
    m_parameterExisted = (it != params.end());
    if (m_parameterExisted) {
        m_oldValue = it.value().value();
        if (m_oldValue == m_newValue) {
            return;
        }
    }
    if (m_editorMutationTarget) {
        std::unique_ptr<Module> durableModule = module->clone();
        durableModule->setParameter(m_paramName, m_newValue);
        if (!m_editorMutationTarget->upsertEditorModuleRecord(*durableModule)) {
            return;
        }
    }

    module->setParameter(m_paramName, m_newValue);
    m_executed = true;
}

// Restore old parameter value or remove if it didn't exist
void SetParameterCommand::undo() {
    m_undone = false;
    if (!m_graph) {
        return;
    }
    Module* module = m_graph->getModule(m_moduleId);
    if (!module) return;

    if (m_editorMutationTarget) {
        std::unique_ptr<Module> durableModule = module->clone();
        applyParameterState(durableModule.get(), m_paramName, m_parameterExisted, m_oldValue);
        if (!m_editorMutationTarget->upsertEditorModuleRecord(*durableModule)) {
            return;
        }
    }

    applyParameterState(module, m_paramName, m_parameterExisted, m_oldValue);
    m_undone = true;
}
