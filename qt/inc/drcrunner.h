#ifndef DRCRUNNER_H
#define DRCRUNNER_H

#include <QList>
#include "validationresult.h"

class Graph;

class DRCRunner {
public:
    QList<ValidationResult> validate(const Graph* graph);
};

#endif
