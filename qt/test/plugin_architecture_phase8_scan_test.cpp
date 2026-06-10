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

void testOnboardingSkillCoversV1QtFrontendFlow() {
    const QString skill =
        readText(QStringLiteral(".agents/skills/finepaper-ip-onboarding/SKILL.md"));

    requireContains(skill,
                    QStringLiteral("finepaper-ip-onboarding"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("ipcraft.package.v1"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("ipcraft.emitted-inputs.v1"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("ipcraft.graph-config.v1"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("loadIpcraftPackageManifests"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("IpCatalogService"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("ProjectGenerationRunner"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("FlowRunner"),
                    QStringLiteral("Finepaper IP onboarding skill"));
}

void testOnboardingSkillNamesAnchorPackages() {
    const QString skill =
        readText(QStringLiteral(".agents/skills/finepaper-ip-onboarding/SKILL.md"));

    requireContains(skill,
                    QStringLiteral("finepaper.noc"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("finepaper.ravenoc"),
                    QStringLiteral("Finepaper IP onboarding skill"));
    requireContains(skill,
                    QStringLiteral("finepaper.opennoc"),
                    QStringLiteral("Finepaper IP onboarding skill"));
}

void testAuthoringGuideKeepsPackageBoundaryExplicit() {
    const QString guide =
        readText(QStringLiteral("docs/architecture/ip-package-authoring-flow.md"));

    requireContains(guide,
                    QStringLiteral("Internal plugins are Finepaper C++ architecture modules"),
                    QStringLiteral("IP package authoring guide"));
    requireContains(guide,
                    QStringLiteral("extensions/packages are external"),
                    QStringLiteral("IP package authoring guide"));
    requireContains(guide,
                    QStringLiteral("package-declared connection rules"),
                    QStringLiteral("IP package authoring guide"));
    requireContains(guide,
                    QStringLiteral("artifact declarations"),
                    QStringLiteral("IP package authoring guide"));
    requireContains(guide,
                    QStringLiteral("does not define a legacy generator compatibility path"),
                    QStringLiteral("IP package authoring guide"));
    requireNotContains(guide,
                       QStringLiteral("third-party plugin"),
                       QStringLiteral("IP package authoring guide"));
}

void testPhase8ScanTargetIsRegistered() {
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));

    requireContains(xmake,
                    QStringLiteral("plugin_architecture_phase8_scan_test"),
                    QStringLiteral("qt xmake"));
    requireContains(xmake,
                    QStringLiteral("test/plugin_architecture_phase8_scan_test.cpp"),
                    QStringLiteral("qt xmake"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testOnboardingSkillCoversV1QtFrontendFlow();
    testOnboardingSkillNamesAnchorPackages();
    testAuthoringGuideKeepsPackageBoundaryExplicit();
    testPhase8ScanTargetIsRegistered();
    std::cout << "plugin_architecture_phase8_scan_test passed\n";
    return 0;
}
