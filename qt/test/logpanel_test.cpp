// Log panel presentation tests.
#include "panels/logpanel.h"

#include <QApplication>
#include <QListWidget>
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

} // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    try {
        testAppendMessageKeepsTimestampOutOfVisibleText();
    } catch (const std::exception& error) {
        std::cerr << "logpanel_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "logpanel_test passed\n";
    return 0;
}
