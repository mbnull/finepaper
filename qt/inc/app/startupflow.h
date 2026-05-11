// StartupFlow selects whether startup should exit or show the main window.
#ifndef STARTUPFLOW_H
#define STARTUPFLOW_H

#include "app/projectlauncher.h"

#include <QStringList>
#include <functional>

struct StartupFlowResult {
    enum class Action { Exit, ShowMainWindow };

    Action action = Action::Exit;
    int exitCode = 0;
};

struct StartupFlowCallbacks {
    std::function<bool(const QString&)> loadProject;
    std::function<bool(const QString&)> createProject;
    std::function<ProjectLauncherResult()> showLauncher;
};

StartupFlowResult selectStartupProject(const QStringList& arguments,
                                       const StartupFlowCallbacks& callbacks);

#endif // STARTUPFLOW_H
