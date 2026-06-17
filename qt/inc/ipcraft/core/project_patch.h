#pragma once

#include "ipcraft/core/project_design.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace ipcraft::core {

// ProjectPatch operations use op names from ipcraft::patchops. Collection adds
// target "component", "connection", "view", or "topology" with a payload object.
// Item edits/removes target "component:<id>", "connection:<id>", "view:<id>",
// or "topology:<id>". Config/metadata paths are single-segment JSON Pointers;
// view node positions use "/nodes/<componentId>" and write ViewDocument::layout.
struct PatchOperation {
    QString op;
    QString target;
    QString path;
    QJsonValue value = QJsonValue(QJsonValue::Undefined);
    QJsonObject payload;
};

struct ProjectPatch {
    QString schema;
    QString id;
    QString description;
    QString author;
    QVector<PatchOperation> ops;
    QJsonObject metadata;
};

// P1/P2 migration boundary backlog before UI durable edits can leave Graph commands:
// remove component, add/remove connection, connection metadata/class/config,
// and layout/view state.
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
