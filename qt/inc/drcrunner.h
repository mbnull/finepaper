// DRCRunner executes external DRC validation and parses results
#ifndef DRCRUNNER_H
#define DRCRUNNER_H

#include <QList>
#include <QHash>
#include <QString>
#include "validationresult.h"

class Graph;

class DRCRunner {
public:
    QList<ValidationResult> validate(const Graph* graph);
private:
    QList<ValidationResult> parseErrors(const QString& stderr);

    QHash<QString, QString> m_externalToInternalIds;
};

#endif
