// LogPanel renders application activity and exposes click-to-select validation elements.
#include "panels/logpanel.h"
#include "graph/connection.h"
#include <QDateTime>
#include <QVBoxLayout>
#include <algorithm>

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::itemClicked, this, &LogPanel::onItemClicked);
}

// Append validation results with color-coded severity.
void LogPanel::setResults(const QList<ValidationResult>& results) {
    const bool onlyConnectionAmbiguity =
        !results.isEmpty() &&
        std::all_of(results.cbegin(), results.cend(), [](const ValidationResult& result) {
            return result.ruleName() == QStringLiteral("connection_ambiguity");
        });
    if (onlyConnectionAmbiguity) {
        for (const ValidationResult& result : results) {
            appendMessage(result.message(), QColor(200, 150, 50), result.elementId());
        }
        return;
    }

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

void LogPanel::appendConnectionAmbiguityWarning(const Connection& connection) {
    if (connection.status() != QStringLiteral("ambiguous") ||
        connection.alternatives().size() < 2) {
        return;
    }

    appendMessage(QStringLiteral("Connection %1 has multiple valid classes: %2")
                      .arg(connection.id(),
                           connection.alternatives().join(QStringLiteral(", "))),
                  QColor(200, 150, 50),
                  connection.id());
}

void LogPanel::appendMessage(const QString& message,
                             const QColor& color,
                             const QString& elementId) {
    auto* item = new QListWidgetItem();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString visibleMessage = elementId.isEmpty()
        ? message
        : QString("%1 [%2]").arg(message, elementId);
    const QString timestampedMessage = QStringLiteral("[%1] %2").arg(timestamp, visibleMessage);
    item->setText(visibleMessage);
    item->setToolTip(timestampedMessage);
    item->setData(Qt::UserRole, elementId);
    item->setData(Qt::UserRole + 1, timestamp);

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
