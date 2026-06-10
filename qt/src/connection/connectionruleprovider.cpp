// Built-in connection rule providers backed by package manifest declarations.
#include "connection/connectionruleprovider.h"

#include "connection/connectionruleservice.h"
#include "ipcraft/ipcraftconnectionvalidator.h"

#include <utility>

namespace {

ProjectConnectionInterfaceRef interfaceRefForPort(const PortSemanticInfo& port) {
    return ProjectConnectionInterfaceRef{
        port.ref.moduleId,
        port.interfaceId.isEmpty() ? port.ref.portId : port.interfaceId
    };
}

IpcraftConnectionParticipant participantForPort(const PortSemanticInfo& port) {
    IpcraftConnectionParticipant participant;
    participant.packageId = port.packageId.isEmpty() ? port.ipcoreId : port.packageId;
    participant.moduleId = port.manifestModuleId.isEmpty() ? port.moduleType : port.manifestModuleId;
    participant.interfaceRef = interfaceRefForPort(port);
    return participant;
}

QString statusString(IpcraftConnectionStatus status) {
    if (status == IpcraftConnectionStatus::Valid) {
        return QStringLiteral("valid");
    }
    if (status == IpcraftConnectionStatus::Ambiguous) {
        return QStringLiteral("ambiguous");
    }
    return QStringLiteral("invalid");
}

QString rejectionReasonForDecision(const IpcraftConnectionDecision& decision) {
    return decision.message.contains(QStringLiteral("already used"))
        ? QStringLiteral("interface_occupied")
        : QStringLiteral("interface_class_mismatch");
}

} // namespace

bool PackageConnectionRuleProvider::canEvaluate(const PortSemanticInfo& source,
                                                const PortSemanticInfo& target) const {
    return !source.acceptRules.isEmpty() && !target.acceptRules.isEmpty();
}

ConnectionRuleProviderResult PackageConnectionRuleProvider::evaluate(
    const ConnectionRuleProviderRequest& request) const {
    IpcraftConnectionValidator validator(request.manifests, request.currentConnections);
    const IpcraftConnectionDecision decision = validator.validate(
        {participantForPort(request.source), participantForPort(request.target)},
        request.selectedConnectionClassId);

    ConnectionRuleProviderResult result;
    if (decision.status == IpcraftConnectionStatus::Invalid) {
        result.status = ConnectionRuleProviderStatus::Rejected;
        result.reasonCode = rejectionReasonForDecision(decision);
        result.message = decision.message;
        result.connectionStatus = statusString(decision.status);
        return result;
    }

    result.status = decision.status == IpcraftConnectionStatus::Ambiguous
        ? ConnectionRuleProviderStatus::Warning
        : ConnectionRuleProviderStatus::Allowed;
    result.connectionClassId = decision.selectedClassId;
    result.connectionStatus = statusString(decision.status);
    result.alternatives = decision.alternatives;
    result.message = decision.message;
    result.normalizedInterfaces = decision.normalizedInterfaces;
    return result;
}
