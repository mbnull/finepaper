#include "contracttesthelpers.h"

#include "contractartifactloader.h"

#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>

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

namespace {

QStringList pythonBytecodePaths(const QString &root) {
    const QDir repository(root);
    QStringList result;
    QDirIterator iterator(root, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        if (absolute.contains(QStringLiteral("/__pycache__")) ||
            absolute.endsWith(QStringLiteral(".pyc"))) {
            result.append(repository.relativeFilePath(absolute));
        }
    }
    return result;
}

bool isWithin(const QString &root, const QString &candidate) {
    QString normalizedRoot = QDir::cleanPath(root);
    const QString normalizedCandidate = QDir::cleanPath(candidate);
    while (normalizedRoot.size() > 1 && normalizedRoot.endsWith(QLatin1Char('/'))) {
        normalizedRoot.chop(1);
    }
    return normalizedCandidate == normalizedRoot ||
           normalizedCandidate.startsWith(normalizedRoot + QLatin1Char('/'));
}

} // namespace

QString runContractPythonVerifier(const QString &relativeScript,
                                  const QStringList &arguments,
                                  const QString &requiredOutputFragment) {
    const QString root = ContractArtifactLoader::repositoryRoot();
    const QString script = root + QLatin1Char('/') + relativeScript;
    requireContract(QFileInfo(script).isFile() && !QFileInfo(script).isSymLink(),
                    relativeScript + QStringLiteral(" must be a regular verifier"));
    requireContract(pythonBytecodePaths(root).isEmpty(),
                    QStringLiteral("repository contains Python bytecode before verifier launch"));

    QProcess process;
    const QString python = QString::fromUtf8(IPCRAFT_CONTRACT_PYTHON);
    requireContract(QFileInfo(python).isAbsolute() && QFileInfo(python).isExecutable(),
                    QStringLiteral("configured Python executable is unavailable: ") + python);
    process.setProgram(python);
    process.setArguments(QStringList{QStringLiteral("-B"), script} + arguments);
    process.setWorkingDirectory(root);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("PYTHONPATH"));
    environment.remove(QStringLiteral("PYTHONHOME"));
    environment.remove(QStringLiteral("PYTHONSTARTUP"));
    environment.remove(QStringLiteral("PYTHONUSERBASE"));
    environment.insert(QStringLiteral("PYTHONHASHSEED"), QStringLiteral("0"));
    environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
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
    requireContract(pythonBytecodePaths(root).isEmpty(),
                    relativeScript + QStringLiteral(" created Python bytecode in the repository"));
    return output;
}

void validateFrozenMarkdownLink(const QString &repositoryRoot,
                                const QString &sourceRelativePath,
                                const QString &rawTarget,
                                const QSet<QString> &frozenPaths) {
    const QString canonicalRoot = QFileInfo(repositoryRoot).canonicalFilePath();
    requireContract(!canonicalRoot.isEmpty(), QStringLiteral("Markdown repository root is missing"));
    QString target = rawTarget.trimmed();
    if (target.startsWith(QLatin1Char('<'))) {
        requireContract(target.endsWith(QLatin1Char('>')),
                        sourceRelativePath + QStringLiteral(": malformed angle-bracket link"));
        target = target.mid(1, target.size() - 2);
    }
    if (target.startsWith(QLatin1Char('#'))) {
        return;
    }

    const QUrl url(target, QUrl::StrictMode);
    requireContract(url.isValid(), sourceRelativePath + QStringLiteral(": invalid Markdown URL"));
    if (!url.scheme().isEmpty()) {
        requireContract(url.scheme() == QStringLiteral("http") ||
                            url.scheme() == QStringLiteral("https") ||
                            url.scheme() == QStringLiteral("mailto"),
                        sourceRelativePath + QStringLiteral(": unsupported Markdown URL scheme"));
        return;
    }
    const QString decodedPath = url.path(QUrl::FullyDecoded);
    requireContract(!decodedPath.isEmpty() && !QDir::isAbsolutePath(decodedPath) &&
                        !decodedPath.contains(QLatin1Char('\\')),
                    sourceRelativePath + QStringLiteral(": link must be a relative portable path"));

    const QString sourceAbsolute =
        QDir(canonicalRoot).absoluteFilePath(QDir::cleanPath(sourceRelativePath));
    requireContract(isWithin(canonicalRoot, sourceAbsolute),
                    sourceRelativePath + QStringLiteral(": source escapes repository"));
    const QString candidateAbsolute = QDir(QFileInfo(sourceAbsolute).path())
                                          .absoluteFilePath(decodedPath);
    const QString cleanCandidate = QDir::cleanPath(candidateAbsolute);
    requireContract(isWithin(canonicalRoot, cleanCandidate),
                    sourceRelativePath + QStringLiteral(": relative link escapes repository"));

    QString current = canonicalRoot;
    const QString relativeCandidate = QDir(canonicalRoot).relativeFilePath(cleanCandidate);
    for (const QString &segment : relativeCandidate.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        current = QDir(current).filePath(segment);
        requireContract(!QFileInfo(current).isSymLink(),
                        sourceRelativePath + QStringLiteral(": relative link crosses a symlink"));
    }
    const QFileInfo info(cleanCandidate);
    requireContract(info.exists() && info.isFile() && !info.isSymLink(),
                    sourceRelativePath + QStringLiteral(": relative link target is missing or not a file"));
    const QString canonicalTarget = info.canonicalFilePath();
    requireContract(isWithin(canonicalRoot, canonicalTarget),
                    sourceRelativePath + QStringLiteral(": relative link resolves outside repository"));
    const QString frozenPath = QDir::fromNativeSeparators(
        QDir(canonicalRoot).relativeFilePath(canonicalTarget));
    requireContract(frozenPaths.contains(frozenPath),
                    sourceRelativePath + QStringLiteral(": relative link target is not frozen: ") +
                        frozenPath);
}
