#ifndef DRCRUNNER_H
#define DRCRUNNER_H

#include <QList>
#include <QString>
#include "validationresult.h"

class Graph;

class DRCRunner {
public:
    QList<ValidationResult> validate(const Graph* graph);
private:
    QString serializeToJson(const Graph* graph);
    QList<ValidationResult> parseErrors(const QString& stderr);
};

#endif
