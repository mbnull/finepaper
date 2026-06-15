#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

enum class CapabilityCoverageStatus {
    Handled,
    Visible,
    Unsupported,
    Blocking,
    Invalid,
};

struct CapabilityHandlerDescriptor {
    QString capabilityId;
    QString ownerPluginId;
    QStringList extensionPoints;
};

struct PackageCapabilityDescriptor {
    QString packageId;
    QString capabilityId;
    QString id;
    bool required = false;
};

struct CapabilityCoverageRecord {
    QString packageId;
    QString capabilityId;
    QString handlerPluginId;
    CapabilityCoverageStatus status = CapabilityCoverageStatus::Unsupported;
    QString message;
};

class CapabilityRegistry {
public:
    bool registerHandler(const CapabilityHandlerDescriptor& handler);
    void recordPackageCapability(const PackageCapabilityDescriptor& capability);
    QVector<CapabilityCoverageRecord> coverageForPackage(const QString& packageId) const;
    QVector<CapabilityHandlerDescriptor> handlers() const;

private:
    CapabilityCoverageRecord coverageFor(const PackageCapabilityDescriptor& capability) const;

    QHash<QString, CapabilityHandlerDescriptor> m_handlersByCapability;
    QStringList m_handlerOrder;
    QVector<PackageCapabilityDescriptor> m_capabilities;
};
