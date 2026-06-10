#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
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

void requireNotContains(const QString& text,
                        const QString& needle,
                        const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

QStringList scannedSourceFiles() {
    QStringList files;
    const QStringList roots{
        QStringLiteral("qt/src/app"),
        QStringLiteral("qt/src/panels"),
        QStringLiteral("qt/src/connection"),
        QStringLiteral("qt/src/project")
    };
    for (const QString& root : roots) {
        QDirIterator iterator(root,
                              QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            files.append(QDir::cleanPath(iterator.next()));
        }
    }
    files.sort();
    return files;
}

bool isOneOf(const QString& value, const QStringList& allowed) {
    return allowed.contains(QDir::cleanPath(value));
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
    requireContains(report, QStringLiteral("ProjectGenerationRequest"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("GraphProjectSerializer"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("adapter/deletion debt"), QStringLiteral("hardening report"));
    requireContains(report, QStringLiteral("m_graph"), QStringLiteral("hardening report"));
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

void testSourceScanRejectsConcretePackageHardcoding() {
    for (const QString& path : scannedSourceFiles()) {
        const QString source = readText(path);
        requireNotContains(source, QStringLiteral("finepaper.noc"), path);
        requireNotContains(source, QStringLiteral("finepaper.ravenoc"), path);
        requireNotContains(source, QStringLiteral("finepaper.opennoc"), path);
    }
}

void testSourceScanRejectsDirectGeneratorOrProcessCallsOutsidePipeline() {
    const QStringList generatorStringAllowed{
        QStringLiteral("qt/src/app/projectgenerationrunner.cpp")
    };
    const QStringList qprocessAllowed{
        QStringLiteral("qt/src/app/uiscale.cpp")
    };
    for (const QString& path : scannedSourceFiles()) {
        const QString source = readText(path);
        if (!isOneOf(path, generatorStringAllowed)) {
            requireNotContains(source, QStringLiteral("ipcraft-generate"), path);
            requireNotContains(source, QStringLiteral("framework_tool"), path);
        }
        if (!isOneOf(path, qprocessAllowed)) {
            requireNotContains(source, QStringLiteral("QProcess"), path);
        }
        requireNotContains(source, QStringLiteral("system("), path);
        requireNotContains(source, QStringLiteral("popen("), path);
    }
}

void testSourceScanRejectsManifestLoadingOutsidePackageServices() {
    for (const QString& path : scannedSourceFiles()) {
        const QString source = readText(path);
        requireNotContains(source, QStringLiteral("loadIpcraftPackageManifests"), path);
        requireNotContains(source, QStringLiteral("ipcraft.json"), path);
    }
}

void testSourceScanAllowsOnlyDocumentedGraphSerializerAdapters() {
    const QStringList serializerAllowed{
        QStringLiteral("qt/src/app/generationartifacts.cpp"),
        QStringLiteral("qt/src/app/projectgenerationrunner.cpp"),
        QStringLiteral("qt/src/project/editorprojectionservice.cpp"),
        QStringLiteral("qt/src/project/graphprojectserializer.cpp")
    };
    for (const QString& path : scannedSourceFiles()) {
        const QString source = readText(path);
        if (!isOneOf(path, serializerAllowed)) {
            requireNotContains(source, QStringLiteral("GraphProjectSerializer"), path);
        }
    }
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
    testSourceScanRejectsConcretePackageHardcoding();
    testSourceScanRejectsDirectGeneratorOrProcessCallsOutsidePipeline();
    testSourceScanRejectsManifestLoadingOutsidePackageServices();
    testSourceScanAllowsOnlyDocumentedGraphSerializerAdapters();
    testPhase8AndPhase9ScansAreRegistered();
    std::cout << "plugin_architecture_phase9_scan_test passed\n";
    return 0;
}
