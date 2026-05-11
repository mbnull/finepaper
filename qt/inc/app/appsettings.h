// AppSettings stores app-local UI and recent-project state in QSettings.
#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>

class AppSettings {
public:
    AppSettings();
    AppSettings(const QString& fileName, QSettings::Format format);

    QStringList recentProjects() const;
    void addRecentProject(const QString& path);

    QString lastDirectory() const;
    void setLastDirectory(const QString& path);

    QStringList ipcorePaths() const;
    void setIpcorePaths(const QStringList& paths);

    QByteArray mainWindowGeometry() const;
    void setMainWindowGeometry(const QByteArray& geometry);

    QByteArray mainWindowState() const;
    void setMainWindowState(const QByteArray& state);

    QString lastOutputRoot() const;
    void setLastOutputRoot(const QString& path);

private:
    QStringList absoluteUniquePaths(const QStringList& paths) const;

    QSettings m_settings;
};

#endif // APPSETTINGS_H
