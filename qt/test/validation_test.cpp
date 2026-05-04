// BasicValidator and plugin DRC integration tests.
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "validation/drcrunner.h"
#include "validation/validator.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

std::unique_ptr<Module> makeEndpoint(const QString& id) {
    auto module = std::make_unique<Module>(id, "Endpoint");
    module->addPort(Port("noc", Port::Direction::Output, "bus", "NoC", {}, "attachment", "ni_link", "noc"));
    return module;
}

std::unique_ptr<Module> makeRaveTile(const QString& id, int x, int y) {
    auto module = std::make_unique<Module>(id, "RaveTile");
    module->setParameter(QStringLiteral("x"), x);
    module->setParameter(QStringLiteral("y"), y);
    return module;
}

std::unique_ptr<Module> makeManualRaveTile(const QString& id, int x, int y) {
    auto module = makeRaveTile(id, x, y);
    module->setParameter(QStringLiteral("mesh_col"), 0);
    module->setParameter(QStringLiteral("mesh_row"), 0);
    return module;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasRule(const QList<ValidationResult>& results, const QString& ruleName) {
    for (const auto& result : results) {
        if (result.ruleName() == ruleName) {
            return true;
        }
    }

    return false;
}

void testBasicValidatorLeavesIpDrcToPluginCommand() {
    Graph graph;
    require(graph.addModule(makeEndpoint("endpoint")), "failed to add endpoint");

    BasicValidator validator;
    const QList<ValidationResult> results = validator.validate(&graph);

    require(!hasRule(results, QStringLiteral("unconnected_port")),
            "unconnected port warnings should come from IP DRC scripts");
    require(!hasRule(results, QStringLiteral("isolated_xp")),
            "IP topology rules should come from IP DRC scripts");
}

void testDrcRunnerUsesPluginGraphFlavorForRaveNoC() {
    require(ModuleRegistry::instance().getType(QStringLiteral("RaveTile")) != nullptr,
            "RaveTile type must be registered for DRC flavor test");

    Graph graph;
    require(graph.addModule(makeRaveTile(QStringLiteral("rave_0_0"), 0, 0)),
            "failed to add first RaveTile");
    require(graph.addModule(makeRaveTile(QStringLiteral("rave_0_1"), 1, 0)),
            "failed to add second RaveTile");

    DRCRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph);
    QStringList messages;
    for (const ValidationResult& result : results) {
        messages.append(result.message());
    }

    const QByteArray messageBytes = messages.join('\n').toLocal8Bit();
    require(results.isEmpty(), messageBytes.constData());
    require(!messages.join('\n').contains(QStringLiteral("expected schema finepaper-plugin-graph-v1")),
            "RaveNoC DRC should receive generic plugin graph JSON");
}

void testDrcRunnerAcceptsManualRaveTilePlacement() {
    require(ModuleRegistry::instance().getType(QStringLiteral("RaveTile")) != nullptr,
            "RaveTile type must be registered for manual placement DRC test");

    Graph graph;
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_a"), 100, 80)),
            "failed to add first manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_b"), 320, 80)),
            "failed to add second manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_c"), 100, 248)),
            "failed to add third manual RaveTile");
    require(graph.addModule(makeManualRaveTile(QStringLiteral("rave_d"), 320, 248)),
            "failed to add fourth manual RaveTile");

    DRCRunner runner;
    const QList<ValidationResult> results = runner.validate(&graph);
    QStringList messages;
    for (const ValidationResult& result : results) {
        messages.append(result.message());
    }

    const QByteArray messageBytes = messages.join('\n').toLocal8Bit();
    require(results.isEmpty(), messageBytes.constData());
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testBasicValidatorLeavesIpDrcToPluginCommand();
        testDrcRunnerUsesPluginGraphFlavorForRaveNoC();
        testDrcRunnerAcceptsManualRaveTilePlacement();
    } catch (const std::exception& error) {
        std::cerr << "validation_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "validation_test passed\n";
    return 0;
}
