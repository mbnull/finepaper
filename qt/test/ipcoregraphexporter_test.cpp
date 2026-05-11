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
    entry.name = ipcoreId;
    entry.version = QStringLiteral("1.0");
    entry.kind = QStringLiteral("noc");
    entry.generator.command = QStringLiteral("ruby");
    entry.generator.inputFormat = QStringLiteral("ipcore_graph_v1");
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
