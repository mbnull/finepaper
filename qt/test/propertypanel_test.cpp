// Property panel tests for IP-instance parameter presentation.
#include "commands/commandmanager.h"
#include "graph/graph.h"
#include "panels/propertypanel.h"
#include "project/ipinstanceparameteradapter.h"
#include "project/ipinstancestate.h"
#include "project/projectdocument.h"
#include "project/projectstateservice.h"

#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QSpinBox>
#include <QToolButton>
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
    const QList<QToolButton*> buttons = panel.findChildren<QToolButton*>();
    for (const QToolButton* button : buttons) {
        if (button->text() == text) {
            return true;
        }
    }
    return false;
}

void testUnselectedPanelShowsIpInstanceParameters() {
    Graph graph;
    ProjectStateService stateService;
    ProjectDocument document;
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };
    document.ipcoreState.push_back(state);
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
    ManifestIpInstanceParameterAdapter adapter(plugin);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    require(hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_0")),
            "property panel should show IP-instance parameter section header");
    require(hasLabel(panel, QStringLiteral("Flit data width")),
            "property panel should show IP-instance parameter row");

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "integer IP-instance parameter should use spin box");
    require(spinBox->value() == 32,
            "IP-instance parameter widget should read the stored project state value");

    spinBox->setValue(64);
    require(commandManager.currentStateId() == 1,
            "IP-instance parameter edit should enter command history");
    require(stateService.parameter(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0"),
                                   QStringLiteral("global_parameters"),
                                   QStringLiteral("flit_data_width")).toInt() == 64,
            "IP-instance parameter edit should update state service");

    commandManager.undo();
    require(stateService.parameter(QStringLiteral("finepaper.ravenoc"),
                                   QStringLiteral("ravenoc_0"),
                                   QStringLiteral("global_parameters"),
                                   QStringLiteral("flit_data_width")).toInt() == 32,
            "undo should restore previous IP-instance parameter value");
    require(spinBox->value() == 32,
            "undo should refresh the visible IP-instance parameter widget");
}

void testIpInstanceParameterSectionCanCollapseAndExpand() {
    Graph graph;
    ProjectStateService stateService;
    ProjectDocument document;
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_0");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 32}
        }}
    };
    document.ipcoreState.push_back(state);
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
    ManifestIpInstanceParameterAdapter adapter(plugin);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    const QString sectionName =
        QStringLiteral("ipInstanceSection_finepaper_ravenoc_ravenoc_0_global_parameters");
    auto* toggle = panel.findChild<QToolButton*>(sectionName + QStringLiteral("Toggle"));
    auto* content = panel.findChild<QWidget*>(sectionName + QStringLiteral("Content"));
    require(toggle != nullptr, "IP-instance section toggle should exist");
    require(content != nullptr, "IP-instance section content should exist");
    require(!content->isHidden(), "IP-instance section should start expanded");

    toggle->click();
    require(content->isHidden(), "IP-instance section toggle should collapse content");
    require(toggle->arrowType() == Qt::RightArrow, "collapsed section should show right arrow");

    toggle->click();
    require(!content->isHidden(), "IP-instance section toggle should expand content");
    require(toggle->arrowType() == Qt::DownArrow, "expanded section should show down arrow");

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "collapsed section should retain parameter widgets");
    require(spinBox->value() == 32,
            "expanded section should keep the stored project state value");
}

void testUnselectedPanelUsesPersistedCustomIpInstanceId() {
    Graph graph;
    ProjectStateService stateService;
    ProjectDocument document;
    ProjectIpInstanceRecord state;
    state.ipcoreId = QStringLiteral("finepaper.ravenoc");
    state.instanceId = QStringLiteral("ravenoc_custom");
    state.schema = QStringLiteral("ravenoc-project-state-v1");
    state.state = QJsonObject{
        {QStringLiteral("global_parameters"), QJsonObject{
            {QStringLiteral("flit_data_width"), 128}
        }}
    };
    document.ipcoreState.push_back(state);
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
    ManifestIpInstanceParameterAdapter adapter(plugin);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    require(hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_custom")),
            "property panel should show persisted IP instance id");
    require(!hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_0")),
            "property panel should not render adapter default instance when persisted state exists");

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "integer IP-instance parameter should use spin box");
    require(spinBox->value() == 128,
            "IP-instance parameter widget should read stored value for custom instance id");
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

void testDefaultIpInstanceSectionWithoutStateIsReadOnly() {
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
    ManifestIpInstanceParameterAdapter adapter(plugin);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox != nullptr, "default IP-instance parameter should still be visible");
    require(spinBox->isReadOnly(),
            "default IP-instance parameter without writable state should be read-only");
    spinBox->setValue(64);
    require(commandManager.currentStateId() == 0,
            "read-only default IP-instance parameter should not enter command history");
    require(stateService.ipInstanceRecords().isEmpty(),
            "read-only default IP-instance parameter should not create state implicitly");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testUnselectedPanelShowsIpInstanceParameters();
        testIpInstanceParameterSectionCanCollapseAndExpand();
        testUnselectedPanelUsesPersistedCustomIpInstanceId();
        testClearingGraphBeforePanelSelectionClearIsSafe();
        testDefaultIpInstanceSectionWithoutStateIsReadOnly();
    } catch (const std::exception& error) {
        std::cerr << "propertypanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "propertypanel_test passed\n";
    return 0;
}
