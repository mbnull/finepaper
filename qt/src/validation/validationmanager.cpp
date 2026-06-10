// ValidationManager orchestrates structural checks and IP-provided DRC.
#include "validation/validationmanager.h"
#include "graph/graph.h"
#include "ipcore/ipcatalogservice.h"
#include "panels/logpanel.h"
#include "project/projectstateservice.h"
#include "validation/projectexternalvalidationrunner.h"
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
      m_projectValidationRunner(new ProjectValidationRunner()),
      m_projectExternalValidationRunner(new ProjectExternalValidationRunner()) {
    (void)activeWorkspaceController;
}

ValidationManager::~ValidationManager() {
    delete m_projectValidationRunner;
    delete m_projectExternalValidationRunner;
}

// Run all validators and update log panel with results
void ValidationManager::runValidation(const QString& projectPath, const QString& designName) {
    qInfo() << "Running validation"
            << "modules" << (m_graph ? m_graph->modules().size() : 0)
            << "connections" << (m_graph ? m_graph->connections().size() : 0)
            << "ipInstances" << (m_projectStateService ? m_projectStateService->ipInstanceRecords().size() : 0);
    const QList<IpCatalogEntry> entries =
        m_catalogService ? m_catalogService->entries() : QList<IpCatalogEntry>{};
    const QVector<ProjectIpInstanceRecord> instances =
        m_projectStateService ? m_projectStateService->ipInstanceRecords() : QVector<ProjectIpInstanceRecord>{};
    const ProjectValidationReport staticReport =
        m_projectValidationRunner->validateDetailed(m_graph, entries, instances);

    ProjectExternalValidationRequest externalRequest;
    externalRequest.graph = m_graph;
    externalRequest.projectPath = projectPath;
    externalRequest.designName = designName;
    externalRequest.catalogEntries = entries;
    externalRequest.instances = instances;
    externalRequest.staticResults = staticReport.diagnostics;
    externalRequest.blockingInstanceIds = staticReport.blockingInstanceIds;
    externalRequest.blockAllExternalValidation =
        staticReport.blockAllExternalValidation || !m_graph;

    const QList<ValidationResult> externalResults =
        m_projectExternalValidationRunner->validate(externalRequest);
    QList<ValidationResult> results = staticReport.diagnostics;
    results += externalResults;
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
            << "staticResults" << staticReport.diagnostics.size()
            << "externalResults" << externalResults.size()
            << "blockedExternalInstances" << staticReport.blockingInstanceIds.size()
            << "externalBlockedAll" << externalRequest.blockAllExternalValidation
            << "errors" << errorCount
            << "warnings" << warningCount;
}
