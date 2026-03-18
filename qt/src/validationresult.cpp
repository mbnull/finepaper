#include "validationresult.h"

ValidationResult::ValidationResult(ValidationSeverity severity, const QString& message,
                                 const QString& elementId, const QString& ruleName)
    : m_severity(severity), m_message(message), m_elementId(elementId), m_ruleName(ruleName) {
}
