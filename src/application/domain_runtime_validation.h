#pragma once

#include "noc/model.h"
#include "package/package.h"

namespace finepaper::domain_runtime_validation {

// Checks the Package execution promise immediately before process-backed
// validation or generation. Authoring validation intentionally does not call
// this function so a partial runtime cannot make an existing Design
// impossible to inspect, repair, or migrate.
QVector<Diagnostic> validateConsumption(const NocDesign& design,
                                        const PackageDefinition& package);

} // namespace finepaper::domain_runtime_validation
