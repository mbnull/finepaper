#include "ipcraft/patchops.h"
#include "ipcraft/schemaids.h"
#include "project/designeditingservice.h"
#include "topology/topologypresetbuilder.h"
#include "topology/topologypresetpatchbuilder.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

TopologyPresetDescriptor meshPreset() {
    TopologyPresetDescriptor preset;
    preset.id = QStringLiteral("mesh");
    preset.label = QStringLiteral("Mesh");
    preset.kind = QStringLiteral("mesh");
    preset.routerModule = QStringLiteral("RaveTile");
    preset.idPattern = QStringLiteral("rave_{row}_{col}");
    preset.ports.insert(QStringLiteral("east"), QStringLiteral("out_e"));
    preset.ports.insert(QStringLiteral("west"), QStringLiteral("in_w"));
    preset.ports.insert(QStringLiteral("north"), QStringLiteral("in_n"));
    preset.ports.insert(QStringLiteral("south"), QStringLiteral("out_s"));
    return preset;
}

TopologyPresetRequest meshRequest() {
    TopologyPresetRequest request;
    request.ipcoreId = QStringLiteral("finepaper.ravenoc");
    request.instanceId = QStringLiteral("ravenoc_0");
    request.preset = meshPreset();
    request.parameters.insert(QStringLiteral("rows"), 2);
    request.parameters.insert(QStringLiteral("cols"), 3);
    return request;
}

ipcraft::core::ProjectDesign emptyProject() {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("project_0");
    design.name = QStringLiteral("Topology Project");
    return design;
}

void testMeshPresetBuildsDesignLevelTopologyPatch() {
    const TopologyPresetPatchBuildResult built =
        TopologyPresetPatchBuilder::build(meshRequest());

    require(built.success, "mesh preset patch should build");
    require(built.patch.schema == ipcraft::schemaids::patchV1,
            "topology preset patch should use ProjectPatch v1 schema");
    require(built.patch.ops.size() == 1, "topology preset patch should contain one operation");

    const ipcraft::core::PatchOperation& op = built.patch.ops.first();
    require(op.op == ipcraft::patchops::topologyAddOrUpdate,
            "topology preset patch should add or update one topology");
    require(op.target == QStringLiteral("topology:ravenoc_0.mesh"),
            "topology preset patch should target the instance-scoped topology id");
    require(op.path.isEmpty(), "topology add_or_update should not require a collection path");

    const QJsonObject payload = op.payload;
    require(payload.value(QStringLiteral("id")).toString() == QStringLiteral("ravenoc_0.mesh"),
            "topology payload id should match the target id");
    require(payload.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::topologyParametricV1,
            "topology payload should use parametric topology schema");
    require(payload.value(QStringLiteral("kind")).toString() == QStringLiteral("parametric"),
            "topology payload should be parametric");
    require(payload.value(QStringLiteral("family")).toString() == QStringLiteral("mesh"),
            "topology payload should declare mesh family");
    require(payload.value(QStringLiteral("ownerComponentId")).toString() ==
                QStringLiteral("ravenoc_0"),
            "topology payload should be owned by the active IP instance");
    require(payload.value(QStringLiteral("providerRef")).toString() ==
                QStringLiteral("ipcraft.capability.noc.topology.mesh"),
            "topology payload should use the mesh provider ref");

    const QJsonObject parameters = payload.value(QStringLiteral("parameters")).toObject();
    require(parameters.value(QStringLiteral("rows")).toInt() == 2,
            "mesh rows should be preserved as parameters");
    require(parameters.value(QStringLiteral("cols")).toInt() == 3,
            "mesh columns should be preserved as parameters");
    const QJsonArray dimensions = parameters.value(QStringLiteral("dimensions")).toArray();
    require(dimensions.size() == 2, "mesh dimensions should contain columns and rows");
    require(dimensions.at(0).toInt() == 3, "mesh dimensions should store columns first");
    require(dimensions.at(1).toInt() == 2, "mesh dimensions should store rows second");

    const QJsonObject metadata = payload.value(QStringLiteral("metadata")).toObject();
    require(metadata.value(QStringLiteral("ipcoreId")).toString() ==
                QStringLiteral("finepaper.ravenoc"),
            "topology metadata should include the source IP-core id");
    require(metadata.value(QStringLiteral("presetId")).toString() == QStringLiteral("mesh"),
            "topology metadata should include the preset id");
    require(metadata.value(QStringLiteral("routerModule")).toString() ==
                QStringLiteral("RaveTile"),
            "topology metadata should include the router module");
    require(metadata.value(QStringLiteral("ports")).toObject().value(QStringLiteral("east")).toString() ==
                QStringLiteral("out_e"),
            "topology metadata should include port mappings");
}

void testMeshPresetPatchAppliesThroughDesignEditingUndoRedo() {
    DesignEditingService service;
    service.replaceDesign(emptyProject());

    const TopologyPresetPatchBuildResult built =
        TopologyPresetPatchBuilder::build(meshRequest());
    require(built.success, "mesh preset patch should build before applying");

    const DesignEditResult applied = service.applyPatch(built.patch);
    require(applied.success, "mesh topology patch should apply through DesignEditingService");
    require(service.design().topologies.size() == 1, "applied patch should add one topology");
    require(service.design().topologies.first().id == QStringLiteral("ravenoc_0.mesh"),
            "applied topology should use the instance-scoped id");
    require(service.design().topologies.first().parameters.value(QStringLiteral("cols")).toInt() == 3,
            "applied topology should preserve mesh columns");
    require(service.canUndo(), "successful topology patch should create design undo history");

    const DesignEditResult undo = service.undo();
    require(undo.success, "topology patch undo should succeed");
    require(service.design().topologies.isEmpty(), "undo should remove the topology");

    const DesignEditResult redo = service.redo();
    require(redo.success, "topology patch redo should succeed");
    require(service.design().topologies.size() == 1, "redo should restore the topology");
}

void testPatchBuilderRejectsUnsupportedPresetKindForFirstSlice() {
    TopologyPresetRequest request = meshRequest();
    request.preset.kind = QStringLiteral("ring");
    request.preset.id = QStringLiteral("ring");

    const TopologyPresetPatchBuildResult built = TopologyPresetPatchBuilder::build(request);

    require(!built.success, "first slice should reject non-mesh topology presets");
    require(built.error.contains(QStringLiteral("Unsupported topology preset kind")),
            "unsupported kind error should explain the first-slice limitation");
    require(built.patch.ops.isEmpty(), "failed patch builds should not return operations");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testMeshPresetBuildsDesignLevelTopologyPatch();
        testMeshPresetPatchAppliesThroughDesignEditingUndoRedo();
        testPatchBuilderRejectsUnsupportedPresetKindForFirstSlice();
    } catch (const std::exception& error) {
        std::cerr << "topology_preset_patch_builder_test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "topology_preset_patch_builder_test passed\n";
    return 0;
}
