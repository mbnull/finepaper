#include "app/capabilityregistry.h"

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

QString capabilityKey(const PackageCapabilityDescriptor& capability) {
    return capability.capabilityId.isEmpty() ? capability.id : capability.capabilityId;
}

bool canonicalList(const QStringList& values) {
    for (const QString& value : values) {
        if (!canonical(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool CapabilityRegistry::registerHandler(const CapabilityHandlerDescriptor& handler) {
    if (!canonical(handler.capabilityId) ||
        !canonical(handler.ownerPluginId) ||
        !canonicalList(handler.extensionPoints) ||
        m_handlersByCapability.contains(handler.capabilityId) ||
        m_handlerOrder.size() >= kMaxHandlers) {
        return false;
    }
    m_handlersByCapability.insert(handler.capabilityId, handler);
    m_handlerOrder.append(handler.capabilityId);
    return true;
}

bool CapabilityRegistry::recordPackageCapability(const PackageCapabilityDescriptor& capability) {
    if (!canonical(capability.packageId)) {
        return false;
    }
    PackageCapabilityDescriptor stored = capability;
    stored.capabilityId = capabilityKey(capability);
    if (!canonical(stored.capabilityId)) {
        return false;
    }
    if (m_capabilities.size() >= kMaxPackageCapabilities) {
        return false;
    }
    m_capabilities.append(stored);
    return true;
}

QVector<CapabilityCoverageRecord> CapabilityRegistry::coverageForPackage(
    const QString& packageId) const {
    QVector<CapabilityCoverageRecord> result;
    for (const PackageCapabilityDescriptor& capability : m_capabilities) {
        if (capability.packageId == packageId) {
            result.append(coverageFor(capability));
        }
    }
    return result;
}

QVector<CapabilityHandlerDescriptor> CapabilityRegistry::handlers() const {
    QVector<CapabilityHandlerDescriptor> result;
    result.reserve(m_handlersByCapability.size());
    for (const QString& capabilityId : m_handlerOrder) {
        result.append(m_handlersByCapability.value(capabilityId));
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
