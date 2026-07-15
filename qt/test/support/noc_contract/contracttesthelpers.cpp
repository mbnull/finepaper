#include "contracttesthelpers.h"

#include "contractartifactloader.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <algorithm>
#include <stdexcept>

#ifndef IPCRAFT_CONTRACT_PYTHON
#error "IPCRAFT_CONTRACT_PYTHON must be configured as an absolute executable"
#endif

void requireContract(bool condition, const QString &message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

QStringList sortedUniqueStrings(const QStringList &values, const QString &location) {
    QStringList result = values;
    std::sort(result.begin(), result.end());
    requireContract(std::adjacent_find(result.cbegin(), result.cend()) == result.cend(),
                    location + QStringLiteral(" contains duplicates"));
    return result;
}

QString runContractPythonVerifier(const QString &relativeScript,
                                  const QStringList &arguments,
                                  const QString &requiredOutputFragment) {
    const QString root = ContractArtifactLoader::repositoryRoot();
    const QString script = root + QLatin1Char('/') + relativeScript;
    requireContract(QFileInfo(script).isFile() && !QFileInfo(script).isSymLink(),
                    relativeScript + QStringLiteral(" must be a regular verifier"));

    QProcess process;
    const QString python = QString::fromUtf8(IPCRAFT_CONTRACT_PYTHON);
    requireContract(QFileInfo(python).isAbsolute() && QFileInfo(python).isExecutable(),
                    QStringLiteral("configured Python executable is unavailable: ") + python);
    process.setProgram(python);
    process.setArguments(QStringList{script} + arguments);
    process.setWorkingDirectory(root);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("PYTHONPATH"));
    environment.remove(QStringLiteral("PYTHONHOME"));
    environment.insert(QStringLiteral("PYTHONHASHSEED"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    environment.insert(QStringLiteral("TZ"), QStringLiteral("UTC"));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    requireContract(process.waitForStarted(5000),
                    relativeScript + QStringLiteral(" failed to start"));
    requireContract(process.waitForFinished(120000),
                    relativeScript + QStringLiteral(" timed out"));
    const QString output = QString::fromUtf8(process.readAll());
    requireContract(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                    relativeScript + QStringLiteral(" failed: ") + output);
    requireContract(output.contains(requiredOutputFragment),
                    relativeScript + QStringLiteral(" omitted required summary: ") + output);
    return output;
}
