// ProjectValidationRunner implementation.
#include "validation/projectvalidationrunner.h"

#include "graph/graph.h"
#include "ipcraft/ipcraftbuiltinvalidator.h"
#include "validation/validator.h"

namespace {

void appendModuleInstanceBlocker(const Graph* graph,
                                 const QString& moduleId,
                                 QSet<QString>& blockingInstanceIds) {
    const Module* module = graph ? graph->getModule(moduleId) : nullptr;
    if (!module) {
        return;
    }

    const QString instanceId = module->instanceId().trimmed();
    if (!instanceId.isEmpty()) {
        blockingInstanceIds.insert(instanceId);
    }
}

bool appendConnectionScopedBlockers(const Graph* graph,
                                    const QString& connectionId,
                                    QSet<QString>& blockingInstanceIds) {
    const Connection* connection = graph ? graph->getConnection(connectionId) : nullptr;
    if (!connection) {
        return false;
    }

    appendModuleInstanceBlocker(graph, connection->source().moduleId, blockingInstanceIds);
    appendModuleInstanceBlocker(graph, connection->target().moduleId, blockingInstanceIds);
    return true;
}

} // namespace

bool ProjectValidationReport::hasErrors() const {
    for (const ValidationResult& diagnostic : diagnostics) {
        if (diagnostic.severity() == ValidationSeverity::Error) {
            return true;
        }
    }
    return false;
}

QList<ValidationResult> ProjectValidationRunner::validate(
    const Graph* graph,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    return validateDetailed(graph, entries, instances).diagnostics;
}

ProjectValidationReport ProjectValidationRunner::validateDetailed(
    const Graph* graph,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    IpcraftBuiltInValidator builtInValidator;
    const IpcraftBuiltInValidator::Result builtInResult =
        builtInValidator.validate(graph,
                                  entries,
                                  instances,
                                  IpcraftBuiltInValidator::CommandPurpose::Validate);
    ProjectValidationReport report;
    report.diagnostics = builtInResult.diagnostics;
    report.blockingInstanceIds = builtInResult.blockingInstanceIds;

    if (!graph) {
        report.blockAllExternalValidation = true;
        return report;
    }

    BasicValidator basicValidator;
    const QList<ValidationResult> basicResults = basicValidator.validate(graph);
    for (const ValidationResult& result : basicResults) {
        if (result.severity() != ValidationSeverity::Error) {
            continue;
        }
        if (!appendConnectionScopedBlockers(graph,
                                            result.elementId(),
                                            report.blockingInstanceIds)) {
            report.blockAllExternalValidation = true;
        }
    }
    report.diagnostics += basicResults;

    if (report.hasErrors() && report.blockingInstanceIds.isEmpty()) {
        report.blockAllExternalValidation = true;
    }

    return report;
}
