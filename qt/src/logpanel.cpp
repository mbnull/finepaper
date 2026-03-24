#include "logpanel.h"
#include <QVBoxLayout>
#include <QIcon>

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::itemClicked, this, &LogPanel::onItemClicked);
}

// Display validation results with color-coded severity
void LogPanel::setResults(const QList<ValidationResult>& results) {
    clear();

    for (const auto& result : results) {
        QListWidgetItem* item = new QListWidgetItem(m_listWidget);

        QString prefix = (result.severity() == ValidationSeverity::Error) ? "❌" : "⚠️";
        item->setText(QString("%1 %2 [%3]").arg(prefix, result.message(), result.elementId()));
        item->setData(Qt::UserRole, result.elementId());

        if (result.severity() == ValidationSeverity::Error) {
            item->setForeground(QColor(220, 50, 50));
        } else {
            item->setForeground(QColor(200, 150, 50));
        }

        m_listWidget->addItem(item);
    }
}

void LogPanel::clear() {
    m_listWidget->clear();
}

// Emit signal when user clicks on validation result
void LogPanel::onItemClicked(QListWidgetItem* item) {
    QString elementId = item->data(Qt::UserRole).toString();
    if (!elementId.isEmpty()) {
        emit elementSelected(elementId);
    }
}
