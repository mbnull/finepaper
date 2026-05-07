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
#include <memory>
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
            {QStringLiteral("flit_data_width"), 32}
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
    require(spinBox->value() == 32,
            "plugin parameter widget should read the stored project state value");

    spinBox->setValue(64);
    require(commandManager.currentStateId() == 1,
            "plugin parameter edit should enter command history");
    require(stateService.parameter(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0"),
                                   QStringLiteral("global_parameters"),
                                   QStringLiteral("flit_data_width")).toInt() == 64,
            "plugin parameter edit should update state service");

    commandManager.undo();
    require(stateService.parameter(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0"),
                                   QStringLiteral("global_parameters"),
                                   QStringLiteral("flit_data_width")).toInt() == 32,
            "undo should restore previous plugin parameter value");
    require(spinBox->value() == 32,
            "undo should refresh the visible plugin parameter widget");
}

void testUnselectedPanelUsesPersistedCustomPluginInstanceId() {
    Graph graph;
    ProjectStateService stateService;
    ProjectDocument document;
    ProjectPluginStateRecord state;
    state.pluginId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_custom");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 128}
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

    require(hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_custom")),
            "property panel should show persisted plugin instance id");
    require(!hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_0")),
            "property panel should not render adapter default instance when persisted state exists");

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "integer plugin parameter should use spin box");
    require(spinBox->value() == 128,
            "plugin parameter widget should read stored value for custom instance id");
}

void testClearingGraphBeforePanelSelectionClearIsSafe() {
    Graph graph;
    auto module = std::make_unique<Module>(QStringLiteral("module_0"), QStringLiteral("Demo"));
    module->setParameter(QStringLiteral("width"), 32);
    graph.addModule(std::move(module));

    CommandManager commandManager;
    PropertyPanel panel(&graph, &commandManager);
    panel.setSelectedModule(QStringLiteral("module_0"));

    graph.clear();
    panel.setSelectedModule(QString());
}

void testDefaultPluginSectionWithoutStateIsReadOnly() {
    Graph graph;
    ProjectStateService stateService;

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

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "default plugin parameter should still be visible");
    require(spinBox->isReadOnly(),
            "default plugin parameter without writable state should be read-only");
    spinBox->setValue(64);
    require(commandManager.currentStateId() == 0,
            "read-only default plugin parameter should not enter command history");
    require(stateService.pluginStates().isEmpty(),
            "read-only default plugin parameter should not create state implicitly");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testUnselectedPanelShowsPluginProjectParameters();
        testUnselectedPanelUsesPersistedCustomPluginInstanceId();
        testClearingGraphBeforePanelSelectionClearIsSafe();
        testDefaultPluginSectionWithoutStateIsReadOnly();
    } catch (const std::exception& error) {
        std::cerr << "propertypanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "propertypanel_test passed\n";
    return 0;
}
