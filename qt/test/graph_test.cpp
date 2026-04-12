#include "graph.h"
#include "frameworkpaths.h"
#include "moduleregistry.h"

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

void testBundlePresentationMetadataLoadsFromXml() {
    const ModuleType* xpType = ModuleRegistry::instance().getType("XP");
    require(xpType != nullptr, "XP type should be registered");
    require(xpType->nodeColor == "#7cb9e8", "XP node color should come from presentation XML");
    require(xpType->editorLayout == "mesh_router", "XP layout should come from presentation XML");
    require(xpType->supportsCollapse, "XP collapse capability should come from presentation XML");
    require(xpType->expandedNodeHeight == 116, "XP expanded height should come from presentation XML");
    require(xpType->configFields.size() == 5, "XP config zone should be defined in presentation XML");

    const ModuleType* endpointType = ModuleRegistry::instance().getType("Endpoint");
    require(endpointType != nullptr, "Endpoint type should be registered");
    require(endpointType->nodeColor == "#d6f4b6", "Endpoint node color should come from presentation XML");
    require(endpointType->configFields.size() == 7, "Endpoint config zone should be defined in presentation XML");
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
        testRemovingModuleAlsoRemovesAttachedConnections();
        testGraphForwardsModuleParameterChanges();
        testBundlePresentationMetadataLoadsFromXml();
        testFrameworkExportOmitsEditorOnlyCollapsedField();
        testXmlExportPreservesEditorGraphContent();
    } catch (const std::exception& error) {
        std::cerr << "graph_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "graph_test passed\n";
    return 0;
}
