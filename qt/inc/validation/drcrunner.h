// DRCRunner executes external DRC validation and parses results
#ifndef DRCRUNNER_H
#define DRCRUNNER_H

#include <QList>
#include <QHash>
#include <QString>
#include "ipcore/ipcatalogservice.h"
#include "project/pluginstate.h"
#include "validation/validationresult.h"

class Graph;

class DRCRunner {
public:
    QList<ValidationResult> validate(const Graph* graph,
                                     const IpCatalogEntry& ipcore,
                                     const ProjectIpInstanceRecord& instance);
private:
    QList<ValidationResult> parseErrors(const QString& stderr);

    QHash<QString, QString> m_externalToInternalIds;
};

#endif
