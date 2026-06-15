#include "app/capabilityregistry.h"

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

QString capabilityKey(const PackageCapabilityDescriptor& capability) {
    return capability.capabilityId.trimmed().isEmpty() ? capability.id : capability.capabilityId;
}

} // namespace

bool CapabilityRegistry::registerHandler(const CapabilityHandlerDescriptor& handler) {
    if (!canonical(handler.capabilityId) ||
        !canonical(handler.ownerPluginId) ||
        m_handlersByCapability.contains(handler.capabilityId)) {
        return false;
    }
    m_handlersByCapability.insert(handler.capabilityId, handler);
    return true;
}

void CapabilityRegistry::recordPackageCapability(const PackageCapabilityDescriptor& capability) {
    if (!canonical(capability.packageId)) {
        return;
    }
    PackageCapabilityDescriptor stored = capability;
    stored.capabilityId = capabilityKey(capability);
    if (!canonical(stored.capabilityId)) {
        return;
    }
    m_capabilitiesByPackage.insert(stored.packageId, stored);
}

QVector<CapabilityCoverageRecord> CapabilityRegistry::coverageForPackage(
    const QString& packageId) const {
    QVector<CapabilityCoverageRecord> result;
    const QList<PackageCapabilityDescriptor> capabilities =
        m_capabilitiesByPackage.values(packageId);
    result.reserve(capabilities.size());
    for (const PackageCapabilityDescriptor& capability : capabilities) {
        result.append(coverageFor(capability));
    }
    return result;
}

QVector<CapabilityHandlerDescriptor> CapabilityRegistry::handlers() const {
    QVector<CapabilityHandlerDescriptor> result;
    result.reserve(m_handlersByCapability.size());
    for (auto it = m_handlersByCapability.cbegin(); it != m_handlersByCapability.cend(); ++it) {
        result.append(it.value());
    }
    return result;
}

CapabilityCoverageRecord CapabilityRegistry::coverageFor(
    const PackageCapabilityDescriptor& capability) const {
    CapabilityCoverageRecord record;
    record.packageId = capability.packageId;
    record.capabilityId = capability.capabilityId;
    const auto handler = m_handlersByCapability.constFind(capability.capabilityId);
    if (handler != m_handlersByCapability.cend()) {
        record.status = CapabilityCoverageStatus::Handled;
        record.handlerPluginId = handler.value().ownerPluginId;
        record.message = QStringLiteral("Capability is handled.");
        return record;
    }
    record.status = capability.required
        ? CapabilityCoverageStatus::Blocking
        : CapabilityCoverageStatus::Unsupported;
    record.message = capability.required
        ? QStringLiteral("Required capability has no registered handler.")
        : QStringLiteral("Optional capability has no registered handler.");
    return record;
}
