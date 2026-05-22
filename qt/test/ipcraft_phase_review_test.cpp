// Phase-review Qt tests for Ipcraft NoC specgen hardening.
#include "ipcraft/ipcraftmanifestreader.h"
#include "ipcraft/ipcraftregistry.h"
#include "ipcore/ipcatalogservice.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

constexpr auto kPackageId = "phase.synthetic.noc";

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "failed to open test file");
    require(file.write(content) == content.size(), "failed to write test file");
}

QString writeTileView(QDir& root, const QString& firstAnchor = QStringLiteral("fabric_tx")) {
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString path = root.filePath(QStringLiteral("views/Tile.xml"));
    writeFile(path,
              QStringLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="Tile">
  <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
    <expanded min_width="128" height="112" caption_left="28" caption_top="6" port_inset="18" />
    <collapsed min_width="96" height="84" caption_left="28" caption_top="24" endpoint_inset="18" />
    <arrangement endpoint_offset_x="152" mesh_spacing_x="200" mesh_spacing_y="160" />
  </graphics>
  <anchors>
    <anchor ref="%1" x="128" y="56" normal_x="1" normal_y="0" label="Fabric East TX" />
    <anchor ref="fabric_rx" x="0" y="56" normal_x="-1" normal_y="0" label="Fabric West RX" />
    <anchor ref="vertical_tx" x="64" y="112" normal_x="0" normal_y="1" label="Fabric South TX" />
    <anchor ref="vertical_rx" x="64" y="0" normal_x="0" normal_y="-1" label="Fabric North RX" />
  </anchors>
</module-view>
)xml").arg(firstAnchor).toUtf8());
    return path;
}

QString writeEndpointView(QDir& root) {
    require(root.mkpath(QStringLiteral("views")), "failed to create views directory");
    const QString path = root.filePath(QStringLiteral("views/Endpoint.xml"));
    writeFile(path,
              QByteArrayLiteral(R"xml(<?xml version="1.0" encoding="UTF-8"?>
<module-view schema="v1" module="Endpoint">
  <graphics layout="endpoint" node_color="#d6f4b6">
    <expanded min_width="104" height="54" caption_left="8" caption_top="6" />
    <arrangement loose_endpoint_spacing_x="168" loose_endpoint_spacing_y="84" loose_endpoint_margin_y="116" />
  </graphics>
  <anchors>
    <anchor ref="local_noc" x="104" y="27" normal_x="1" normal_y="0" label="Local NoC" />
  </anchors>
</module-view>
)xml"));
    return path;
}

QByteArray syntheticManifest(const QString& packageId = QString::fromUtf8(kPackageId)) {
    return QStringLiteral(R"json({
  "schema": "ipcraft.package.v1",
  "id": "%1",
  "name": "Phase Synthetic NoC",
  "version": "1.0.0",
  "extensions": [
    "noc.v1",
    "ipcraft.views"
  ],
  "views": [
    { "module": "Tile", "file": "views/Tile.xml" },
    { "module": "Endpoint", "file": "views/Endpoint.xml" }
  ],
  "native": {
    "ipcraft": {
      "editor": {
        "extensions": {
          "noc.v1": { "enabled": true }
        },
        "connection_classes": [
          { "id": "fabric_link", "roles": ["initiator", "target"], "symmetric": false },
          { "id": "endpoint_link", "roles": ["initiator", "target"], "symmetric": false }
        ],
        "modules": [
          {
            "id": "Tile",
            "name": "Tile",
            "graph_role": "host",
            "display": {
              "label_parameter": "display_name",
              "short_label_parameter": "external_id"
            },
            "parameters": {
              "display_name": { "type": "string", "default": "", "emit": "editor", "label": "Display name" },
              "external_id": { "type": "string", "default": "", "emit": "editor", "label": "External ID" },
              "mesh_col": { "type": "int", "default": 0, "configurable": false, "emit": "attribute" },
              "mesh_row": { "type": "int", "default": 0, "configurable": false, "emit": "attribute" }
            },
            "interfaces": [
              {
                "id": "fabric_tx",
                "label": "Fabric East TX",
                "modes": ["initiator"],
                "accepts": [{ "class": "fabric_link", "role": "initiator" }],
                "topology": { "side": "east", "opposite": "fabric_rx" }
              },
              {
                "id": "fabric_rx",
                "label": "Fabric West RX",
                "modes": ["target"],
                "accepts": [{ "class": "fabric_link", "role": "target" }],
                "topology": { "side": "west", "opposite": "fabric_tx" }
              },
              {
                "id": "vertical_tx",
                "label": "Fabric South TX",
                "modes": ["initiator"],
                "accepts": [{ "class": "fabric_link", "role": "initiator" }],
                "topology": { "side": "south", "opposite": "vertical_rx" }
              },
              {
                "id": "vertical_rx",
                "label": "Fabric North RX",
                "modes": ["target"],
                "accepts": [{ "class": "fabric_link", "role": "target" }],
                "topology": { "side": "north", "opposite": "vertical_tx" }
              }
            ]
          },
          {
            "id": "Endpoint",
            "name": "Endpoint",
            "graph_role": "attached",
            "display": {
              "label_parameter": "display_name",
              "short_label_parameter": "external_id"
            },
            "parameters": {
              "display_name": { "type": "string", "default": "", "emit": "editor", "label": "Display name" },
              "external_id": { "type": "string", "default": "", "emit": "editor", "label": "External ID" }
            },
            "interfaces": [
              {
                "id": "local_noc",
                "label": "Local NoC",
                "modes": ["initiator"],
                "accepts": [{ "class": "endpoint_link", "role": "initiator" }]
              }
            ]
          }
        ],
        "topologies": [
          {
            "id": "mesh",
            "label": "Mesh",
            "kind": "mesh",
            "module": "Tile",
            "id_pattern": "tile_{row}_{col}",
            "parameters": {
              "rows": { "label": "Rows", "default": 2, "min": 1, "max": 8 },
              "cols": { "label": "Columns", "default": 2, "min": 1, "max": 8 }
            }
          }
        ],
        "generation": {
          "engine": "ipcraft.common.v1",
          "module_mappings": {
            "Tile": "router",
            "Endpoint": "endpoint"
          },
          "coordinate_bindings": {
            "Tile": { "col": "mesh_col", "row": "mesh_row" }
          },
          "outputs": [
            { "id": "manifest", "kind": "json", "path": "manifest.json" }
          ]
        },
        "commands": {
          "generate": {
            "framework_tool": "ipcraft-generate",
            "input_schema": "ipcraft.noc.project.v1",
            "args": ["--manifest", "{manifest}", "--input", "{input}", "--output", "{output}"]
          }
        }
      }
    }
  }
})json").arg(packageId).toUtf8();
}

QString writeSyntheticRuntimePackage(QTemporaryDir& temp,
                                     const QString& directoryName = QStringLiteral("synthetic"),
                                     const QString& packageId = QString::fromUtf8(kPackageId),
                                     const QString& firstTileAnchor = QStringLiteral("fabric_tx")) {
    QDir workspace(temp.path());
    require(workspace.mkpath(directoryName), "failed to create package directory");
    QDir root(workspace.filePath(directoryName));
    writeTileView(root, firstTileAnchor);
    writeEndpointView(root);
    writeFile(root.filePath(QStringLiteral("ipcraft.json")), syntheticManifest(packageId));
    return root.absolutePath();
}

bool diagnosticsContain(const QVector<IpcraftDiagnostic>& diagnostics, const QString& text) {
    for (const IpcraftDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.packageRootPath.contains(text) ||
            diagnostic.path.contains(text) ||
            diagnostic.message.contains(text)) {
            return true;
        }
    }
    return false;
}

void testQtRuntimeLoadsSyntheticPackageWithoutSourceYaml() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    const QString rootPath = writeSyntheticRuntimePackage(temp);
    require(!QFileInfo(QDir(rootPath).filePath(QStringLiteral("ipcore.yml"))).exists(),
            "review fixture should intentionally omit ipcore.yml");

    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(rootPath);

    require(result.ok, "Qt runtime should load package from ipcraft.json and views without ipcore.yml");
    require(result.manifest.id == QString::fromUtf8(kPackageId),
            "runtime manifest package id should parse");
    require(result.manifest.generation.engine == QStringLiteral("ipcraft.common.v1"),
            "generation engine should parse");
    require(result.manifest.commands.value(QStringLiteral("generate")).frameworkTool ==
                QStringLiteral("ipcraft-generate"),
            "framework tool command should parse");

    const IpcraftModuleDescriptor* tile = result.manifest.module(QStringLiteral("Tile"));
    require(tile != nullptr, "Tile module should parse");
    require(tile->displayLabelParameter == QStringLiteral("display_name"),
            "display label parameter should parse");
    const IpcraftInterfaceDescriptor* fabricTx = tile->interfaceDescriptor(QStringLiteral("fabric_tx"));
    require(fabricTx != nullptr, "fabric_tx interface should parse");
    require(fabricTx->topology.side == QStringLiteral("east"),
            "topology side should parse");
    require(fabricTx->topology.oppositeInterfaceId == QStringLiteral("fabric_rx"),
            "topology opposite interface should parse");
}

void testModuleRegistryPropagatesSyntheticManifestMetadata() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    const QString rootPath = writeSyntheticRuntimePackage(temp);
    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(rootPath);
    require(result.ok, "synthetic runtime package should parse before registry load");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpcraftPackages({result.manifest}),
            "ModuleRegistry should load synthetic package modules");

    const ModuleType* tile = registry.getType(QString::fromUtf8(kPackageId), QStringLiteral("Tile"));
    require(tile != nullptr, "Tile module type should be registered by package/module id");
    require(tile->name == ModuleRegistry::scopedTypeName(QString::fromUtf8(kPackageId),
                                                         QStringLiteral("Tile")),
            "registered type name should be package scoped");
    require(tile->displayLabelParameter == QStringLiteral("display_name"),
            "display label parameter should propagate to ModuleType");
    require(tile->shortLabelParameter == QStringLiteral("external_id"),
            "short label parameter should propagate to ModuleType");
    require(tile->supportsMeshCoordinates,
            "Tile should advertise mesh coordinate support from manifest parameters");
    require(tile->interfaceMetadata.contains(QStringLiteral("fabric_tx")),
            "fabric_tx interface metadata should propagate");

    const ModuleInterfaceMetadata fabricTx =
        tile->interfaceMetadata.value(QStringLiteral("fabric_tx"));
    require(fabricTx.label == QStringLiteral("Fabric East TX"),
            "interface label should propagate to ModuleType metadata");
    require(fabricTx.topologySide == QStringLiteral("east"),
            "topology side should propagate to ModuleType metadata");
    require(fabricTx.oppositeInterfaceId == QStringLiteral("fabric_rx"),
            "topology opposite interface should propagate to ModuleType metadata");
}

void testRegistryKeepsValidSyntheticPackageWhenAnotherRootIsInvalid() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    const QString validRoot = writeSyntheticRuntimePackage(temp, QStringLiteral("valid"));
    const QString invalidRoot =
        writeSyntheticRuntimePackage(temp,
                                     QStringLiteral("invalid"),
                                     QStringLiteral("phase.synthetic.invalid"),
                                     QStringLiteral("missing_interface"));

    IpcraftRegistry registry;
    const bool loaded = registry.loadPackageRoots({validRoot, invalidRoot});

    require(!loaded, "mixed valid/invalid roots should expose diagnostics");
    require(registry.package(QString::fromUtf8(kPackageId)) != nullptr,
            "valid package should remain registered");
    require(registry.package(QStringLiteral("phase.synthetic.invalid")) == nullptr,
            "invalid package should not partially register");
    require(diagnosticsContain(registry.diagnostics(), QStringLiteral("missing_interface")),
            "diagnostics should name the invalid view/interface reference");
}

void testRegistryRejectsDuplicateSyntheticPackageIdsWithoutDroppingUnrelatedPackage() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    const QString unrelatedRoot =
        writeSyntheticRuntimePackage(temp,
                                     QStringLiteral("unrelated"),
                                     QStringLiteral("phase.synthetic.unrelated"));
    const QString firstDuplicate =
        writeSyntheticRuntimePackage(temp, QStringLiteral("duplicate_a"));
    const QString secondDuplicate =
        writeSyntheticRuntimePackage(temp, QStringLiteral("duplicate_b"));

    IpcraftRegistry registry;
    const bool loaded = registry.loadPackageRoots({unrelatedRoot, firstDuplicate, secondDuplicate});

    require(!loaded, "duplicate package ids should expose diagnostics");
    require(registry.package(QStringLiteral("phase.synthetic.unrelated")) != nullptr,
            "unrelated package should remain registered");
    require(registry.package(QString::fromUtf8(kPackageId)) == nullptr,
            "duplicate package id should not be silently selected");
    require(diagnosticsContain(registry.diagnostics(), QString::fromUtf8(kPackageId)),
            "duplicate diagnostic should name the duplicated package id");
}

void testCatalogEntryExposesSyntheticPackageRuntimeSurface() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");

    const QString rootPath = writeSyntheticRuntimePackage(temp);
    const IpcraftManifestReadResult result = IpcraftManifestReader().readPackage(rootPath);
    require(result.ok, "synthetic runtime package should parse before catalog load");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    require(registry.loadIpcraftPackages({result.manifest}),
            "ModuleRegistry should load synthetic package before catalog construction");

    IpCatalogService catalog({result.manifest}, &registry);
    const std::optional<IpCatalogEntry> entry = catalog.entry(QString::fromUtf8(kPackageId));

    require(entry.has_value(), "catalog should expose synthetic package");
    require(entry->isSelectable(), "synthetic package should be selectable");
    require(entry->kind == QStringLiteral("noc"),
            "noc extension should produce a noc catalog entry");
    require(entry->moduleTypes.contains(ModuleRegistry::scopedTypeName(QString::fromUtf8(kPackageId),
                                                                       QStringLiteral("Tile"))),
            "catalog should expose package-scoped Tile module type");
    require(entry->topologyPresets.size() == 1,
            "catalog should expose synthetic mesh topology preset");
    require(entry->generator.command == QStringLiteral("ipcraft-generate"),
            "catalog generator command should expose framework tool name");
    require(entry->generator.inputFormat == QStringLiteral("ipcraft.noc.project.v1"),
            "catalog generator should expose command input schema");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testQtRuntimeLoadsSyntheticPackageWithoutSourceYaml();
        testModuleRegistryPropagatesSyntheticManifestMetadata();
        testRegistryKeepsValidSyntheticPackageWhenAnotherRootIsInvalid();
        testRegistryRejectsDuplicateSyntheticPackageIdsWithoutDroppingUnrelatedPackage();
        testCatalogEntryExposesSyntheticPackageRuntimeSurface();
    } catch (const std::exception& error) {
        std::cerr << "ipcraft_phase_review_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcraft_phase_review_test passed\n";
    return 0;
}
