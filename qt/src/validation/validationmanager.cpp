// ValidationManager orchestrates local and framework validators and updates the log panel.
#include "validation/validationmanager.h"
#include "graph/graph.h"
#include "panels/logpanel.h"
#include "validation/validator.h"
#include "validation/drcrunner.h"
#include <QDebug>

// Initialize validators. Validation is triggered explicitly by the UI.
ValidationManager::ValidationManager(Graph* graph, LogPanel* logPanel, QObject* parent)
    : QObject(parent), m_graph(graph), m_logPanel(logPanel),
      m_validator(new BasicValidator()), m_drcRunner(new DRCRunner()) {}

ValidationManager::~ValidationManager() {
    delete m_validator;
    delete m_drcRunner;
}

// Run all validators and update log panel with results
void ValidationManager::runValidation() {
    qInfo() << "Running validation"
            << "modules" << m_graph->modules().size()
            << "connections" << m_graph->connections().size();
    QList<ValidationResult> results = m_validator->validate(m_graph);
    results.append(m_drcRunner->validate(m_graph));
    m_logPanel->setResults(results);

    int errorCount = 0;
    int warningCount = 0;
    for (const auto& result : results) {
        if (result.severity() == ValidationSeverity::Error) {
            ++errorCount;
            qCritical().noquote() << QString("Validation error [%1] element=%2 message=%3")
                                         .arg(result.ruleName(),
                                              result.elementId().isEmpty() ? QStringLiteral("-") : result.elementId(),
                                              result.message());
        } else {
            ++warningCount;
            qWarning().noquote() << QString("Validation warning [%1] element=%2 message=%3")
                                        .arg(result.ruleName(),
                                             result.elementId().isEmpty() ? QStringLiteral("-") : result.elementId(),
                                             result.message());
        }
    }

    if (results.isEmpty()) {
        qInfo() << "Validation passed with no findings";
    }

    qInfo() << "Validation complete"
            << "results" << results.size()
            << "errors" << errorCount
            << "warnings" << warningCount;
}
