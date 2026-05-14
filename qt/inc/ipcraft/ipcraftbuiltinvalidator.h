// Qt-side built-in validation for Ipcraft package/project invariants.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"
#include "validation/validationresult.h"

#include <QList>
#include <QSet>
#include <QVector>

class Graph;

class IpcraftBuiltInValidator {
public:
    enum class CommandPurpose {
        Validate,
        Generate
    };

    struct Result {
        QList<ValidationResult> diagnostics;
        QSet<QString> blockingInstanceIds;

        bool hasErrors() const;
    };

    Result validate(const Graph* graph,
                    const QList<IpCatalogEntry>& entries,
                    const QVector<ProjectIpInstanceRecord>& instances,
                    CommandPurpose commandPurpose = CommandPurpose::Validate) const;
};
