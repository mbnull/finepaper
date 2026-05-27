#pragma once

#include "ipcraft/compositionmodel.h"
#include "ipcraft/configschema.h"
#include "ipcraft/diagnostics.h"
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

namespace ipcraft {

struct EmittedInputFile {
    QString id;
    QString kind;
    QString path;
    QJsonObject source;
    QString sha256;
    qint64 size = -1;

    QJsonObject toJson() const;
};

struct EmittedInputsManifest {
    QString schema = schemaids::emittedInputsV1;
    QString projectId;
    QString instanceId;
    QString packageId;
    QString packageVersion;
    QString runId;
    QVector<EmittedInputFile> files;
    DiagnosticStore diagnostics;

    QJsonObject toJson() const;
};

struct PackageInputBuildRequest {
    QString projectId;
    QString instanceId;
    QString runId;
    QString outputRoot;
    PackageSpec package;
    QString packageRoot;
    ConfigBundle config;
    CompositionModel composition;
    std::optional<GraphConfig> graphConfig;
};

struct PackageInputBuildResult {
    bool ok = false;
    EmittedInputsManifest manifest;
};

class PackageInputBuilder {
public:
    static PackageInputBuildResult emitInputs(const PackageInputBuildRequest& request);
};

} // namespace ipcraft
