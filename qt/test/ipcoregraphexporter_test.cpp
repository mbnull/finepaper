// IpCoreGraphExporter tests for generator/DRC boundary JSON.
#include "ipcore/ipcoregraphexporter.h"

#include "graph/graph.h"
#include "modules/moduleregistry.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QString legacyPluginOwnerKey() {
    return QStringLiteral("plug") + QStringLiteral("in");
}

ModuleType registerOwnedType(const QString& typeName, const QString& ipcoreId) {
    ModuleType type;
    type.name = typeName;
    type.ipcoreId = ipcoreId;
    ModuleRegistry::instance().registerType(type);
    return type;
}

ModuleType registerPackageScopedType(const QString& packageId, const QString& moduleId) {
    ModuleType type;
    type.name = ModuleRegistry::scopedTypeName(packageId, moduleId);
    type.packageId = packageId;
    type.moduleId = moduleId;
    type.ipcoreId = packageId;
    ModuleRegistry::instance().registerType(type);
    return type;
}

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   const QString& ipcoreId,
                                   const QString& instanceId,
                                   std::vector<Port> ports) {
    auto module = std::make_unique<Module>(id, type);
    module->setIpcoreId(ipcoreId);
    module->setInstanceId(instanceId);
    for (const Port& port : ports) {
        module->addPort(port);
    }
    return module;
}

IpCatalogEntry catalogEntry(const QString& ipcoreId) {
    IpCatalogEntry entry;
    entry.id = ipcoreId;
    entry.packageId = ipcoreId;
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.inputFormat = QStringLiteral("ipcore_graph_v1");
    return entry;
}

IpCatalogEntry ipcraftCatalogEntry(const QString& packageId) {
    IpCatalogEntry entry = catalogEntry(packageId);
    entry.packageManifest.schema = QStringLiteral("ipcraft.manifest.v1");
    entry.packageManifest.id = packageId;
    entry.packageManifest.name = packageId;
    entry.packageManifest.version = QStringLiteral("1.0");
    entry.generator.inputFormat = QStringLiteral("ipcraft.noc.project.v1");
    return entry;
}

ProjectIpInstanceRecord instanceRecord(const QString& ipcoreId, const QString& instanceId) {
    ProjectIpInstanceRecord record;
    record.ipcoreId = ipcoreId;
    record.instanceId = instanceId;
    record.schema = ipcoreId + QStringLiteral("-project-state-v1");
    record.state = QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("noc")},
        {QStringLiteral("global_parameters"), QJsonObject{{QStringLiteral("data_width"), 64}}}
    };
    return record;
}

IpCoreGraphExportRequest ipcraftExportRequest(const Graph& graph,
                                              const QString& packageId,
                                              const QString& instanceId = QStringLiteral("noc_0")) {
    return IpCoreGraphExportRequest{
        &graph,
        ipcraftCatalogEntry(packageId),
        instanceRecord(packageId, instanceId),
        QStringLiteral("design"),
        nullptr
    };
}

void setModuleParameter(Module* module, const QString& name, const QJsonValue& value) {
    if (value.isBool()) {
        module->setParameter(name, value.toBool());
        return;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        const int integer = value.toInt();
        if (number == integer) {
            module->setParameter(name, integer);
        } else {
            module->setParameter(name, number);
        }
        return;
    }
    module->setParameter(name, value.toString());
}

void addPackageScopedModule(Graph& graph,
                            const QString& runtimeId,
                            const QString& scopedTypeName,
                            const QString& packageId,
                            const QString& instanceId,
                            const QJsonObject& parameters) {
    ModuleType type;
    type.name = scopedTypeName;
    type.packageId = packageId;
    type.moduleId = scopedTypeName.section(QStringLiteral("::"), -1);
    type.ipcoreId = packageId;
    ModuleRegistry::instance().registerType(type);

    auto module = std::make_unique<Module>(runtimeId, scopedTypeName);
    module->setIpcoreId(packageId);
    module->setInstanceId(instanceId);
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        setModuleParameter(module.get(), it.key(), it.value());
    }
    require(graph.addModule(std::move(module)), "package-scoped module should add");
}

IpCoreGraphExportResult exportGraph(const Graph& graph,
                                    const QString& ipcoreId,
                                    const QString& instanceId = QStringLiteral("noc_0"),
                                    QHash<QString, QString>* externalToInternalIds = nullptr) {
    return IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
        &graph,
        catalogEntry(ipcoreId),
        instanceRecord(ipcoreId, instanceId),
        QStringLiteral("design"),
        externalToInternalIds
    });
}

bool hasInterface(const QJsonArray& interfaces,
                  const QString& instanceId,
                  const QString& interfaceId) {
    for (const QJsonValue& value : interfaces) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("instance")).toString() == instanceId &&
            object.value(QStringLiteral("interface")).toString() == interfaceId) {
            return true;
        }
    }
    return false;
}

void testExportsIpcraftNocProjectV1Schema() {
    Graph graph;
    const QString packageId = QStringLiteral("org.example.ravenoc");
    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            ipcraftCatalogEntry(packageId),
            instanceRecord(packageId, QStringLiteral("noc_0")),
            QStringLiteral("design"),
            nullptr
        });

    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();

    require(root.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("ipcraft.noc.project.v1"),
            "ipcraft package export should use the public NoC project schema");
    require(!root.value(QStringLiteral("schema")).toString().contains(QStringLiteral("finepaper")),
            "ipcraft command input schema should not use finepaper public names");
    require(root.value(QStringLiteral("package")).toString() == packageId,
            "ipcraft project export should include package id");
    require(root.value(QStringLiteral("instances")).isArray(),
            "ipcraft project export should include instances array");
    require(root.value(QStringLiteral("instances")).toArray().isEmpty(),
            "empty ipcraft project export should have no module instances");
    require(root.value(QStringLiteral("connections")).isArray(),
            "ipcraft project export should include connections array");
    require(root.value(QStringLiteral("connections")).toArray().isEmpty(),
            "empty ipcraft project export should have no connections");
}

void testIpcraftExportUsesManifestModuleIdsAndDisplayParameters() {
    Graph graph;
    addPackageScopedModule(graph,
                           QStringLiteral("runtime_uuid"),
                           QStringLiteral("org.example.noc::Tile"),
                           QStringLiteral("org.example.noc"),
                           QStringLiteral("noc_0"),
                           QJsonObject{{QStringLiteral("display_name"), QStringLiteral("Tile A")}});

    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(ipcraftExportRequest(graph, QStringLiteral("org.example.noc")));

    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject instance =
        result.document.object().value(QStringLiteral("instances")).toArray().first().toObject();
    require(instance.value(QStringLiteral("module")).toString() == QStringLiteral("Tile"),
            "export should use manifest module id");
    require(instance.value(QStringLiteral("module")).toString() != QStringLiteral("org.example.noc::Tile"),
            "export should not use the Qt package-scoped runtime type");
    require(instance.value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("display_name")).toString() == QStringLiteral("Tile A"),
            "export should include dynamic display parameter");
}

void testExportsInterfaceConnections() {
    const QString packageId = QStringLiteral("org.example.ravenoc");
    registerOwnedType(QStringLiteral("Tile"), packageId);
    Graph graph;
    auto source = makeModule(QStringLiteral("runtime_source"),
                             QStringLiteral("Tile"),
                             packageId,
                             QStringLiteral("noc_0"),
                             {Port(QStringLiteral("noc_port"),
                                   Port::Direction::InOut,
                                   QStringLiteral("bus"),
                                   QStringLiteral("NoC"),
                                   {},
                                   QStringLiteral("router"),
                                   QStringLiteral("noc_link"),
                                   QStringLiteral("noc"))});
    source->setParameter(QStringLiteral("external_id"), QStringLiteral("tile_a"));
    auto target = makeModule(QStringLiteral("runtime_target"),
                             QStringLiteral("Tile"),
                             packageId,
                             QStringLiteral("noc_0"),
                             {Port(QStringLiteral("noc_port"),
                                   Port::Direction::InOut,
                                   QStringLiteral("bus"),
                                   QStringLiteral("NoC"),
                                   {},
                                   QStringLiteral("router"),
                                   QStringLiteral("noc_link"),
                                   QStringLiteral("noc"))});
    target->setParameter(QStringLiteral("external_id"), QStringLiteral("tile_b"));
    require(graph.addModule(std::move(source)), "source should add");
    require(graph.addModule(std::move(target)), "target should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("runtime_connection"),
        PortRef{QStringLiteral("runtime_source"), QStringLiteral("noc_port")},
        PortRef{QStringLiteral("runtime_target"), QStringLiteral("noc_port")},
        QStringLiteral("noc_link"),
        QVector<ConnectionInterfaceRef>{
            {QStringLiteral("runtime_source"), QStringLiteral("noc")},
            {QStringLiteral("runtime_target"), QStringLiteral("noc")}
        },
        QStringLiteral("ambiguous"),
        QStringList{QStringLiteral("noc_link"), QStringLiteral("monitor_tap")}));

    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            ipcraftCatalogEntry(packageId),
            instanceRecord(packageId, QStringLiteral("noc_0")),
            QStringLiteral("design"),
            nullptr
        });

    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();
    const QJsonObject connection =
        root.value(QStringLiteral("connections")).toArray().first().toObject();
    const QJsonArray interfaces = connection.value(QStringLiteral("interfaces")).toArray();

    require(connection.value(QStringLiteral("class")).toString() == QStringLiteral("noc_link"),
            "ipcraft connection should export selected class");
    require(!connection.contains(QStringLiteral("source")) &&
                !connection.contains(QStringLiteral("target")),
            "ipcraft connection should use unordered interfaces instead of source/target");
    require(interfaces.size() == 2, "ipcraft connection should export both interfaces");
    require(hasInterface(interfaces, QStringLiteral("tile_a"), QStringLiteral("noc")),
            "ipcraft connection should export source interface by artifact instance id");
    require(hasInterface(interfaces, QStringLiteral("tile_b"), QStringLiteral("noc")),
            "ipcraft connection should export target interface by artifact instance id");
    require(connection.value(QStringLiteral("status")).toString() == QStringLiteral("ambiguous"),
            "ipcraft connection should export connection status");
    require(connection.value(QStringLiteral("alternatives")).toArray().size() == 2,
            "ipcraft connection should export class alternatives");
}

void testIpcraftExportIncludesConnectionBoundaryFieldsWhenEmpty() {
    const QString packageId = QStringLiteral("org.example.boundary");
    registerOwnedType(QStringLiteral("BoundaryTile"), packageId);
    Graph graph;
    auto source = makeModule(QStringLiteral("source_runtime"),
                             QStringLiteral("BoundaryTile"),
                             packageId,
                             QStringLiteral("noc_0"),
                             {Port(QStringLiteral("out"),
                                   Port::Direction::Output,
                                   QStringLiteral("bus"),
                                   QStringLiteral("Out"),
                                   {},
                                   {},
                                   {},
                                   QStringLiteral("egress"))});
    source->setParameter(QStringLiteral("external_id"), QStringLiteral("source_tile"));
    auto target = makeModule(QStringLiteral("target_runtime"),
                             QStringLiteral("BoundaryTile"),
                             packageId,
                             QStringLiteral("noc_0"),
                             {Port(QStringLiteral("in"),
                                   Port::Direction::Input,
                                   QStringLiteral("bus"),
                                   QStringLiteral("In"),
                                   {},
                                   {},
                                   {},
                                   QStringLiteral("ingress"))});
    target->setParameter(QStringLiteral("external_id"), QStringLiteral("target_tile"));
    require(graph.addModule(std::move(source)), "source should add");
    require(graph.addModule(std::move(target)), "target should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("runtime_connection"),
        PortRef{QStringLiteral("source_runtime"), QStringLiteral("out")},
        PortRef{QStringLiteral("target_runtime"), QStringLiteral("in")}));

    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(ipcraftExportRequest(graph, packageId));

    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject connection =
        result.document.object().value(QStringLiteral("connections")).toArray().first().toObject();
    const QJsonArray interfaces = connection.value(QStringLiteral("interfaces")).toArray();
    require(connection.contains(QStringLiteral("class")),
            "ipcraft connection should always include class");
    require(connection.contains(QStringLiteral("status")),
            "ipcraft connection should always include status");
    require(connection.contains(QStringLiteral("alternatives")),
            "ipcraft connection should always include alternatives");
    require(connection.value(QStringLiteral("status")).toString() == QStringLiteral("valid"),
            "ipcraft connection should normalize empty status to valid");
    require(connection.value(QStringLiteral("alternatives")).toArray().isEmpty(),
            "ipcraft connection should include an empty alternatives array when none are available");
    require(interfaces.size() == 2, "ipcraft connection should export normalized interface refs");
    require(hasInterface(interfaces, QStringLiteral("source_tile"), QStringLiteral("egress")),
            "ipcraft connection should export source interface by artifact instance id");
    require(hasInterface(interfaces, QStringLiteral("target_tile"), QStringLiteral("ingress")),
            "ipcraft connection should export target interface by artifact instance id");
    require(!connection.contains(QStringLiteral("source")) &&
                !connection.contains(QStringLiteral("target")),
            "ipcraft command input should not serialize legacy endpoint fields");
}

void testExportsManifestModuleIdForPackageScopedModuleTypes() {
    const QString packageId = QStringLiteral("org.example.ravenoc");
    const ModuleType type = registerPackageScopedType(packageId, QStringLiteral("Tile"));
    Graph graph;
    auto tile = makeModule(QStringLiteral("runtime_tile"),
                           type.name,
                           packageId,
                           QStringLiteral("noc_0"),
                           {Port(QStringLiteral("noc"),
                                 Port::Direction::InOut,
                                 QStringLiteral("bus"),
                                 QStringLiteral("NoC"))});
    tile->setParameter(QStringLiteral("external_id"), QStringLiteral("tile_0"));
    require(graph.addModule(std::move(tile)), "package-scoped module should add");

    const IpCoreGraphExportResult result =
        IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
            &graph,
            ipcraftCatalogEntry(packageId),
            instanceRecord(packageId, QStringLiteral("noc_0")),
            QStringLiteral("design"),
            nullptr
        });

    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject instance =
        result.document.object().value(QStringLiteral("instances")).toArray().first().toObject();
    require(instance.value(QStringLiteral("module")).toString() == QStringLiteral("Tile"),
            "ipcraft project export should use manifest module id, not Qt package-scoped type name");
    const QString text = QString::fromUtf8(result.document.toJson(QJsonDocument::Compact));
    require(!text.contains(type.name),
            "ipcraft command input should not leak Qt package-scoped module type names");
}

void testExportsIpcoreSchemaStateAndModuleOwner() {
    registerOwnedType(QStringLiteral("RaveTile"), QStringLiteral("finepaper.ravenoc"));
    Graph graph;
    auto tile = makeModule(
        QStringLiteral("tile_runtime"),
        QStringLiteral("RaveTile"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc_0"),
        {Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"))});
    tile->setParameter(QStringLiteral("external_id"), QStringLiteral("rave_0_0"));
    tile->setParameter(QStringLiteral("mesh_col"), 0);
    tile->setParameter(QStringLiteral("mesh_row"), 0);
    require(graph.addModule(std::move(tile)), "tile should add");

    const IpCoreGraphExportResult result = exportGraph(graph, QStringLiteral("finepaper.ravenoc"));
    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();

    require(root.value(QStringLiteral("schema")).toString() == QStringLiteral("finepaper-ipcore-graph-v1"),
            "export should use IP-core graph schema");
    require(root.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "export should name selected IP core");
    require(root.value(QStringLiteral("instance")).toString() == QStringLiteral("noc_0"),
            "export should name selected instance");
    require(root.value(QStringLiteral("ipcore_state")).toArray().size() == 1,
            "export should include selected IP-core state");

    const QJsonObject module = root.value(QStringLiteral("modules")).toArray().first().toObject();
    require(module.value(QStringLiteral("ipcore")).toString() == QStringLiteral("finepaper.ravenoc"),
            "module owner field should be ipcore");
    require(module.value(QStringLiteral("instance")).toString() == QStringLiteral("noc_0"),
            "module owner field should keep selected instance");
    require(!module.contains(legacyPluginOwnerKey()),
            "module owner field should not use plugin");
}

void testExporterUsesArtifactIdsAndMapping() {
    registerOwnedType(QStringLiteral("Source"), QStringLiteral("finepaper.noc"));
    registerOwnedType(QStringLiteral("Target"), QStringLiteral("finepaper.noc"));
    Graph graph;
    auto source = makeModule(QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"),
                             QStringLiteral("Source"),
                             QStringLiteral("finepaper.noc"),
                             QStringLiteral("noc_0"),
                             {Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Out"))});
    source->setParameter(QStringLiteral("external_id"), QStringLiteral("source_0"));
    auto target = makeModule(QStringLiteral("9ed21db3_a343_4420_afcb_d6b19cb997fe"),
                             QStringLiteral("Target"),
                             QStringLiteral("finepaper.noc"),
                             QStringLiteral("noc_0"),
                             {Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("In"))});
    target->setParameter(QStringLiteral("external_id"), QStringLiteral("target_0"));
    require(graph.addModule(std::move(source)), "source should add");
    require(graph.addModule(std::move(target)), "target should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("3c357093_4961_4ac7_8302_cad7f44f909d"),
        PortRef{QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"), QStringLiteral("out")},
        PortRef{QStringLiteral("9ed21db3_a343_4420_afcb_d6b19cb997fe"), QStringLiteral("in")}));

    QHash<QString, QString> externalToInternalIds;
    const IpCoreGraphExportResult result =
        exportGraph(graph,
                    QStringLiteral("finepaper.noc"),
                    QStringLiteral("noc_0"),
                    &externalToInternalIds);
    require(result.success, result.error.toLocal8Bit().constData());
    const QJsonObject root = result.document.object();
    const QJsonArray modules = root.value(QStringLiteral("modules")).toArray();
    const QJsonArray connections = root.value(QStringLiteral("connections")).toArray();

    require(modules.at(0).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("source_0"),
            "export should use source external_id");
    require(modules.at(1).toObject().value(QStringLiteral("id")).toString() == QStringLiteral("target_0"),
            "export should use target external_id");
    require(connections.first().toObject().value(QStringLiteral("id")).toString() ==
                QStringLiteral("source_0_out_to_target_0_in"),
            "export should generate readable connection id");
    require(externalToInternalIds.value(QStringLiteral("source_0")) ==
                QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4"),
            "export should map artifact source id to runtime source id");

    const QString text = QString::fromUtf8(result.document.toJson(QJsonDocument::Compact));
    require(!text.contains(QStringLiteral("0bf35d18_a3d3_4ce3_b89d_36e120b847b4")),
            "export should not leak runtime source UUID");
    require(!text.contains(QStringLiteral("9ed21db3_a343_4420_afcb_d6b19cb997fe")),
            "export should not leak runtime target UUID");
    require(!text.contains(QStringLiteral("3c357093_4961_4ac7_8302_cad7f44f909d")),
            "export should not leak runtime connection UUID");
}

void testExporterIgnoresModulesOutsideSelectedInstance() {
    registerOwnedType(QStringLiteral("ActiveTile"), QStringLiteral("finepaper.ravenoc"));
    registerOwnedType(QStringLiteral("OtherTile"), QStringLiteral("finepaper.other"));
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("active"),
                                       QStringLiteral("ActiveTile"),
                                       QStringLiteral("finepaper.ravenoc"),
                                       QStringLiteral("noc_0"),
                                       {Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"))})),
            "active module should add");
    require(graph.addModule(makeModule(QStringLiteral("other"),
                                       QStringLiteral("OtherTile"),
                                       QStringLiteral("finepaper.other"),
                                       QStringLiteral("other_0"),
                                       {Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"), QStringLiteral("East"))})),
            "other module should add");

    const IpCoreGraphExportResult result = exportGraph(graph, QStringLiteral("finepaper.ravenoc"));
    require(result.success, result.error.toLocal8Bit().constData());
    require(result.document.object().value(QStringLiteral("modules")).toArray().size() == 1,
            "export should keep only modules from the selected instance scope");
}

void testExporterRejectsCrossInstanceConnectionTouchingSelectedInstance() {
    registerOwnedType(QStringLiteral("ScopedTile"), QStringLiteral("finepaper.noc"));
    Graph graph;
    require(graph.addModule(makeModule(QStringLiteral("active"),
                                       QStringLiteral("ScopedTile"),
                                       QStringLiteral("finepaper.noc"),
                                       QStringLiteral("noc_0"),
                                       {Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"), QStringLiteral("Out"))})),
            "active module should add");
    require(graph.addModule(makeModule(QStringLiteral("other"),
                                       QStringLiteral("ScopedTile"),
                                       QStringLiteral("finepaper.noc"),
                                       QStringLiteral("noc_1"),
                                       {Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"), QStringLiteral("In"))})),
            "other instance module should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("cross_instance"),
        PortRef{QStringLiteral("active"), QStringLiteral("out")},
        PortRef{QStringLiteral("other"), QStringLiteral("in")}));

    const IpCoreGraphExportResult result =
        exportGraph(graph, QStringLiteral("finepaper.noc"), QStringLiteral("noc_0"));
    require(!result.success, "cross-instance connection touching selected instance should be rejected");
    require(result.error.contains(QStringLiteral("noc_0")),
            "cross-instance connection error should mention selected instance id");
}

void testExporterRejectsMismatchedInstance() {
    Graph graph;
    const IpCoreGraphExportResult result = IpCoreGraphExporter::exportGraph(IpCoreGraphExportRequest{
        &graph,
        catalogEntry(QStringLiteral("finepaper.ravenoc")),
        instanceRecord(QStringLiteral("finepaper.noc"), QStringLiteral("noc_0")),
        QStringLiteral("design"),
        nullptr
    });
    require(!result.success, "mismatched instance should reject");
    require(result.error.contains(QStringLiteral("finepaper.ravenoc")),
            "mismatch error should mention requested IP core");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testExportsIpcraftNocProjectV1Schema();
        testIpcraftExportUsesManifestModuleIdsAndDisplayParameters();
        testExportsInterfaceConnections();
        testIpcraftExportIncludesConnectionBoundaryFieldsWhenEmpty();
        testExportsManifestModuleIdForPackageScopedModuleTypes();
        testExportsIpcoreSchemaStateAndModuleOwner();
        testExporterUsesArtifactIdsAndMapping();
        testExporterIgnoresModulesOutsideSelectedInstance();
        testExporterRejectsCrossInstanceConnectionTouchingSelectedInstance();
        testExporterRejectsMismatchedInstance();
    } catch (const std::exception& error) {
        std::cerr << "ipcoregraphexporter_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "ipcoregraphexporter_test passed\n";
    return 0;
}
