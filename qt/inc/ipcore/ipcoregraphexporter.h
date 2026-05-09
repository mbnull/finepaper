// IpCoreGraphExporter serializes the active IP-core graph for generator/DRC tools.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"

#include <QHash>
#include <QJsonDocument>
#include <QString>

class Graph;

struct IpCoreGraphExportRequest {
    const Graph* graph = nullptr;
    IpCatalogEntry ipcore;
    ProjectIpInstanceRecord instance;
    QString designName;
    QHash<QString, QString>* externalToInternalIds = nullptr;
};

struct IpCoreGraphExportResult {
    bool success = false;
    QJsonDocument document;
    QString error;
};

class IpCoreGraphExporter {
public:
    static QString schemaName();
    static IpCoreGraphExportResult exportGraph(const IpCoreGraphExportRequest& request);
};
