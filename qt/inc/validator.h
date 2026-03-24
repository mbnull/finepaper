// Validator interface for checking graph topology correctness
#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <QList>
#include "validationresult.h"

class Graph;

class Validator {
public:
    virtual ~Validator() = default;
    virtual QList<ValidationResult> validate(const Graph* graph) = 0;
};

// BasicValidator checks for invalid connections and unconnected ports
class BasicValidator : public Validator {
public:
    QList<ValidationResult> validate(const Graph* graph) override;

private:
    void checkInvalidConnections(const Graph* graph, QList<ValidationResult>& results);
    void checkUnconnectedPorts(const Graph* graph, QList<ValidationResult>& results);
};

#endif
