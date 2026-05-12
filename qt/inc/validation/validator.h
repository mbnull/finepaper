// Validator interface for checking graph topology correctness
#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <QList>
#include "validation/validationresult.h"

class Graph;

class Validator {
public:
    virtual ~Validator() = default;
    virtual QList<ValidationResult> validate(const Graph* graph) = 0;
};

// BasicValidator checks framework-level graph consistency only.
// IP/domain DRC is provided by each project IP instance's DRC command.
class BasicValidator : public Validator {
public:
    QList<ValidationResult> validate(const Graph* graph) override;

private:
    void checkInvalidConnections(const Graph* graph, QList<ValidationResult>& results);
};

#endif
