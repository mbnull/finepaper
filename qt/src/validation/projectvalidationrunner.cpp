// ProjectValidationRunner implementation.
#include "validation/projectvalidationrunner.h"

#include "ipcraft/ipcraftbuiltinvalidator.h"
#include "validation/validator.h"

QList<ValidationResult> ProjectValidationRunner::validate(
    const Graph* graph,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    IpcraftBuiltInValidator builtInValidator;
    const IpcraftBuiltInValidator::Result builtInResult =
        builtInValidator.validate(graph,
                                  entries,
                                  instances,
                                  IpcraftBuiltInValidator::CommandPurpose::Validate);
    QList<ValidationResult> results = builtInResult.diagnostics;

    if (!graph) {
        return results;
    }

    BasicValidator basicValidator;
    results += basicValidator.validate(graph);

    return results;
}
