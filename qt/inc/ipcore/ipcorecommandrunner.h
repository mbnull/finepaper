#pragma once

#include "ipcore/ipcatalogservice.h"

#include <QString>
#include <QStringList>

struct IpCoreResolvedCommand {
    bool valid = false;
    QString errorMessage;
    QString ipcoreId;
    QString workingDirectory;
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList arguments;
};

class IpCoreCommandRunner {
public:
    static IpCoreResolvedCommand resolveGenerator(const IpCatalogEntry& entry,
                                                  const QString& inputPath,
                                                  const QString& outputDirectory);
    static IpCoreResolvedCommand resolveDrc(const IpCatalogEntry& entry,
                                            const QString& inputPath,
                                            const QString& outputDirectory);
};
