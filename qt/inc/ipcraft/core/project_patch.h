#pragma once

#include "ipcraft/core/project_design.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace ipcraft::core {

struct PatchOperation {
    QString op;
    QString target;
    QString path;
    QJsonValue value;
    QJsonObject payload;
};

struct ProjectPatch {
    QString schema;
    QString id;
    QString description;
    QVector<PatchOperation> ops;
    QJsonObject metadata;
};

struct ProjectPatchReadResult {
    bool success = false;
    ProjectPatch patch;
    QVector<ValidationIssue> issues;
};

struct PatchApplyResult {
    bool success = false;
    ProjectDesign project;
    QVector<ValidationIssue> issues;
};

class ProjectPatchCodec {
public:
    static ProjectPatchReadResult readObject(const QJsonObject& object);
    static QJsonObject writeObject(const ProjectPatch& patch);
};

class ProjectPatchApi {
public:
    static ProjectPatchReadResult readObject(const QJsonObject& object);
    static QJsonObject writeObject(const ProjectPatch& patch);
};

PatchApplyResult applyPatch(const ProjectDesign& project, const ProjectPatch& patch);

} // namespace ipcraft::core
