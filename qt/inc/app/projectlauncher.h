// ProjectLauncherDialog chooses a saved project before editing starts.
#ifndef PROJECTLAUNCHER_H
#define PROJECTLAUNCHER_H

#include <QString>
#include <memory>

class AppSettings;
class QDialog;
class QListWidget;
class QListWidgetItem;
class QWidget;

struct ProjectLauncherResult {
    enum class Action { Cancel, NewProject, OpenProject, };

    Action action = Action::Cancel;
    QString path;
};

class ProjectLauncherDialog {
public:
    explicit ProjectLauncherDialog(QWidget* parent = nullptr);
    ~ProjectLauncherDialog();

    int exec();
    ProjectLauncherResult result() const;

private:
    void chooseNewProject();
    void chooseOpenProject();
    void openRecentProject(QListWidgetItem* item);
    void openSelectedRecentProject();
    QString defaultDirectoryPath() const;

    std::unique_ptr<QDialog> m_dialog;
    std::unique_ptr<AppSettings> m_appSettings;
    QListWidget* m_recentProjects = nullptr;
    ProjectLauncherResult m_result;
};

#endif // PROJECTLAUNCHER_H
