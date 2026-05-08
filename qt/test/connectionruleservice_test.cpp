// ConnectionRuleService tests for v1 editor-time connection decisions.
#include "connection/connectionruleservice.h"
#include "graph/graph.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<Module> makeProducer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Producer"));
    module->addPort(Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("Out"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

std::unique_ptr<Module> makeConsumer(const QString& id) {
    auto module = std::make_unique<Module>(id, QStringLiteral("Consumer"));
    module->addPort(Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("In"), {}, {}, QStringLiteral("demo_bus"), {}));
    return module;
}

void testAllowsSimplePortToPortConnection() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("out")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Allowed,
            "matching output/input bus ports should be allowed");
    require(result.options.size() == 1, "simple port-to-port should produce one option");
    require(result.options.first().source.moduleId == QStringLiteral("producer"),
            "source module should be producer");
    require(result.options.first().target.moduleId == QStringLiteral("consumer"),
            "target module should be consumer");
}

void testRejectsMissingPortWithReason() {
    Graph graph;
    require(graph.addModule(makeProducer(QStringLiteral("producer"))), "failed to add producer");
    require(graph.addModule(makeConsumer(QStringLiteral("consumer"))), "failed to add consumer");

    ConnectionRuleService service(&graph, {});
    const ConnectionCheckResult result = service.check(
        ConnectionRequest::portToPort(PortRef{QStringLiteral("producer"), QStringLiteral("missing")},
                                      PortRef{QStringLiteral("consumer"), QStringLiteral("in")},
                                      ConnectionRequestKind::Programmatic));

    require(result.status == ConnectionCheckStatus::Rejected,
            "missing port should reject");
    require(result.reasonCode == QStringLiteral("missing_port"),
            "missing port rejection should have reason code");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testAllowsSimplePortToPortConnection();
        testRejectsMissingPortWithReason();
    } catch (const std::exception& error) {
        std::cerr << "connectionruleservice_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "connectionruleservice_test passed\n";
    return 0;
}
