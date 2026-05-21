// ProjectValidationRunner runs static built-in/editor validation only.
// External package validation is executed explicitly through FlowRunner.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"
#include "validation/validationresult.h"

#include <QList>
#include <QVector>

class Graph;

class ProjectValidationRunner {
public:
    QList<ValidationResult> validate(const Graph* graph,
                                     const QList<IpCatalogEntry>& entries,
                                     const QVector<ProjectIpInstanceRecord>& instances) const;
};
