#pragma once

#include "package/package.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper::attachment {

using PortOffset = qsizetype;

enum class PolicySource : quint8 {
    Package,
    InferredReadOnly,
};

struct PortDefinition {
    QString id;
    QString label;
    std::optional<QString> exactSlot = std::nullopt;

    bool operator==(const PortDefinition&) const = default;
};

// Normalized Package policy used by every Endpoint attachment entry point.
// InferredReadOnly policies exist only to preserve a damaged/missing-Package
// design projection; they can never authorize a design mutation.
struct Policy {
    PolicySource source = PolicySource::Package;
    AttachmentSlotMode slotMode = AttachmentSlotMode::Invalid;
    int maxPerRouter = 0;
    QVector<PortDefinition> ports;

    bool operator==(const Policy&) const = default;
};

struct AttachmentTarget {
    RouterPosition router;
    std::optional<QString> exactSlot = std::nullopt;

    bool operator==(const AttachmentTarget&) const = default;
};

struct DetachedEndpointSnapshot {
    EndpointInstance endpoint;
    QHash<QString, QStringList> domainAssignments;
    QVector<DomainEdgeOverride> attachmentOverrides;
    QVector<ElementConfiguration> attachmentConfigurations;
};

// A typed callback result avoids rediscovering a newly-created Endpoint by
// scanning a synchronously rebuilt design projection.
struct CreateEndpointResult {
    bool success = false;
    QString endpointId;
};

enum class ConnectionHandleKind : quint8 {
    Missing,
    EndpointAttachmentOutput,
    RouterAttachmentInput,
    RouterTopologyPort,
    Other,
};

struct ConnectionCandidate {
    ConnectionHandleKind output = ConnectionHandleKind::Missing;
    ConnectionHandleKind input = ConnectionHandleKind::Missing;
};

enum class Rejection : quint8 {
    None,
    ReadOnly,
    InvalidPolicy,
    UnknownRouter,
    InvalidEndpoint,
    DerivedRouterTopology,
    InvalidPort,
    PortOccupied,
    CapacityReached,
    NoFreePort,
    ExactSlotUnavailable,
    InvalidTransition,
};

struct RuleDecision {
    bool allowed = false;
    Rejection rejection = Rejection::InvalidTransition;

    static RuleDecision allow() { return {true, Rejection::None}; }
    static RuleDecision reject(Rejection reason) { return {false, reason}; }
};

struct PortLayout {
    QHash<QString, PortOffset> portsByEndpoint;
    QHash<PortOffset, QString> endpointsByPort;
    QStringList overflowEndpointIds;

    [[nodiscard]] std::optional<PortOffset> portForEndpoint(
        const QString& endpointId) const;
    [[nodiscard]] bool portAvailable(
        PortOffset port,
        const QString& ignoredEndpointId = {}) const;
    [[nodiscard]] std::optional<PortOffset> firstAvailablePort(
        qsizetype portCount,
        const QString& ignoredEndpointId = {}) const;
};

struct RouterOccupancy {
    qsizetype endpointCount = 0;
    PortLayout layout;
    struct SlotOccupancy {
        qsizetype endpointCount = 0;
        QString soleEndpointId;
    };

    QHash<QString, SlotOccupancy> slotOccupancies;
    qsizetype occupiedPolicySlotCount = 0;
    std::optional<PortOffset> firstUnoccupiedPort = std::nullopt;
};

// Immutable semantic index for one design/policy projection. Interactive
// adapters cache it so pointer-move validation never rescans the whole design.
struct DesignIndex {
    Policy policy;
    bool validPolicy = false;
    QHash<QString, PortOffset> portOffsetsById;
    QHash<QString, PortOffset> portOffsetsByExactSlot;
    QHash<QString, EndpointAttachment> endpointAttachments;
    QHash<QString, RouterOccupancy> routers;
};

enum class RouterHitKind : quint8 {
    Body,
    AttachmentPort,
    TopologyPort,
};

struct RouterHit {
    RouterPosition router;
    RouterHitKind kind = RouterHitKind::Body;
    std::optional<PortOffset> port = std::nullopt;
};

struct TargetResolution {
    RuleDecision decision;
    std::optional<AttachmentTarget> target = std::nullopt;
    std::optional<PortOffset> displayPort = std::nullopt;
};

struct SlotChoice {
    QString id;
    QString label;

    bool operator==(const SlotChoice&) const = default;
};

enum class SlotResolutionKind : quint8 {
    Rejected,
    Automatic,
    Exact,
    NeedsUserChoice,
};

enum class SlotResolutionDetail : quint8 {
    ValidateOnly,
    EnumerateChoices,
};

struct SlotResolution {
    SlotResolutionKind kind = SlotResolutionKind::Rejected;
    Rejection rejection = Rejection::InvalidTransition;
    std::optional<QString> resolvedSlot = std::nullopt;
    QVector<SlotChoice> choices;
};

enum class EndpointLifecycle : quint8 {
    PendingNew,
    Attached,
    Detached,
};

struct EndpointSubject {
    EndpointLifecycle lifecycle = EndpointLifecycle::PendingNew;
    QString endpointId;
    std::optional<EndpointAttachment> durableAttachment = std::nullopt;
};

enum class TransitionIntent : quint8 {
    Attach,
    Detach,
    Delete,
};

enum class AttachmentCommandKind : quint8 {
    None,
    PreserveAttachedEndpoint,
    CreateEndpoint,
    MoveEndpoint,
    RestoreDetachedEndpoint,
    DetachToRecoverableDraft,
    DeleteAttachedEndpoint,
    DiscardPendingEndpoint,
    DiscardDetachedDraft,
};

struct TransitionRequest {
    EndpointSubject subject;
    TransitionIntent intent = TransitionIntent::Attach;
    std::optional<AttachmentTarget> target = std::nullopt;
};

struct TransitionPlan {
    RuleDecision decision;
    AttachmentCommandKind command = AttachmentCommandKind::None;
    EndpointLifecycle stateAfterSuccess = EndpointLifecycle::PendingNew;
};

[[nodiscard]] Policy policyFromPackage(const AttachmentDefinition& definition);
[[nodiscard]] Policy inferReadOnlyPolicy(const NocDesign& design);
[[nodiscard]] bool policyValid(const Policy& policy);
[[nodiscard]] bool routerBelongsToDerivedMesh(
    const NocDesign& design,
    RouterPosition router);

[[nodiscard]] RuleDecision connectionPossible(
    bool editingEnabled,
    const ConnectionCandidate& candidate);

[[nodiscard]] PortLayout resolvePortLayout(
    const NocDesign& design,
    const Policy& policy,
    RouterPosition router);

[[nodiscard]] DesignIndex buildDesignIndex(
    const NocDesign& design,
    const Policy& policy);

[[nodiscard]] SlotResolution resolveSlot(
    const NocDesign& design,
    const Policy& policy,
    AttachmentTarget target,
    const QString& ignoredEndpointId = {});

[[nodiscard]] SlotResolution resolveSlot(
    const NocDesign& design,
    const DesignIndex& index,
    AttachmentTarget target,
    const QString& ignoredEndpointId = {},
    SlotResolutionDetail detail = SlotResolutionDetail::EnumerateChoices);

[[nodiscard]] TargetResolution resolveTarget(
    const NocDesign& design,
    const Policy& policy,
    bool editingEnabled,
    const RouterHit& hit,
    const QString& ignoredEndpointId = {});

[[nodiscard]] TargetResolution resolveTarget(
    const NocDesign& design,
    const DesignIndex& index,
    bool editingEnabled,
    const RouterHit& hit,
    const QString& ignoredEndpointId = {});

[[nodiscard]] TransitionPlan decideTransition(
    bool editingEnabled,
    const Policy& policy,
    const TransitionRequest& request);

[[nodiscard]] QString rejectionMessage(
    Rejection rejection,
    std::optional<RouterPosition> router = std::nullopt);

} // namespace finepaper::attachment
