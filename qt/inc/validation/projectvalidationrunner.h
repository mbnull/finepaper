// ProjectValidationRunner runs static built-in/editor validation only.
// External package validation is executed explicitly through FlowRunner.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"
#include "validation/validationresult.h"

#include <QList>
#include <QSet>
#include <QVector>

namespace ipcraft::core {
struct ProjectDesign;
}

struct ProjectValidationReport {
    QList<ValidationResult> diagnostics;
    QSet<QString> blockingInstanceIds;
    bool blockAllExternalValidation = false;

    bool hasErrors() const;
};

class ProjectValidationRunner {
public:
    QList<ValidationResult> validate(const ipcraft::core::ProjectDesign* projectDesign,
                                     const QList<IpCatalogEntry>& entries,
                                     const QVector<ProjectIpInstanceRecord>& instances) const;
    ProjectValidationReport validateDetailed(const ipcraft::core::ProjectDesign* projectDesign,
                                             const QList<IpCatalogEntry>& entries,
                                             const QVector<ProjectIpInstanceRecord>& instances) const;
};
