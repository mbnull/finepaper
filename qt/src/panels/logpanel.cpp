// LogPanel renders application activity and exposes click-to-select validation elements.
#include "panels/logpanel.h"
#include <QDateTime>
#include <QVBoxLayout>

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::itemClicked, this, &LogPanel::onItemClicked);
}

// Append validation results with color-coded severity.
void LogPanel::setResults(const QList<ValidationResult>& results) {
    int errorCount = 0;
    int warningCount = 0;
    for (const auto& result : results) {
        if (result.severity() == ValidationSeverity::Error) {
            ++errorCount;
        } else {
            ++warningCount;
        }
    }

    if (errorCount > 0) {
        appendMessage(QString("[Validation] Failed: %1 error(s), %2 warning(s).")
                          .arg(errorCount)
                          .arg(warningCount),
                      QColor(220, 50, 50));
    } else if (warningCount > 0) {
        appendMessage(QString("[Validation] Completed with %1 warning(s).")
                          .arg(warningCount),
                      QColor(200, 150, 50));
    } else {
        appendMessage(QStringLiteral("[Validation] Passed: no errors or warnings."),
                      QColor(40, 140, 80));
    }

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
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString timestampedMessage = QStringLiteral("[%1] %2").arg(timestamp, message);
    item->setText(elementId.isEmpty()
        ? timestampedMessage
        : QString("%1 [%2]").arg(timestampedMessage, elementId));
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
