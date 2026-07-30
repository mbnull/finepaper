#include "features/attachment/endpoint_attachment_rules.h"

#include <QSet>

#include <algorithm>
#include <utility>

namespace finepaper::attachment {
namespace {

QString automaticPortLabel(int index, int count) {
    return count == 1
        ? QStringLiteral("EP")
        : QStringLiteral("EP%1").arg(index);
}

bool validPortDefinitions(const Policy& policy) {
    if (policy.ports.isEmpty()) {
        return false;
    }
    QSet<QString> ids;
    QSet<QString> exactSlots;
    for (const PortDefinition& port : policy.ports) {
        const QString id = port.id.trimmed();
        if (id.isEmpty() || ids.contains(id)) {
            return false;
        }
        ids.insert(id);
        if (policy.slotMode == AttachmentSlotMode::Explicit) {
            if (!port.exactSlot || port.exactSlot->trimmed().isEmpty()
                || exactSlots.contains(*port.exactSlot)) {
                return false;
            }
            exactSlots.insert(*port.exactSlot);
        } else if (port.exactSlot) {
            return false;
        }
    }
    return true;
}

QString routerDescription(std::optional<RouterPosition> router) {
    return router
        ? QStringLiteral("Router (%1, %2)").arg(router->x).arg(router->y)
        : QStringLiteral("The Router");
}

} // namespace

std::optional<PortOffset> PortLayout::portForEndpoint(
    const QString& endpointId) const {
    const auto port = portsByEndpoint.constFind(endpointId);
    return port == portsByEndpoint.constEnd()
        ? std::nullopt : std::optional<PortOffset>(*port);
}

bool PortLayout::portAvailable(
    PortOffset port,
    const QString& ignoredEndpointId) const {
    if (port < 0) {
        return false;
    }
    const auto owner = endpointsByPort.constFind(port);
    return owner == endpointsByPort.constEnd()
        || *owner == ignoredEndpointId;
}

std::optional<PortOffset> PortLayout::firstAvailablePort(
    qsizetype portCount,
    const QString& ignoredEndpointId) const {
    for (PortOffset port = 0; port < portCount; ++port) {
        if (portAvailable(port, ignoredEndpointId)) {
            return port;
        }
    }
    return std::nullopt;
}

Policy policyFromPackage(const AttachmentDefinition& definition) {
    Policy policy;
    policy.source = PolicySource::Package;
    policy.slotMode = definition.slotMode;
    policy.maxPerRouter = definition.maxPerRouter;
    if (definition.maxPerRouter <= 0
        || definition.maxPerRouter
            > kMaximumEndpointAttachmentsPerRouter) {
        return policy;
    }

    if (definition.slotMode == AttachmentSlotMode::Explicit) {
        const QVector<AttachmentSlotDefinition> definitions =
            effectiveExplicitAttachmentSlots(definition);
        policy.ports.reserve(definitions.size());
        for (const AttachmentSlotDefinition& slot : definitions) {
            policy.ports.append({
                slot.id,
                slot.label.trimmed().isEmpty() ? slot.id : slot.label,
                slot.id,
            });
        }
    } else if (definition.slotMode == AttachmentSlotMode::Automatic
               && definition.maxPerRouter > 0) {
        policy.ports.reserve(definition.maxPerRouter);
        for (int index = 0; index < definition.maxPerRouter; ++index) {
            policy.ports.append({
                QString::number(index),
                automaticPortLabel(index, definition.maxPerRouter),
                std::nullopt,
            });
        }
    }
    return policy;
}

Policy inferReadOnlyPolicy(const NocDesign& design) {
    Policy policy;
    policy.source = PolicySource::InferredReadOnly;
    policy.slotMode = AttachmentSlotMode::Automatic;

    QHash<QString, int> endpointsPerRouter;
    QSet<QString> persistedSlotIds;
    for (const EndpointInstance& endpoint : design.endpoints) {
        const QString router = routerId(endpoint.attachment.router);
        int& endpointCount = endpointsPerRouter[router];
        endpointCount = (std::min)(
            endpointCount + 1, kMaximumEndpointAttachmentsPerRouter);
        policy.maxPerRouter = (std::max)(
            policy.maxPerRouter, endpointCount);
        if (endpoint.attachment.slot
            && !endpoint.attachment.slot->trimmed().isEmpty()) {
            persistedSlotIds.insert(*endpoint.attachment.slot);
        }
    }
    policy.maxPerRouter = (std::max)(policy.maxPerRouter, 1);

    QStringList orderedSlotIds(
        persistedSlotIds.cbegin(), persistedSlotIds.cend());
    orderedSlotIds.sort(Qt::CaseSensitive);
    if (orderedSlotIds.size() > kMaximumEndpointAttachmentsPerRouter) {
        orderedSlotIds.resize(kMaximumEndpointAttachmentsPerRouter);
    }
    policy.ports.reserve((std::max)(
        policy.maxPerRouter, static_cast<int>(orderedSlotIds.size())));
    for (const QString& id : std::as_const(orderedSlotIds)) {
        policy.ports.append({id, id, std::nullopt});
    }

    int fallbackIndex = 0;
    while (policy.ports.size() < policy.maxPerRouter) {
        QString id;
        do {
            id = QStringLiteral("__view_%1").arg(fallbackIndex++);
        } while (persistedSlotIds.contains(id));
        persistedSlotIds.insert(id);
        policy.ports.append({
            id,
            automaticPortLabel(
                static_cast<int>(policy.ports.size()), policy.maxPerRouter),
            std::nullopt,
        });
    }
    policy.maxPerRouter = (std::max)(
        policy.maxPerRouter, static_cast<int>(policy.ports.size()));
    return policy;
}

bool policyValid(const Policy& policy) {
    if (policy.maxPerRouter <= 0
        || policy.maxPerRouter > kMaximumEndpointAttachmentsPerRouter
        || policy.ports.size() > kMaximumEndpointAttachmentsPerRouter
        || (policy.slotMode != AttachmentSlotMode::Automatic
            && policy.slotMode != AttachmentSlotMode::Explicit)) {
        return false;
    }
    return validPortDefinitions(policy)
        && (policy.slotMode != AttachmentSlotMode::Automatic
            || policy.ports.size() == policy.maxPerRouter);
}

bool routerBelongsToDerivedMesh(
    const NocDesign& design,
    RouterPosition router) {
    return design.topology.type == QStringLiteral("mesh")
        && design.topology.rows > 0
        && design.topology.columns > 0
        && design.topology.rows <= kMaximumMeshDimension
        && design.topology.columns <= kMaximumMeshDimension
        && static_cast<qint64>(design.topology.rows)
               * static_cast<qint64>(design.topology.columns)
               <= kMaximumProjectedRouterCount
        && router.x >= 0
        && router.y >= 0
        && router.x < design.topology.columns
        && router.y < design.topology.rows;
}

RuleDecision connectionPossible(
    bool editingEnabled,
    const ConnectionCandidate& candidate) {
    if (!editingEnabled) {
        return RuleDecision::reject(Rejection::ReadOnly);
    }
    if (candidate.output == ConnectionHandleKind::RouterTopologyPort
        || candidate.input == ConnectionHandleKind::RouterTopologyPort) {
        return RuleDecision::reject(Rejection::DerivedRouterTopology);
    }

    const bool outputMissing =
        candidate.output == ConnectionHandleKind::Missing;
    const bool inputMissing =
        candidate.input == ConnectionHandleKind::Missing;
    if (outputMissing && inputMissing) {
        return RuleDecision::reject(Rejection::InvalidPort);
    }
    if (outputMissing) {
        return candidate.input == ConnectionHandleKind::RouterAttachmentInput
            ? RuleDecision::allow()
            : RuleDecision::reject(Rejection::InvalidPort);
    }
    if (inputMissing) {
        return candidate.output
                == ConnectionHandleKind::EndpointAttachmentOutput
            ? RuleDecision::allow()
            : RuleDecision::reject(Rejection::InvalidPort);
    }
    return candidate.output
                   == ConnectionHandleKind::EndpointAttachmentOutput
            && candidate.input
                   == ConnectionHandleKind::RouterAttachmentInput
        ? RuleDecision::allow()
        : RuleDecision::reject(Rejection::InvalidPort);
}

namespace {

PortLayout portLayoutForSortedEndpoints(
    const QVector<const EndpointInstance*>& endpoints,
    const Policy& policy,
    const QHash<QString, PortOffset>& portOffsetsById) {
    PortLayout layout;
    QVector<std::optional<PortOffset>> assignedPorts(
        endpoints.size(), std::nullopt);
    QSet<PortOffset> usedPorts;
    // Reserve valid persisted slots first. Otherwise an earlier unslotted or
    // damaged Endpoint could steal the labeled port owned by a later one.
    for (qsizetype endpointIndex = 0;
         endpointIndex < endpoints.size(); ++endpointIndex) {
        const EndpointInstance* endpoint = endpoints.at(endpointIndex);
        if (!endpoint->attachment.slot
            || endpoint->attachment.slot->trimmed().isEmpty()) {
            continue;
        }
        const auto port = portOffsetsById.constFind(*endpoint->attachment.slot);
        if (port != portOffsetsById.constEnd() && !usedPorts.contains(*port)) {
            assignedPorts[endpointIndex] = *port;
            usedPorts.insert(*port);
        }
    }

    PortOffset nextFreePort = 0;
    for (qsizetype endpointIndex = 0;
         endpointIndex < endpoints.size(); ++endpointIndex) {
        const EndpointInstance* endpoint = endpoints.at(endpointIndex);
        std::optional<PortOffset> selectedPort = assignedPorts.at(endpointIndex);
        if (!selectedPort) {
            while (nextFreePort < policy.ports.size()
                   && usedPorts.contains(nextFreePort)) {
                ++nextFreePort;
            }
            if (nextFreePort < policy.ports.size()) {
                selectedPort = nextFreePort;
                usedPorts.insert(nextFreePort++);
            }
        }
        if (!selectedPort) {
            layout.overflowEndpointIds.append(endpoint->id);
            continue;
        }
        layout.portsByEndpoint.insert(endpoint->id, *selectedPort);
        layout.endpointsByPort.insert(*selectedPort, endpoint->id);
    }
    return layout;
}

void sortEndpointsById(QVector<const EndpointInstance*>& endpoints) {
    std::sort(
        endpoints.begin(), endpoints.end(),
        [](const auto* left, const auto* right) {
            return left->id < right->id;
        });
}

} // namespace

PortLayout resolvePortLayout(
    const NocDesign& design,
    const Policy& policy,
    RouterPosition router) {
    if (!policyValid(policy) || !routerBelongsToDerivedMesh(design, router)) {
        return {};
    }

    QVector<const EndpointInstance*> endpoints;
    for (const EndpointInstance& endpoint : design.endpoints) {
        if (endpoint.attachment.router == router) {
            endpoints.append(&endpoint);
        }
    }
    sortEndpointsById(endpoints);
    QHash<QString, PortOffset> portOffsetsById;
    portOffsetsById.reserve(policy.ports.size());
    for (PortOffset port = 0; port < policy.ports.size(); ++port) {
        portOffsetsById.insert(policy.ports.at(port).id, port);
    }
    return portLayoutForSortedEndpoints(
        endpoints, policy, portOffsetsById);
}

DesignIndex buildDesignIndex(
    const NocDesign& design,
    const Policy& policy) {
    DesignIndex index;
    index.policy = policy;
    index.validPolicy = policyValid(policy);
    if (index.validPolicy) {
        index.portOffsetsById.reserve(policy.ports.size());
        index.portOffsetsByExactSlot.reserve(policy.ports.size());
        for (PortOffset port = 0; port < policy.ports.size(); ++port) {
            const PortDefinition& definition = policy.ports.at(port);
            index.portOffsetsById.insert(definition.id, port);
            if (definition.exactSlot) {
                index.portOffsetsByExactSlot.insert(
                    *definition.exactSlot, port);
            }
        }
    }
    QHash<QString, QVector<const EndpointInstance*>> endpointsByRouter;
    index.endpointAttachments.reserve(design.endpoints.size());
    for (const EndpointInstance& endpoint : design.endpoints) {
        if (!index.endpointAttachments.contains(endpoint.id)) {
            index.endpointAttachments.insert(endpoint.id, endpoint.attachment);
        }
        endpointsByRouter[routerId(endpoint.attachment.router)].append(&endpoint);
    }

    index.routers.reserve(endpointsByRouter.size());
    for (auto iterator = endpointsByRouter.begin();
         iterator != endpointsByRouter.end(); ++iterator) {
        QVector<const EndpointInstance*>& endpoints = iterator.value();
        sortEndpointsById(endpoints);
        RouterOccupancy occupancy;
        occupancy.endpointCount = endpoints.size();
        for (const EndpointInstance* endpoint : std::as_const(endpoints)) {
            if (endpoint->attachment.slot
                && !endpoint->attachment.slot->trimmed().isEmpty()) {
                RouterOccupancy::SlotOccupancy& slot =
                    occupancy.slotOccupancies[*endpoint->attachment.slot];
                ++slot.endpointCount;
                slot.soleEndpointId = slot.endpointCount == 1
                    ? endpoint->id : QString{};
            }
        }
        if (index.validPolicy) {
            occupancy.layout = portLayoutForSortedEndpoints(
                endpoints, policy, index.portOffsetsById);
            occupancy.firstUnoccupiedPort =
                occupancy.layout.firstAvailablePort(policy.ports.size());
            for (auto slot = occupancy.slotOccupancies.constBegin();
                 slot != occupancy.slotOccupancies.constEnd(); ++slot) {
                if (index.portOffsetsByExactSlot.contains(slot.key())) {
                    ++occupancy.occupiedPolicySlotCount;
                }
            }
        }
        index.routers.insert(iterator.key(), std::move(occupancy));
    }
    return index;
}

namespace {

bool slotAvailable(
    const RouterOccupancy* occupancy,
    const QString& exactSlot,
    const QString& ignoredEndpointId) {
    if (!occupancy) {
        return true;
    }
    const auto slot = occupancy->slotOccupancies.constFind(exactSlot);
    return slot == occupancy->slotOccupancies.constEnd()
        || slot->endpointCount == 0
        || (slot->endpointCount == 1
            && slot->soleEndpointId == ignoredEndpointId);
}

bool explicitSlotAvailable(
    const DesignIndex& index,
    const RouterOccupancy* occupancy,
    const QString& ignoredEndpointId) {
    if (!occupancy
        || occupancy->occupiedPolicySlotCount
            < index.portOffsetsByExactSlot.size()) {
        return true;
    }
    const auto ignoredAttachment = index.endpointAttachments.constFind(
        ignoredEndpointId);
    return ignoredAttachment != index.endpointAttachments.constEnd()
        && ignoredAttachment->slot
        && index.portOffsetsByExactSlot.contains(*ignoredAttachment->slot)
        && slotAvailable(
            occupancy, *ignoredAttachment->slot, ignoredEndpointId);
}

std::optional<PortOffset> firstAvailableDisplayPort(
    const RouterOccupancy* occupancy,
    const QString& ignoredEndpointId) {
    if (!occupancy) {
        return PortOffset{0};
    }
    std::optional<PortOffset> available = occupancy->firstUnoccupiedPort;
    const std::optional<PortOffset> ignoredPort =
        occupancy->layout.portForEndpoint(ignoredEndpointId);
    if (ignoredPort && (!available || *ignoredPort < *available)) {
        available = ignoredPort;
    }
    return available;
}

} // namespace

SlotResolution resolveSlot(
    const NocDesign& design,
    const Policy& policy,
    AttachmentTarget target,
    const QString& ignoredEndpointId) {
    const DesignIndex index = buildDesignIndex(design, policy);
    return resolveSlot(
        design,
        index,
        std::move(target),
        ignoredEndpointId);
}

SlotResolution resolveSlot(
    const NocDesign& design,
    const DesignIndex& index,
    AttachmentTarget target,
    const QString& ignoredEndpointId,
    SlotResolutionDetail detail) {
    SlotResolution result;
    const Policy& policy = index.policy;
    if (policy.source != PolicySource::Package) {
        result.rejection = Rejection::ReadOnly;
        return result;
    }
    if (!index.validPolicy) {
        result.rejection = Rejection::InvalidPolicy;
        return result;
    }
    if (!routerBelongsToDerivedMesh(design, target.router)) {
        result.rejection = Rejection::UnknownRouter;
        return result;
    }
    if (!ignoredEndpointId.isEmpty()
        && !index.endpointAttachments.contains(ignoredEndpointId)) {
        result.rejection = Rejection::InvalidEndpoint;
        return result;
    }

    const QString targetRouterId = routerId(target.router);
    const auto occupancyIterator = index.routers.constFind(targetRouterId);
    const RouterOccupancy* occupancy =
        occupancyIterator == index.routers.constEnd()
        ? nullptr : &*occupancyIterator;
    qsizetype attachedEndpointCount = occupancy
        ? occupancy->endpointCount : 0;
    const auto ignoredAttachment = index.endpointAttachments.constFind(
        ignoredEndpointId);
    if (ignoredAttachment != index.endpointAttachments.constEnd()
        && ignoredAttachment->router == target.router) {
        --attachedEndpointCount;
    }
    if (attachedEndpointCount >= policy.maxPerRouter) {
        result.rejection = Rejection::CapacityReached;
        return result;
    }

    if (policy.slotMode == AttachmentSlotMode::Automatic) {
        if (target.exactSlot) {
            result.rejection = Rejection::ExactSlotUnavailable;
            return result;
        }
        result.kind = SlotResolutionKind::Automatic;
        result.rejection = Rejection::None;
        return result;
    }

    if (target.exactSlot) {
        if (!index.portOffsetsByExactSlot.contains(*target.exactSlot)
            || !slotAvailable(
                occupancy, *target.exactSlot, ignoredEndpointId)) {
            result.rejection = Rejection::ExactSlotUnavailable;
            return result;
        }
        result.kind = SlotResolutionKind::Exact;
        result.rejection = Rejection::None;
        result.resolvedSlot = *target.exactSlot;
        return result;
    }

    if (detail == SlotResolutionDetail::ValidateOnly) {
        if (!explicitSlotAvailable(index, occupancy, ignoredEndpointId)) {
            result.rejection = Rejection::NoFreePort;
            return result;
        }
        result.kind = SlotResolutionKind::NeedsUserChoice;
        result.rejection = Rejection::None;
        return result;
    }

    for (const PortDefinition& port : policy.ports) {
        if (port.exactSlot
            && slotAvailable(occupancy, *port.exactSlot, ignoredEndpointId)) {
            result.choices.append({*port.exactSlot, port.label});
        }
    }
    if (result.choices.isEmpty()) {
        result.rejection = Rejection::NoFreePort;
        return result;
    }
    result.kind = SlotResolutionKind::NeedsUserChoice;
    result.rejection = Rejection::None;
    return result;
}

TargetResolution resolveTarget(
    const NocDesign& design,
    const Policy& policy,
    bool editingEnabled,
    const RouterHit& hit,
    const QString& ignoredEndpointId) {
    const DesignIndex index = buildDesignIndex(design, policy);
    return resolveTarget(
        design,
        index,
        editingEnabled,
        hit,
        ignoredEndpointId);
}

TargetResolution resolveTarget(
    const NocDesign& design,
    const DesignIndex& index,
    bool editingEnabled,
    const RouterHit& hit,
    const QString& ignoredEndpointId) {
    TargetResolution result;
    const Policy& policy = index.policy;
    if (!editingEnabled || policy.source != PolicySource::Package) {
        result.decision = RuleDecision::reject(Rejection::ReadOnly);
        return result;
    }
    if (!index.validPolicy) {
        result.decision = RuleDecision::reject(Rejection::InvalidPolicy);
        return result;
    }
    if (!routerBelongsToDerivedMesh(design, hit.router)) {
        result.decision = RuleDecision::reject(Rejection::UnknownRouter);
        return result;
    }
    if (!ignoredEndpointId.isEmpty()
        && !index.endpointAttachments.contains(ignoredEndpointId)) {
        result.decision = RuleDecision::reject(Rejection::InvalidEndpoint);
        return result;
    }
    if (hit.kind == RouterHitKind::TopologyPort) {
        result.decision = RuleDecision::reject(Rejection::DerivedRouterTopology);
        return result;
    }

    const auto occupancyIterator = index.routers.constFind(
        routerId(hit.router));
    const RouterOccupancy* occupancy =
        occupancyIterator == index.routers.constEnd()
        ? nullptr : &*occupancyIterator;
    const PortLayout emptyLayout;
    const PortLayout& layout = occupancy ? occupancy->layout : emptyLayout;
    AttachmentTarget target = {hit.router, std::nullopt};
    if (hit.kind == RouterHitKind::AttachmentPort) {
        if (!hit.port || *hit.port < 0 || *hit.port >= policy.ports.size()) {
            result.decision = RuleDecision::reject(Rejection::InvalidPort);
            return result;
        }
        if (!layout.portAvailable(*hit.port, ignoredEndpointId)) {
            result.decision = RuleDecision::reject(Rejection::PortOccupied);
            return result;
        }
        result.displayPort = *hit.port;
        target.exactSlot = policy.ports.at(*hit.port).exactSlot;
    } else {
        const SlotResolution slot = resolveSlot(
            design,
            index,
            target,
            ignoredEndpointId,
            SlotResolutionDetail::ValidateOnly);
        if (slot.kind == SlotResolutionKind::Rejected) {
            result.decision = RuleDecision::reject(slot.rejection);
            return result;
        }
        result.displayPort = firstAvailableDisplayPort(
            occupancy, ignoredEndpointId);
        if (!result.displayPort) {
            result.decision = RuleDecision::reject(Rejection::NoFreePort);
            return result;
        }
        result.decision = RuleDecision::allow();
        result.target = std::move(target);
        return result;
    }

    const SlotResolution slot = resolveSlot(
        design,
        index,
        target,
        ignoredEndpointId,
        SlotResolutionDetail::ValidateOnly);
    if (slot.kind == SlotResolutionKind::Rejected) {
        result.decision = RuleDecision::reject(slot.rejection);
        result.displayPort.reset();
        return result;
    }
    result.decision = RuleDecision::allow();
    result.target = std::move(target);
    return result;
}

TransitionPlan decideTransition(
    bool editingEnabled,
    const Policy& policy,
    const TransitionRequest& request) {
    TransitionPlan plan;
    plan.stateAfterSuccess = request.subject.lifecycle;
    if (!editingEnabled || policy.source != PolicySource::Package) {
        plan.decision = RuleDecision::reject(Rejection::ReadOnly);
        return plan;
    }
    if (!policyValid(policy)) {
        plan.decision = RuleDecision::reject(Rejection::InvalidPolicy);
        return plan;
    }

    const bool durable = request.subject.lifecycle != EndpointLifecycle::PendingNew;
    if (durable
        && (request.subject.endpointId.trimmed().isEmpty()
            || !request.subject.durableAttachment)) {
        plan.decision = RuleDecision::reject(Rejection::InvalidEndpoint);
        return plan;
    }

    if (request.intent == TransitionIntent::Attach) {
        if (!request.target) {
            plan.decision = RuleDecision::reject(Rejection::InvalidTransition);
            return plan;
        }
        switch (request.subject.lifecycle) {
        case EndpointLifecycle::PendingNew:
            plan.command = AttachmentCommandKind::CreateEndpoint;
            plan.stateAfterSuccess = EndpointLifecycle::Attached;
            break;
        case EndpointLifecycle::Detached:
            plan.command = AttachmentCommandKind::RestoreDetachedEndpoint;
            plan.stateAfterSuccess = EndpointLifecycle::Attached;
            break;
        case EndpointLifecycle::Attached:
            const bool sameRouter =
                request.subject.durableAttachment->router
                == request.target->router;
            const bool unchangedAutomatic =
                policy.slotMode == AttachmentSlotMode::Automatic
                && sameRouter && !request.target->exactSlot;
            const bool unchangedExplicit =
                policy.slotMode == AttachmentSlotMode::Explicit
                && sameRouter && request.target->exactSlot
                && request.subject.durableAttachment->slot
                    == request.target->exactSlot;
            plan.command = unchangedAutomatic || unchangedExplicit
                ? AttachmentCommandKind::PreserveAttachedEndpoint
                : AttachmentCommandKind::MoveEndpoint;
            plan.stateAfterSuccess = EndpointLifecycle::Attached;
            break;
        }
        plan.decision = RuleDecision::allow();
        return plan;
    }

    if (request.intent == TransitionIntent::Detach
        && request.subject.lifecycle == EndpointLifecycle::Attached) {
        plan.command = AttachmentCommandKind::DetachToRecoverableDraft;
        plan.stateAfterSuccess = EndpointLifecycle::Detached;
        plan.decision = RuleDecision::allow();
        return plan;
    }

    if (request.intent == TransitionIntent::Delete) {
        switch (request.subject.lifecycle) {
        case EndpointLifecycle::PendingNew:
            plan.command = AttachmentCommandKind::DiscardPendingEndpoint;
            break;
        case EndpointLifecycle::Attached:
            plan.command = AttachmentCommandKind::DeleteAttachedEndpoint;
            break;
        case EndpointLifecycle::Detached:
            plan.command = AttachmentCommandKind::DiscardDetachedDraft;
            break;
        }
        plan.decision = RuleDecision::allow();
        return plan;
    }

    plan.decision = RuleDecision::reject(Rejection::InvalidTransition);
    return plan;
}

QString rejectionMessage(
    Rejection rejection,
    std::optional<RouterPosition> router) {
    const QString target = routerDescription(router);
    switch (rejection) {
    case Rejection::None:
        return {};
    case Rejection::ReadOnly:
        return QStringLiteral("Endpoint attachments are read-only until the exact Package is available.");
    case Rejection::InvalidPolicy:
        return QStringLiteral("The Package has no usable Endpoint attachment policy.");
    case Rejection::UnknownRouter:
        return target + QStringLiteral(" is not part of the derived Mesh.");
    case Rejection::InvalidEndpoint:
        return QStringLiteral("The Endpoint no longer exists in the current design.");
    case Rejection::DerivedRouterTopology:
        return QStringLiteral("Router direction ports belong to the derived Mesh and cannot be rewired.");
    case Rejection::InvalidPort:
        return QStringLiteral("Only an Endpoint EP port and a Router EP port can be connected.");
    case Rejection::PortOccupied:
        return target + QStringLiteral(" attachment port is already occupied.");
    case Rejection::CapacityReached:
        return target + QStringLiteral(" has reached its Endpoint capacity.");
    case Rejection::NoFreePort:
        return target + QStringLiteral(" has no free Endpoint attachment port.");
    case Rejection::ExactSlotUnavailable:
        return target + QStringLiteral(" does not have the requested free Endpoint slot.");
    case Rejection::InvalidTransition:
        return QStringLiteral("This Endpoint attachment operation is not valid in its current state.");
    }
    return QStringLiteral("The Endpoint attachment operation was rejected.");
}

} // namespace finepaper::attachment
