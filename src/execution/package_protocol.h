#pragma once

#include "noc/model.h"

#include <QJsonObject>
#include <QString>
#include <QtTypes>
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

// Package results contain metadata only; generated payloads belong in artifact
// files. Bounding this document keeps a damaged Package from monopolizing an
// uncancellable QJson parse.
inline constexpr qint64 kMaximumPackageOperationResultBytes =
    16 * 1024 * 1024;

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
    ArtifactResultPolicy artifactPolicy = ArtifactResultPolicy::Optional,
    const ValidationCancellationCheck& cancellationRequested = {});

} // namespace finepaper
