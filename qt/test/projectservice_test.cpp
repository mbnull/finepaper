// ProjectService tests for durable project source-of-truth behavior.
#include "ipcraft/core/project_patch.h"
#include "ipcraft/schemaids.h"
#include "graph/module.h"
#include "project/projectdesignserializer.h"
#include "project/projectservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasDiagnosticRule(const ProjectServiceResult& result, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : result.diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

bool hasDiagnosticRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

QJsonObject componentGraphConfig(const ipcraft::core::ProjectDesign& design,
                                 const QString& componentId) {
    for (const ipcraft::core::ComponentInstance& component : design.components) {
        if (component.id == componentId) {
            return component.extensionData.value(QStringLiteral("graph_config")).toObject();
        }
    }
    return {};
}

bool graphConfigHasObject(const QJsonObject& graphConfig, const QString& objectId) {
    const QJsonArray objects = graphConfig.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue& objectValue : objects) {
        if (objectValue.isObject() &&
            objectValue.toObject().value(QStringLiteral("id")).toString() == objectId) {
            return true;
        }
    }
    return false;
}

QJsonObject graphConfigObject(const QJsonObject& graphConfig, const QString& objectId) {
    const QJsonArray objects = graphConfig.value(QStringLiteral("objects")).toArray();
    for (const QJsonValue& objectValue : objects) {
        if (objectValue.isObject() &&
            objectValue.toObject().value(QStringLiteral("id")).toString() == objectId) {
            return objectValue.toObject();
        }
    }
    return {};
}

QJsonObject layoutNodeForObject(const ProjectDocument& document, const QString& objectId) {
    const QJsonArray views = document.layout.value(QStringLiteral("views")).toArray();
    for (const QJsonValue& viewValue : views) {
        if (!viewValue.isObject()) {
            continue;
        }
        const QJsonObject nodes = viewValue.toObject()
                                      .value(QStringLiteral("canvas")).toObject()
                                      .value(QStringLiteral("nodes")).toObject();
        const QJsonValue nodeValue = nodes.value(objectId);
        if (nodeValue.isObject()) {
            return nodeValue.toObject();
        }
    }
    return {};
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "project JSON fixture should open");
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    require(json.isObject(), "project JSON fixture should be an object");
    return json.object();
}

ipcraft::DiagnosticStore diagnosticsFixture(const QString& ruleId) {
    ipcraft::Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("warning");
    diagnostic.source = QStringLiteral("projectservice_test");
    diagnostic.ruleId = ruleId;
    diagnostic.category = QStringLiteral("project");
    diagnostic.message = QStringLiteral("preserved diagnostic");

    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = QStringLiteral("$.instances[0]");
    diagnostic.locations.append(location);

    ipcraft::DiagnosticStore store;
    store.records.append(diagnostic);
    return store;
}

ipcraft::core::ProjectDesign patchableDesign() {
    ipcraft::core::ProjectDesign design;
    design.schema = ipcraft::schemaids::projectV1;
    design.id = QStringLiteral("project_0");
    design.name = QStringLiteral("Patchable");
    design.packages.append(ipcraft::core::PackageRef{QStringLiteral("pkg"), QStringLiteral("1.0")});
    ipcraft::core::ComponentInstance component;
    component.id = QStringLiteral("cpu0");
    component.type = QStringLiteral("core");
    component.packageRef = QStringLiteral("pkg@1.0");
    design.components.append(component);
    return design;
}

ProjectDocument preservableDocumentFixture() {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = QStringLiteral("original_project");
    document.projectName = QStringLiteral("Original Project");
    document.name = document.projectName;
    document.projectDescription = QStringLiteral("description to preserve");
    document.projectDisplay = QJsonObject{{QStringLiteral("title"), QStringLiteral("Displayed")}};
    document.projectMetadata = QJsonObject{{QStringLiteral("old_metadata"), true}};
    document.projectNative = QJsonObject{{QStringLiteral("project_native"), QStringLiteral("keep")}};
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("vendor.original"),
                                                QStringLiteral("1.0.0")});

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("component0");
    instance.instanceId = instance.id;
    instance.ipcoreId = QStringLiteral("vendor.original");
    instance.displayName = QStringLiteral("Preserved Display Name");
    instance.package = ProjectPackageRef{QStringLiteral("vendor.original"),
                                         QStringLiteral("1.0.0")};
    instance.config = QJsonObject{
        {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("width"), 4}}}
    };
    instance.hasGraphConfig = true;
    instance.graphConfig = QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("graph_node_0")},
                {QStringLiteral("type"), QStringLiteral("GraphNode")},
                {QStringLiteral("properties"), QJsonObject{{QStringLiteral("latency"), 2}}}
            }
        }},
        {QStringLiteral("relationships"), QJsonArray{}},
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("editor")}}},
        {QStringLiteral("native"), QJsonObject{{QStringLiteral("graph_native"), true}}}
    };
    instance.view = QJsonObject{{QStringLiteral("x"), 10}, {QStringLiteral("y"), 20}};
    instance.lastRuns = QJsonObject{{QStringLiteral("drc"), QStringLiteral("passed")}};
    instance.artifacts = QJsonObject{{QStringLiteral("report"), QStringLiteral("artifact.json")}};
    instance.diagnostics = diagnosticsFixture(QStringLiteral("instance.keep")).toJson();
    instance.native = QJsonObject{
        {QStringLiteral("componentType"), QStringLiteral("OldType")},
        {QStringLiteral("identity"), QJsonObject{{QStringLiteral("label"), QStringLiteral("Old")}}},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("role"), QStringLiteral("old")}}},
        {QStringLiteral("extensionData"), QJsonObject{{QStringLiteral("old_ext"), true}}},
        {QStringLiteral("vendor.keep"), QJsonObject{{QStringLiteral("token"), QStringLiteral("keep")}}}
    };
    document.instances.append(instance);

    ProjectConnectionRecord connection;
    connection.id = QStringLiteral("conn0");
    connection.type = QStringLiteral("interface");
    connection.sourceKind = QStringLiteral("user");
    connection.endpoints.append(ProjectEndpointRef{QStringLiteral("component0"),
                                                   QStringLiteral("out"),
                                                   {},
                                                   QStringLiteral("initiator"),
                                                   QJsonObject{{QStringLiteral("lane"), 0}}});
    connection.endpoints.append(ProjectEndpointRef{QStringLiteral("component0"),
                                                   QStringLiteral("in"),
                                                   {},
                                                   QStringLiteral("target"),
                                                   QJsonObject{{QStringLiteral("lane"), 1}}});
    connection.properties = QJsonObject{{QStringLiteral("keep"), QStringLiteral("connection")}};
    connection.native = QJsonObject{{QStringLiteral("native_conn"), true}};
    document.composition.connections.append(connection);

    ProjectExternalPortRecord externalPort;
    externalPort.id = QStringLiteral("ext0");
    externalPort.name = QStringLiteral("External");
    externalPort.hasInterface = true;
    externalPort.interfaceRef = ProjectEndpointRef{QStringLiteral("component0"),
                                                   QStringLiteral("out"),
                                                   QStringLiteral("port0"),
                                                   QStringLiteral("initiator"),
                                                   QJsonObject{}};
    externalPort.properties = QJsonObject{{QStringLiteral("side"), QStringLiteral("north")}};
    externalPort.native = QJsonObject{{QStringLiteral("native_port"), true}};
    document.composition.externalPorts.append(externalPort);
    document.composition.groups.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("group0")}});
    document.composition.properties = QJsonObject{{QStringLiteral("composition"), QStringLiteral("keep")}};
    document.composition.native = QJsonObject{{QStringLiteral("native_composition"), true}};

    document.layout = QJsonObject{{QStringLiteral("views"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("graph")},
                    {QStringLiteral("kind"), QStringLiteral("canvas")}}
    }}};
    document.diagnostics = diagnosticsFixture(QStringLiteral("root.keep"));
    document.artifacts = QJsonObject{{QStringLiteral("build"), QStringLiteral("artifact.zip")}};
    document.migration.fromSchema = QStringLiteral("legacy.schema");
    document.migration.fromVersion = QStringLiteral("0.9");
    document.migration.preserved = QJsonObject{{QStringLiteral("legacy"), true}};
    document.migration.metadata = QJsonObject{{QStringLiteral("migration"), QStringLiteral("keep")}};
    document.migration.native = QJsonObject{{QStringLiteral("native_migration"), true}};
    document.native = QJsonObject{{QStringLiteral("root_native"), QStringLiteral("keep")}};
    return document;
}

void testCreateSaveLoadRoundtrip() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("chip.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Chip Top"));
    require(createResult.success, "createNew should succeed");
    require(service.hasDocument(), "service should have a document after createNew");
    require(service.document().schema == ipcraft::schemaids::projectV1,
            "created document should use project v1 schema");
    require(service.document().projectName == QStringLiteral("Chip Top"),
            "created document should store project name");

    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed");
    require(QFileInfo::exists(path), "project file should be written");
    require(service.currentPath() == QFileInfo(path).absoluteFilePath(),
            "saveFile should store absolute current path");

    ProjectService loaded;
    ProjectServiceResult loadResult = loaded.loadFile(path);
    require(loadResult.success, "loadFile should succeed");
    require(loaded.hasDocument(), "loaded service should have a document");
    require(loaded.document().projectName == QStringLiteral("Chip Top"),
            "loaded document should preserve project name");
    require(loaded.currentPath() == QFileInfo(path).absoluteFilePath(),
            "loadFile should store absolute current path");
}

void testReplaceDocumentFromProjection() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("projection.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Original"));
    require(createResult.success, "createNew should succeed before projection replace");
    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed before projection replace");
    const QString savedPath = service.currentPath();

    ProjectDocument document;
    document.projectName = QStringLiteral("Projection");
    document.projectId = QStringLiteral("projection_0");

    const ProjectServiceResult result = service.replaceDocumentFromProjection(document);
    require(result.success, "replaceDocumentFromProjection should accept valid document");
    require(service.hasDocument(), "service should have a document after projection replace");
    require(service.document().projectName == QStringLiteral("Projection"),
            "projection replacement should update durable document");
    require(service.currentPath() == savedPath,
            "projection replacement should preserve the current path");
}

void testReplaceDocumentFromLoadedFileStoresAbsolutePath() {
    ProjectService service;
    ProjectDocument document;
    document.projectName = QStringLiteral("Loaded");
    document.projectId = QStringLiteral("loaded_0");
    const QString path = QStringLiteral("relative-loaded.fpproj");

    const ProjectServiceResult result =
        service.replaceDocumentFromLoadedFile(document, path);

    require(result.success, "replaceDocumentFromLoadedFile should accept a loaded document");
    require(service.hasDocument(), "loaded-file replacement should establish a document");
    require(service.document().projectName == QStringLiteral("Loaded"),
            "loaded-file replacement should preserve the document");
    require(service.currentPath() == QFileInfo(path).absoluteFilePath(),
            "loaded-file replacement should store the absolute current path");
}

void testCreateNewClearsSavedPath() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("saved.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Saved"));
    require(createResult.success, "createNew should succeed before save");
    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed before creating another document");
    require(!service.currentPath().isEmpty(), "saveFile should establish a current path");

    ProjectServiceResult newResult = service.createNew(QStringLiteral("Other"));
    require(newResult.success, "createNew should succeed after save");
    require(service.document().projectName == QStringLiteral("Other"),
            "createNew should replace the durable document");
    require(service.currentPath().isEmpty(),
            "createNew should clear the previous saved path");
}

void testReplaceDocumentClearsSavedPath() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("saved.fpproj"));

    ProjectService service;
    ProjectServiceResult createResult = service.createNew(QStringLiteral("Saved"));
    require(createResult.success, "createNew should succeed before replaceDocument path test");
    ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should succeed before replaceDocument");
    require(!service.currentPath().isEmpty(), "saveFile should establish a current path");

    ProjectDocument replacement;
    replacement.projectName = QStringLiteral("Replacement");
    replacement.projectId = QStringLiteral("replacement_0");

    const ProjectServiceResult result = service.replaceDocument(replacement);
    require(result.success, "replaceDocument should accept a valid replacement");
    require(service.document().projectName == QStringLiteral("Replacement"),
            "replaceDocument should update the durable document");
    require(service.currentPath().isEmpty(),
            "replaceDocument should clear the previous saved path");
}

void testApplyDesignPatch() {
    ProjectService service;
    ipcraft::core::ProjectPatch patch;
    patch.schema = ipcraft::schemaids::patchV1;
    patch.id = QStringLiteral("patch_0");
    patch.ops.append(ipcraft::core::PatchOperation{
        QStringLiteral("set_config"),
        QStringLiteral("component:cpu0"),
        QStringLiteral("/frequency_mhz"),
        QJsonValue(800),
        {}
    });

    const ipcraft::core::PatchApplyResult result =
        service.applyDesignPatch(patchableDesign(), patch);

    require(result.success, "applyDesignPatch should succeed");
    require(result.project.components.first().config.value(QStringLiteral("frequency_mhz")).toInt() == 800,
            "patch should update component config");
}

void testFlatDesignConfigSavesAsV1BundleAndReloadsAsRuntimeConfig() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("flat_config.fpproj"));

    ipcraft::core::ProjectDesign design = patchableDesign();
    design.components.first().config.insert(QStringLiteral("width"), 4);

    ProjectService service;
    service.replaceDesign(design);
    const ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "saveFile should accept flat runtime design config");

    const QJsonObject root = readJsonObject(path);
    const QJsonObject savedConfig = root.value(QStringLiteral("instances"))
                                        .toArray()
                                        .first()
                                        .toObject()
                                        .value(QStringLiteral("config"))
                                        .toObject();
    require(savedConfig.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("width")).toInt() == 4,
            "saved project config should use the V1 parameters bundle");
    require(!savedConfig.contains(QStringLiteral("width")),
            "saved project config should not write flat keys at bundle top level");

    ProjectService loaded;
    const ProjectServiceResult loadResult = loaded.loadFile(path);
    require(loadResult.success, "fresh ProjectService should load the saved V1 document");
    require(loaded.design().components.size() == 1,
            "loaded runtime design should preserve the component");
    require(loaded.design().components.first().config.value(QStringLiteral("width")).toInt() == 4,
            "loaded runtime design should restore flat component config");
}

void testProjectDesignSerializerMigratesExplicitLegacyComponentType() {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = QStringLiteral("legacy_type_project");
    document.projectName = QStringLiteral("Legacy Type Project");
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("vendor.legacy"),
                                                QStringLiteral("1.0")});

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("legacy_0");
    instance.instanceId = instance.id;
    instance.ipcoreId = QStringLiteral("vendor.legacy");
    instance.package = ProjectPackageRef{QStringLiteral("vendor.legacy"),
                                         QStringLiteral("1.0")};
    instance.state.insert(QStringLiteral("componentType"), QStringLiteral("LegacyTile"));
    document.instances.append(instance);

    const ipcraft::core::ProjectDesign design =
        ProjectDesignSerializer::fromDocument(document);

    require(design.components.size() == 1,
            "legacy document should restore one design component");
    require(design.components.first().type == QStringLiteral("LegacyTile"),
            "serializer should migrate explicit legacy component type deterministically");
    require(design.components.first().packageRef == QStringLiteral("vendor.legacy@1.0"),
            "legacy component type migration should not guess from the package name");
}

void testReplaceDesignSaveLoadPreservesSemanticDesignFields() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("semantic_design.fpproj"));

    ipcraft::core::ProjectDesign design = patchableDesign();
    design.packages.append(ipcraft::core::PackageRef{QStringLiteral("pkg.semantic"),
                                                     QStringLiteral("2.0")});

    ipcraft::core::ComponentInstance endpoint;
    endpoint.id = QStringLiteral("endpoint0");
    endpoint.type = QStringLiteral("endpoint");
    endpoint.packageRef = QStringLiteral("pkg.semantic@2.0");
    endpoint.config = QJsonObject{{QStringLiteral("role"), QStringLiteral("sink")}};
    design.components.append(endpoint);

    ipcraft::core::InterfaceInstance source;
    source.id = QStringLiteral("out");
    source.ownerComponentId = QStringLiteral("cpu0");
    source.type = QStringLiteral("stream");
    source.role = QStringLiteral("producer");
    source.direction = QStringLiteral("source");
    source.protocol = QStringLiteral("axis");
    design.interfaces.append(source);

    ipcraft::core::InterfaceInstance sink;
    sink.id = QStringLiteral("in");
    sink.ownerComponentId = QStringLiteral("endpoint0");
    sink.type = QStringLiteral("stream");
    sink.role = QStringLiteral("consumer");
    sink.direction = QStringLiteral("sink");
    sink.protocol = QStringLiteral("axis");
    design.interfaces.append(sink);

    ipcraft::core::Connection connection;
    connection.id = QStringLiteral("semantic_link");
    connection.from = ipcraft::core::EndpointRef{QStringLiteral("cpu0"), QStringLiteral("out")};
    connection.to = ipcraft::core::EndpointRef{QStringLiteral("endpoint0"), QStringLiteral("in")};
    connection.constraints = QJsonObject{{QStringLiteral("latency"), 1}};
    design.connections.append(connection);

    ipcraft::core::TopologyGraph topology;
    topology.id = QStringLiteral("semantic_topology");
    topology.schema = ipcraft::schemaids::topologyGraphV1;
    topology.kind = QStringLiteral("explicit_graph");
    topology.nodes.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("node0")}});
    design.topologies.append(topology);

    design.constraints = QJsonObject{{QStringLiteral("clock"), QStringLiteral("core_clk")}};
    ipcraft::core::ViewDocument view;
    view.id = QStringLiteral("semantic_view");
    view.schema = ipcraft::schemaids::viewV1;
    view.kind = QStringLiteral("schematic");
    view.targetRef = QStringLiteral("semantic_topology");
    view.providerRef = QStringLiteral("qt.test");
    design.views.append(view);
    design.diagnostics.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("semantic.keep")}});
    design.artifacts.append(QJsonObject{{QStringLiteral("path"), QStringLiteral("semantic.json")}});

    ipcraft::core::ExtensionBlock extension;
    extension.ownerPackageId = QStringLiteral("pkg.semantic");
    extension.schemaId = QStringLiteral("pkg.semantic.extension.v1");
    extension.version = 1;
    extension.data = QJsonObject{{QStringLiteral("enabled"), true}};
    design.extensions.append(extension);

    ProjectService service;
    service.replaceDesign(design);
    require(service.document()
                .native.value(QStringLiteral("ipcraft.projectDesignSupplement.v1"))
                .isObject(),
            "replaceDesign should persist semantic design fields in document native");
    const ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "semantic design document should save");

    ProjectService loaded;
    const ProjectServiceResult loadResult = loaded.loadFile(path);
    require(loadResult.success, "semantic design document should reload");
    require(loaded.design().components.size() == 2,
            "loaded semantic design should preserve projected components");
    require(loaded.design().interfaces.size() == 2,
            "loaded semantic design should preserve interfaces");
    require(loaded.design().connections.size() == 1 &&
                loaded.design().connections.first().id == QStringLiteral("semantic_link"),
            "loaded semantic design should preserve connections");
    require(loaded.design().topologies.size() == 1 &&
                loaded.design().topologies.first().id == QStringLiteral("semantic_topology"),
            "loaded semantic design should preserve topologies");
    require(loaded.design().constraints.value(QStringLiteral("clock")).toString() ==
                QStringLiteral("core_clk"),
            "loaded semantic design should preserve constraints");
    require(loaded.design().views.size() == 1 &&
                loaded.design().views.first().id == QStringLiteral("semantic_view"),
            "loaded semantic design should preserve views");
    require(loaded.design().diagnostics.size() == 1 &&
                loaded.design().diagnostics.first().value(QStringLiteral("code")).toString() ==
                    QStringLiteral("semantic.keep"),
            "loaded semantic design should preserve diagnostics");
    require(loaded.design().artifacts.size() == 1 &&
                loaded.design().artifacts.first().value(QStringLiteral("path")).toString() ==
                    QStringLiteral("semantic.json"),
            "loaded semantic design should preserve artifacts");
    require(loaded.design().extensions.size() == 1 &&
                loaded.design().extensions.first().schemaId ==
                    QStringLiteral("pkg.semantic.extension.v1"),
            "loaded semantic design should preserve extensions");
}

void testMergeDesignOnlyComponentsPreservesProjectedInstancesAndSemanticSupplement() {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = QStringLiteral("projection_project");
    document.projectName = QStringLiteral("Projection Project");
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("pkg"), QStringLiteral("1.0")});
    document.native = QJsonObject{{QStringLiteral("root_native"), QStringLiteral("keep")}};

    ProjectIpInstanceRecord projected;
    projected.id = QStringLiteral("cpu0");
    projected.instanceId = QStringLiteral("cpu0");
    projected.ipcoreId = QStringLiteral("pkg");
    projected.package = ProjectPackageRef{QStringLiteral("pkg"), QStringLiteral("1.0")};
    projected.config = QJsonObject{
        {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("width"), 99}}}
    };
    projected.native = QJsonObject{{QStringLiteral("componentType"), QStringLiteral("projected_core")}};
    document.instances.append(projected);

    ProjectService service;
    const ProjectServiceResult replaceResult = service.replaceDocument(document);
    require(replaceResult.success, "projection document should load into service");

    ipcraft::core::ProjectDesign design = patchableDesign();
    design.components.first().type = QStringLiteral("stale_core");
    design.components.first().config = QJsonObject{{QStringLiteral("width"), 4}};

    ipcraft::core::ComponentInstance endpoint;
    endpoint.id = QStringLiteral("endpoint0");
    endpoint.type = QStringLiteral("endpoint");
    endpoint.packageRef = QStringLiteral("pkg@1.0");
    endpoint.config = QJsonObject{{QStringLiteral("role"), QStringLiteral("sink")}};
    design.components.append(endpoint);

    ipcraft::core::InterfaceInstance source;
    source.id = QStringLiteral("out");
    source.ownerComponentId = QStringLiteral("cpu0");
    source.type = QStringLiteral("stream");
    source.role = QStringLiteral("producer");
    source.direction = QStringLiteral("source");
    source.protocol = QStringLiteral("axis");
    design.interfaces.append(source);

    ipcraft::core::InterfaceInstance sink;
    sink.id = QStringLiteral("in");
    sink.ownerComponentId = QStringLiteral("endpoint0");
    sink.type = QStringLiteral("stream");
    sink.role = QStringLiteral("consumer");
    sink.direction = QStringLiteral("sink");
    sink.protocol = QStringLiteral("axis");
    design.interfaces.append(sink);

    ipcraft::core::Connection connection;
    connection.id = QStringLiteral("merge_semantic_link");
    connection.from = ipcraft::core::EndpointRef{QStringLiteral("cpu0"), QStringLiteral("out")};
    connection.to = ipcraft::core::EndpointRef{QStringLiteral("endpoint0"), QStringLiteral("in")};
    design.connections.append(connection);

    service.mergeDesignOnlyComponents(design);

    const ProjectDocument& merged = service.document();
    require(merged.instances.size() == 2,
            "mergeDesignOnlyComponents should append design-only instances");
    require(merged.instances.first().native.value(QStringLiteral("componentType")).toString() ==
                QStringLiteral("projected_core"),
            "mergeDesignOnlyComponents should not overwrite projected component type");
    require(merged.instances.first().config.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("width")).toInt() == 99,
            "mergeDesignOnlyComponents should not overwrite projected config");
    require(merged.native.value(QStringLiteral("root_native")).toString() == QStringLiteral("keep"),
            "mergeDesignOnlyComponents should preserve unrelated root native data");
    require(merged.native.value(QStringLiteral("ipcraft.projectDesignSupplement.v1")).isObject(),
            "mergeDesignOnlyComponents should persist semantic design fields in document native");
    require(service.design().connections.size() == 1 &&
                service.design().connections.first().id ==
                    QStringLiteral("merge_semantic_link"),
            "mergeDesignOnlyComponents should reload semantic fields from the supplement");
}

void testReplaceDesignPreservesNonDesignProjectFields() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("preserved_fields.fpproj"));

    ProjectService service;
    const ProjectServiceResult replaceResult = service.replaceDocument(preservableDocumentFixture());
    require(replaceResult.success, "replaceDocument should accept preservable fixture");

    ipcraft::core::ProjectDesign design = service.design();
    design.id = QStringLiteral("edited_project");
    design.name = QStringLiteral("Edited Project");
    design.metadata = QJsonObject{{QStringLiteral("design_metadata"), QStringLiteral("updated")}};
    design.packages = {ipcraft::core::PackageRef{QStringLiteral("vendor.updated"),
                                                 QStringLiteral("2.0.0")}};
    require(design.components.size() == 1,
            "preservable fixture should expose one design component");
    ipcraft::core::ComponentInstance& component = design.components.first();
    component.type = QStringLiteral("UpdatedType");
    component.packageRef = QStringLiteral("vendor.updated@2.0.0");
    component.config = QJsonObject{{QStringLiteral("width"), 8},
                                   {QStringLiteral("mode"), QStringLiteral("fast")}};
    component.identity = QJsonObject{{QStringLiteral("label"), QStringLiteral("Updated")}};
    component.metadata = QJsonObject{{QStringLiteral("role"), QStringLiteral("design")}};
    component.extensionData = QJsonObject{{QStringLiteral("updated_ext"), true}};

    service.replaceDesign(design);
    const ProjectDocument& merged = service.document();
    require(merged.projectId == QStringLiteral("edited_project"),
            "replaceDesign should update project id");
    require(merged.projectName == QStringLiteral("Edited Project"),
            "replaceDesign should update project name");
    require(merged.name == QStringLiteral("Edited Project"),
            "replaceDesign should update the transitional project name alias");
    require(merged.projectMetadata.value(QStringLiteral("design_metadata")).toString() ==
                QStringLiteral("updated"),
            "replaceDesign should update design-owned project metadata");
    require(merged.ipcores.size() == 1 &&
                merged.ipcores.first().id == QStringLiteral("vendor.updated") &&
                merged.ipcores.first().version == QStringLiteral("2.0.0"),
            "replaceDesign should replace package refs from the runtime design");
    require(merged.instances.size() == 1,
            "replaceDesign should keep the design component as a project instance");
    const ProjectIpInstanceRecord& mergedInstance = merged.instances.first();
    require(mergedInstance.package.id == QStringLiteral("vendor.updated") &&
                mergedInstance.package.version == QStringLiteral("2.0.0"),
            "replaceDesign should update design-owned instance package");
    require(mergedInstance.config.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("width")).toInt() == 8,
            "replaceDesign should update design-owned instance config");
    require(mergedInstance.native.value(QStringLiteral("componentType")).toString() ==
                QStringLiteral("UpdatedType"),
            "replaceDesign should update design-owned component type");
    require(mergedInstance.native.value(QStringLiteral("identity")).toObject()
                .value(QStringLiteral("label")).toString() == QStringLiteral("Updated"),
            "replaceDesign should update design-owned identity");
    require(mergedInstance.native.value(QStringLiteral("metadata")).toObject()
                .value(QStringLiteral("role")).toString() == QStringLiteral("design"),
            "replaceDesign should update design-owned metadata");
    require(mergedInstance.native.value(QStringLiteral("extensionData")).toObject()
                .value(QStringLiteral("updated_ext")).toBool(),
            "replaceDesign should update design-owned extension data");

    require(merged.projectDescription == QStringLiteral("description to preserve"),
            "replaceDesign should preserve project description");
    require(merged.projectDisplay.value(QStringLiteral("title")).toString() == QStringLiteral("Displayed"),
            "replaceDesign should preserve project display");
    require(merged.projectNative.value(QStringLiteral("project_native")).toString() ==
                QStringLiteral("keep"),
            "replaceDesign should preserve project native data");
    require(merged.layout.value(QStringLiteral("views")).toArray().size() == 1,
            "replaceDesign should preserve layout");
    require(hasDiagnosticRule(merged.diagnostics, QStringLiteral("root.keep")),
            "replaceDesign should preserve root diagnostics");
    require(merged.artifacts.value(QStringLiteral("build")).toString() == QStringLiteral("artifact.zip"),
            "replaceDesign should preserve root artifacts");
    require(merged.migration.fromSchema == QStringLiteral("legacy.schema") &&
                merged.migration.preserved.value(QStringLiteral("legacy")).toBool(),
            "replaceDesign should preserve migration fields");
    require(merged.native.value(QStringLiteral("root_native")).toString() == QStringLiteral("keep"),
            "replaceDesign should preserve root native data");
    require(merged.composition.connections.size() == 1 &&
                merged.composition.externalPorts.size() == 1 &&
                merged.composition.groups.size() == 1,
            "replaceDesign should preserve composition records");
    require(mergedInstance.displayName == QStringLiteral("Preserved Display Name"),
            "replaceDesign should preserve instance display name");
    require(mergedInstance.hasGraphConfig &&
                mergedInstance.graphConfig.value(QStringLiteral("objects")).toArray().size() == 1,
            "replaceDesign should preserve instance graph_config");
    require(mergedInstance.view.value(QStringLiteral("x")).toInt() == 10,
            "replaceDesign should preserve instance view");
    require(mergedInstance.lastRuns.value(QStringLiteral("drc")).toString() == QStringLiteral("passed"),
            "replaceDesign should preserve instance last_runs");
    require(mergedInstance.artifacts.value(QStringLiteral("report")).toString() ==
                QStringLiteral("artifact.json"),
            "replaceDesign should preserve instance artifacts");
    require(mergedInstance.diagnostics.value(QStringLiteral("records")).toArray().size() == 1,
            "replaceDesign should preserve instance diagnostics");
    require(mergedInstance.native.value(QStringLiteral("vendor.keep")).toObject()
                .value(QStringLiteral("token")).toString() == QStringLiteral("keep"),
            "replaceDesign should preserve unrelated instance native keys");

    const ProjectServiceResult saveResult = service.saveFile(path);
    require(saveResult.success, "merged design document should save");
    ProjectService loaded;
    const ProjectServiceResult loadResult = loaded.loadFile(path);
    require(loadResult.success, "merged design document should reload");
    require(loaded.document().projectDisplay.value(QStringLiteral("title")).toString() ==
                QStringLiteral("Displayed"),
            "saved merged document should preserve project display after reload");
    require(loaded.document().composition.connections.size() == 1 &&
                loaded.document().composition.externalPorts.size() == 1,
            "saved merged document should preserve composition after reload");
    require(hasDiagnosticRule(loaded.document().diagnostics, QStringLiteral("root.keep")),
            "saved merged document should preserve root diagnostics after reload");
    require(loaded.document().migration.fromSchema == QStringLiteral("legacy.schema"),
            "saved merged document should preserve migration after reload");
    require(loaded.document().instances.first().hasGraphConfig,
            "saved merged document should preserve instance graph_config after reload");
    require(loaded.document().instances.first().view.value(QStringLiteral("x")).toInt() == 10,
            "saved merged document should preserve instance view after reload");
    require(loaded.document().instances.first().lastRuns.value(QStringLiteral("drc")).toString() ==
                QStringLiteral("passed"),
            "saved merged document should preserve instance last_runs after reload");
    require(loaded.document().instances.first().native.value(QStringLiteral("vendor.keep")).toObject()
                .value(QStringLiteral("token")).toString() == QStringLiteral("keep"),
            "saved merged document should preserve unrelated native keys after reload");
}

void testEditorModuleMutationRefreshesRuntimeDesignBeforeSave() {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectId = QStringLiteral("editor_refresh_project");
    document.projectName = QStringLiteral("Editor Refresh");
    document.ipcores.append(ProjectIpcoreRecord{QStringLiteral("vendor.designpkg"),
                                                QStringLiteral("1.0.0")});

    ProjectIpInstanceRecord instance;
    instance.id = QStringLiteral("component0");
    instance.instanceId = instance.id;
    instance.ipcoreId = QStringLiteral("vendor.designpkg");
    instance.package = ProjectPackageRef{QStringLiteral("vendor.designpkg"),
                                         QStringLiteral("1.0.0")};
    document.instances.append(instance);

    ProjectService service;
    const ProjectServiceResult replaceResult = service.replaceDocument(document);
    require(replaceResult.success, "editor refresh fixture should load into ProjectService");

    Module module(QStringLiteral("editor_node"), QStringLiteral("EditorNode"));
    module.setIpcoreId(QStringLiteral("vendor.designpkg"));
    module.setInstanceId(QStringLiteral("component0"));
    module.setParameter(QStringLiteral("vc_count"), 7);
    module.setParameter(QStringLiteral("x"), 123);

    require(service.upsertEditorModuleRecord(module),
            "editor module mutation should update the durable document");

    const QJsonObject graphConfig =
        componentGraphConfig(service.design(), QStringLiteral("component0"));
    require(graphConfigHasObject(graphConfig, QStringLiteral("editor_node")),
            "ProjectService runtime design should expose the editor module before save");
    require(graphConfigObject(graphConfig, QStringLiteral("editor_node"))
                .value(QStringLiteral("properties")).toObject()
                .value(QStringLiteral("vc_count")).toInt() == 7,
            "ProjectService runtime design should expose editor graph parameters before save");
    require(layoutNodeForObject(service.document(), QStringLiteral("editor_node"))
                .value(QStringLiteral("x")).toInt() == 123,
            "ProjectService document should expose editor layout before save");
}

void testRejectsUnsupportedDocumentKind() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("not-a-project.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "fixture should open");
    }
    file.write("{\"schema\":\"unknown\"}");
    file.close();

    ProjectService service;
    const ProjectServiceResult result = service.loadFile(path);
    require(!result.success, "loadFile should reject unsupported project kind");
    require(result.error.contains(QStringLiteral("Unsupported project schema")),
            "unsupported project schema error should come from ProjectReader");
    require(hasDiagnosticRule(result, QStringLiteral("project.unsupported_schema")),
            "unsupported project schema should expose structured diagnostics");
    require(!service.hasDocument(), "failed load should not replace current document");
}

void testLoadFileReportsInvalidJsonDiagnostics() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("broken.fpproj"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        require(false, "invalid JSON fixture should open");
    }
    file.write("{not-json");
    file.close();

    ProjectService service;
    const ProjectServiceResult result = service.loadFile(path);

    require(!result.success, "loadFile should reject invalid JSON");
    require(result.error.contains(QStringLiteral("Invalid project JSON")),
            "invalid JSON error should come from ProjectReader");
    require(hasDiagnosticRule(result, QStringLiteral("project.invalid_json")),
            "invalid JSON should expose structured diagnostics");
    require(!service.hasDocument(), "failed invalid JSON load should not replace current document");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testCreateSaveLoadRoundtrip();
        testReplaceDocumentFromProjection();
        testReplaceDocumentFromLoadedFileStoresAbsolutePath();
        testCreateNewClearsSavedPath();
        testReplaceDocumentClearsSavedPath();
        testApplyDesignPatch();
        testFlatDesignConfigSavesAsV1BundleAndReloadsAsRuntimeConfig();
        testProjectDesignSerializerMigratesExplicitLegacyComponentType();
        testReplaceDesignSaveLoadPreservesSemanticDesignFields();
        testMergeDesignOnlyComponentsPreservesProjectedInstancesAndSemanticSupplement();
        testReplaceDesignPreservesNonDesignProjectFields();
        testEditorModuleMutationRefreshesRuntimeDesignBeforeSave();
        testRejectsUnsupportedDocumentKind();
        testLoadFileReportsInvalidJsonDiagnostics();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }
    std::cout << "projectservice_test passed\n";
    return 0;
}
