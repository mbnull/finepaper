#pragma once

#include <QList>
#include <QString>
#include <functional>

class QAction;
class QObject;
class QWidget;

enum class WorkbenchPanelArea {
    Left,
    Right,
    Bottom
};

struct WorkbenchActionContribution {
    QString id;
    QString text;
    QString menuPath;
    QString toolBar;
    QString objectName;
    std::function<QAction*(QObject*)> factory;
};

struct WorkbenchPanelContribution {
    QString id;
    QString title;
    QString objectName;
    WorkbenchPanelArea area = WorkbenchPanelArea::Left;
    std::function<QWidget*(QWidget*)> factory;
};

struct WorkbenchEditorContribution {
    QString id;
    QString title;
    QString objectName;
    std::function<QWidget*(QWidget*)> factory;
};

class WorkbenchService {
public:
    bool addAction(const WorkbenchActionContribution& contribution);
    bool addPanel(const WorkbenchPanelContribution& contribution);
    bool addEditor(const WorkbenchEditorContribution& contribution);

    QList<WorkbenchActionContribution> actions() const;
    QList<WorkbenchPanelContribution> panels() const;
    QList<WorkbenchEditorContribution> editors() const;

private:
    bool hasActionId(const QString& id) const;
    bool hasPanelId(const QString& id) const;
    bool hasEditorId(const QString& id) const;

    QList<WorkbenchActionContribution> m_actions;
    QList<WorkbenchPanelContribution> m_panels;
    QList<WorkbenchEditorContribution> m_editors;
};
