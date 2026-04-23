// BasicValidator tests for topology-level validation rules.
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "validation/validator.h"

#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

std::unique_ptr<Module> makeXp(const QString& id) {
    auto module = std::make_unique<Module>(id, "XP");
    module->addPort(Port("out", Port::Direction::Output, "bus", "OUT", {}, "router", "router"));
    module->addPort(Port("in", Port::Direction::Input, "bus", "IN", {}, "router", "router"));
    return module;
}

std::unique_ptr<Module> makeEndpoint(const QString& id) {
    auto module = std::make_unique<Module>(id, "Endpoint");
    module->addPort(Port("noc", Port::Direction::Input, "bus", "NoC", {}, "attachment", "ni2router"));
    return module;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasIsolatedXpError(const QList<ValidationResult>& results, const QString& elementId) {
    for (const auto& result : results) {
        if (result.severity() == ValidationSeverity::Error &&
            result.ruleName() == "isolated_xp" &&
            result.elementId() == elementId) {
            return true;
        }
    }

    return false;
}

void testSingleStandaloneXpIsAllowed() {
    Graph graph;
    require(graph.addModule(makeXp("xp_only")), "failed to add standalone XP");

    BasicValidator validator;
    const QList<ValidationResult> results = validator.validate(&graph);

    require(!hasIsolatedXpError(results, "xp_only"),
            "single XP as the only graph module should be allowed");
}

void testIsolatedXpInLargerGraphIsRejected() {
    Graph graph;
    require(graph.addModule(makeXp("xp_a")), "failed to add first XP");
    require(graph.addModule(makeXp("xp_b")), "failed to add second XP");
    require(graph.addModule(makeXp("xp_lonely")), "failed to add isolated XP");

    graph.addConnection(std::make_unique<Connection>(
        "xp_a_to_xp_b",
        PortRef{"xp_a", "out"},
        PortRef{"xp_b", "in"}));

    BasicValidator validator;
    const QList<ValidationResult> results = validator.validate(&graph);

    require(hasIsolatedXpError(results, "xp_lonely"),
            "XP without any connection should be an error when other graph modules exist");
    require(!hasIsolatedXpError(results, "xp_a"),
            "connected XP should not be reported as isolated");
    require(!hasIsolatedXpError(results, "xp_b"),
            "connected XP should not be reported as isolated");
}

void testSingleXpWithOtherModuleIsRejectedWhenUnconnected() {
    Graph graph;
    require(graph.addModule(makeXp("xp_lonely")), "failed to add XP");
    require(graph.addModule(makeEndpoint("endpoint")), "failed to add endpoint");

    BasicValidator validator;
    const QList<ValidationResult> results = validator.validate(&graph);

    require(hasIsolatedXpError(results, "xp_lonely"),
            "unconnected XP should be rejected unless it is the only graph module");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        require(ModuleRegistry::instance().getType("XP") != nullptr,
                "XP type must be registered for validation tests");
        testSingleStandaloneXpIsAllowed();
        testIsolatedXpInLargerGraphIsRejected();
        testSingleXpWithOtherModuleIsRejectedWhenUnconnected();
    } catch (const std::exception& error) {
        std::cerr << "validation_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "validation_test passed\n";
    return 0;
}
