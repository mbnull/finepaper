// StartupFlow selects whether startup should exit or show the main window.
#include "app/startupflow.h"

namespace {

StartupFlowResult exitResult(int exitCode) {
    return StartupFlowResult{StartupFlowResult::Action::Exit, exitCode};
}

StartupFlowResult showMainWindowResult() {
    return StartupFlowResult{StartupFlowResult::Action::ShowMainWindow, 0};
}

} // namespace

StartupFlowResult selectStartupProject(const QStringList& arguments,
                                       const StartupFlowCallbacks& callbacks) {
    if (arguments.size() > 1) {
        if (!callbacks.loadProject) {
            return exitResult(1);
        }
        return callbacks.loadProject(arguments.at(1)) ? showMainWindowResult() : exitResult(1);
    }

    if (!callbacks.showLauncher) {
        return exitResult(1);
    }

    while (true) {
        const ProjectLauncherResult launcherResult = callbacks.showLauncher();
        switch (launcherResult.action) {
        case ProjectLauncherResult::Action::Cancel:
            return exitResult(0);
        case ProjectLauncherResult::Action::NewProject:
            if (!callbacks.createProject) {
                return exitResult(1);
            }
            if (callbacks.createProject(launcherResult.path)) {
                return showMainWindowResult();
            }
            break;
        case ProjectLauncherResult::Action::OpenProject:
            if (!callbacks.loadProject) {
                return exitResult(1);
            }
            if (callbacks.loadProject(launcherResult.path)) {
                return showMainWindowResult();
            }
            break;
        }
    }
}
