// GeneratorRunner selects and builds IP-core generator process commands.
#pragma once

#include "ipcore/ipcatalogservice.h"

#include <QString>
#include <QStringList>

struct GeneratorCommand {
    bool valid = false;
    QString errorMessage;
    QString ipcoreId;
    QString workingDirectory;
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList arguments;
};

class GeneratorRunner {
public:
    static GeneratorCommand resolveForIpcore(const IpCatalogEntry& entry,
                                             const QString& inputPath,
                                             const QString& outputDirectory);
    static GeneratorCommand resolveDrcForIpcore(const IpCatalogEntry& entry,
                                                const QString& inputPath,
                                                const QString& outputDirectory);
};
