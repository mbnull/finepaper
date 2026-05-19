// Property panel tests for IP-instance parameter presentation.
#include "commands/commandmanager.h"
#include "graph/graph.h"
#include "panels/propertypanel.h"
#include "project/ipinstanceparameteradapter.h"
#include "project/ipinstancestate.h"
#include "project/projectdocument.h"
#include "project/projectstateservice.h"

#include <QApplication>
#include <QComboBox>
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

const Connection* findConnection(const Graph& graph, const QString& connectionId) {
    for (const std::unique_ptr<Connection>& connection : graph.connections()) {
        if (connection->id() == connectionId) {
            return connection.get();
        }
    }
    return nullptr;
}

QString ambiguityLogMessage(const Connection& connection) {
    if (connection.status() != QStringLiteral("ambiguous") ||
        connection.alternatives().size() < 2) {
        return {};
    }

    return QStringLiteral("Connection %1 has multiple valid classes: %2")
        .arg(connection.id(), connection.alternatives().join(QStringLiteral(", ")));
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

    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.ravenoc");
    runtime.name = QStringLiteral("RaveNoC");
    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    runtime.instanceParameters.insert(width.name, width);
    RuntimeIpInstanceParameterAdapter adapter(runtime);

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

    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.ravenoc");
    runtime.name = QStringLiteral("RaveNoC");
    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    runtime.instanceParameters.insert(width.name, width);
    RuntimeIpInstanceParameterAdapter adapter(runtime);

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

    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.ravenoc");
    runtime.name = QStringLiteral("RaveNoC");
    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    runtime.instanceParameters.insert(width.name, width);
    RuntimeIpInstanceParameterAdapter adapter(runtime);

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

void testIpInstanceSectionWithoutProjectInstanceIsHidden() {
    Graph graph;
    ProjectStateService stateService;

    IpCoreRuntimeDescriptor runtime;
    runtime.id = QStringLiteral("finepaper.ravenoc");
    runtime.name = QStringLiteral("RaveNoC");
    IpCoreInstanceParameterDescriptor width;
    width.name = QStringLiteral("flit_data_width");
    width.type = QStringLiteral("int");
    width.defaultValue = 32;
    width.label = QStringLiteral("Flit data width");
    runtime.instanceParameters.insert(width.name, width);
    RuntimeIpInstanceParameterAdapter adapter(runtime);

    CommandManager commandManager;
    PropertyPanel panel(&graph, &stateService, {&adapter}, &commandManager);
    panel.setSelectedModule(QString());

    QSpinBox* spinBox = panel.findChild<QSpinBox*>();
    require(spinBox == nullptr,
            "IP-instance parameters should stay hidden until that IP exists in the project");
    require(!hasLabel(panel, QStringLiteral("RaveNoC / ravenoc_0")),
            "property panel should not synthesize a default IP-instance section");
    require(!hasLabel(panel, QStringLiteral("Flit data width")),
            "property panel should not show global parameters before IP instantiation");
    require(commandManager.currentStateId() == 0,
            "hidden IP-instance parameters should not enter command history");
    require(stateService.ipInstanceRecords().isEmpty(),
            "hidden IP-instance parameters should not create state implicitly");
}

void testAmbiguousConnectionAppearsInPropertyPanelAndLog() {
    Graph graph;
    auto source = std::make_unique<Module>(QStringLiteral("source"), QStringLiteral("Source"));
    source->addPort(Port(QStringLiteral("out"), Port::Direction::Output, QStringLiteral("bus"),
                         QStringLiteral("Out")));
    auto target = std::make_unique<Module>(QStringLiteral("target"), QStringLiteral("Target"));
    target->addPort(Port(QStringLiteral("in"), Port::Direction::Input, QStringLiteral("bus"),
                         QStringLiteral("In")));
    require(graph.addModule(std::move(source)), "source module should add");
    require(graph.addModule(std::move(target)), "target module should add");
    graph.addConnection(std::make_unique<Connection>(
        QStringLiteral("conn_1"),
        PortRef{QStringLiteral("source"), QStringLiteral("out")},
        PortRef{QStringLiteral("target"), QStringLiteral("in")},
        QStringLiteral("chi_node_interface"),
        QVector<ConnectionInterfaceRef>{
            ConnectionInterfaceRef{QStringLiteral("source"), QStringLiteral("out")},
            ConnectionInterfaceRef{QStringLiteral("target"), QStringLiteral("in")}
        },
        QStringLiteral("ambiguous"),
        QStringList{QStringLiteral("chi_node_interface"), QStringLiteral("monitor_tap")}));
    const Connection* loggedConnection = findConnection(graph, QStringLiteral("conn_1"));
    require(loggedConnection != nullptr,
            "ambiguous connection should exist before it is shown in panels");
    const QString logMessage = ambiguityLogMessage(*loggedConnection);
    require(logMessage.contains(QStringLiteral("conn_1")),
            "ambiguous connection log message should include the connection id");
    require(logMessage.contains(QStringLiteral("chi_node_interface")) &&
                logMessage.contains(QStringLiteral("monitor_tap")),
            "ambiguous connection log message should include every class alternative");

    CommandManager commandManager;
    PropertyPanel panel(&graph, &commandManager);
    panel.setSelectedModule(QStringLiteral("conn_1"));

    require(hasLabel(panel, QStringLiteral("Connection class")),
            "property panel should show connection class for ambiguous connection");

    QComboBox* comboBox = panel.findChild<QComboBox*>(QStringLiteral("connectionClassCombo"));
    require(comboBox != nullptr, "ambiguous connection class should use a combo box");
    require(comboBox->currentText() == QStringLiteral("chi_node_interface"),
            "connection class combo box should show selected class");
    const int monitorTapIndex = comboBox->findText(QStringLiteral("monitor_tap"));
    require(monitorTapIndex >= 0,
            "connection class combo box should include class alternatives");

    comboBox->setCurrentIndex(monitorTapIndex);

    const Connection* changedConnection = findConnection(graph, QStringLiteral("conn_1"));
    require(changedConnection != nullptr,
            "connection class selection should keep the connection in the graph");
    require(changedConnection->connectionClassId() == QStringLiteral("monitor_tap"),
            "connection class selection should update the selected connection class");
    require(changedConnection->status() == QStringLiteral("valid"),
            "choosing an ambiguous alternative should resolve the warning status");
    require(changedConnection->alternatives().isEmpty(),
            "resolved connection should not retain ambiguity alternatives");
    require(commandManager.currentStateId() == 1,
            "connection class selection should enter command history");
    QApplication::processEvents();
    require(panel.findChild<QComboBox*>(QStringLiteral("connectionClassCombo")) == nullptr,
            "resolved connection should clear the connection class selector");

    commandManager.undo();
    QApplication::processEvents();
    const Connection* undoneConnection = findConnection(graph, QStringLiteral("conn_1"));
    require(undoneConnection != nullptr,
            "undo should keep the connection in the graph");
    require(undoneConnection->connectionClassId() == QStringLiteral("chi_node_interface"),
            "undo should restore the original connection class");
    require(undoneConnection->status() == QStringLiteral("ambiguous"),
            "undo should restore the ambiguous status");
    require(undoneConnection->alternatives() == QStringList({QStringLiteral("chi_node_interface"),
                                                             QStringLiteral("monitor_tap")}),
            "undo should restore class alternatives");
    QComboBox* undoCombo = panel.findChild<QComboBox*>(QStringLiteral("connectionClassCombo"));
    require(undoCombo != nullptr,
            "undo should restore the connection class selector for an ambiguous connection");
    require(undoCombo->currentText() == QStringLiteral("chi_node_interface"),
            "undo should refresh the selector to the restored connection class");

    commandManager.redo();
    QApplication::processEvents();
    const Connection* redoneConnection = findConnection(graph, QStringLiteral("conn_1"));
    require(redoneConnection != nullptr,
            "redo should keep the connection in the graph");
    require(redoneConnection->connectionClassId() == QStringLiteral("monitor_tap"),
            "redo should reapply the selected connection class");
    require(redoneConnection->status() == QStringLiteral("valid"),
            "redo should restore the resolved status");
    require(panel.findChild<QComboBox*>(QStringLiteral("connectionClassCombo")) == nullptr,
            "redo should clear the selector instead of leaving the undo state visible");
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
        testIpInstanceSectionWithoutProjectInstanceIsHidden();
        testAmbiguousConnectionAppearsInPropertyPanelAndLog();
    } catch (const std::exception& error) {
        std::cerr << "propertypanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "propertypanel_test passed\n";
    return 0;
}
