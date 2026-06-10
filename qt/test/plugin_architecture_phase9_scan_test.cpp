#include <QCoreApplication>
#include <QFile>
#include <QString>
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

void requireNotContains(const QString& text,
                        const QString& needle,
                        const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

void testHardeningReportCoversPhasesAndAnchors() {
    const QString report =
        readText(QStringLiteral("docs/architecture/plugin-architecture-hardening-report.md"));

    for (int phase = 2; phase <= 10; ++phase) {
        requireContains(report,
                        QStringLiteral("Phase %1").arg(phase),
                        QStringLiteral("hardening report"));
    }
    requireContains(report, QStringLiteral("finepaper.noc"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("finepaper.ravenoc"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("finepaper.opennoc"), QStringLiteral("hardening report"));
}

void testHardeningReportCoversV1Schemas() {
    const QString report =
        readText(QStringLiteral("docs/architecture/plugin-architecture-hardening-report.md"));

    requireContains(report, QStringLiteral("ipcraft.project.v1"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("ipcraft.package.v1"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("ipcraft.graph-config.v1"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("ipcraft.emitted-inputs.v1"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("ipcraft.diagnostics.v1"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("ipcraft.diagnostic.v1"), QStringLiteral("hardening report"));
}

void testHardeningReportCoversDeletionGates() {
    const QString report =
        readText(QStringLiteral("docs/architecture/plugin-architecture-hardening-report.md"));

    requireContains(report, QStringLiteral("Graph source-of-truth"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("MainWindow"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("UI JSON parsing"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("Direct generator calls"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("Connection hardcoding"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("Legacy compatibility paths"), QStringLiteral("hardening report"));
}

void testAuthoringAndOnboardingKeepExtensionPackageTerms() {
    const QString guide =
        readText(QStringLiteral("docs/architecture/ip-package-authoring-flow.md"));
    const QString skill =
        readText(QStringLiteral(".agents/skills/finepaper-ip-onboarding/SKILL.md"));

    requireContains(guide, QStringLiteral("extensions/packages"), QStringLiteral("authoring guide"));
    requireContains(skill, QStringLiteral("extension/package"), QStringLiteral("onboarding skill"));
    requireNotContains(guide, QStringLiteral("third-party plugin"), QStringLiteral("authoring guide"));
    requireNotContains(skill, QStringLiteral("third-party plugin"), QStringLiteral("onboarding skill"));
}

void testCommercialGateStillUsesUnifiedGenerationAndArtifactChecks() {
    const QString commercialGate =
        readText(QStringLiteral("qt/test/commercial_noc_mvp_test.cpp"));

    requireContains(commercialGate,
                    QStringLiteral("ProjectGenerationRunner"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(commercialGate,
                    QStringLiteral("loadIpcraftPackageManifests"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(commercialGate,
                    QStringLiteral("artifact.isFile() && artifact.size() > 0"),
                    QStringLiteral("commercial NoC MVP gate"));
}

void testPhase8AndPhase9ScansAreRegistered() {
    const QString phase8 =
        readText(QStringLiteral("qt/test/plugin_architecture_phase8_scan_test.cpp"));
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));

    requireContains(phase8,
                    QStringLiteral("finepaper-ip-onboarding"),
                    QStringLiteral("Phase 8 scan"));
    requireContains(xmake,
                    QStringLiteral("plugin_architecture_phase8_scan_test"),
                    QStringLiteral("qt xmake"));
    requireContains(xmake,
                    QStringLiteral("plugin_architecture_phase9_scan_test"),
                    QStringLiteral("qt xmake"));
    requireContains(xmake,
                    QStringLiteral("test/plugin_architecture_phase9_scan_test.cpp"),
                    QStringLiteral("qt xmake"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testHardeningReportCoversPhasesAndAnchors();
    testHardeningReportCoversV1Schemas();
    testHardeningReportCoversDeletionGates();
    testAuthoringAndOnboardingKeepExtensionPackageTerms();
    testCommercialGateStillUsesUnifiedGenerationAndArtifactChecks();
    testPhase8AndPhase9ScansAreRegistered();
    std::cout << "plugin_architecture_phase9_scan_test passed\n";
    return 0;
}
