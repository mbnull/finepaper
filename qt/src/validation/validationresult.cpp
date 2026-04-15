// ValidationResult — plain value type carrying a single DRC/validation finding:
// severity, human-readable message, the element it applies to, and the rule name.
#include "validation/validationresult.h"

ValidationResult::ValidationResult(ValidationSeverity severity,
                                   const QString &message,
                                   const QString &elementId,
                                   const QString &ruleName)
    : m_severity(severity), m_message(message), m_elementId(elementId),
      m_ruleName(ruleName) {}
