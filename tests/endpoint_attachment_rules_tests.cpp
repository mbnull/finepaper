#include "features/attachment/endpoint_attachment_rules.h"

#include <QCoreApplication>
#include <QSet>
#include <QTextStream>

namespace {

using namespace finepaper;
using namespace finepaper::attachment;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
}

NocDesign designFixture() {
    NocDesign design;
    design.id = QStringLiteral("attachment-rules");
    design.topology = {QStringLiteral("mesh"), 2, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("endpoint-b"),
            QStringLiteral("device"),
            EndpointAttachment{RouterPosition{0, 0}, QStringLiteral("local1")},
            {},
        },
        EndpointInstance{
            QStringLiteral("endpoint-a"),
            QStringLiteral("device"),
            EndpointAttachment{RouterPosition{0, 0}, QStringLiteral("local0")},
            {},
        },
        EndpointInstance{
            QStringLiteral("endpoint-c"),
            QStringLiteral("device"),
            EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
            {},
        },
    };
    return design;
}

AttachmentDefinition automaticDefinition(int capacity = 2) {
    AttachmentDefinition definition;
    definition.maxPerRouter = capacity;
    definition.slotMode = AttachmentSlotMode::Automatic;
    return definition;
}

AttachmentDefinition explicitDefinition() {
    AttachmentDefinition definition;
    definition.maxPerRouter = 3;
    definition.slotMode = AttachmentSlotMode::Explicit;
    definition.positions = {
        {QStringLiteral("local0"), QStringLiteral("Local port 0")},
        {QStringLiteral("local1"), QStringLiteral("Local port 1")},
        {QStringLiteral("local2"), QStringLiteral("Local port 2")},
    };
    return definition;
}

void packagePolicyIsNormalizedOnce() {
    const Policy automatic = policyFromPackage(automaticDefinition());
    check(policyValid(automatic)
              && automatic.ports
                  == QVector<PortDefinition>{
                      {QStringLiteral("0"), QStringLiteral("EP0"), std::nullopt},
                      {QStringLiteral("1"), QStringLiteral("EP1"), std::nullopt},
                  },
          QStringLiteral("automatic Package policy creates stable visual EP ports"));

    AttachmentDefinition legacyExplicit;
    legacyExplicit.maxPerRouter = 2;
    legacyExplicit.slotMode = AttachmentSlotMode::Explicit;
    const Policy explicitPolicy = policyFromPackage(legacyExplicit);
    check(policyValid(explicitPolicy)
              && explicitPolicy.ports
                  == QVector<PortDefinition>{
                      {QStringLiteral("0"), QStringLiteral("Local port 0"),
                       QStringLiteral("0")},
                      {QStringLiteral("1"), QStringLiteral("Local port 1"),
                       QStringLiteral("1")},
                  },
          QStringLiteral("legacy explicit slots are normalized in the policy boundary"));

    AttachmentDefinition invalid;
    invalid.maxPerRouter = 0;
    invalid.slotMode = AttachmentSlotMode::Automatic;
    check(!policyValid(policyFromPackage(invalid)),
          QStringLiteral("invalid capacity cannot become an editable policy"));

    Policy truncatedAutomatic = automatic;
    truncatedAutomatic.ports.removeLast();
    Policy expandedAutomatic = automatic;
    expandedAutomatic.ports.append({
        QStringLiteral("2"), QStringLiteral("EP2"), std::nullopt});
    AttachmentDefinition oversized = automaticDefinition(
        kMaximumEndpointAttachmentsPerRouter + 1);
    check(!policyValid(truncatedAutomatic)
              && !policyValid(expandedAutomatic)
              && !policyValid(policyFromPackage(oversized)),
          QStringLiteral(
              "automatic policies require one bounded visual port per capacity unit"));
}

void readOnlyInferencePreservesProjectionWithoutGrantingMutation() {
    NocDesign design = designFixture();
    design.endpoints.append({
        QStringLiteral("endpoint-d"),
        QStringLiteral("device"),
        EndpointAttachment{RouterPosition{1, 0}, QStringLiteral("__view_0")},
        {},
    });
    const Policy policy = inferReadOnlyPolicy(design);
    check(policy.source == PolicySource::InferredReadOnly
              && policyValid(policy)
              && policy.maxPerRouter >= 3,
          QStringLiteral("missing-Package projection infers enough deterministic ports"));
    QSet<QString> ids;
    for (const PortDefinition& port : policy.ports) {
        ids.insert(port.id);
    }
    check(ids.contains(QStringLiteral("local0"))
              && ids.contains(QStringLiteral("local1"))
              && ids.contains(QStringLiteral("__view_0"))
              && ids.size() == policy.ports.size(),
          QStringLiteral("inferred fallback ids do not collide with persisted slot ids"));

    const SlotResolution resolution = resolveSlot(
        design,
        policy,
        AttachmentTarget{RouterPosition{0, 1}, std::nullopt});
    check(resolution.kind == SlotResolutionKind::Rejected
              && resolution.rejection == Rejection::ReadOnly,
          QStringLiteral("inferred view policy can never authorize attachment mutation"));
}

void connectionMatrixKeepsRouterTopologyClosed() {
    const auto possible = [](ConnectionHandleKind output,
                             ConnectionHandleKind input) {
        return connectionPossible(true, {output, input});
    };
    check(possible(ConnectionHandleKind::EndpointAttachmentOutput,
                   ConnectionHandleKind::Missing).allowed
              && possible(ConnectionHandleKind::Missing,
                          ConnectionHandleKind::RouterAttachmentInput).allowed
              && possible(ConnectionHandleKind::EndpointAttachmentOutput,
                          ConnectionHandleKind::RouterAttachmentInput).allowed,
          QStringLiteral("only valid incomplete and complete EP handles are accepted"));
    check(!possible(ConnectionHandleKind::RouterTopologyPort,
                    ConnectionHandleKind::RouterAttachmentInput).allowed
              && possible(ConnectionHandleKind::RouterTopologyPort,
                          ConnectionHandleKind::RouterAttachmentInput).rejection
                     == Rejection::DerivedRouterTopology
              && !possible(ConnectionHandleKind::EndpointAttachmentOutput,
                           ConnectionHandleKind::Other).allowed
              && !connectionPossible(
                      false,
                      {ConnectionHandleKind::EndpointAttachmentOutput,
                       ConnectionHandleKind::RouterAttachmentInput})
                      .allowed,
          QStringLiteral("directional Router wiring, invalid pairs, and read-only edits are rejected"));
}

void completeDesignOccupancyDrivesPortProjection() {
    const NocDesign design = designFixture();
    const Policy explicitPolicy = policyFromPackage(explicitDefinition());
    const PortLayout explicitLayout = resolvePortLayout(
        design, explicitPolicy, RouterPosition{0, 0});
    check(explicitLayout.portForEndpoint(QStringLiteral("endpoint-a"))
                  == std::optional<PortOffset>(0)
              && explicitLayout.portForEndpoint(QStringLiteral("endpoint-b"))
                  == std::optional<PortOffset>(1)
              && !explicitLayout.portAvailable(0)
              && explicitLayout.portAvailable(
                  0, QStringLiteral("endpoint-a")),
          QStringLiteral("explicit bindings use Package slot order and can ignore only the moved Endpoint"));

    NocDesign automaticDesign = design;
    automaticDesign.endpoints[0].attachment.slot.reset();
    automaticDesign.endpoints[1].attachment.slot.reset();
    const PortLayout automaticLayout = resolvePortLayout(
        automaticDesign,
        policyFromPackage(automaticDefinition()),
        RouterPosition{0, 0});
    check(automaticLayout.portForEndpoint(QStringLiteral("endpoint-a"))
                  == std::optional<PortOffset>(0)
              && automaticLayout.portForEndpoint(QStringLiteral("endpoint-b"))
                  == std::optional<PortOffset>(1),
          QStringLiteral("automatic visual slots are deterministic by Endpoint id"));

    NocDesign damaged = design;
    damaged.endpoints = {
        EndpointInstance{
            QStringLiteral("endpoint-a"),
            QStringLiteral("device"),
            EndpointAttachment{
                RouterPosition{0, 0}, QStringLiteral("unknown")},
            {},
        },
        EndpointInstance{
            QStringLiteral("endpoint-b"),
            QStringLiteral("device"),
            EndpointAttachment{
                RouterPosition{0, 0}, QStringLiteral("local0")},
            {},
        },
    };
    const PortLayout damagedLayout = resolvePortLayout(
        damaged, explicitPolicy, RouterPosition{0, 0});
    check(damagedLayout.portForEndpoint(QStringLiteral("endpoint-b"))
                  == std::optional<PortOffset>(0)
              && damagedLayout.portForEndpoint(QStringLiteral("endpoint-a"))
                  == std::optional<PortOffset>(1),
          QStringLiteral(
              "valid persisted slot owners are reserved before damaged fallbacks"));

    automaticDesign.endpoints.append({
        QStringLiteral("endpoint-overflow"),
        QStringLiteral("device"),
        EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
        {},
    });
    const PortLayout overflow = resolvePortLayout(
        automaticDesign,
        policyFromPackage(automaticDefinition()),
        RouterPosition{0, 0});
    check(overflow.overflowEndpointIds
              == QStringList{QStringLiteral("endpoint-overflow")},
          QStringLiteral("invalid over-capacity projection reports deterministic overflow"));
}

void slotsResolveAtomicallyFromFullDesign() {
    const NocDesign design = designFixture();
    const Policy policy = policyFromPackage(explicitDefinition());

    const SlotResolution body = resolveSlot(
        design,
        policy,
        AttachmentTarget{RouterPosition{0, 0}, std::nullopt},
        QStringLiteral("endpoint-a"));
    check(body.kind == SlotResolutionKind::NeedsUserChoice
              && body.choices
                  == QVector<SlotChoice>{
                      {QStringLiteral("local0"), QStringLiteral("Local port 0")},
                      {QStringLiteral("local2"), QStringLiteral("Local port 2")},
                  },
          QStringLiteral("moving an Endpoint excludes only itself and preserves Package choice order"));

    const SlotResolution exact = resolveSlot(
        design,
        policy,
        AttachmentTarget{RouterPosition{0, 0}, QStringLiteral("local2")});
    check(exact.kind == SlotResolutionKind::Exact
              && exact.resolvedSlot == std::optional<QString>(
                  QStringLiteral("local2")),
          QStringLiteral("an available exact explicit slot resolves without a dialog"));

    const SlotResolution occupied = resolveSlot(
        design,
        policy,
        AttachmentTarget{RouterPosition{0, 0}, QStringLiteral("local1")});
    check(occupied.kind == SlotResolutionKind::Rejected
              && occupied.rejection == Rejection::ExactSlotUnavailable,
          QStringLiteral("an occupied exact slot fails before any mutation"));

    NocDesign full = design;
    full.endpoints.append({
        QStringLiteral("endpoint-full"),
        QStringLiteral("device"),
        EndpointAttachment{RouterPosition{0, 0}, QStringLiteral("local2")},
        {},
    });
    const SlotResolution capacity = resolveSlot(
        full,
        policy,
        AttachmentTarget{RouterPosition{0, 0}, std::nullopt});
    check(capacity.kind == SlotResolutionKind::Rejected
              && capacity.rejection == Rejection::CapacityReached,
          QStringLiteral("full Router capacity is shared by every entry point"));

    AttachmentDefinition sparseExplicit;
    sparseExplicit.maxPerRouter = 3;
    sparseExplicit.slotMode = AttachmentSlotMode::Explicit;
    sparseExplicit.positions = {{
        QStringLiteral("local0"), QStringLiteral("Only local port")}};
    NocDesign noFreeDesign = design;
    noFreeDesign.endpoints = {design.endpoints.at(1)};
    const SlotResolution noFreeSlot = resolveSlot(
        noFreeDesign,
        policyFromPackage(sparseExplicit),
        AttachmentTarget{RouterPosition{0, 0}, std::nullopt});
    check(noFreeSlot.kind == SlotResolutionKind::Rejected
              && noFreeSlot.rejection == Rejection::NoFreePort,
          QStringLiteral("no explicit slot remains distinct from count capacity"));

    const SlotResolution unknownRouter = resolveSlot(
        design,
        policy,
        AttachmentTarget{RouterPosition{9, 9}, std::nullopt});
    const SlotResolution unknownEndpoint = resolveSlot(
        design,
        policy,
        AttachmentTarget{RouterPosition{0, 1}, std::nullopt},
        QStringLiteral("missing"));
    check(unknownRouter.rejection == Rejection::UnknownRouter
              && unknownEndpoint.rejection == Rejection::InvalidEndpoint,
          QStringLiteral("only derived Mesh Routers and existing moved Endpoints are valid"));

    NocDesign automaticDesign = design;
    automaticDesign.endpoints.clear();
    const SlotResolution automatic = resolveSlot(
        automaticDesign,
        policyFromPackage(automaticDefinition()),
        AttachmentTarget{RouterPosition{0, 0}, std::nullopt});
    check(automatic.kind == SlotResolutionKind::Automatic
              && !automatic.resolvedSlot,
          QStringLiteral("automatic policy never persists a derived display slot"));
}

void targetResolutionUsesInvisibleOccupancy() {
    NocDesign design = designFixture();
    const Policy policy = policyFromPackage(explicitDefinition());
    const DesignIndex index = buildDesignIndex(design, policy);
    // The rules receive the complete design, independent of whether the
    // Router is collapsed and its Endpoint nodes are absent from the scene.
    const TargetResolution occupied = resolveTarget(
        design,
        index,
        true,
        RouterHit{RouterPosition{0, 0}, RouterHitKind::AttachmentPort, 1});
    const TargetResolution free = resolveTarget(
        design,
        index,
        true,
        RouterHit{RouterPosition{0, 0}, RouterHitKind::AttachmentPort, 2});
    const TargetResolution topologyPort = resolveTarget(
        design,
        index,
        true,
        RouterHit{RouterPosition{0, 0}, RouterHitKind::TopologyPort, 0});
    const TargetResolution body = resolveTarget(
        design,
        index,
        true,
        RouterHit{RouterPosition{0, 0}, RouterHitKind::Body, std::nullopt});
    check(!occupied.decision.allowed
              && occupied.decision.rejection == Rejection::PortOccupied
              && free.decision.allowed
              && free.target
              && free.target->exactSlot
                  == std::optional<QString>(QStringLiteral("local2"))
              && body.decision.allowed
              && body.displayPort == std::optional<PortOffset>(2)
              && body.target && !body.target->exactSlot
              && topologyPort.decision.rejection
                  == Rejection::DerivedRouterTopology,
          QStringLiteral("target hit resolution sees full occupancy and rejects Mesh topology ports"));

    NocDesign fullExplicitDesign = design;
    fullExplicitDesign.endpoints.append({
        QStringLiteral("endpoint-c"),
        QStringLiteral("device"),
        EndpointAttachment{
            RouterPosition{0, 0}, QStringLiteral("local2")},
        {},
    });
    const DesignIndex fullExplicitIndex = buildDesignIndex(
        fullExplicitDesign, policy);
    const TargetResolution moveWithinFullRouter = resolveTarget(
        fullExplicitDesign,
        fullExplicitIndex,
        true,
        RouterHit{RouterPosition{0, 0}, RouterHitKind::Body, std::nullopt},
        QStringLiteral("endpoint-a"));
    check(moveWithinFullRouter.decision.allowed
              && moveWithinFullRouter.displayPort
                  == std::optional<PortOffset>(0),
          QStringLiteral(
              "cached occupancy treats the moved Endpoint's own slot and display port as available"));

    AttachmentDefinition morePortsThanCapacity = explicitDefinition();
    morePortsThanCapacity.maxPerRouter = 1;
    NocDesign capacityDesign = design;
    capacityDesign.endpoints = {design.endpoints.at(1)};
    const DesignIndex capacityIndex = buildDesignIndex(
        capacityDesign, policyFromPackage(morePortsThanCapacity));
    const TargetResolution spareButOverCapacity = resolveTarget(
        capacityDesign,
        capacityIndex,
        true,
        RouterHit{RouterPosition{0, 0}, RouterHitKind::AttachmentPort, 1});
    check(!spareButOverCapacity.decision.allowed
              && spareButOverCapacity.decision.rejection
                  == Rejection::CapacityReached,
          QStringLiteral(
              "a visually spare explicit port cannot bypass Package capacity"));
}

void lifecyclePlansNeverMutateRouterTopology() {
    const AttachmentTarget target{RouterPosition{1, 1}, std::nullopt};
    const auto plan = [&](EndpointLifecycle lifecycle,
                          TransitionIntent intent,
                          std::optional<AttachmentTarget> requestedTarget) {
        EndpointSubject subject;
        subject.lifecycle = lifecycle;
        if (lifecycle != EndpointLifecycle::PendingNew) {
            subject.endpointId = QStringLiteral("endpoint-a");
            subject.durableAttachment = EndpointAttachment{
                RouterPosition{0, 0}, QStringLiteral("local0")};
        }
        return decideTransition(
            true,
            policyFromPackage(automaticDefinition()),
            TransitionRequest{subject, intent, requestedTarget});
    };

    check(plan(EndpointLifecycle::PendingNew,
               TransitionIntent::Attach,
               target).command == AttachmentCommandKind::CreateEndpoint
              && plan(EndpointLifecycle::Detached,
                      TransitionIntent::Attach,
                      target).command
                     == AttachmentCommandKind::RestoreDetachedEndpoint
              && plan(EndpointLifecycle::Attached,
                      TransitionIntent::Attach,
                      target).command == AttachmentCommandKind::MoveEndpoint
              && plan(EndpointLifecycle::Attached,
                      TransitionIntent::Detach,
                      std::nullopt).command
                     == AttachmentCommandKind::DetachToRecoverableDraft,
          QStringLiteral("attach and detach lifecycle states map to explicit Endpoint commands"));

    check(plan(EndpointLifecycle::PendingNew,
               TransitionIntent::Delete,
               std::nullopt).command
                  == AttachmentCommandKind::DiscardPendingEndpoint
              && plan(EndpointLifecycle::Detached,
                      TransitionIntent::Delete,
                      std::nullopt).command
                     == AttachmentCommandKind::DiscardDetachedDraft
              && plan(EndpointLifecycle::Attached,
                      TransitionIntent::Delete,
                      std::nullopt).command
                     == AttachmentCommandKind::DeleteAttachedEndpoint,
          QStringLiteral("delete distinguishes transient, detached, and durable Endpoints"));

    EndpointSubject attached = {
        EndpointLifecycle::Attached,
        QStringLiteral("endpoint-a"),
        EndpointAttachment{RouterPosition{0, 0}, QStringLiteral("local0")},
    };
    const TransitionPlan noOp = decideTransition(
        true,
        policyFromPackage(automaticDefinition()),
        {attached,
         TransitionIntent::Attach,
         AttachmentTarget{RouterPosition{0, 0}, std::nullopt}});
    const TransitionPlan readOnly = decideTransition(
        true,
        inferReadOnlyPolicy(designFixture()),
        {attached, TransitionIntent::Delete, std::nullopt});
    const Policy explicitPolicy = policyFromPackage(explicitDefinition());
    const TransitionPlan explicitBody = decideTransition(
        true,
        explicitPolicy,
        {attached,
         TransitionIntent::Attach,
         AttachmentTarget{RouterPosition{0, 0}, std::nullopt}});
    const TransitionPlan explicitSameSlot = decideTransition(
        true,
        explicitPolicy,
        {attached,
         TransitionIntent::Attach,
         AttachmentTarget{
             RouterPosition{0, 0}, QStringLiteral("local0")}});
    check(noOp.command == AttachmentCommandKind::PreserveAttachedEndpoint
              && explicitBody.command == AttachmentCommandKind::MoveEndpoint
              && explicitSameSlot.command
                  == AttachmentCommandKind::PreserveAttachedEndpoint
              && readOnly.decision.rejection == Rejection::ReadOnly,
          QStringLiteral(
              "automatic same-target is stable while explicit body reconnect still chooses a slot"));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    packagePolicyIsNormalizedOnce();
    readOnlyInferencePreservesProjectionWithoutGrantingMutation();
    connectionMatrixKeepsRouterTopologyClosed();
    completeDesignOccupancyDrivesPortProjection();
    slotsResolveAtomicallyFromFullDesign();
    targetResolutionUsesInvisibleOccupancy();
    lifecyclePlansNeverMutateRouterTopology();

    if (failures == 0) {
        QTextStream(stdout) << "All Endpoint attachment rule tests passed."
                            << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
