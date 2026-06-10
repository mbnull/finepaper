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

void requireContains(const QString& text,
                     const QString& needle,
                     const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void testCompletionReportCoversPhaseMatrix() {
    const QString report =
        readText(QStringLiteral("docs/architecture/plugin-architecture-completion-report.md"));

    requireContains(report, QStringLiteral("Final verdict: go-with-debt"), QStringLiteral("completion report"));
    for (int phase = 2; phase <= 10; ++phase) {
        requireContains(report,
                        QStringLiteral("Phase %1").arg(phase),
                        QStringLiteral("completion report"));
    }
}

void testCompletionReportCoversAnchorPackagesAndSchemas() {
    const QString report =
        readText(QStringLiteral("docs/architecture/plugin-architecture-completion-report.md"));

    const QStringList packages{
        QStringLiteral("finepaper.noc"),
        QStringLiteral("finepaper.ravenoc"),
        QStringLiteral("finepaper.opennoc")
    };
    for (const QString& packageId : packages) {
        requireContains(report, packageId, QStringLiteral("completion report"));
    }

    const QStringList schemas{
        QStringLiteral("ipcraft.project.v1"),
        QStringLiteral("ipcraft.package.v1"),
        QStringLiteral("ipcraft.graph-config.v1"),
        QStringLiteral("ipcraft.emitted-inputs.v1"),
        QStringLiteral("ipcraft.diagnostics.v1"),
        QStringLiteral("ipcraft.diagnostic.v1")
    };
    for (const QString& schema : schemas) {
        requireContains(report, schema, QStringLiteral("completion report"));
    }
}

void testCompletionReportCoversReviewAndDebt() {
    const QString report =
        readText(QStringLiteral("docs/architecture/plugin-architecture-completion-report.md"));

    requireContains(report, QStringLiteral("qt-cpp-review"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("Architecture Scan Status"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("Phases 1 through 10 pass"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("Legacy Path And Deletion Gate Status"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("accepted debt"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("ProjectGenerationRequest::graph"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("GraphProjectSerializer"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("MainWindow"), QStringLiteral("completion report"));
    requireContains(report, QStringLiteral("PluginHost"), QStringLiteral("completion report"));
}

void testReadmeAndHardeningReportReferencePhase10() {
    const QString readme = readText(QStringLiteral("docs/architecture/README.md"));
    const QString hardening =
        readText(QStringLiteral("docs/architecture/plugin-architecture-hardening-report.md"));

    requireContains(readme,
                    QStringLiteral("plugin-architecture-completion-report.md"),
                    QStringLiteral("architecture README"));
    requireContains(hardening, QStringLiteral("Phase 10"), QStringLiteral("hardening report"));
    requireContains(hardening, QStringLiteral("adapter/deletion debt"), QStringLiteral("hardening report"));
    requireContains(hardening, QStringLiteral("qt-cpp-review"), QStringLiteral("hardening report"));
}

void testPhase10ScanIsRegistered() {
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));

    requireContains(xmake,
                    QStringLiteral("plugin_architecture_phase10_scan_test"),
                    QStringLiteral("qt xmake"));
    requireContains(xmake,
                    QStringLiteral("test/plugin_architecture_phase10_scan_test.cpp"),
                    QStringLiteral("qt xmake"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testCompletionReportCoversPhaseMatrix();
    testCompletionReportCoversAnchorPackagesAndSchemas();
    testCompletionReportCoversReviewAndDebt();
    testReadmeAndHardeningReportReferencePhase10();
    testPhase10ScanIsRegistered();
    std::cout << "plugin_architecture_phase10_scan_test passed\n";
    return 0;
}
