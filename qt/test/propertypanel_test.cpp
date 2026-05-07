// Property panel tests for plugin project parameter presentation.
#include "commands/commandmanager.h"
#include "graph/graph.h"
#include "panels/propertypanel.h"
#include "plugins/pluginprojectadapter.h"
#include "project/pluginstate.h"
#include "project/projectdocument.h"
#include "project/projectstateservice.h"

#include <QApplication>
#include <QJsonObject>
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

void testUnselectedPanelShowsPluginProjectParameters() {
    Graph graph;
    ProjectStateService stateService;
    ProjectDocument document;
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 64}
        }}
    };
    document.pluginStates.push_back(state);
    stateService.loadFromDocument(document);

    PluginDescriptor plugin;
    plugin.id = QStringLiteral("finepaper.ravenoc");
    plugin.name = QStringLiteral("RaveNoC");
    PluginInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    plugin.instanceParameters.insert(width.name, width);
    ManifestPluginProjectAdapter adapter(plugin);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    require(hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_0")),
            "property panel should show plugin parameter section header");
    require(hasLabel(panel, QStringLiteral("Flit data width")),
            "property panel should show plugin parameter row");

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "integer plugin parameter should use spin box");
    require(spinBox->value() == 64,
            "plugin parameter widget should read the stored project state value");
    require(!graph.ipInstance().has_value(),
            "plugin parameter rendering should not require graph IP instance state");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testUnselectedPanelShowsPluginProjectParameters();
    } catch (const std::exception& error) {
        std::cerr << "propertypanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "propertypanel_test passed\n";
    return 0;
}
