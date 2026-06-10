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

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" should contain ") + needle);
}

void testCommercialWorkflowGateCoversAllAnchorPackages() {
    const QString source = readText(QStringLiteral("qt/test/commercial_noc_mvp_test.cpp"));

    requireContains(source,
                    QStringLiteral("finepaper.noc"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(source,
                    QStringLiteral("finepaper.ravenoc"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(source,
                    QStringLiteral("finepaper.opennoc"),
                    QStringLiteral("commercial NoC MVP gate"));
}

void testCommercialWorkflowGateUsesQtProjectPipeline() {
    const QString source = readText(QStringLiteral("qt/test/commercial_noc_mvp_test.cpp"));

    requireContains(source,
                    QStringLiteral("ProjectGenerationRunner"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(source,
                    QStringLiteral("TopologyPresetBuilder"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(source,
                    QStringLiteral("loadIpcraftPackageManifests"),
                    QStringLiteral("commercial NoC MVP gate"));
    requireContains(source,
                    QStringLiteral("ConnectionRuleService"),
                    QStringLiteral("commercial NoC MVP gate"));
}

void testCommercialWorkflowTargetIsRegistered() {
    const QString xmake = readText(QStringLiteral("qt/xmake.lua"));

    requireContains(xmake,
                    QStringLiteral("commercial_noc_mvp_test"),
                    QStringLiteral("qt xmake"));
    requireContains(xmake,
                    QStringLiteral("src/topology/topologypresetbuilder.cpp"),
                    QStringLiteral("commercial NoC MVP target"));
    requireContains(xmake,
                    QStringLiteral("src/app/projectgenerationrunner.cpp"),
                    QStringLiteral("commercial NoC MVP target"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testCommercialWorkflowGateCoversAllAnchorPackages();
    testCommercialWorkflowGateUsesQtProjectPipeline();
    testCommercialWorkflowTargetIsRegistered();
    std::cout << "plugin_architecture_phase7_scan_test passed\n";
    return 0;
}
