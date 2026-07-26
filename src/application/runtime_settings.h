#pragma once

#include <QString>
#include <QStringList>

namespace finepaper {

// Locations are host policy, not design data. Every northbound entrypoint uses
// this one resolver so Package discovery and default output placement agree.
struct RuntimeLocations {
    QStringList packageRoots;
    QString defaultOutputRoot;
};

RuntimeLocations resolveRuntimeLocations(
    const QStringList& explicitPackageRoots = {},
    const QString& workingDirectory = QString());

void appendPackageRoots(RuntimeLocations& locations,
                        const QStringList& additionalPackageRoots,
                        const QString& workingDirectory = QString());

} // namespace finepaper
