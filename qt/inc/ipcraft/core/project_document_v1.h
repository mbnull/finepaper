#pragma once

#include "ipcraft/core/project_design.h"

#include <QJsonObject>

namespace ipcraft::core {

struct ProjectDocumentReadResult {
    bool success = false;
    ProjectDesign project;
    QVector<ValidationIssue> issues;
};

class ProjectDocumentV1 {
public:
    static ProjectDocumentReadResult readObject(const QJsonObject& object);
    static QJsonObject writeObject(const ProjectDesign& project);
};

} // namespace ipcraft::core
