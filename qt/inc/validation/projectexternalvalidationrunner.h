#pragma once

#include "ipcore/ipcatalogservice.h"
#include "ipcraft/core/project_design.h"
#include "project/ipinstancestate.h"
#include "validation/validationresult.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class Graph;

struct ProjectExternalValidationRequest {
    const ipcraft::core::ProjectDesign* projectDesign = nullptr;
    // Optional UI projection used only to resolve/highlight diagnostic targets.
    const Graph* graph = nullptr;
    QString projectPath;
    QString designName;
    QList<IpCatalogEntry> catalogEntries;
    QVector<ProjectIpInstanceRecord> instances;
    QList<ValidationResult> staticResults;
    QSet<QString> blockingInstanceIds;
    bool blockAllExternalValidation = false;
    QStringList frameworkToolSearchPaths;
};

class ProjectExternalValidationRunner {
public:
    ProjectExternalValidationRunner();
    explicit ProjectExternalValidationRunner(QStringList frameworkToolSearchPaths);

    QStringList frameworkToolSearchPaths() const;
    void setFrameworkToolSearchPaths(QStringList searchPaths);

    QList<ValidationResult> validate(const ProjectExternalValidationRequest& request) const;

private:
    QStringList m_frameworkToolSearchPaths;
};
