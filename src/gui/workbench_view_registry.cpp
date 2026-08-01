#include "gui/workbench_view_registry.h"

#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

namespace finepaper {

WorkbenchViewRegistry::WorkbenchViewRegistry(QTabWidget* tabs)
    : m_tabs(tabs) {}

bool WorkbenchViewRegistry::addView(WorkbenchViewDefinition definition, QWidget* widget) {
    if (!m_tabs || !widget || definition.id.isEmpty() || definition.title.isEmpty()) {
        return false;
    }
    for (const WorkbenchViewDefinition& existing : m_views) {
        if (existing.id == definition.id) {
            return false;
        }
    }
    widget->setProperty("finepaper.viewId", definition.id);
    if (widget->accessibleName().isEmpty()) {
        widget->setAccessibleName(definition.title);
    }
    const QString visibleTitle = definition.tabTitle.isEmpty()
        ? definition.title : definition.tabTitle;
    const int tabIndex = m_tabs->addTab(widget, visibleTitle);
    m_tabs->setTabToolTip(tabIndex, definition.title);
    m_tabs->tabBar()->setAccessibleTabName(tabIndex, definition.title);
    m_views.append(std::move(definition));
    return true;
}

bool WorkbenchViewRegistry::select(const QString& id) {
    if (!m_tabs) {
        return false;
    }
    for (int index = 0; index < m_tabs->count(); ++index) {
        if (m_tabs->widget(index)->property("finepaper.viewId").toString() == id) {
            m_tabs->setCurrentIndex(index);
            return true;
        }
    }
    return false;
}

QString WorkbenchViewRegistry::currentViewId() const {
    if (!m_tabs) {
        return {};
    }
    const int index = m_tabs->currentIndex();
    return index >= 0 && index < m_tabs->count()
        ? m_tabs->widget(index)->property("finepaper.viewId").toString()
        : QString();
}

const QVector<WorkbenchViewDefinition>& WorkbenchViewRegistry::views() const {
    return m_views;
}

} // namespace finepaper
