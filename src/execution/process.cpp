#include "execution/process.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <utility>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace finepaper {

struct TemporaryDirectoryLease::State final {
    State(const QString& pathTemplate, QString cleanupDescriptionValue)
        : directory(pathTemplate),
          cleanupDescription(std::move(cleanupDescriptionValue)) {}

    mutable std::mutex mutex;
    QTemporaryDir directory;
    QString cleanupDescription;
    bool finalized = false;
    TemporaryDirectoryFinalizationResult finalization;
};

TemporaryDirectoryLease::TemporaryDirectoryLease(
    const QString& pathTemplate,
    const QString& cleanupDescription)
    : m_state(std::make_shared<State>(
          pathTemplate, cleanupDescription.trimmed())) {}

bool TemporaryDirectoryLease::isValid() const {
    if (!m_state) {
        return false;
    }
    const std::scoped_lock lock(m_state->mutex);
    return m_state->directory.isValid();
}

QString TemporaryDirectoryLease::directoryPath() const {
    if (!m_state) {
        return {};
    }
    const std::scoped_lock lock(m_state->mutex);
    return m_state->directory.path();
}

TemporaryDirectoryFinalizationResult TemporaryDirectoryLease::finalize(
    TemporaryDirectoryFinalizationMode mode) const {
    if (!m_state) {
        return {};
    }
    const std::scoped_lock lock(m_state->mutex);
    if (m_state->finalized) {
        return m_state->finalization;
    }

    m_state->finalized = true;
    if (!m_state->directory.isValid()) {
        return m_state->finalization;
    }
    const QString path = m_state->directory.path();
    // Disable the destructor's implicit retry: every failure after explicit
    // finalization must remain observable to the caller.
    m_state->directory.setAutoRemove(false);
    if (mode == TemporaryDirectoryFinalizationMode::Retain) {
        m_state->finalization.cleanupUnresolved = true;
        m_state->finalization.retainedPath = path;
        return m_state->finalization;
    }
    if (m_state->directory.remove()) {
        return m_state->finalization;
    }

    m_state->finalization.cleanupUnresolved = true;
    m_state->finalization.retainedPath = path;
    m_state->finalization.error = QStringLiteral(
        "could not remove %1 at %2")
        .arg(m_state->cleanupDescription.isEmpty()
                 ? QStringLiteral("temporary Package runtime directory")
                 : m_state->cleanupDescription,
             path);
    return m_state->finalization;
}

namespace {

using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kProcessPollInterval = 25ms;
constexpr std::chrono::milliseconds kTerminationGrace = 500ms;
constexpr std::chrono::milliseconds kKillWait = 1000ms;
constexpr std::chrono::milliseconds kStartTimeout = 30s;
constexpr qint64 kMaximumInMemoryChannelBytes = 1024 * 1024;

constexpr qint64 milliseconds(
    std::chrono::milliseconds duration) noexcept {
    return duration / 1ms;
}

#ifdef Q_OS_UNIX
bool processGroupExists(pid_t processGroup) {
    if (processGroup <= 0) {
        return false;
    }
    errno = 0;
    return ::kill(-processGroup, 0) == 0 || errno == EPERM;
}
#endif

bool processTreeIsRunning(const QProcess& process, qint64 processGroupId) {
    if (process.state() != QProcess::NotRunning) {
        return true;
    }
#ifdef Q_OS_UNIX
    return processGroupExists(static_cast<pid_t>(processGroupId));
#else
    Q_UNUSED(processGroupId)
    return false;
#endif
}

bool stopProcessTree(QProcess& process, qint64 processGroupId) {
#ifdef Q_OS_UNIX
    const pid_t processGroup = static_cast<pid_t>(processGroupId);
    const bool groupTerminated = processGroup > 0
        && ::kill(-processGroup, SIGTERM) == 0;
    if (!groupTerminated && process.state() != QProcess::NotRunning) {
        process.terminate();
    }
#else
    Q_UNUSED(processGroupId)
    if (process.state() == QProcess::NotRunning) {
        return true;
    }
    process.terminate();
#endif

    // Give the complete operation group its grace period. Waiting only for the
    // direct child is insufficient: it may exit immediately while a helper is
    // still flushing files or handling SIGTERM.
    const qint64 terminationGraceMilliseconds = milliseconds(
        kTerminationGrace);
    QElapsedTimer graceTimer;
    graceTimer.start();
    while (graceTimer.elapsed() < terminationGraceMilliseconds) {
#ifdef Q_OS_UNIX
        if (process.state() == QProcess::NotRunning
            && !processGroupExists(processGroup)) {
            break;
        }
#else
        if (process.state() == QProcess::NotRunning) {
            break;
        }
#endif
        const int remaining = static_cast<int>(terminationGraceMilliseconds)
            - static_cast<int>(graceTimer.elapsed());
        const int slice = (std::min)(
            static_cast<int>(milliseconds(kProcessPollInterval)), remaining);
        if (process.state() == QProcess::NotRunning) {
            QThread::msleep(static_cast<unsigned long>(slice));
        } else {
            process.waitForFinished(slice);
        }
    }
    const bool parentFinished = process.state() == QProcess::NotRunning;
#ifdef Q_OS_UNIX
    const bool groupKilled = processGroupExists(processGroup)
        && ::kill(-processGroup, SIGKILL) == 0;
    if (!groupKilled && !parentFinished) {
        process.kill();
    }
#else
    if (!parentFinished) {
        process.kill();
    }
#endif
    if (!parentFinished) {
        process.waitForFinished(static_cast<int>(milliseconds(kKillWait)));
    }
#ifdef Q_OS_UNIX
    if (groupKilled) {
        const qint64 killWaitMilliseconds = milliseconds(kKillWait);
        QElapsedTimer killTimer;
        killTimer.start();
        while (processGroupExists(processGroup)
               && killTimer.elapsed() < killWaitMilliseconds) {
            QThread::msleep(static_cast<unsigned long>(
                milliseconds(kProcessPollInterval)));
        }
    }
#endif

    return !processTreeIsRunning(process, processGroupId);
}

void recordCleanupFailure(ProcessResult& result,
                          bool processTreeStopped,
                          qint64 processGroupId) {
    if (processTreeStopped) {
        return;
    }
    result.cleanupFailed = true;
#ifdef Q_OS_UNIX
    const QString cleanupError = QStringLiteral(
        "could not verify that Package process group %1 stopped")
        .arg(processGroupId);
#else
    Q_UNUSED(processGroupId)
    const QString cleanupError = QStringLiteral(
        "could not verify that the Package process stopped");
#endif
    if (result.error.isEmpty()) {
        result.error = cleanupError;
    } else {
        result.error += QStringLiteral("; ") + cleanupError;
    }
}

void appendProcessError(ProcessResult& result, const QString& error) {
    if (error.isEmpty()) {
        return;
    }
    if (result.error.isEmpty()) {
        result.error = error;
    } else {
        result.error += QStringLiteral("; ") + error;
    }
}

QString readCapturedChannel(const QString& path,
                            const QString& channelName,
                            ProcessResult& result,
                            bool* truncated) {
    if (!QFileInfo::exists(path)) {
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendProcessError(
            result,
            QStringLiteral("could not read captured Package %1")
                .arg(channelName));
        return {};
    }
    if (truncated) {
        *truncated = file.size() > kMaximumInMemoryChannelBytes;
    }
    if (file.size() <= kMaximumInMemoryChannelBytes) {
        return QString::fromUtf8(file.readAll());
    }
    const qint64 sectionBytes = kMaximumInMemoryChannelBytes / 2;
    QByteArray bytes = file.read(sectionBytes);
    bytes += QStringLiteral(
        "\n[Finepaper: middle of %1 omitted from the in-memory summary; the complete log remains on disk.]\n")
                 .arg(channelName)
                 .toUtf8();
    if (!file.seek(file.size() - sectionBytes)) {
        appendProcessError(
            result,
            QStringLiteral("could not seek captured Package %1")
                .arg(channelName));
        return QString::fromUtf8(bytes);
    }
    bytes += file.read(sectionBytes);
    return QString::fromUtf8(bytes);
}

} // namespace

ProcessResult runProcess(const QString& executable,
                         const QStringList& arguments,
                         const QString& workingDirectory,
                         int timeoutMilliseconds,
                         const QProcessEnvironment& environment) {
    return runProcess(executable,
                      arguments,
                      workingDirectory,
                      std::chrono::milliseconds(timeoutMilliseconds),
                      environment,
                      CancellationToken{});
}

ProcessResult runProcess(const QString& executable,
                         const QStringList& arguments,
                         const QString& workingDirectory,
                         std::chrono::milliseconds timeout,
                         const QProcessEnvironment& environment,
                         const CancellationToken& cancellation) {
    ProcessResult result;
    if (timeout <= 0ms) {
        result.error = QStringLiteral("process timeout must be a positive number of milliseconds");
        return result;
    }
    const auto executionLimit = milliseconds(timeout);
    if (cancellation.isCancellationRequested()) {
        result.cancelled = true;
        return result;
    }

    TemporaryDirectoryLease capture(
        QDir(workingDirectory).filePath(
            QStringLiteral(".finepaper-process-output-XXXXXX")),
        QStringLiteral("captured Package process output"));
    if (!capture.isValid()) {
        capture = TemporaryDirectoryLease(
            QDir::temp().filePath(
                QStringLiteral("finepaper-process-output-XXXXXX")),
            QStringLiteral("captured Package process output"));
    }
    if (!capture.isValid()) {
        result.error = QStringLiteral(
            "could not create the Package process output capture directory");
        return result;
    }
    const QString standardOutputPath = QDir(capture.directoryPath()).filePath(
        QStringLiteral("stdout.log"));
    const QString standardErrorPath = QDir(capture.directoryPath()).filePath(
        QStringLiteral("stderr.log"));
    result.standardOutputCapturePath = standardOutputPath;
    result.standardErrorCapturePath = standardErrorPath;
    result.outputCapture = capture;
    const auto finalizeCapturedOutput = [&] {
        if (result.cleanupFailed) {
            const TemporaryDirectoryFinalizationResult retained =
                result.outputCapture.finalize(
                    TemporaryDirectoryFinalizationMode::Retain);
            appendProcessError(
                result,
                QStringLiteral("captured process output was retained at %1")
                    .arg(retained.retainedPath));
        }
        result.standardOutput = readCapturedChannel(
            standardOutputPath,
            QStringLiteral("stdout"),
            result,
            &result.standardOutputTruncated);
        result.standardError = readCapturedChannel(
            standardErrorPath,
            QStringLiteral("stderr"),
            result,
            &result.standardErrorTruncated);
    };

    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(workingDirectory);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardOutputFile(
        standardOutputPath, QIODevice::Truncate);
    process.setStandardErrorFile(
        standardErrorPath, QIODevice::Truncate);
#ifdef Q_OS_UNIX
    // Package tools frequently launch another interpreter or vendor executable.
    // A private process group lets a timeout clean up that complete operation.
    process.setChildProcessModifier([&process] {
        if (::setpgid(0, 0) != 0) {
            process.failChildProcessModifier("setpgid", errno);
        }
    });
#endif
    process.start();

    bool started = false;
    const auto startLimit = milliseconds(kStartTimeout);
    QElapsedTimer startTimer;
    startTimer.start();
    while (process.state() == QProcess::Starting
           && startTimer.elapsed() < startLimit
           && !cancellation.isCancellationRequested()) {
        if (process.waitForStarted(
                static_cast<int>(milliseconds(kProcessPollInterval)))) {
            started = true;
            break;
        }
    }
    started = started || process.state() == QProcess::Running;
    const qint64 processGroupId = process.processId();
    if (cancellation.isCancellationRequested()) {
        result.started = started;
        result.cancelled = true;
        recordCleanupFailure(
            result,
            stopProcessTree(process, processGroupId),
            processGroupId);
        finalizeCapturedOutput();
        return result;
    }
    if (!started) {
        result.error = process.errorString();
        recordCleanupFailure(
            result,
            stopProcessTree(process, processGroupId),
            processGroupId);
        finalizeCapturedOutput();
        return result;
    }
    result.started = true;

    QElapsedTimer executionTimer;
    executionTimer.start();
    while (processTreeIsRunning(process, processGroupId)) {
        if (cancellation.isCancellationRequested()) {
            result.cancelled = true;
            recordCleanupFailure(
                result,
                stopProcessTree(process, processGroupId),
                processGroupId);
            break;
        }
        const qint64 remaining = executionLimit - executionTimer.elapsed();
        if (remaining <= 0) {
            result.timedOut = true;
            recordCleanupFailure(
                result,
                stopProcessTree(process, processGroupId),
                processGroupId);
            break;
        }

        const auto waitSlice = static_cast<int>((std::min)(
            remaining,
            milliseconds(kProcessPollInterval)));
        if (process.state() == QProcess::NotRunning) {
            // A launcher may exit before its helpers. Keep the operation alive
            // and cancellable until the complete private process group exits.
            QThread::msleep(static_cast<unsigned long>(waitSlice));
        } else {
            process.waitForFinished(waitSlice);
        }
    }

    // Cancellation can arrive while waitForFinished() is dispatching the
    // direct child's exit. The launcher may already be NotRunning while a
    // helper in its private process group is still alive, so re-check the
    // token and clean up the recorded group before publishing a normal result.
    if (!result.cancelled && !result.timedOut
        && cancellation.isCancellationRequested()) {
        result.cancelled = true;
        recordCleanupFailure(
            result,
            stopProcessTree(process, processGroupId),
            processGroupId);
    }
    finalizeCapturedOutput();
    result.crashed = !result.cancelled && !result.timedOut
        && process.exitStatus() == QProcess::CrashExit;
    if (!result.cancelled && !result.timedOut && !result.crashed
        && process.exitStatus() == QProcess::NormalExit) {
        result.exitCode = process.exitCode();
    }
    if (!result.cancelled && !result.timedOut
        && process.error() != QProcess::UnknownError
        && process.error() != QProcess::Timedout
        && result.error.isEmpty()) {
        result.error = process.errorString();
    }
    return result;
}

} // namespace finepaper
