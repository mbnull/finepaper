#pragma once

#include "noc/model.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace finepaper {

struct Artifact {
    QString id;
    QString type;
    QString path;
    bool primary = false;
};

enum class ArtifactResultPolicy {
    Optional,
    Required
};

struct PackageOperationResult {
    bool protocolValid = false;
    bool success = false;
    QVector<Diagnostic> diagnostics;
    QVector<Artifact> artifacts;
};

PackageOperationResult parsePackageOperationResult(
    const QJsonObject& object,
    const QString& resultPath,
    const QString& defaultSource,
    ArtifactResultPolicy artifactPolicy = ArtifactResultPolicy::Optional);

} // namespace finepaper
