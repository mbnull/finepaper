// ProjectValidationRunner implementation.
#include "validation/projectvalidationrunner.h"

#include "validation/drcrunner.h"
#include "validation/validator.h"

namespace {

const IpCatalogEntry* findEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId) {
    for (const IpCatalogEntry& entry : entries) {
        if (entry.id == ipcoreId) {
            return &entry;
        }
    }

    return nullptr;
}

ValidationResult withInstancePrefix(const ProjectIpInstanceRecord& instance,
                                    const ValidationResult& result) {
    return ValidationResult(result.severity(),
                            QStringLiteral("%1: %2").arg(instance.instanceId, result.message()),
                            result.elementId(),
                            result.ruleName());
}

} // namespace

QList<ValidationResult> ProjectValidationRunner::validate(
    const Graph* graph,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    if (!graph) {
        return {ValidationResult(ValidationSeverity::Error,
                                 QStringLiteral("Graph is not available."),
                                 QString(),
                                 QStringLiteral("validation"))};
    }

    BasicValidator basicValidator;
    QList<ValidationResult> results = basicValidator.validate(graph);

    for (const ProjectIpInstanceRecord& instance : instances) {
        const IpCatalogEntry* entry = findEntry(entries, instance.ipcoreId);
        if (!entry) {
            results.append(ValidationResult(
                ValidationSeverity::Error,
                QStringLiteral("IP instance '%1' references missing IP core runtime/catalog entry '%2'.")
                    .arg(instance.instanceId, instance.ipcoreId),
                instance.instanceId,
                QStringLiteral("DRC")));
            continue;
        }

        if (!entry->drc.hasCommand()) {
            results.append(ValidationResult(
                ValidationSeverity::Warning,
                QStringLiteral("IP instance '%1' does not declare a DRC command; skipping DRC.")
                    .arg(instance.instanceId),
                instance.instanceId,
                QStringLiteral("DRC")));
            continue;
        }

        DRCRunner drcRunner;
        const QList<ValidationResult> drcResults = drcRunner.validate(graph, *entry, instance);
        for (const ValidationResult& result : drcResults) {
            results.append(withInstancePrefix(instance, result));
        }
    }

    return results;
}
