#pragma once

#include "noc/model.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace finepaper {

using EndpointDomainAssignments = QHash<QString, QStringList>;

// Domain memberships are stored independently from Endpoint instances.  A
// detached Endpoint therefore needs an explicit snapshot of its assignments
// if reconnecting it is expected to preserve optional and multiple choices.
// These helpers also tolerate duplicate membership records in a damaged draft
// and return one deterministic, normalized assignment map.
[[nodiscard]] EndpointDomainAssignments normalizeEndpointDomainAssignments(
    const EndpointDomainAssignments& assignments);

[[nodiscard]] EndpointDomainAssignments endpointDomainAssignments(
    const NocDesign& design,
    const QString& endpointId);

} // namespace finepaper
