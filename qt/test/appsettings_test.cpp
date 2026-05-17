// AppSettings persistence tests.
#include "app/appsettings.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testRecentProjectsAreNewestFirstUniqueAndAbsolute() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    const QString settingsPath = tempDir.filePath(QStringLiteral("appsettings.ini"));
    AppSettings settings(settingsPath, QSettings::IniFormat);

    const QString projectA = tempDir.filePath(QStringLiteral("alpha.fpproj"));
    const QString projectB = tempDir.filePath(QStringLiteral("nested/../beta.fpproj"));

    settings.addRecentProject(projectA);
    settings.addRecentProject(projectB);
    settings.addRecentProject(projectA);

    const QStringList recent = settings.recentProjects();
    require(recent.size() == 2, "recent projects should deduplicate entries");
    require(recent.at(0) == QFileInfo(projectA).absoluteFilePath(),
            "most recent project should be first and absolute");
    require(recent.at(1) == QFileInfo(projectB).absoluteFilePath(),
            "older project should remain after newer duplicate insert");
}

void testIpcorePathsPersistAsAbsoluteUniquePaths() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    const QString settingsPath = tempDir.filePath(QStringLiteral("appsettings.ini"));
    const QString pathA = tempDir.filePath(QStringLiteral("ipcores"));
    const QString pathB = tempDir.filePath(QStringLiteral("nested/../custom-ipcores"));

    {
        AppSettings settings(settingsPath, QSettings::IniFormat);
        settings.setIpcorePaths(QStringList{pathA, pathB, pathA});
    }

    AppSettings reloaded(settingsPath, QSettings::IniFormat);
    const QStringList persisted = reloaded.ipcorePaths();
    require(persisted.size() == 2, "ipcore paths should be deduplicated");
    require(persisted.at(0) == QFileInfo(pathA).absoluteFilePath(),
            "ipcore path A should persist as absolute");
    require(persisted.at(1) == QFileInfo(pathB).absoluteFilePath(),
            "ipcore path B should persist as absolute");
}

void testIpcorePathsDeduplicateAndPersist() {
    QTemporaryDir root;
    require(root.isValid(), "temporary settings root should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ipcore_paths_test_org"));
    QCoreApplication::setApplicationName(QStringLiteral("ipcore_paths_test_app"));

    AppSettings settings;
    settings.setIpcorePaths({QStringLiteral("/tmp/a"),
                             QStringLiteral("/tmp/a"),
                             QStringLiteral("/tmp/b")});

    const QStringList paths = settings.ipcorePaths();
    require(paths.size() == 2, "ipcore paths should deduplicate");
    require(paths.at(0).endsWith(QStringLiteral("/tmp/a")), "first path should persist");
    require(paths.at(1).endsWith(QStringLiteral("/tmp/b")), "second path should persist");
}

void testDefaultConstructorUsesCurrentApplicationIdentity() {
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temporary directory should be valid");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());

    const QString sharedOrganization = QStringLiteral("appsettings_test_org");
    const QString firstApplication = QStringLiteral("appsettings_test_app_one");
    const QString secondApplication = QStringLiteral("appsettings_test_app_two");
    const QString firstPath = tempDir.filePath(QStringLiteral("identity-one-ipcores"));

    QCoreApplication::setOrganizationName(sharedOrganization);
    QCoreApplication::setApplicationName(firstApplication);
    {
        AppSettings settings;
        settings.setIpcorePaths(QStringList{firstPath});
    }

    QCoreApplication::setApplicationName(secondApplication);
    require(AppSettings().ipcorePaths().isEmpty(),
            "default settings should isolate values by application identity");

    QCoreApplication::setApplicationName(firstApplication);
    const QStringList restoredPaths = AppSettings().ipcorePaths();
    require(restoredPaths.size() == 1, "first identity should still see its stored path");
    require(restoredPaths.first() == QFileInfo(firstPath).absoluteFilePath(),
            "default settings should restore values for the original identity");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testRecentProjectsAreNewestFirstUniqueAndAbsolute();
        testIpcorePathsPersistAsAbsoluteUniquePaths();
        testIpcorePathsDeduplicateAndPersist();
        testDefaultConstructorUsesCurrentApplicationIdentity();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }

    std::cout << "appsettings_test passed\n";
    return 0;
}
