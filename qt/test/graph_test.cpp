// Graph integration-style tests for JSON import/export and topology behavior.
#include "graph/graph.h"
#include "common/frameworkpaths.h"
#include "modules/moduleregistry.h"
#include "modules/moduleprovider.h"
#include "common/portlayout.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

std::unique_ptr<Module> makeModule(const QString& id,
                                   const QString& type,
                                   std::initializer_list<Port> ports) {
    auto module = std::make_unique<Module>(id, type);
    for (const auto& port : ports) {
        module->addPort(port);
    }
    return module;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char* name)
        : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)) {
        if (m_wasSet) {
            m_value = qgetenv(name);
        }
    }

    ~ScopedEnvironmentVariable() {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_value);
            return;
        }

        qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    QByteArray m_value;
    bool m_wasSet = false;
};

void testConnectionValidationPreventsPortReuse() {
    Graph graph;

    require(graph.addModule(makeModule(
        "source",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add source module");
    require(graph.addModule(makeModule(
        "target_a",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add first target module");
    require(graph.addModule(makeModule(
        "target_b",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add second target module");

    const PortRef source{"source", "out"};
    const PortRef targetA{"target_a", "in"};
    const PortRef targetB{"target_b", "in"};

    require(graph.isValidConnection(source, targetA), "expected first connection to be valid");

    graph.addConnection(std::make_unique<Connection>("c1", source, targetA));

    require(graph.connections().size() == 1, "expected connection to be stored");
    require(!graph.isValidConnection(source, targetB), "source port should not be reusable");
    require(!graph.isValidConnection(source, targetA), "duplicate connection should be rejected");
}

void testInoutBusConnectionsAreValid() {
    Graph graph;

    require(graph.addModule(makeModule(
        "router_a",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add first inout bus module");
    require(graph.addModule(makeModule(
        "router_b",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add second inout bus module");

    const PortRef source{"router_a", "bus"};
    const PortRef target{"router_b", "bus"};

    require(graph.isValidConnection(source, target), "matching inout bus ports should connect");

    graph.addConnection(std::make_unique<Connection>("bus_link", source, target));
    require(graph.connections().size() == 1, "expected inout bus connection to be stored");
}

void testInoutPortsCannotBeReusedAcrossConnectionSides() {
    Graph graph;

    require(graph.addModule(makeModule(
        "router_a",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add first inout module");
    require(graph.addModule(makeModule(
        "router_b",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add second inout module");
    require(graph.addModule(makeModule(
        "router_c",
        "Endpoint",
        {Port("bus", Port::Direction::InOut, "bus", "BUS", {}, {}, "router")})),
        "failed to add third inout module");

    const PortRef source{"router_a", "bus"};
    const PortRef middle{"router_b", "bus"};
    const PortRef target{"router_c", "bus"};

    require(graph.isValidConnection(source, middle), "expected first inout connection to be valid");
    graph.addConnection(std::make_unique<Connection>("bus_link_ab", source, middle));

    require(graph.connections().size() == 1, "expected initial inout connection to be stored");
    require(!graph.isValidConnection(middle, target),
            "inout port already used as a target should not be reusable as a source");
    require(!graph.isValidConnection(target, middle),
            "inout port already used as a target should not be reusable as a target again");
}

void testRemovingModuleAlsoRemovesAttachedConnections() {
    Graph graph;

    require(graph.addModule(makeModule(
        "producer",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add producer module");
    require(graph.addModule(makeModule(
        "consumer",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add consumer module");

    graph.addConnection(std::make_unique<Connection>(
        "producer_consumer",
        PortRef{"producer", "out"},
        PortRef{"consumer", "in"}));

    require(graph.connections().size() == 1, "expected connection before module removal");

    graph.removeModule("producer");

    require(graph.getModule("producer") == nullptr, "removed module should no longer exist");
    require(graph.connections().empty(), "attached connections should be removed with the module");
}

void testClearRemovesAllModulesAndConnections() {
    Graph graph;

    require(graph.addModule(makeModule(
        "producer",
        "Endpoint",
        {Port("out", Port::Direction::Output, "endpoint", "out")})),
        "failed to add producer module");
    require(graph.addModule(makeModule(
        "consumer",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")})),
        "failed to add consumer module");

    graph.addConnection(std::make_unique<Connection>(
        "producer_consumer",
        PortRef{"producer", "out"},
        PortRef{"consumer", "in"}));

    require(graph.connections().size() == 1, "expected connection before clear");

    graph.clear();

    require(graph.modules().empty(), "clear should remove every module");
    require(graph.connections().empty(), "clear should remove every connection");
}

void testGraphForwardsModuleParameterChanges() {
    Graph graph;
    QString changedModuleId;
    QString changedParameterName;
    int signalCount = 0;

    QObject::connect(&graph, &Graph::parameterChanged, &graph,
        [&](const QString& moduleId, const QString& paramName) {
            ++signalCount;
            changedModuleId = moduleId;
            changedParameterName = paramName;
        });

    auto module = makeModule(
        "configurable",
        "Endpoint",
        {Port("in", Port::Direction::Input, "endpoint", "in")});
    Module* modulePtr = module.get();

    require(graph.addModule(std::move(module)), "failed to add configurable module");

    modulePtr->setParameter("buffer_depth", 8);

    require(signalCount == 1, "expected exactly one forwarded parameterChanged signal");
    require(changedModuleId == "configurable", "forwarded signal should include module id");
    require(changedParameterName == "buffer_depth", "forwarded signal should include parameter name");
}

void testLegacyEndpointTypeStillClassifiesAsEndpointPort() {
    const Port legacyEndpointPort("noc", Port::Direction::Input, "endpoint", "NoC");
    require(PortLayout::isEndpointPort(legacyEndpointPort),
            "legacy endpoint type should classify as endpoint port");
}

void testBundleMetadataLoadsFromXml() {
    const ModuleType* xpType = ModuleRegistry::instance().getType("XP");
    require(xpType != nullptr, "XP type should be registered");
    require(xpType->description.contains("Mesh router"), "XP description should come from bundle XML");
    require(xpType->nodeColor == "#7cb9e8", "XP node color should come from bundle XML");
    require(xpType->editorLayout == "mesh_router", "XP layout should come from bundle XML");
    require(xpType->supportsCollapse, "XP collapse capability should come from bundle XML");
    require(xpType->expandedNodeHeight == 116, "XP expanded height should come from bundle XML");
    require(xpType->configFields.size() == 5, "XP config zone should be generated from configurable parameters");
    require(xpType->configFields.first().description.contains("canvas"),
            "XP parameter descriptions should be preserved in config fields");

    const ModuleType* endpointType = ModuleRegistry::instance().getType("Endpoint");
    require(endpointType != nullptr, "Endpoint type should be registered");
    require(endpointType->description.contains("Endpoint interface"),
            "Endpoint description should come from bundle XML");
    require(endpointType->nodeColor == "#d6f4b6", "Endpoint node color should come from bundle XML");
    require(endpointType->configFields.size() == 7,
            "Endpoint config zone should be generated from configurable parameters");
}

void testXmlBundleWithoutGraphicsFallsBackToSimpleNode() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for xml fallback bundle test");

    const QString bundlePath = QDir(tempDir.path()).filePath("modules.xml");
    QFile bundleFile(bundlePath);
    require(bundleFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to create XML fallback bundle");
    bundleFile.write(R"XML(<?xml version="1.0" encoding="UTF-8"?>
<module-bundle>
  <module name="DMA" palette_label="DMA" description="Generic DMA block.">
    <ports>
      <port id="cfg" direction="input" type="config" name="CFG" description="left configuration port" />
      <port id="irq" direction="output" type="interrupt" name="IRQ" description="right interrupt output" />
      <port id="trace" direction="output" type="debug" name="TRACE" description="top trace port" />
    </ports>
    <parameters>
      <parameter name="x" type="int" default="0" configurable="false" />
      <parameter name="mode" type="string" default="linear" label="Mode" description="DMA transfer mode." />
    </parameters>
  </module>
</module-bundle>)XML");
    bundleFile.close();

    XmlModuleTypeSource source(bundlePath);
    const QHash<QString, ModuleType> types = source.loadModuleTypes();
    auto dmaIt = types.find("DMA");
    require(dmaIt != types.end(), "DMA type should load from XML bundle");
    require(dmaIt->editorLayout == "fallback",
            "modules without explicit graphics should use fallback layout");
    require(dmaIt->configFields.size() == 1,
            "fallback XML bundle should auto-generate config fields from configurable parameters");
    require(dmaIt->defaultPorts.size() == 3, "port descriptions should load from XML bundle");
    require(dmaIt->defaultPorts[2].description().contains("top"),
            "port description should be preserved for fallback layout parsing");
}

void testXmlBundleLoadsExtendedParameterMetadataWhenPresent() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for parameter metadata bundle test");

    const QString bundlePath = QDir(tempDir.path()).filePath("modules.xml");
    QFile bundleFile(bundlePath);
    require(bundleFile.open(QIODevice::WriteOnly | QIODevice::Text),
            "failed to create XML metadata bundle");
    bundleFile.write(R"XML(<?xml version="1.0" encoding="UTF-8"?>
<module-bundle>
  <module name="Router" palette_label="Router" graph_group="xps" description="Router test bundle.">
    <parameters>
      <parameter name="routing_algorithm" type="string" default="xy" label="Routing algorithm">
        <choices>
          <choice value="xy" label="XY" />
          <choice value="odd_even" label="Odd-Even" />
        </choices>
      </parameter>
      <parameter name="vc_count" type="int" default="2" min="1" max="8" unit="VCs"
                 label="VC count" description="Virtual channel count." />
      <parameter name="external_id" type="string" default="" read_only="true"
                 label="External ID" description="Framework-facing ID." />
    </parameters>
  </module>
</module-bundle>)XML");
    bundleFile.close();

    XmlModuleTypeSource source(bundlePath);
    const QHash<QString, ModuleType> types = source.loadModuleTypes();
    auto routerIt = types.find("Router");
    require(routerIt != types.end(), "Router type should load from XML bundle");
    require(routerIt->parameterMetadata.contains("routing_algorithm"),
            "routing_algorithm metadata should be available");
    require(routerIt->parameterMetadata.value("routing_algorithm").choices.size() == 2,
            "routing_algorithm should expose two editor choices");
    require(routerIt->parameterMetadata.value("vc_count").minimumValue.value_or(0.0) == 1.0,
            "vc_count minimum should load from XML metadata");
    require(routerIt->parameterMetadata.value("vc_count").maximumValue.value_or(0.0) == 8.0,
            "vc_count maximum should load from XML metadata");
    require(routerIt->parameterMetadata.value("vc_count").unit == "VCs",
            "vc_count unit should load from XML metadata");
    require(routerIt->parameterMetadata.value("external_id").readOnly,
            "external_id read_only flag should load from XML metadata");
}

void testExplicitBundlePathWithoutSidecarDoesNotFallbackPresentation() {
    ScopedEnvironmentVariable bundlePathGuard("BUNDLE_PATH");
    ScopedEnvironmentVariable bundleUiPathGuard("BUNDLE_UI_PATH");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory for bundle path test");

    const QString bundlePath = QDir(tempDir.path()).filePath("modules.json");
    QFile bundleFile(bundlePath);
    require(bundleFile.open(QIODevice::WriteOnly), "failed to create bundle file");
    bundleFile.write("{}");
    bundleFile.close();

    qputenv("BUNDLE_PATH", bundlePath.toUtf8());
    qunsetenv("BUNDLE_UI_PATH");

    require(FrameworkPaths::resolveModuleBundlePath() == QFileInfo(bundlePath).absoluteFilePath(),
            "explicit bundle path should still resolve the selected modules.json");
    require(FrameworkPaths::resolveModulePresentationPath().isEmpty(),
            "presentation path should not fall back outside the explicit bundle");
}

void testFrameworkExportOmitsEditorOnlyCollapsedField() {
    Graph graph;

    auto xp = makeModule(
        "xp_internal",
        "XP",
        {Port("ep0", Port::Direction::Output, "endpoint", "EP0")});
    xp->setParameter("external_id", QString("xp_0_0"));
    xp->setParameter("x", 0);
    xp->setParameter("y", 0);
    xp->setParameter("collapsed", true);
    xp->setParameter("routing_algorithm", QString("xy"));

    auto endpoint = makeModule(
        "ep_internal",
        "Endpoint",
        {Port("noc", Port::Direction::Input, "endpoint", "NoC")});
    endpoint->setParameter("external_id", QString("ep_0"));
    endpoint->setParameter("type", QString("master"));
    endpoint->setParameter("protocol", QString("axi4"));
    endpoint->setParameter("data_width", 64);

    require(graph.addModule(std::move(xp)), "failed to add XP module");
    require(graph.addModule(std::move(endpoint)), "failed to add endpoint module");

    graph.addConnection(std::make_unique<Connection>(
        "xp_ep",
        PortRef{"xp_internal", "ep0"},
        PortRef{"ep_internal", "noc"}));

    require(graph.connections().size() == 1, "expected endpoint connection to be stored");

    const QJsonObject frameworkRoot =
        graph.toJsonDocument("design", GraphJsonFlavor::Framework).object();
    const QJsonObject editorRoot =
        graph.toJsonDocument("design", GraphJsonFlavor::Editor).object();

    const QJsonObject frameworkConfig =
        frameworkRoot["xps"].toArray().first().toObject()["config"].toObject();
    const QJsonObject editorConfig =
        editorRoot["xps"].toArray().first().toObject()["config"].toObject();

    require(!frameworkConfig.contains("collapsed"),
            "framework export should omit editor-only collapsed state");
    require(editorConfig.contains("collapsed"),
            "editor export should preserve collapsed state");
}

void testXmlExportPreservesEditorGraphContent() {
    Graph graph;

    auto xp = makeModule(
        "xp_internal",
        "XP",
        {Port("ep0", Port::Direction::Output, "endpoint", "EP0")});
    xp->setParameter("external_id", QString("xp_0_0"));
    xp->setParameter("x", 12);
    xp->setParameter("y", 34);
    xp->setParameter("collapsed", true);

    auto endpoint = makeModule(
        "ep_internal",
        "Endpoint",
        {Port("noc", Port::Direction::Input, "endpoint", "NoC")});
    endpoint->setParameter("external_id", QString("ep_0"));
    endpoint->setParameter("type", QString("master"));
    endpoint->setParameter("protocol", QString("axi4"));

    require(graph.addModule(std::move(xp)), "failed to add XP module for XML export");
    require(graph.addModule(std::move(endpoint)), "failed to add endpoint module for XML export");

    graph.addConnection(std::make_unique<Connection>(
        "xp_ep",
        PortRef{"xp_internal", "ep0"},
        PortRef{"ep_internal", "noc"}));

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "failed to create temporary directory");

    const QString xmlPath = QDir(tempDir.path()).filePath("design.xml");
    require(graph.saveToXml(xmlPath), "failed to save graph XML");

    QFile file(xmlPath);
    require(file.open(QIODevice::ReadOnly), "failed to reopen graph XML");
    const QString xml = QString::fromUtf8(file.readAll());

    require(xml.contains("<graph>"), "XML export should contain graph root");
    require(xml.contains("<name type=\"string\">design</name>"),
            "XML export should contain design name");
    require(xml.contains("<id type=\"string\">xp_0_0</id>"),
            "XML export should contain XP external id");
    require(xml.contains("<collapsed type=\"bool\">true</collapsed>"),
            "XML export should preserve editor-only collapsed flag");
    require(xml.contains("<id type=\"string\">ep_0</id>"),
            "XML export should contain endpoint external id");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testExplicitBundlePathWithoutSidecarDoesNotFallbackPresentation();
        testConnectionValidationPreventsPortReuse();
        testInoutBusConnectionsAreValid();
        testInoutPortsCannotBeReusedAcrossConnectionSides();
        testRemovingModuleAlsoRemovesAttachedConnections();
        testClearRemovesAllModulesAndConnections();
        testGraphForwardsModuleParameterChanges();
        testLegacyEndpointTypeStillClassifiesAsEndpointPort();
        testBundleMetadataLoadsFromXml();
        testXmlBundleWithoutGraphicsFallsBackToSimpleNode();
        testXmlBundleLoadsExtendedParameterMetadataWhenPresent();
        testFrameworkExportOmitsEditorOnlyCollapsedField();
        testXmlExportPreservesEditorGraphContent();
    } catch (const std::exception& error) {
        std::cerr << "graph_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "graph_test passed\n";
    return 0;
}
