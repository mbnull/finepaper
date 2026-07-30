#include "application/endpoint_domain_assignment.h"

#include <algorithm>

namespace finepaper {

EndpointDomainAssignments normalizeEndpointDomainAssignments(
    const EndpointDomainAssignments& assignments) {
    EndpointDomainAssignments normalized;
    for (auto assignment = assignments.constBegin();
         assignment != assignments.constEnd(); ++assignment) {
        const QString typeId = assignment.key().trimmed();
        if (typeId.isEmpty()) {
            continue;
        }
        QStringList& domainIds = normalized[typeId];
        for (const QString& value : assignment.value()) {
            const QString domainId = value.trimmed();
            if (!domainId.isEmpty()) {
                domainIds.append(domainId);
            }
        }
    }

    for (auto assignment = normalized.begin();
         assignment != normalized.end();) {
        QStringList& domainIds = assignment.value();
        std::sort(domainIds.begin(), domainIds.end());
        domainIds.erase(
            std::unique(domainIds.begin(), domainIds.end()), domainIds.end());
        if (domainIds.isEmpty()) {
            assignment = normalized.erase(assignment);
        } else {
            ++assignment;
        }
    }
    return normalized;
}

EndpointDomainAssignments endpointDomainAssignments(
    const NocDesign& design,
    const QString& endpointId) {
    EndpointDomainAssignments combined;
    const QString requestedId = endpointId.trimmed();
    if (requestedId.isEmpty()) {
        return combined;
    }

    for (const DomainMembership& membership : design.domainMemberships) {
        if (membership.element.kind != ElementKind::Endpoint
            || membership.element.id != requestedId) {
            continue;
        }
        for (auto assignment = membership.assignments.constBegin();
             assignment != membership.assignments.constEnd(); ++assignment) {
            combined[assignment.key()].append(assignment.value());
        }
    }
    return normalizeEndpointDomainAssignments(combined);
}

} // namespace finepaper
