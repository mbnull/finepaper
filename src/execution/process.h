#pragma once

#include "execution/cancellation.h"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <chrono>
#include <memory>

namespace finepaper {

enum class TemporaryDirectoryFinalizationMode {
    Remove,
    Retain
};

struct TemporaryDirectoryFinalizationResult {
    bool cleanupUnresolved = false;
    QString retainedPath;
    QString error;
};

class TemporaryDirectoryLease final {
public:
    TemporaryDirectoryLease() = default;
    TemporaryDirectoryLease(const QString& pathTemplate,
                            const QString& cleanupDescription);

    bool isValid() const;
    QString directoryPath() const;
    TemporaryDirectoryFinalizationResult finalize(
        TemporaryDirectoryFinalizationMode mode =
            TemporaryDirectoryFinalizationMode::Remove) const;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    bool crashed = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
    QString standardOutputCapturePath;
    QString standardErrorCapturePath;
    QString error;
    bool cancelled = false;
    bool cleanupFailed = false;
    bool standardOutputTruncated = false;
    bool standardErrorTruncated = false;
    // Copies share one typed lease. Callers that persist the full logs must
    // explicitly finalize it so capture cleanup failures remain observable.
    TemporaryDirectoryLease outputCapture;
};

ProcessResult runProcess(const QString& executable,
                         const QStringList& arguments,
                         const QString& workingDirectory,
                         int timeoutMilliseconds,
                         const QProcessEnvironment& environment =
                             QProcessEnvironment::systemEnvironment());

ProcessResult runProcess(const QString& executable,
                         const QStringList& arguments,
                         const QString& workingDirectory,
                         std::chrono::milliseconds timeout,
                         const QProcessEnvironment& environment,
                         const CancellationToken& cancellation);

} // namespace finepaper
