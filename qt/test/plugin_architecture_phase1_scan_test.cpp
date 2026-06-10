#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

QString readText(const QString& path) {
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read " + path).toStdString());
    }
    return QString::fromUtf8(source.readAll());
}

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void testFoundationFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/app/workbenchservice.h"),
        QStringLiteral("qt/src/app/workbenchservice.cpp"),
        QStringLiteral("qt/inc/app/appcontext.h"),
        QStringLiteral("qt/inc/app/pluginhost.h"),
        QStringLiteral("qt/src/app/pluginhost.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing foundation file: ") + file);
    }
}

void testMainWindowConsumesWorkbenchService() {
    const QString header = readText(QStringLiteral("qt/inc/app/mainwindow.h"));
    const QString source = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));

    requireContains(header, QStringLiteral("WorkbenchService"), QStringLiteral("mainwindow header"));
    requireContains(source,
                    QStringLiteral("registerBuiltinWorkbenchContributions"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("m_workbenchService->panels()"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("m_workbenchService->editors()"),
                    QStringLiteral("mainwindow source"));
}

void testPhaseOnePlanDocumentsReviewGate() {
    const QString plan =
        readText(QStringLiteral("docs/superpowers/plans/2026-06-10-plugin-host-foundation.md"));
    requireContains(plan, QStringLiteral("Phase 1"), QStringLiteral("phase plan"));
    requireContains(plan, QStringLiteral("review gate"), QStringLiteral("phase plan"));
    requireContains(plan, QStringLiteral("workbenchservice_test"), QStringLiteral("phase plan"));
    requireContains(plan, QStringLiteral("pluginhost_foundation_test"), QStringLiteral("phase plan"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testFoundationFilesExist();
    testMainWindowConsumesWorkbenchService();
    testPhaseOnePlanDocumentsReviewGate();
    std::cout << "plugin_architecture_phase1_scan_test passed\n";
    return 0;
}
