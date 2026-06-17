#include "topology/topologypresetpatchbuilder.h"

#include "ipcraft/patchops.h"
#include "ipcraft/schemaids.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {

constexpr int kDefaultMeshRows = 2;
constexpr int kDefaultMeshCols = 2;

TopologyPresetPatchBuildResult failure(const QString& error) {
    TopologyPresetPatchBuildResult result;
    result.error = error;
    return result;
}

QString topologyIdFor(const TopologyPresetRequest& request) {
    return request.instanceId.trimmed() + QLatin1Char('.') + request.preset.id.trimmed();
}

QJsonObject portsObject(const QHash<QString, QString>& ports) {
    QJsonObject object;
    for (auto it = ports.cbegin(); it != ports.cend(); ++it) {
        if (!it.key().trimmed().isEmpty() && !it.value().trimmed().isEmpty()) {
            object.insert(it.key(), it.value());
        }
    }
    return object;
}

QJsonObject metadataObject(const TopologyPresetRequest& request) {
    QJsonObject metadata;
    if (!request.ipcoreId.trimmed().isEmpty()) {
        metadata.insert(QStringLiteral("ipcoreId"), request.ipcoreId.trimmed());
    }
    if (!request.preset.id.trimmed().isEmpty()) {
        metadata.insert(QStringLiteral("presetId"), request.preset.id.trimmed());
    }
    if (!request.preset.routerModule.trimmed().isEmpty()) {
        metadata.insert(QStringLiteral("routerModule"), request.preset.routerModule.trimmed());
    }

    const QJsonObject ports = portsObject(request.preset.ports);
    if (!ports.isEmpty()) {
        metadata.insert(QStringLiteral("ports"), ports);
    }
    return metadata;
}

} // namespace

TopologyPresetPatchBuildResult TopologyPresetPatchBuilder::build(
    const TopologyPresetRequest& request) {
    if (request.instanceId.trimmed().isEmpty()) {
        return failure(QStringLiteral("Active IP instance is required."));
    }
    if (request.preset.id.trimmed().isEmpty()) {
        return failure(QStringLiteral("Topology preset id is required."));
    }
    if (request.preset.kind != QStringLiteral("mesh")) {
        return failure(QStringLiteral("Unsupported topology preset kind for design patches: %1")
                           .arg(request.preset.kind));
    }

    const int rows = request.parameters.value(QStringLiteral("rows"), kDefaultMeshRows);
    const int cols = request.parameters.value(QStringLiteral("cols"), kDefaultMeshCols);
    if (rows < 1 || cols < 1) {
        return failure(QStringLiteral("Mesh rows and columns must be positive."));
    }

    const QString topologyId = topologyIdFor(request);
    QJsonObject parameters;
    parameters.insert(QStringLiteral("rows"), rows);
    parameters.insert(QStringLiteral("cols"), cols);
    parameters.insert(QStringLiteral("dimensions"), QJsonArray{cols, rows});

    QJsonObject payload;
    payload.insert(QStringLiteral("id"), topologyId);
    payload.insert(QStringLiteral("schema"), ipcraft::schemaids::topologyParametricV1);
    payload.insert(QStringLiteral("kind"), QStringLiteral("parametric"));
    payload.insert(QStringLiteral("family"), QStringLiteral("mesh"));
    payload.insert(QStringLiteral("ownerComponentId"), request.instanceId.trimmed());
    payload.insert(QStringLiteral("providerRef"),
                   QStringLiteral("ipcraft.capability.noc.topology.mesh"));
    payload.insert(QStringLiteral("parameters"), parameters);

    const QJsonObject metadata = metadataObject(request);
    if (!metadata.isEmpty()) {
        payload.insert(QStringLiteral("metadata"), metadata);
    }

    ipcraft::core::PatchOperation op;
    op.op = ipcraft::patchops::topologyAddOrUpdate;
    op.target = QStringLiteral("topology:") + topologyId;
    op.payload = payload;

    TopologyPresetPatchBuildResult result;
    result.success = true;
    result.patch.schema = ipcraft::schemaids::patchV1;
    result.patch.id = QStringLiteral("topology-preset.%1").arg(topologyId);
    result.patch.description = QStringLiteral("Apply topology preset %1").arg(request.preset.id);
    result.patch.ops.append(op);
    return result;
}
