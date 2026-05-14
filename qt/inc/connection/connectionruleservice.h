// ConnectionRuleService resolves v1 editor-time connection requests.
#pragma once

#include "graph/connection.h"
#include "graph/parameter.h"
#include "ipcraft/ipcraftmanifest.h"
#include "project/ipinstancestate.h"
#include "project/projectdocument.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

class Graph;
class Module;
class Port;

enum class ConnectionRequestKind {
    PortToPort,
    PortToNode,
    NodeToPort,
    Programmatic,
    ProjectLoad
};

enum class ConnectionVisualSide {
    None,
    Input,
    Output
};

struct ConnectionEndpointRequest {
    QString moduleId;
    std::optional<QString> portId;
    QPointF scenePos;
    ConnectionVisualSide visualSide = ConnectionVisualSide::None;
    bool fromNodeBody = false;
    bool hiddenPortsAllowed = false;
};

struct ConnectionRequest {
    ConnectionRequestKind kind = ConnectionRequestKind::Programmatic;
    ConnectionEndpointRequest start;
    ConnectionEndpointRequest end;
    bool interactive = true;
    bool allowAutoComplete = true;
    bool allowAlternatives = true;
    QString connectionClassId;

    static ConnectionRequest portToPort(const PortRef& start,
                                        const PortRef& end,
                                        ConnectionRequestKind kind);
};

struct PortSemanticInfo {
    PortRef ref;
    QString moduleType;
    QString ipcoreId;
    QString packageId;
    QString manifestModuleId;
    QString instanceId;
    QString graphGroup;
    QString editorLayout;
    QString portName;
    QString direction;
    QString busType;
    QString portRole;
    QString interfaceId;
    QString interfaceBus;
    QString interfaceRole;
    QStringList compatibleRoles;
    QHash<QString, QStringList> matchFieldValues;
    QString cardinality = QStringLiteral("one");
    QString autocompleteGroup;
    QString topologyRule;
    QVector<IpcraftInterfaceAcceptRule> acceptRules;
    bool supportsInput = false;
    bool supportsOutput = false;
    bool occupiedAsSource = false;
    bool occupiedAsTarget = false;
    bool visibleInUi = true;
};

enum class ConnectionRuleLayer {
    Structural,
    EditorRule,
    Ipcore,
    FinalDrc
};

enum class ConnectionCheckStatus {
    Allowed,
    NeedsSelection,
    Rejected
};

struct ConnectionResolvedOption {
    PortRef source;
    PortRef target;
    QString label;
    QString connectionClassId;
    QString connectionStatus = QStringLiteral("valid");
    QStringList alternatives;
    QVector<ProjectConnectionInterfaceRef> normalizedInterfaces;
    int priority = 0;
};

struct ConnectionCheckResult {
    ConnectionCheckStatus status = ConnectionCheckStatus::Rejected;
    ConnectionRuleLayer layer = ConnectionRuleLayer::Structural;
    QVector<ConnectionResolvedOption> options;
    QString reasonCode;
    QString message;

    bool hasSingleOption() const {
        return status == ConnectionCheckStatus::Allowed && options.size() == 1;
    }
};

class ConnectionRuleService {
public:
    ConnectionRuleService(const Graph* graph,
                          QVector<ProjectIpInstanceRecord> ipInstanceRecords);
    ConnectionRuleService(const Graph* graph,
                          QVector<ProjectIpInstanceRecord> ipInstanceRecords,
                          QVector<IpcraftPackageManifest> manifests);

    ConnectionCheckResult check(const ConnectionRequest& request) const;

private:
    ConnectionCheckResult reject(ConnectionRuleLayer layer, QString reasonCode, QString message) const;
    std::optional<ConnectionCheckResult> checkStructuralRules(const ConnectionRequest& request) const;
    QVector<PortSemanticInfo> resolveEndpointPorts(const ConnectionEndpointRequest& endpoint) const;
    std::optional<PortSemanticInfo> resolvePort(const QString& moduleId,
                                                const QString& portId,
                                                bool visibleInUi) const;
    QVector<ConnectionResolvedOption> buildOptions(const QVector<PortSemanticInfo>& startPorts,
                                                   const QVector<PortSemanticInfo>& endPorts,
                                                   const ConnectionRequest& request,
                                                   ConnectionRuleLayer* rejectionLayer,
                                                   QString* rejectionReason,
                                                   QString* rejectionMessage) const;

    const Graph* m_graph = nullptr;
    QVector<ProjectIpInstanceRecord> m_ipInstanceRecords;
    QVector<IpcraftPackageManifest> m_manifests;
};
