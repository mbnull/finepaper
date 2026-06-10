// WorkbenchService contribution registry tests.
#include "app/workbenchservice.h"

#include <QAction>
#include <QApplication>
#include <QLabel>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

WorkbenchActionContribution makeAction(const QString& id) {
    return WorkbenchActionContribution{
        .id = id,
        .text = QStringLiteral("Open"),
        .menuPath = QStringLiteral("File"),
        .toolBar = QStringLiteral("Main"),
        .objectName = QStringLiteral("openAction"),
        .factory = [](QObject* parent) {
            return new QAction(QStringLiteral("Open"), parent);
        }
    };
}

WorkbenchPanelContribution makePanel(const QString& id) {
    return WorkbenchPanelContribution{
        .id = id,
        .title = QStringLiteral("Project"),
        .objectName = QStringLiteral("projectPanel"),
        .area = WorkbenchPanelArea::Right,
        .factory = [](QWidget* parent) {
            return new QLabel(QStringLiteral("Project"), parent);
        }
    };
}

WorkbenchEditorContribution makeEditor(const QString& id) {
    return WorkbenchEditorContribution{
        .id = id,
        .title = QStringLiteral("Graph"),
        .objectName = QStringLiteral("graphEditor"),
        .factory = [](QWidget* parent) {
            return new QLabel(QStringLiteral("Graph"), parent);
        }
    };
}

void testRegistrationOrderAndFieldsRoundtrip() {
    WorkbenchService service;

    require(service.addAction(makeAction(QStringLiteral("action.open"))),
            "action contribution should register");
    require(service.addPanel(makePanel(QStringLiteral("panel.project"))),
            "panel contribution should register");
    require(service.addEditor(makeEditor(QStringLiteral("editor.graph"))),
            "editor contribution should register");

    const QList<WorkbenchActionContribution> actions = service.actions();
    const QList<WorkbenchPanelContribution> panels = service.panels();
    const QList<WorkbenchEditorContribution> editors = service.editors();

    require(actions.size() == 1, "one action should be registered");
    require(panels.size() == 1, "one panel should be registered");
    require(editors.size() == 1, "one editor should be registered");

    require(actions.at(0).id == QStringLiteral("action.open"), "action id should roundtrip");
    require(actions.at(0).text == QStringLiteral("Open"), "action text should roundtrip");
    require(actions.at(0).menuPath == QStringLiteral("File"), "action menu path should roundtrip");
    require(actions.at(0).toolBar == QStringLiteral("Main"), "action toolbar should roundtrip");
    require(actions.at(0).objectName == QStringLiteral("openAction"),
            "action object name should roundtrip");
    require(actions.at(0).factory != nullptr, "action factory should roundtrip");

    require(panels.at(0).id == QStringLiteral("panel.project"), "panel id should roundtrip");
    require(panels.at(0).title == QStringLiteral("Project"), "panel title should roundtrip");
    require(panels.at(0).objectName == QStringLiteral("projectPanel"),
            "panel object name should roundtrip");
    require(panels.at(0).area == WorkbenchPanelArea::Right, "panel area should roundtrip");
    require(panels.at(0).factory != nullptr, "panel factory should roundtrip");

    require(editors.at(0).id == QStringLiteral("editor.graph"), "editor id should roundtrip");
    require(editors.at(0).title == QStringLiteral("Graph"), "editor title should roundtrip");
    require(editors.at(0).objectName == QStringLiteral("graphEditor"),
            "editor object name should roundtrip");
    require(editors.at(0).factory != nullptr, "editor factory should roundtrip");
}

void testDuplicateIdsAreRejectedPerContributionType() {
    WorkbenchService service;

    require(service.addAction(makeAction(QStringLiteral("duplicate"))),
            "first action should register");
    require(!service.addAction(makeAction(QStringLiteral("duplicate"))),
            "duplicate action id should be rejected");

    require(service.addPanel(makePanel(QStringLiteral("duplicate"))),
            "first panel should register");
    require(!service.addPanel(makePanel(QStringLiteral("duplicate"))),
            "duplicate panel id should be rejected");

    require(service.addEditor(makeEditor(QStringLiteral("duplicate"))),
            "first editor should register");
    require(!service.addEditor(makeEditor(QStringLiteral("duplicate"))),
            "duplicate editor id should be rejected");
}

void testIncompleteContributionsAreRejected() {
    WorkbenchService service;

    WorkbenchActionContribution action = makeAction(QStringLiteral("action.open"));
    action.id.clear();
    require(!service.addAction(action), "action with empty id should be rejected");

    WorkbenchPanelContribution panel = makePanel(QStringLiteral("panel.project"));
    panel.factory = {};
    require(!service.addPanel(panel), "panel without factory should be rejected");

    WorkbenchEditorContribution editor = makeEditor(QStringLiteral("editor.graph"));
    editor.objectName.clear();
    require(!service.addEditor(editor), "editor with empty object name should be rejected");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testRegistrationOrderAndFieldsRoundtrip();
        testDuplicateIdsAreRejectedPerContributionType();
        testIncompleteContributionsAreRejected();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }

    std::cout << "workbenchservice_test passed\n";
    return 0;
}
