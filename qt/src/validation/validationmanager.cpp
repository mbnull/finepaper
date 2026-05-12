// ValidationManager orchestrates structural checks and IP-provided DRC.
#include "validation/validationmanager.h"
#include "graph/graph.h"
#include "ipcore/ipcatalogservice.h"
#include "panels/logpanel.h"
#include "project/projectstateservice.h"
#include "validation/projectvalidationrunner.h"
#include <QDebug>

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
      m_logPanel(logPanel),
      m_projectValidationRunner(new ProjectValidationRunner()) {
    (void)activeWorkspaceController;
}

ValidationManager::~ValidationManager() {
    delete m_projectValidationRunner;
}

// Run all validators and update log panel with results
void ValidationManager::runValidation() {
    qInfo() << "Running validation"
            << "modules" << (m_graph ? m_graph->modules().size() : 0)
            << "connections" << (m_graph ? m_graph->connections().size() : 0)
            << "ipInstances" << (m_projectStateService ? m_projectStateService->ipInstanceRecords().size() : 0);
    const QList<IpCatalogEntry> entries =
        m_catalogService ? m_catalogService->entries() : QList<IpCatalogEntry>{};
    const QVector<ProjectIpInstanceRecord> instances =
        m_projectStateService ? m_projectStateService->ipInstanceRecords() : QVector<ProjectIpInstanceRecord>{};
    QList<ValidationResult> results = m_projectValidationRunner->validate(m_graph, entries, instances);
    if (m_logPanel) {
        m_logPanel->setResults(results);
    }

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
