// LogPanel displays validation results and allows clicking to select elements
#ifndef LOGPANEL_H
#define LOGPANEL_H

#include <QWidget>
#include <QColor>
#include <QListWidget>
#include "validationresult.h"

class LogPanel : public QWidget {
    Q_OBJECT

public:
    explicit LogPanel(QWidget* parent = nullptr);

    void setResults(const QList<ValidationResult>& results);
    void appendMessage(const QString& message,
                       const QColor& color = QColor(),
                       const QString& elementId = QString());
    void clear();

signals:
    void elementSelected(const QString& elementId);

private slots:
    void onItemClicked(QListWidgetItem* item);

private:
    QListWidget* m_listWidget;
};

#endif
