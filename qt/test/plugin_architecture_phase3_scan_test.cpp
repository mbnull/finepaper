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

void requireNotContains(const QString& text, const QString& needle, const QString& context) {
    require(!text.contains(needle), context + QStringLiteral(" should not contain ") + needle);
}

void testPackagePluginFilesExist() {
    const QStringList files = {
        QStringLiteral("qt/inc/package/packageservice.h"),
        QStringLiteral("qt/src/package/packageservice.cpp"),
        QStringLiteral("qt/inc/package/packageplugin.h"),
        QStringLiteral("qt/src/package/packageplugin.cpp")
    };

    for (const QString& file : files) {
        require(QFile::exists(file), QStringLiteral("missing package plugin file: ") + file);
    }
}

void testPackageServiceOwnsPackageAndCatalogLoading() {
    const QString header = readText(QStringLiteral("qt/inc/package/packageservice.h"));
    const QString source = readText(QStringLiteral("qt/src/package/packageservice.cpp"));

    requireContains(header, QStringLiteral("PackageServiceLoadResult"), QStringLiteral("package service header"));
    requireContains(header, QStringLiteral("reloadPackageRoots"), QStringLiteral("package service header"));
    requireContains(source,
                    QStringLiteral("loadIpcraftPackageManifestsWithDiagnostics"),
                    QStringLiteral("package service source"));
    requireContains(source,
                    QStringLiteral("loadIpcraftPackages"),
                    QStringLiteral("package service source"));
    requireContains(source, QStringLiteral("IpCatalogService("), QStringLiteral("package service source"));
}

void testMainWindowUsesPackageServiceForCatalogReload() {
    const QString header = readText(QStringLiteral("qt/inc/app/mainwindow.h"));
    const QString source = readText(QStringLiteral("qt/src/app/mainwindow.cpp"));

    requireContains(header, QStringLiteral("PackageService"), QStringLiteral("mainwindow header"));
    requireContains(source,
                    QStringLiteral("m_packageService->reloadPackageRoots"),
                    QStringLiteral("mainwindow source"));
    requireContains(source,
                    QStringLiteral("PackageService diagnosticsService"),
                    QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("loadIpcraftPackageManifestsWithDiagnostics"),
                       QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("loadIpcraftPackages(loadResult.manifests)"),
                       QStringLiteral("mainwindow source"));
    requireNotContains(source,
                       QStringLiteral("IpCatalogService(loadResult.manifests"),
                       QStringLiteral("mainwindow source"));
}

void testAnchorPackagesCoveredByServiceTest() {
    const QString testSource = readText(QStringLiteral("qt/test/packageservice_test.cpp"));

    requireContains(testSource, QStringLiteral("finepaper.noc"), QStringLiteral("package service test"));
    requireContains(testSource, QStringLiteral("finepaper.ravenoc"), QStringLiteral("package service test"));
    requireContains(testSource, QStringLiteral("finepaper.opennoc"), QStringLiteral("package service test"));
    requireContains(testSource, QStringLiteral("repositoryPath(QStringLiteral(\"ipcores\""),
                    QStringLiteral("package service test"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testPackagePluginFilesExist();
    testPackageServiceOwnsPackageAndCatalogLoading();
    testMainWindowUsesPackageServiceForCatalogReload();
    testAnchorPackagesCoveredByServiceTest();
    std::cout << "plugin_architecture_phase3_scan_test passed\n";
    return 0;
}
