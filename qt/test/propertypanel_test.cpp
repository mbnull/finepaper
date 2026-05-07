// Property panel tests for IP instance parameter presentation.
#include "commands/commandmanager.h"
#include "graph/graph.h"
#include "panels/propertypanel.h"

#include <QApplication>
#include <QLabel>
#include <QSpinBox>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasLabel(PropertyPanel& panel, const QString& text) {
    const QList<QLabel*> labels = panel.findChildren<QLabel*>();
    for (const QLabel* label : labels) {
        if (label->text() == text) {
            return true;
        }
    }
    return false;
}

void testUnselectedPanelShowsIpInstanceParameters() {
    Graph graph;
    graph.configureIpInstance(
        QStringLiteral("ravenoc_0"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("noc"),
        QStringLiteral("RaveNoC"),
        QHash<QString, Parameter>{
            {QStringLiteral("flit_data_width"),
             Parameter(QStringLiteral("flit_data_width"), 32)}
        });

    CommandManager commandManager;
    PropertyPanel panel(&graph, &commandManager);
    panel.setSelectedModule(QString());

    require(hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_0")),
            "property panel should show IP instance section header");
    require(hasLabel(panel, QStringLiteral("Flit data width")),
            "property panel should show IP instance parameter row");

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "integer IP instance parameter should use spin box");
    spinBox->setValue(64);
    require(commandManager.currentStateId() == 1,
            "editing IP instance parameter should create a command history state");

    const auto stored = graph.ipInstance()->parameters.value(QStringLiteral("flit_data_width")).value();
    const auto* value = std::get_if<int>(&stored);
    require(value && *value == 64,
            "editing IP instance parameter should update graph state");

    commandManager.undo();
    const auto undone = graph.ipInstance()->parameters.value(QStringLiteral("flit_data_width")).value();
    const auto* undoneValue = std::get_if<int>(&undone);
    require(undoneValue && *undoneValue == 32,
            "undo should restore previous IP instance parameter value");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testUnselectedPanelShowsIpInstanceParameters();
    } catch (const std::exception& error) {
        std::cerr << "propertypanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "propertypanel_test passed\n";
    return 0;
}
