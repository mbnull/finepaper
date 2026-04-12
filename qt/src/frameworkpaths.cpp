#include "frameworkpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

const auto frameworkpath = "../framework";


namespace {

bool isValidFrameworkRoot(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }

    const QDir dir(path);
    return QFileInfo(dir.filePath("bin/generate")).exists() &&
           QFileInfo(dir.filePath("template")).isDir();
}

QString normalizeFrameworkRoot(const QString& path) {
    if (path.isEmpty()) {
        return {};
    }

    QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }

    if (info.isDir()) {
        const QString directoryPath = info.absoluteFilePath();
        if (isValidFrameworkRoot(directoryPath)) {
            return QDir(directoryPath).absolutePath();
        }
        return {};
    }

    if (info.fileName() == "generate" && info.dir().dirName() == "bin") {
        QDir frameworkDir(info.dir());
        frameworkDir.cdUp();
        const QString candidate = frameworkDir.absolutePath();
        if (isValidFrameworkRoot(candidate)) {
            return candidate;
        }
    }

    return {};
}

QString findFrameworkPathFrom(const QString& startPath) {
    if (startPath.isEmpty()) {
        return {};
    }

    QDir dir(startPath);
    while (true) {
        const QString directCandidate = normalizeFrameworkRoot(dir.filePath("framework"));
        if (!directCandidate.isEmpty()) {
            return directCandidate;
        }

        const QString siblingCandidate = normalizeFrameworkRoot(dir.filePath("../framework"));
        if (!siblingCandidate.isEmpty()) {
            return siblingCandidate;
        }

        if (!dir.cdUp()) {
            return {};
        }
    }
}

QString firstExistingFile(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isFile()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return {};
}

QString resolveBundleArtifactFromEnvironment(const QString& envVarName,
                                             const QStringList& directoryCandidates,
                                             const QStringList& siblingCandidates = {}) {
    const QString envPath = qEnvironmentVariable(envVarName.toUtf8().constData());
    if (envPath.isEmpty()) {
        return {};
    }

    QFileInfo info(envPath);
    if (info.isDir()) {
        QStringList candidates;
        for (const QString& relativePath : directoryCandidates) {
            candidates.append(QDir(info.absoluteFilePath()).filePath(relativePath));
        }
        return firstExistingFile(candidates);
    }

    if (info.isFile()) {
        if (siblingCandidates.isEmpty()) {
            return info.absoluteFilePath();
        }

        QStringList candidates;
        const QDir dir = info.absoluteDir();
        for (const QString& relativePath : siblingCandidates) {
            candidates.append(dir.filePath(relativePath));
        }
        return firstExistingFile(candidates);
    }

    return {};
}

QString resolveBundleArtifact(const QStringList& candidates,
                              const QString& envVarName,
                              const QStringList& envDirCandidates,
                              const QStringList& envSiblingCandidates = {}) {
    const QString envBundlePath = resolveBundleArtifactFromEnvironment(envVarName,
                                                                      envDirCandidates,
                                                                      envSiblingCandidates);
    if (!envBundlePath.isEmpty()) {
        return envBundlePath;
    }

    QString frameworkPath = FrameworkPaths::resolveFrameworkPath();
    if (!frameworkPath.isEmpty()) {
        QStringList frameworkCandidates;
        for (const QString& candidate : candidates) {
            frameworkCandidates.append(QDir(frameworkPath).filePath(candidate));
            frameworkCandidates.append(QDir(frameworkPath).filePath("ip/" + candidate));
        }
        const QString frameworkBundlePath = firstExistingFile(frameworkCandidates);
        if (!frameworkBundlePath.isEmpty()) {
            return frameworkBundlePath;
        }
    }

    for (const QString& root : {QDir::currentPath(), QCoreApplication::applicationDirPath()}) {
        QDir dir(root);
        while (true) {
            QStringList localCandidates;
            for (const QString& candidate : candidates) {
                localCandidates.append(dir.filePath(candidate));
                localCandidates.append(dir.filePath("framework/" + candidate));
                localCandidates.append(dir.filePath("framework/ip/" + candidate));
            }
            const QString bundlePath = firstExistingFile(localCandidates);
            if (!bundlePath.isEmpty()) {
                return bundlePath;
            }

            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return {};
}

} // namespace

namespace FrameworkPaths {

QString resolveFrameworkPath() {
    const QString envPath = normalizeFrameworkRoot(qEnvironmentVariable("FRAMEWORK_PATH"));
    if (!envPath.isEmpty()) {
        return envPath;
    }

    const QString currentDirPath = findFrameworkPathFrom(QDir::currentPath());
    if (!currentDirPath.isEmpty()) {
        return currentDirPath;
    }

    return findFrameworkPathFrom(QCoreApplication::applicationDirPath());
}

QString resolveTemplatePath() {
    const QString frameworkPath = resolveFrameworkPath();
    if (frameworkPath.isEmpty()) {
        return {};
    }

    return QDir(frameworkPath).filePath("template");
}

QString resolveModuleBundlePath() {
    return resolveBundleArtifact({QStringLiteral("bundles/modules.json")},
                                 QStringLiteral("BUNDLE_PATH"),
                                 {QStringLiteral("bundles/modules.json"),
                                  QStringLiteral("modules.json")});
}

QString resolveModulePresentationPath() {
    const QString explicitPresentationPath =
        resolveBundleArtifactFromEnvironment(QStringLiteral("BUNDLE_UI_PATH"),
                                            {QStringLiteral("bundles/modules.ui.xml"),
                                             QStringLiteral("modules.ui.xml")});
    if (!explicitPresentationPath.isEmpty()) {
        return explicitPresentationPath;
    }

    if (qEnvironmentVariableIsSet("BUNDLE_PATH")) {
        return resolveBundleArtifactFromEnvironment(QStringLiteral("BUNDLE_PATH"),
                                                    {QStringLiteral("bundles/modules.ui.xml"),
                                                     QStringLiteral("modules.ui.xml")},
                                                    {QStringLiteral("modules.ui.xml")});
    }

    return resolveBundleArtifact({QStringLiteral("bundles/modules.ui.xml")},
                                 QString(),
                                 {});
}

} // namespace FrameworkPaths
