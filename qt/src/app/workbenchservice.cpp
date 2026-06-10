#include "app/workbenchservice.h"

namespace {

bool isValidId(const QString& id) {
    return !id.trimmed().isEmpty();
}

bool isValidObjectName(const QString& objectName) {
    return !objectName.trimmed().isEmpty();
}

} // namespace

bool WorkbenchService::addAction(const WorkbenchActionContribution& contribution) {
    if (!isValidId(contribution.id) ||
        contribution.text.trimmed().isEmpty() ||
        !isValidObjectName(contribution.objectName) ||
        !contribution.factory ||
        hasActionId(contribution.id)) {
        return false;
    }

    m_actions.append(contribution);
    return true;
}

bool WorkbenchService::addPanel(const WorkbenchPanelContribution& contribution) {
    if (!isValidId(contribution.id) ||
        contribution.title.trimmed().isEmpty() ||
        !isValidObjectName(contribution.objectName) ||
        !contribution.factory ||
        hasPanelId(contribution.id)) {
        return false;
    }

    m_panels.append(contribution);
    return true;
}

bool WorkbenchService::addEditor(const WorkbenchEditorContribution& contribution) {
    if (!isValidId(contribution.id) ||
        contribution.title.trimmed().isEmpty() ||
        !isValidObjectName(contribution.objectName) ||
        !contribution.factory ||
        hasEditorId(contribution.id)) {
        return false;
    }

    m_editors.append(contribution);
    return true;
}

QList<WorkbenchActionContribution> WorkbenchService::actions() const {
    return m_actions;
}

QList<WorkbenchPanelContribution> WorkbenchService::panels() const {
    return m_panels;
}

QList<WorkbenchEditorContribution> WorkbenchService::editors() const {
    return m_editors;
}

bool WorkbenchService::hasActionId(const QString& id) const {
    for (const WorkbenchActionContribution& contribution : m_actions) {
        if (contribution.id == id) {
            return true;
        }
    }
    return false;
}

bool WorkbenchService::hasPanelId(const QString& id) const {
    for (const WorkbenchPanelContribution& contribution : m_panels) {
        if (contribution.id == id) {
            return true;
        }
    }
    return false;
}

bool WorkbenchService::hasEditorId(const QString& id) const {
    for (const WorkbenchEditorContribution& contribution : m_editors) {
        if (contribution.id == id) {
            return true;
        }
    }
    return false;
}
