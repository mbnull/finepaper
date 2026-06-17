// ArrangeCommand tests for connection-derived mesh placement.
#include "legacy/graphcommands/arrangecommand.h"
#include "graph/graph.h"
#include "modules/moduleregistry.h"
#include "legacy/graphcommands/editormutationtarget.h"

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

class FailingEditorMutationTarget final : public EditorMutationTarget {
public:
    bool failModuleUpsert = false;

    bool upsertEditorModuleRecord(const Module&) override { return !failModuleUpsert; }
    bool removeEditorModuleRecord(const QString&) override { return true; }
    bool upsertEditorConnectionRecord(const Connection&) override { return true; }
    bool removeEditorConnectionRecord(const QString&) override { return true; }
};

int intParameter(const Module* module, const QString& name) {
    require(module != nullptr, "module should exist");
    const auto it = module->parameters().find(name);
    require(it != module->parameters().end(), "int parameter should exist");
    const Parameter::Value parameterValue = it.value().value();
    const auto* value = std::get_if<int>(&parameterValue);
    require(value != nullptr, "parameter should be an int");
    return *value;
}

void registerArrangeRouterType() {
    ModuleType type;
    type.name = QStringLiteral("ArrangeXP");
    type.editorLayout = QStringLiteral("mesh_router");
    type.graphGroup = QStringLiteral("xps");
    type.externalIdPrefix = QStringLiteral("xp");
    type.supportsMeshCoordinates = true;
    type.supportsCollapse = true;
    type.meshSpacingX = 220;
    type.meshSpacingY = 168;
    type.defaultPorts = {
        Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("North"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("north")),
        Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("East"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("east")),
        Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("South"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("south")),
        Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
             QStringLiteral("West"), {}, QStringLiteral("router"),
             QStringLiteral("router_link"), QStringLiteral("west"))
    };

    ModuleRegistry::instance().registerType(type);
}

std::unique_ptr<Module> makeRouter(const QString& id,
                                   const QString& externalId,
                                   int x,
                                   int y) {
    auto module = std::make_unique<Module>(id, QStringLiteral("ArrangeXP"));
    module->addPort(Port(QStringLiteral("north"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("North"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("north")));
    module->addPort(Port(QStringLiteral("east"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("East"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("east")));
    module->addPort(Port(QStringLiteral("south"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("South"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("south")));
    module->addPort(Port(QStringLiteral("west"), Port::Direction::InOut, QStringLiteral("bus"),
                         QStringLiteral("West"), {}, QStringLiteral("router"),
                         QStringLiteral("router_link"), QStringLiteral("west")));
    module->setParameter(QStringLiteral("external_id"), externalId);
    module->setParameter(QStringLiteral("x"), x);
    module->setParameter(QStringLiteral("y"), y);
    return module;
}

void testArrangeUsesRouterConnectionsInsteadOfExternalIdCoordinates() {
    registerArrangeRouterType();

    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left_runtime"),
                                       QStringLiteral("xp_0_1"),
                                       220,
                                       0)),
            "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right_runtime"),
                                       QStringLiteral("xp_0_0"),
                                       0,
                                       0)),
            "failed to add right router");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("left_to_right"),
        PortRef{QStringLiteral("left_runtime"), QStringLiteral("east")},
        PortRef{QStringLiteral("right_runtime"), QStringLiteral("west")}));

    ArrangeCommand command(&graph);
    command.execute();

    require(intParameter(graph.getModule(QStringLiteral("left_runtime")), QStringLiteral("x")) <
                intParameter(graph.getModule(QStringLiteral("right_runtime")), QStringLiteral("x")),
            "east/west connection should place the target router to the right even when external ids disagree");
}

void testArrangeRejectsDurableUpsertFailureWithoutGraphDivergence() {
    registerArrangeRouterType();

    Graph graph;
    require(graph.addModule(makeRouter(QStringLiteral("left_runtime"),
                                       QStringLiteral("xp_0_1"),
                                       220,
                                       0)),
            "failed to add left router");
    require(graph.addModule(makeRouter(QStringLiteral("right_runtime"),
                                       QStringLiteral("xp_0_0"),
                                       0,
                                       0)),
            "failed to add right router");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("left_to_right"),
        PortRef{QStringLiteral("left_runtime"), QStringLiteral("east")},
        PortRef{QStringLiteral("right_runtime"), QStringLiteral("west")}));

    FailingEditorMutationTarget target;
    target.failModuleUpsert = true;
    ArrangeCommand command(&graph, &target);
    command.execute();

    require(!command.wasExecuted(),
            "durable module upsert failure should reject arrange command");
    require(intParameter(graph.getModule(QStringLiteral("left_runtime")), QStringLiteral("x")) == 220,
            "durable module upsert failure should keep left router x unchanged");
    require(intParameter(graph.getModule(QStringLiteral("right_runtime")), QStringLiteral("x")) == 0,
            "durable module upsert failure should keep right router x unchanged");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testArrangeUsesRouterConnectionsInsteadOfExternalIdCoordinates();
        testArrangeRejectsDurableUpsertFailureWithoutGraphDivergence();
    } catch (const std::exception& error) {
        std::cerr << "arrangecommand_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "arrangecommand_test passed\n";
    return 0;
}
