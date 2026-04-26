// Log formatting tests for process-wide Qt logs.
#include "app/logformat.h"

#include <QDateTime>
#include <QMessageLogContext>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testDefaultContextOmitsEmptyCategoryAndFileLinePlaceholders() {
    const QDateTime timestamp = QDateTime::fromString(QStringLiteral("2026-04-26T21:23:24.283"),
                                                      Qt::ISODateWithMs);
    const QString line = LogFormat::formatMessage(QtInfoMsg,
                                                  QMessageLogContext(),
                                                  QStringLiteral("[Startup] Plugin finepaper.noc"),
                                                  timestamp);

    require(line == QStringLiteral("[2026-04-26T21:23:24.283] [INFO] [Startup] Plugin finepaper.noc"),
            "default log context should not print [default] or -:- placeholders");
}

void testUsefulContextIsPreservedWhenAvailable() {
    const QDateTime timestamp = QDateTime::fromString(QStringLiteral("2026-04-26T21:23:24.283"),
                                                      Qt::ISODateWithMs);
    const QMessageLogContext context("source.cpp", 42, nullptr, "finepaper.qt");
    const QString line = LogFormat::formatMessage(QtWarningMsg,
                                                  context,
                                                  QStringLiteral("loaded deprecated bundle"),
                                                  timestamp);

    require(line == QStringLiteral("[2026-04-26T21:23:24.283] [WARN] [finepaper.qt] source.cpp:42 loaded deprecated bundle"),
            "non-default category and valid file line should remain in log output");
}

} // namespace

int main() {
    try {
        testDefaultContextOmitsEmptyCategoryAndFileLinePlaceholders();
        testUsefulContextIsPreservedWhenAvailable();
    } catch (const std::exception& error) {
        std::cerr << "logformat_test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "logformat_test passed\n";
    return 0;
}
