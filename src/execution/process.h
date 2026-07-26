#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace finepaper {

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    bool crashed = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
    QString error;
};

ProcessResult runProcess(const QString& executable,
                         const QStringList& arguments,
                         const QString& workingDirectory,
                         int timeoutMilliseconds,
                         const QProcessEnvironment& environment =
                             QProcessEnvironment::systemEnvironment());

} // namespace finepaper
