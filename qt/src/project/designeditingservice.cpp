// DesignEditingService applies validated project patches with undo/redo.
#include "project/designeditingservice.h"

#include <utility>

DesignEditingService::DesignEditingService(QObject* parent) : QObject(parent) {}

const ipcraft::core::ProjectDesign& DesignEditingService::design() const {
    return m_design;
}

void DesignEditingService::replaceDesign(ipcraft::core::ProjectDesign design) {
    m_design = std::move(design);
    m_undo.clear();
    m_redo.clear();
    emit designChanged();
}

DesignEditResult DesignEditingService::applyPatch(const ipcraft::core::ProjectPatch& patch) {
    const ipcraft::core::PatchApplyResult patchResult = ipcraft::core::applyPatch(m_design, patch);
    if (!patchResult.success) {
        return DesignEditResult{false, QStringLiteral("Project patch was rejected."), patchResult.issues};
    }

    m_undo.append(m_design);
    m_design = patchResult.project;
    m_redo.clear();
    emit designChanged();
    return DesignEditResult{true, {}, {}};
}

bool DesignEditingService::canUndo() const {
    return !m_undo.isEmpty();
}

bool DesignEditingService::canRedo() const {
    return !m_redo.isEmpty();
}

DesignEditResult DesignEditingService::undo() {
    if (m_undo.isEmpty()) {
        return DesignEditResult{false, QStringLiteral("No design edit is available to undo."), {}};
    }

    m_redo.append(m_design);
    m_design = m_undo.takeLast();
    emit designChanged();
    return DesignEditResult{true, {}, {}};
}

DesignEditResult DesignEditingService::redo() {
    if (m_redo.isEmpty()) {
        return DesignEditResult{false, QStringLiteral("No design edit is available to redo."), {}};
    }

    m_undo.append(m_design);
    m_design = m_redo.takeLast();
    emit designChanged();
    return DesignEditResult{true, {}, {}};
}
