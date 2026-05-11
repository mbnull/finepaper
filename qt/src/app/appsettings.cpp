// AppSettings stores small per-user app state via QSettings.
#include "app/appsettings.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>

namespace {

constexpr auto RecentProjectsKey = "projects/recent";
constexpr auto LastDirectoryKey = "projects/lastDirectory";
constexpr auto IpcorePathsKey = "ipcores/paths";
constexpr auto MainWindowGeometryKey = "ui/mainWindowGeometry";
constexpr auto MainWindowStateKey = "ui/mainWindowState";
constexpr auto LastOutputRootKey = "generation/lastOutputRoot";
constexpr int MaxRecentProjects = 10;

QString absolutePath(const QString& path) {
    return path.trimmed().isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}

} // namespace

AppSettings::AppSettings()
    : m_settings(QSettings::IniFormat,
                 QSettings::UserScope,
                 QCoreApplication::organizationName(),
                 QCoreApplication::applicationName()) {
    m_settings.setFallbacksEnabled(false);
}

AppSettings::AppSettings(const QString& fileName, QSettings::Format format)
    : m_settings(fileName, format) {
    m_settings.setFallbacksEnabled(false);
}

QStringList AppSettings::recentProjects() const {
    return absoluteUniquePaths(m_settings.value(QString::fromLatin1(RecentProjectsKey)).toStringList());
}

void AppSettings::addRecentProject(const QString& path) {
    const QString normalizedPath = absolutePath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }

    QStringList recent = recentProjects();
    recent.removeAll(normalizedPath);
    recent.prepend(normalizedPath);
    while (recent.size() > MaxRecentProjects) {
        recent.removeLast();
    }
    m_settings.setValue(QString::fromLatin1(RecentProjectsKey), recent);
}

QString AppSettings::lastDirectory() const {
    return absolutePath(m_settings.value(QString::fromLatin1(LastDirectoryKey)).toString());
}

void AppSettings::setLastDirectory(const QString& path) {
    const QString normalizedPath = absolutePath(path);
    if (normalizedPath.isEmpty()) {
        m_settings.remove(QString::fromLatin1(LastDirectoryKey));
        return;
    }
    m_settings.setValue(QString::fromLatin1(LastDirectoryKey), normalizedPath);
}

QStringList AppSettings::ipcorePaths() const {
    return absoluteUniquePaths(m_settings.value(QString::fromLatin1(IpcorePathsKey)).toStringList());
}

void AppSettings::setIpcorePaths(const QStringList& paths) {
    m_settings.setValue(QString::fromLatin1(IpcorePathsKey), absoluteUniquePaths(paths));
}

QByteArray AppSettings::mainWindowGeometry() const {
    return m_settings.value(QString::fromLatin1(MainWindowGeometryKey)).toByteArray();
}

void AppSettings::setMainWindowGeometry(const QByteArray& geometry) {
    if (geometry.isEmpty()) {
        m_settings.remove(QString::fromLatin1(MainWindowGeometryKey));
        return;
    }
    m_settings.setValue(QString::fromLatin1(MainWindowGeometryKey), geometry);
}

QByteArray AppSettings::mainWindowState() const {
    return m_settings.value(QString::fromLatin1(MainWindowStateKey)).toByteArray();
}

void AppSettings::setMainWindowState(const QByteArray& state) {
    if (state.isEmpty()) {
        m_settings.remove(QString::fromLatin1(MainWindowStateKey));
        return;
    }
    m_settings.setValue(QString::fromLatin1(MainWindowStateKey), state);
}

QString AppSettings::lastOutputRoot() const {
    return absolutePath(m_settings.value(QString::fromLatin1(LastOutputRootKey)).toString());
}

void AppSettings::setLastOutputRoot(const QString& path) {
    const QString normalizedPath = absolutePath(path);
    if (normalizedPath.isEmpty()) {
        m_settings.remove(QString::fromLatin1(LastOutputRootKey));
        return;
    }
    m_settings.setValue(QString::fromLatin1(LastOutputRootKey), normalizedPath);
}

QStringList AppSettings::absoluteUniquePaths(const QStringList& paths) const {
    QStringList normalizedPaths;
    QSet<QString> seenPaths;
    for (const QString& path : paths) {
        const QString normalizedPath = absolutePath(path);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath)) {
            continue;
        }
        seenPaths.insert(normalizedPath);
        normalizedPaths.append(normalizedPath);
    }
    return normalizedPaths;
}
