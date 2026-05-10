// ValidationManager orchestrates structural checks and IP-provided DRC.
#include "validation/validationmanager.h"
#include "graph/graph.h"
#include "ipcore/ipcatalogservice.h"
#include "panels/logpanel.h"
#include "project/projectstateservice.h"
#include "validation/validator.h"
#include "validation/drcrunner.h"
#include "workspace/activeworkspacecontroller.h"
#include <QDebug>
#include <optional>

// Initialize validators. Validation is triggered explicitly by the UI.
ValidationManager::ValidationManager(Graph* graph,
                                     ProjectStateService* projectStateService,
                                     const IpCatalogService* catalogService,
                                     const ActiveWorkspaceController* activeWorkspaceController,
                                     LogPanel* logPanel,
                                     QObject* parent)
    : QObject(parent),
      m_graph(graph),
      m_projectStateService(projectStateService),
      m_catalogService(catalogService),
      m_activeWorkspaceController(activeWorkspaceController),
      m_logPanel(logPanel),
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
    if (m_activeWorkspaceController && m_activeWorkspaceController->state().hasActiveIp) {
        const std::optional<ActiveWorkspaceContext> context =
            m_activeWorkspaceController->activeContext();
        if (context.has_value()) {
            results.append(m_drcRunner->validate(m_graph, context->entry, context->record));
        } else {
            results.append(ValidationResult(ValidationSeverity::Error,
                                            QStringLiteral("Active IP instance is not available for DRC."),
                                            "",
                                            "DRC"));
        }
    }
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
