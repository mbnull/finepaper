#pragma once

#include <QString>
#include <QVector>

class QTabWidget;
class QWidget;

namespace finepaper {

struct WorkbenchViewDefinition {
    QString id;
    QString title;
};

class WorkbenchViewRegistry {
public:
    explicit WorkbenchViewRegistry(QTabWidget* tabs);

    bool addView(WorkbenchViewDefinition definition, QWidget* widget);
    bool select(const QString& id);
    QString currentViewId() const;
    const QVector<WorkbenchViewDefinition>& views() const;

private:
    QTabWidget* m_tabs = nullptr;
    QVector<WorkbenchViewDefinition> m_views;
};

} // namespace finepaper
