#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace finepaper {

// Package discovery, installation, and runtime probing must agree on root
// identity even while a path is temporarily missing. Existing paths use their
// canonical identity; unavailable paths retain a clean absolute fallback.
[[nodiscard]] inline QString normalizedPackageRootPath(
    const QString& path) {
    const QFileInfo info(path);
    const QString canonicalPath = info.canonicalFilePath();
    return canonicalPath.isEmpty()
        ? QDir::cleanPath(info.absoluteFilePath())
        : canonicalPath;
}

} // namespace finepaper
