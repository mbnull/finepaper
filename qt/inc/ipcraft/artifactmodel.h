#pragma once

#include "ipcraft/diagnostics.h"
#include "ipcraft/packagespec.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ipcraft {

struct ArtifactRecord {
    QString id;
    QString type;
    QString path;
    qint64 size = -1;
    QString modifiedTime;
    QString sourceInstanceId;
    QString flowRunId;
    QJsonObject metadata;

    QJsonObject toJson() const;
};

struct ArtifactIndex {
    QString flowRunId;
    QVector<ArtifactRecord> records;

    QJsonObject toJson() const;
};

struct ArtifactCollectRequest {
    QString runRoot;
    QString outputRoot;
    QString flowRunId;
    QString sourceInstanceId;
    PackageSpec package;
};

struct ArtifactCollectResult {
    bool ok = false;
    ArtifactIndex index;
    DiagnosticStore diagnostics;
};

class ArtifactCollector {
public:
    static ArtifactCollectResult collect(const ArtifactCollectRequest& request);
};

} // namespace ipcraft
