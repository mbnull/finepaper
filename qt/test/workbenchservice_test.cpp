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

    WorkbenchActionContribution openAction = makeAction(QStringLiteral("action.open"));
    WorkbenchActionContribution saveAction = makeAction(QStringLiteral("action.save"));
    saveAction.text = QStringLiteral("Save");
    saveAction.menuPath = QStringLiteral("File/Save");
    saveAction.toolBar = QStringLiteral("Project");
    saveAction.objectName = QStringLiteral("saveAction");
    saveAction.factory = [](QObject* parent) {
        return new QAction(QStringLiteral("Save"), parent);
    };

    WorkbenchPanelContribution projectPanel = makePanel(QStringLiteral("panel.project"));
    WorkbenchPanelContribution logPanel = makePanel(QStringLiteral("panel.log"));
    logPanel.title = QStringLiteral("Log");
    logPanel.objectName = QStringLiteral("logPanel");
    logPanel.area = WorkbenchPanelArea::Bottom;
    logPanel.factory = [](QWidget* parent) {
        return new QLabel(QStringLiteral("Log"), parent);
    };

    WorkbenchEditorContribution graphEditor = makeEditor(QStringLiteral("editor.graph"));
    WorkbenchEditorContribution textEditor = makeEditor(QStringLiteral("editor.text"));
    textEditor.title = QStringLiteral("Text");
    textEditor.objectName = QStringLiteral("textEditor");
    textEditor.factory = [](QWidget* parent) {
        return new QLabel(QStringLiteral("Text"), parent);
    };

    require(service.addAction(openAction), "first action contribution should register");
    require(service.addAction(saveAction), "second action contribution should register");
    require(service.addPanel(projectPanel), "first panel contribution should register");
    require(service.addPanel(logPanel), "second panel contribution should register");
    require(service.addEditor(graphEditor), "first editor contribution should register");
    require(service.addEditor(textEditor), "second editor contribution should register");

    const QList<WorkbenchActionContribution> actions = service.actions();
    const QList<WorkbenchPanelContribution> panels = service.panels();
    const QList<WorkbenchEditorContribution> editors = service.editors();

    require(actions.size() == 2, "two actions should be registered");
    require(panels.size() == 2, "two panels should be registered");
    require(editors.size() == 2, "two editors should be registered");

    require(actions.at(0).id == QStringLiteral("action.open"), "first action id should roundtrip");
    require(actions.at(0).text == QStringLiteral("Open"), "first action text should roundtrip");
    require(actions.at(0).menuPath == QStringLiteral("File"), "first action menu path should roundtrip");
    require(actions.at(0).toolBar == QStringLiteral("Main"), "first action toolbar should roundtrip");
    require(actions.at(0).objectName == QStringLiteral("openAction"),
            "first action object name should roundtrip");
    require(actions.at(0).factory != nullptr, "first action factory should roundtrip");
    require(actions.at(1).id == QStringLiteral("action.save"), "second action id should preserve order");
    require(actions.at(1).text == QStringLiteral("Save"), "second action text should roundtrip");
    require(actions.at(1).menuPath == QStringLiteral("File/Save"),
            "second action menu path should roundtrip");
    require(actions.at(1).toolBar == QStringLiteral("Project"),
            "second action toolbar should roundtrip");
    require(actions.at(1).objectName == QStringLiteral("saveAction"),
            "second action object name should roundtrip");
    require(actions.at(1).factory != nullptr, "second action factory should roundtrip");

    require(panels.at(0).id == QStringLiteral("panel.project"), "first panel id should roundtrip");
    require(panels.at(0).title == QStringLiteral("Project"), "first panel title should roundtrip");
    require(panels.at(0).objectName == QStringLiteral("projectPanel"),
            "first panel object name should roundtrip");
    require(panels.at(0).area == WorkbenchPanelArea::Right, "first panel area should roundtrip");
    require(panels.at(0).factory != nullptr, "first panel factory should roundtrip");
    require(panels.at(1).id == QStringLiteral("panel.log"), "second panel id should preserve order");
    require(panels.at(1).title == QStringLiteral("Log"), "second panel title should roundtrip");
    require(panels.at(1).objectName == QStringLiteral("logPanel"),
            "second panel object name should roundtrip");
    require(panels.at(1).area == WorkbenchPanelArea::Bottom, "second panel area should roundtrip");
    require(panels.at(1).factory != nullptr, "second panel factory should roundtrip");

    require(editors.at(0).id == QStringLiteral("editor.graph"), "first editor id should roundtrip");
    require(editors.at(0).title == QStringLiteral("Graph"), "first editor title should roundtrip");
    require(editors.at(0).objectName == QStringLiteral("graphEditor"),
            "first editor object name should roundtrip");
    require(editors.at(0).factory != nullptr, "first editor factory should roundtrip");
    require(editors.at(1).id == QStringLiteral("editor.text"), "second editor id should preserve order");
    require(editors.at(1).title == QStringLiteral("Text"), "second editor title should roundtrip");
    require(editors.at(1).objectName == QStringLiteral("textEditor"),
            "second editor object name should roundtrip");
    require(editors.at(1).factory != nullptr, "second editor factory should roundtrip");
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
