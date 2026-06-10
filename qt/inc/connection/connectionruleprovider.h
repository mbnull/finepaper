// Connection rule providers supply package-declared editor-time connection checks.
#pragma once

#include "ipcraft/ipcraftmanifest.h"
#include "project/projectdocument.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct PortSemanticInfo;
class IpcraftConnectionValidator;

enum class ConnectionRuleProviderStatus {
    Allowed,
    Warning,
    Rejected,
};

struct ConnectionRuleProviderRequest {
    const PortSemanticInfo& source;
    const PortSemanticInfo& target;
    QString selectedConnectionClassId;
    const QVector<IpcraftPackageManifest>& manifests;
    const QVector<ProjectConnectionRecord>& currentConnections;
    const IpcraftConnectionValidator* validator = nullptr;
};

struct ConnectionRuleProviderResult {
    ConnectionRuleProviderStatus status = ConnectionRuleProviderStatus::Rejected;
    QString reasonCode;
    QString message;
    QString connectionClassId;
    QString connectionStatus = QStringLiteral("valid");
    QStringList alternatives;
    QVector<ProjectConnectionInterfaceRef> normalizedInterfaces;

    bool accepted() const {
        return status == ConnectionRuleProviderStatus::Allowed ||
               status == ConnectionRuleProviderStatus::Warning;
    }
};

class ConnectionRuleProvider {
public:
    ConnectionRuleProvider() = default;
    virtual ~ConnectionRuleProvider() = default;

    ConnectionRuleProvider(const ConnectionRuleProvider&) = delete;
    ConnectionRuleProvider& operator=(const ConnectionRuleProvider&) = delete;
    ConnectionRuleProvider(ConnectionRuleProvider&&) = delete;
    ConnectionRuleProvider& operator=(ConnectionRuleProvider&&) = delete;

    virtual bool canEvaluate(const PortSemanticInfo& source,
                             const PortSemanticInfo& target) const = 0;
    virtual ConnectionRuleProviderResult evaluate(
        const ConnectionRuleProviderRequest& request) const = 0;
};

class PackageConnectionRuleProvider final : public ConnectionRuleProvider {
public:
    bool canEvaluate(const PortSemanticInfo& source,
                     const PortSemanticInfo& target) const override;
    ConnectionRuleProviderResult evaluate(
        const ConnectionRuleProviderRequest& request) const override;
};
