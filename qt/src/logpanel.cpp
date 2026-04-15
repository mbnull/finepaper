// LogPanel renders validation output and exposes click-to-select graph elements.
#include "logpanel.h"
#include <QVBoxLayout>

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
        const QString prefix = result.severity() == ValidationSeverity::Error
            ? QStringLiteral("[ERROR]")
            : QStringLiteral("[WARN]");
        const QColor color = result.severity() == ValidationSeverity::Error
            ? QColor(220, 50, 50)
            : QColor(200, 150, 50);
        appendMessage(QString("%1 %2").arg(prefix, result.message()), color, result.elementId());
    }
}

void LogPanel::appendMessage(const QString& message,
                             const QColor& color,
                             const QString& elementId) {
    auto* item = new QListWidgetItem();
    item->setText(elementId.isEmpty()
        ? message
        : QString("%1 [%2]").arg(message, elementId));
    item->setData(Qt::UserRole, elementId);

    if (color.isValid()) {
        item->setForeground(color);
    }

    m_listWidget->addItem(item);
    m_listWidget->scrollToBottom();
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
