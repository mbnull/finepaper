// Log panel presentation tests.
#include "graph/connection.h"
#include "panels/logpanel.h"

#include <QApplication>
#include <QListWidget>
#include <QMetaObject>
#include <QRegularExpression>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testAppendMessageKeepsTimestampOutOfVisibleText() {
    LogPanel panel;
    panel.appendMessage(QStringLiteral("[Startup] Plugin finepaper.noc loaded"));

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 1, "one log item should be appended");

    QListWidgetItem* item = list->item(0);
    require(item->text() == QStringLiteral("[Startup] Plugin finepaper.noc loaded"),
            "visible log text should not include timestamp prefix");

    const QString tooltip = item->toolTip();
    require(tooltip.contains(QStringLiteral("[Startup] Plugin finepaper.noc loaded")),
            "tooltip should include the log message");
    require(QRegularExpression(QStringLiteral(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})"))
                .match(tooltip)
                .hasMatch(),
            "tooltip should include a readable timestamp");
}

void testConnectionAmbiguityWarningUsesSingleVisibleLine() {
    LogPanel panel;
    panel.setResults({ValidationResult(
        ValidationSeverity::Warning,
        QStringLiteral("Connection conn_1 has multiple valid classes: chi_node_interface, monitor_tap"),
        QStringLiteral("conn_1"),
        QStringLiteral("connection_ambiguity"))});

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 1, "connection ambiguity should append one warning line");

    QListWidgetItem* item = list->item(0);
    require(item->text() ==
                QStringLiteral("Connection conn_1 has multiple valid classes: chi_node_interface, monitor_tap [conn_1]"),
            "connection ambiguity visible text should match warning message");
}

void testConnectionAmbiguityResultsSelectConnectionWhenClicked() {
    LogPanel panel;
    QString selectedElement;
    QObject::connect(&panel, &LogPanel::elementSelected, &panel, [&](const QString& elementId) {
        selectedElement = elementId;
    });

    panel.setResults({ValidationResult(
        ValidationSeverity::Warning,
        QStringLiteral("Connection conn_1 has multiple valid classes: chi_node_interface, monitor_tap"),
        QStringLiteral("conn_1"),
        QStringLiteral("connection_ambiguity"))});

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 1, "connection ambiguity should append one warning line");
    require(QMetaObject::invokeMethod(list,
                                      "itemClicked",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem*, list->item(0))),
            "test should invoke the log item click signal");

    require(selectedElement == QStringLiteral("conn_1"),
            "clicking an ambiguity validation result should select the connection id");
}

void testValidationResultsSelectElementWhenDoubleClicked() {
    LogPanel panel;
    QString selectedElement;
    QObject::connect(&panel, &LogPanel::elementSelected, &panel, [&](const QString& elementId) {
        selectedElement = elementId;
    });

    panel.setResults({ValidationResult(
        ValidationSeverity::Error,
        QStringLiteral("Connection conn_1 failed validation"),
        QStringLiteral("conn_1"),
        QStringLiteral("DRC"))});

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 2, "validation error should append summary and detail lines");
    require(QMetaObject::invokeMethod(list,
                                      "itemDoubleClicked",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem*, list->item(1))),
            "test should invoke the log item double-click signal");

    require(selectedElement == QStringLiteral("conn_1"),
            "double-clicking a validation result should select the element id");
}

void testAppendConnectionAmbiguityWarningFromConnection() {
    LogPanel panel;
    Connection connection(
        QStringLiteral("conn_1"),
        PortRef{QStringLiteral("source"), QStringLiteral("out")},
        PortRef{QStringLiteral("target"), QStringLiteral("in")},
        QStringLiteral("chi_node_interface"),
        QVector<ConnectionInterfaceRef>{
            ConnectionInterfaceRef{QStringLiteral("source"), QStringLiteral("out")},
            ConnectionInterfaceRef{QStringLiteral("target"), QStringLiteral("in")}
        },
        QStringLiteral("ambiguous"),
        QStringList{QStringLiteral("chi_node_interface"), QStringLiteral("monitor_tap")});

    panel.appendConnectionAmbiguityWarning(connection);

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 1, "ambiguous connection should append one warning line");
    require(list->item(0)->text() ==
                QStringLiteral("Connection conn_1 has multiple valid classes: chi_node_interface, monitor_tap [conn_1]"),
            "clickable connection ambiguity warning should show its element id suffix");

    Connection validConnection(
        QStringLiteral("conn_2"),
        PortRef{QStringLiteral("source"), QStringLiteral("out")},
        PortRef{QStringLiteral("target"), QStringLiteral("in")});
    panel.appendConnectionAmbiguityWarning(validConnection);
    require(list->count() == 1, "valid connections should not append ambiguity warnings");
}

void testAppendConnectionAmbiguityWarningSelectsConnectionWhenClicked() {
    LogPanel panel;
    Connection connection(
        QStringLiteral("conn_1"),
        PortRef{QStringLiteral("source"), QStringLiteral("out")},
        PortRef{QStringLiteral("target"), QStringLiteral("in")},
        QStringLiteral("chi_node_interface"),
        QVector<ConnectionInterfaceRef>{
            ConnectionInterfaceRef{QStringLiteral("source"), QStringLiteral("out")},
            ConnectionInterfaceRef{QStringLiteral("target"), QStringLiteral("in")}
        },
        QStringLiteral("ambiguous"),
        QStringList{QStringLiteral("chi_node_interface"), QStringLiteral("monitor_tap")});

    QString selectedElement;
    QObject::connect(&panel, &LogPanel::elementSelected, &panel, [&](const QString& elementId) {
        selectedElement = elementId;
    });

    panel.appendConnectionAmbiguityWarning(connection);

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 1, "ambiguous connection should append one warning line");
    require(QMetaObject::invokeMethod(list,
                                      "itemClicked",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem*, list->item(0))),
            "test should invoke the log item click signal");

    require(selectedElement == QStringLiteral("conn_1"),
            "clicking an ambiguity warning should select the connection id");
}

void testAppendMessageCapsVisibleHistory() {
    LogPanel panel;

    for (int i = 0; i < 1005; ++i) {
        panel.appendMessage(QStringLiteral("[Log] message %1").arg(i));
    }

    auto* list = panel.findChild<QListWidget*>();
    require(list != nullptr, "log panel list widget should exist");
    require(list->count() == 1000, "log panel should cap retained visible messages");
    require(list->item(0)->text() == QStringLiteral("[Log] message 5"),
            "log panel should discard the oldest messages first");
    require(list->item(list->count() - 1)->text() == QStringLiteral("[Log] message 1004"),
            "log panel should keep the newest message after trimming");
}

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testAppendMessageKeepsTimestampOutOfVisibleText();
        testConnectionAmbiguityWarningUsesSingleVisibleLine();
        testConnectionAmbiguityResultsSelectConnectionWhenClicked();
        testValidationResultsSelectElementWhenDoubleClicked();
        testAppendConnectionAmbiguityWarningFromConnection();
        testAppendConnectionAmbiguityWarningSelectsConnectionWhenClicked();
        testAppendMessageCapsVisibleHistory();
    } catch (const std::exception& error) {
        std::cerr << "logpanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "logpanel_test passed\n";
    return 0;
}
