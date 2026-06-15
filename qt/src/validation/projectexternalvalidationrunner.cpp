#include "validation/projectexternalvalidationrunner.h"

#include "app/projectflowsupport.h"
#include "graph/graph.h"
#include "ipcraft/flowrunner.h"

#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <utility>

namespace {

QString instanceIdFor(const ProjectIpInstanceRecord& instance) {
    const QString instanceId = instance.instanceId.trimmed();
    return instanceId.isEmpty() ? instance.id.trimmed() : instanceId;
}

QString withValidationInstanceContext(const ProjectIpInstanceRecord& instance,
                                      const QString& message) {
    const QString instanceId = instanceIdFor(instance);
    if (instanceId.isEmpty() || message.contains(instanceId)) {
        return message;
    }
    return QStringLiteral("Instance '%1': %2").arg(instanceId, message);
}

void appendUniqueResult(QList<ValidationResult>& results,
                        ValidationSeverity severity,
                        const QString& message,
                        const QString& elementId,
                        const QString& ruleName) {
    for (const ValidationResult& result : results) {
        if (result.severity() == severity &&
            result.message() == message &&
            result.elementId() == elementId &&
            result.ruleName() == ruleName) {
            return;
        }
    }
    results.append(ValidationResult(severity, message, elementId, ruleName));
}

QString resolveGraphElementId(const Graph* graph, const QString& rawElementId) {
    const QString elementId = rawElementId.trimmed();
    if (elementId.isEmpty() || !graph) {
        return elementId;
    }
    if (graph->getModule(elementId) || graph->getConnection(elementId)) {
        return elementId;
    }

    for (const std::unique_ptr<Module>& module : graph->modules()) {
        if (module && module->instanceId() == elementId) {
            return module->id();
        }
    }
    return elementId;
}

QString elementIdForLocation(const Graph* graph, const ipcraft::DiagnosticLocation& location) {
    if (!location.graphObjectId.trimmed().isEmpty()) {
        return resolveGraphElementId(graph, location.graphObjectId);
    }
    if (!location.connectionId.trimmed().isEmpty()) {
        return resolveGraphElementId(graph, location.connectionId);
    }
    if (!location.instanceId.trimmed().isEmpty()) {
        return resolveGraphElementId(graph, location.instanceId);
    }
    return {};
}

QString elementIdForDiagnostic(const Graph* graph, const ipcraft::Diagnostic& diagnostic) {
    for (const ipcraft::DiagnosticLocation& location : diagnostic.locations) {
        const QString elementId = elementIdForLocation(graph, location);
        if (!elementId.isEmpty()) {
            return elementId;
        }
    }
    return {};
}

void parseCapturedText(const ProjectIpInstanceRecord& instance,
                       const Graph* graph,
                       const QString& text,
                       bool includeStructuredErrors,
                       bool includeStructuredWarnings,
                       bool includeUnstructuredErrors,
                       QList<ValidationResult>& results) {
    static const QRegularExpression structuredLine(
        QStringLiteral("^\\s*(ERROR|WARNING|error|warning)\\s+([^:]+):\\s*(.*)\\s*$"));

    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QRegularExpressionMatch match = structuredLine.match(line);
        if (match.hasMatch()) {
            const bool isWarning =
                match.captured(1).startsWith(QStringLiteral("warn"), Qt::CaseInsensitive);
            if ((isWarning && !includeStructuredWarnings) ||
                (!isWarning && !includeStructuredErrors)) {
                continue;
            }

            const ValidationSeverity severity =
                isWarning ? ValidationSeverity::Warning : ValidationSeverity::Error;
            const QString elementId = resolveGraphElementId(graph, match.captured(2));
            const QString message =
                withValidationInstanceContext(instance, match.captured(3).trimmed());
            appendUniqueResult(results, severity, message, elementId, QStringLiteral("DRC"));
            continue;
        }

        if (includeUnstructuredErrors) {
            appendUniqueResult(results,
                               ValidationSeverity::Error,
                               withValidationInstanceContext(instance, line),
                               QString(),
                               QStringLiteral("DRC"));
        }
    }
}

QList<ValidationResult> capturedDiagnostics(const ProjectIpInstanceRecord& instance,
                                            const Graph* graph,
                                            const QString& standardOutput,
                                            const QString& standardError,
                                            bool flowSucceeded) {
    QList<ValidationResult> results;
    if (flowSucceeded) {
        parseCapturedText(instance,
                          graph,
                          standardOutput,
                          true,
                          true,
                          false,
                          results);
        parseCapturedText(instance,
                          graph,
                          standardError,
                          true,
                          true,
                          false,
                          results);
        return results;
    }

    parseCapturedText(instance,
                      graph,
                      standardOutput,
                      true,
                      true,
                      true,
                      results);
    parseCapturedText(instance,
                      graph,
                      standardError,
                      true,
                      true,
                      true,
                      results);
    return results;
}

ValidationSeverity severityForFlowDiagnostic(const ipcraft::Diagnostic& diagnostic) {
    return diagnostic.severity.trimmed().compare(QStringLiteral("warning"),
                                                 Qt::CaseInsensitive) == 0
        ? ValidationSeverity::Warning
        : ValidationSeverity::Error;
}

QString ruleNameForFlowDiagnostic(const ipcraft::Diagnostic& diagnostic) {
    const QString ruleId = diagnostic.ruleId.trimmed();
    return ruleId.isEmpty() ? QStringLiteral("flow") : ruleId;
}

void appendFlowDiagnostics(const ProjectIpInstanceRecord& instance,
                           const Graph* graph,
                           const ipcraft::FlowRunResult& flowResult,
                           QList<ValidationResult>& results) {
    for (const ipcraft::Diagnostic& diagnostic : flowResult.diagnostics.records) {
        const QString message = diagnostic.message.trimmed().isEmpty()
            ? QStringLiteral("Flow diagnostic did not include a message.")
            : diagnostic.message.trimmed();
        appendUniqueResult(results,
                           severityForFlowDiagnostic(diagnostic),
                           withValidationInstanceContext(instance, message),
                           elementIdForDiagnostic(graph, diagnostic),
                           ruleNameForFlowDiagnostic(diagnostic));
    }
}

bool hasError(const QList<ValidationResult>& results) {
    for (const ValidationResult& result : results) {
        if (result.severity() == ValidationSeverity::Error) {
            return true;
        }
    }
    return false;
}

void appendGenericFlowFailure(const ProjectIpInstanceRecord& instance,
                              QList<ValidationResult>& results) {
    appendUniqueResult(results,
                       ValidationSeverity::Error,
                       withValidationInstanceContext(
                           instance,
                           QStringLiteral("Validate flow failed.")),
                       QString(),
                       QStringLiteral("flow"));
}

QStringList frameworkToolSearchPathsForRequest(
    const ProjectExternalValidationRequest& request,
    const QStringList& runnerSearchPaths) {
    return request.frameworkToolSearchPaths.isEmpty()
        ? runnerSearchPaths
        : request.frameworkToolSearchPaths;
}

} // namespace

ProjectExternalValidationRunner::ProjectExternalValidationRunner()
    : m_frameworkToolSearchPaths(ProjectFlowSupport::defaultFrameworkToolSearchPaths()) {}

ProjectExternalValidationRunner::ProjectExternalValidationRunner(QStringList frameworkToolSearchPaths)
    : m_frameworkToolSearchPaths(std::move(frameworkToolSearchPaths)) {}

QStringList ProjectExternalValidationRunner::frameworkToolSearchPaths() const {
    return m_frameworkToolSearchPaths;
}

void ProjectExternalValidationRunner::setFrameworkToolSearchPaths(QStringList searchPaths) {
    m_frameworkToolSearchPaths = std::move(searchPaths);
}

QList<ValidationResult> ProjectExternalValidationRunner::validate(
    const ProjectExternalValidationRequest& request) const {
    QList<ValidationResult> results;
    if (request.instances.isEmpty()) {
        return results;
    }
    if (request.blockAllExternalValidation) {
        return results;
    }

    QTemporaryDir validationRoot;
    if (!validationRoot.isValid()) {
        for (const ProjectIpInstanceRecord& instance : request.instances) {
            const QString instanceId = instanceIdFor(instance);
            if (!request.blockingInstanceIds.contains(instanceId)) {
                results.append(ValidationResult(
                    ValidationSeverity::Error,
                    withValidationInstanceContext(
                        instance,
                        QStringLiteral("Could not create temporary validation directory.")),
                    QString(),
                    QStringLiteral("DRC")));
            }
        }
        return results;
    }

    const QString designName =
        ProjectFlowSupport::designNameForProject(request.projectPath, request.designName);
    const QStringList frameworkToolSearchPaths =
        frameworkToolSearchPathsForRequest(request, m_frameworkToolSearchPaths);

    for (qsizetype index = 0; index < request.instances.size(); ++index) {
        const ProjectIpInstanceRecord& instance = request.instances.at(index);
        const QString instanceId = instanceIdFor(instance);
        if (request.blockingInstanceIds.contains(instanceId)) {
            continue;
        }

        const IpCatalogEntry* entry =
            ProjectFlowSupport::findCatalogEntry(request.catalogEntries, instance.ipcoreId);
        if (!entry) {
            continue;
        }

        const ProjectFlowSupport::PackageFlowContext flowContext =
            ProjectFlowSupport::packageFlowContextForEntry(*entry, QStringLiteral("validate"));
        if (!flowContext.ok) {
            if (flowContext.errorKind ==
                ProjectFlowSupport::PackageFlowContext::ErrorKind::MissingFlow) {
                const QString packageId = entry->id.trimmed().isEmpty()
                    ? instance.ipcoreId
                    : entry->id;
                results.append(ValidationResult(
                    ValidationSeverity::Warning,
                    withValidationInstanceContext(
                        instance,
                        QStringLiteral("Package '%1' does not declare a validate flow.")
                            .arg(packageId)),
                    QString(),
                    QStringLiteral("DRC")));
            } else {
                results.append(ValidationResult(
                    ValidationSeverity::Error,
                    withValidationInstanceContext(instance, flowContext.error),
                    QString(),
                    QStringLiteral("DRC")));
            }
            continue;
        }

        if (!ProjectFlowSupport::isSafeInstanceOutputKey(instanceId)) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                withValidationInstanceContext(
                    instance,
                    QStringLiteral("Unsafe IP instance id for external validation: %1")
                        .arg(instanceId)),
                QString(),
                QStringLiteral("DRC")));
            continue;
        }

        const QString runRoot = QDir(validationRoot.path()).filePath(
            QStringLiteral("%1-%2").arg(index).arg(instanceId));

        ipcraft::FlowRunRequest flowRequest;
        flowRequest.projectId = designName;
        flowRequest.instanceId = instanceId;
        flowRequest.flowId = QStringLiteral("validate");
        flowRequest.runId = instanceId;
        flowRequest.runRoot = runRoot;
        flowRequest.outputRoot = runRoot;
        flowRequest.packageRoot = flowContext.packageRoot;
        flowRequest.package = flowContext.package;
        flowRequest.config = ipcraft::ConfigBundle::fromJson(instance.config);
        flowRequest.graphConfig = ProjectFlowSupport::graphConfigForInstance(instance);
        flowRequest.frameworkToolSearchPaths = frameworkToolSearchPaths;

        const ipcraft::FlowRunResult flowResult = ipcraft::FlowRunner::runFlow(flowRequest);
        const QDir runDir(runRoot);
        const QString standardOutput = ProjectFlowSupport::readTextFileIfPresent(
            runDir.filePath(QStringLiteral("stdout.log")));
        const QString standardError = ProjectFlowSupport::readTextFileIfPresent(
            runDir.filePath(QStringLiteral("stderr.log")));

        QList<ValidationResult> instanceResults =
            capturedDiagnostics(instance, request.graph, standardOutput, standardError, flowResult.ok);
        appendFlowDiagnostics(instance, request.graph, flowResult, instanceResults);
        if (flowResult.ok) {
            results.append(instanceResults);
            continue;
        }

        if (!hasError(instanceResults)) {
            appendGenericFlowFailure(instance, instanceResults);
        }
        results.append(instanceResults);
    }

    return results;
}
