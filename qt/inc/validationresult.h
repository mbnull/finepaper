// ValidationResult represents a single validation error or warning
#ifndef VALIDATIONRESULT_H
#define VALIDATIONRESULT_H

#include <QString>

enum class ValidationSeverity {
    Error,
    Warning
};

class ValidationResult {
public:
    ValidationResult(ValidationSeverity severity, const QString& message,
                    const QString& elementId, const QString& ruleName);

    ValidationSeverity severity() const { return m_severity; }
    QString message() const { return m_message; }
    QString elementId() const { return m_elementId; }
    QString ruleName() const { return m_ruleName; }

private:
    ValidationSeverity m_severity;
    QString m_message;
    QString m_elementId;
    QString m_ruleName;
};

#endif
