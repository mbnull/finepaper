// FrameworkPaths implementation with environment override and parent-directory probing.
#include "common/frameworkpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

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

} // namespace FrameworkPaths
