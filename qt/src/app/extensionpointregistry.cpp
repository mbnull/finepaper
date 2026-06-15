#include "app/extensionpointregistry.h"

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

} // namespace

bool ExtensionPointRegistry::registerContribution(const ExtensionContribution& contribution) {
    if (!canonical(contribution.id) ||
        !canonical(contribution.extensionPoint) ||
        !canonical(contribution.ownerPluginId) ||
        m_byId.contains(contribution.id) ||
        m_contributionOrder.size() >= kMaxContributions) {
        return false;
    }
    m_byId.insert(contribution.id, contribution);
    m_contributionOrder.append(contribution.id);
    return true;
}

QVector<ExtensionContribution> ExtensionPointRegistry::contributions(
    const QString& extensionPoint) const {
    QVector<ExtensionContribution> result;
    for (const QString& id : m_contributionOrder) {
        const ExtensionContribution contribution = m_byId.value(id);
        if (contribution.extensionPoint == extensionPoint) {
            result.append(contribution);
        }
    }
    return result;
}

QVector<ExtensionContribution> ExtensionPointRegistry::allContributions() const {
    QVector<ExtensionContribution> result;
    result.reserve(m_byId.size());
    for (const QString& id : m_contributionOrder) {
        result.append(m_byId.value(id));
    }
    return result;
}
