// Startup flow tests.
#include "app/startupflow.h"

#include <QCoreApplication>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testCliLoadFailureReturnsExitAndSkipsLauncher() {
    int loadCalls = 0;
    int launcherCalls = 0;
    const StartupFlowResult result = selectStartupProject(
        QStringList{QStringLiteral("finepaper"), QStringLiteral("/tmp/missing.fpproj")},
        StartupFlowCallbacks{
            .loadProject = [&](const QString& path) {
                ++loadCalls;
                require(path == QStringLiteral("/tmp/missing.fpproj"),
                        "CLI path should be passed to loadProject");
                return false;
            },
            .showLauncher = [&]() {
                ++launcherCalls;
                return ProjectLauncherResult{};
            }
        });

    require(result.action == StartupFlowResult::Action::Exit,
            "CLI load failure should exit");
    require(result.exitCode == 1, "CLI load failure should return exit code 1");
    require(loadCalls == 1, "CLI load failure should call loadProject once");
    require(launcherCalls == 0, "CLI load failure should not show launcher");
}

void testCliLoadSuccessReturnsShowMainWindowAndSkipsLauncher() {
    int loadCalls = 0;
    int launcherCalls = 0;
    const StartupFlowResult result = selectStartupProject(
        QStringList{QStringLiteral("finepaper"), QStringLiteral("/tmp/project.fpproj")},
        StartupFlowCallbacks{
            .loadProject = [&](const QString& path) {
                ++loadCalls;
                require(path == QStringLiteral("/tmp/project.fpproj"),
                        "CLI path should be passed to loadProject");
                return true;
            },
            .showLauncher = [&]() {
                ++launcherCalls;
                return ProjectLauncherResult{};
            }
        });

    require(result.action == StartupFlowResult::Action::ShowMainWindow,
            "CLI load success should show main window");
    require(result.exitCode == 0, "CLI load success should keep exit code 0");
    require(loadCalls == 1, "CLI load success should call loadProject once");
    require(launcherCalls == 0, "CLI load success should not show launcher");
}

void testLauncherCancelReturnsExitZero() {
    int launcherCalls = 0;
    const StartupFlowResult result = selectStartupProject(
        QStringList{QStringLiteral("finepaper")},
        StartupFlowCallbacks{
            .showLauncher = [&]() {
                ++launcherCalls;
                return ProjectLauncherResult{};
            }
        });

    require(result.action == StartupFlowResult::Action::Exit,
            "launcher cancel should exit");
    require(result.exitCode == 0, "launcher cancel should return exit code 0");
    require(launcherCalls == 1, "launcher cancel should show launcher once");
}

void testLauncherRetriesAfterOpenFailureUntilCreateSucceeds() {
    int loadCalls = 0;
    int createCalls = 0;
    int launcherCalls = 0;

    const QList<ProjectLauncherResult> launcherResults{
        ProjectLauncherResult{ProjectLauncherResult::Action::OpenProject,
                              QStringLiteral("/tmp/bad.fpproj")},
        ProjectLauncherResult{ProjectLauncherResult::Action::NewProject,
                              QStringLiteral("/tmp/new-project.fpproj")}
    };

    const StartupFlowResult result = selectStartupProject(
        QStringList{QStringLiteral("finepaper")},
        StartupFlowCallbacks{
            .loadProject = [&](const QString& path) {
                ++loadCalls;
                require(path == QStringLiteral("/tmp/bad.fpproj"),
                        "launcher open should pass path to loadProject");
                return false;
            },
            .createProject = [&](const QString& path) {
                ++createCalls;
                require(path == QStringLiteral("/tmp/new-project.fpproj"),
                        "launcher new project should pass path to createProject");
                return true;
            },
            .showLauncher = [&]() {
                require(launcherCalls < launcherResults.size(),
                        "launcher should not be called after success");
                return launcherResults.at(launcherCalls++);
            }
        });

    require(result.action == StartupFlowResult::Action::ShowMainWindow,
            "successful retry should show main window");
    require(result.exitCode == 0, "successful retry should keep exit code 0");
    require(loadCalls == 1, "open failure path should call loadProject once");
    require(createCalls == 1, "new project retry should call createProject once");
    require(launcherCalls == 2, "launcher should retry after failed open");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    try {
        testCliLoadFailureReturnsExitAndSkipsLauncher();
        testCliLoadSuccessReturnsShowMainWindowAndSkipsLauncher();
        testLauncherCancelReturnsExitZero();
        testLauncherRetriesAfterOpenFailureUntilCreateSucceeds();
    } catch (const std::exception& exception) {
        qCritical("%s", exception.what());
        return 1;
    }

    std::cout << "startupflow_test passed\n";
    return 0;
}
