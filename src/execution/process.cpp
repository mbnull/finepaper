#include "execution/process.h"

#include <QProcess>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace finepaper {

ProcessResult runProcess(const QString& executable,
                         const QStringList& arguments,
                         const QString& workingDirectory,
                         int timeoutMilliseconds,
                         const QProcessEnvironment& environment) {
    ProcessResult result;
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(workingDirectory);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_UNIX
    // Package tools frequently launch another interpreter or vendor executable.
    // A private process group lets a timeout clean up that complete operation.
    process.setChildProcessModifier([] {
        ::setpgid(0, 0);
    });
#endif
    process.start();

    if (!process.waitForStarted()) {
        result.error = process.errorString();
        return result;
    }
    result.started = true;

    if (!process.waitForFinished(timeoutMilliseconds)) {
        result.timedOut = true;
#ifdef Q_OS_UNIX
        const pid_t processGroup = static_cast<pid_t>(process.processId());
        if (processGroup > 0) {
            ::kill(-processGroup, SIGTERM);
        } else {
            process.terminate();
        }
#else
        process.terminate();
#endif
        if (!process.waitForFinished(3000)) {
#ifdef Q_OS_UNIX
            if (processGroup > 0) {
                ::kill(-processGroup, SIGKILL);
            } else {
                process.kill();
            }
#else
            process.kill();
#endif
            process.waitForFinished(3000);
        }
    }

    result.standardOutput = QString::fromUtf8(process.readAllStandardOutput());
    result.standardError = QString::fromUtf8(process.readAllStandardError());
    result.exitCode = process.exitCode();
    result.crashed = process.exitStatus() == QProcess::CrashExit;
    if (process.error() != QProcess::UnknownError && result.error.isEmpty()) {
        result.error = process.errorString();
    }
    return result;
}

} // namespace finepaper
