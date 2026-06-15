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
        m_byId.contains(contribution.id)) {
        return false;
    }
    m_byId.insert(contribution.id, contribution);
    m_idsByExtensionPoint.insert(contribution.extensionPoint, contribution.id);
    return true;
}

QVector<ExtensionContribution> ExtensionPointRegistry::contributions(
    const QString& extensionPoint) const {
    QVector<ExtensionContribution> result;
    const QList<QString> ids = m_idsByExtensionPoint.values(extensionPoint);
    result.reserve(ids.size());
    for (const QString& id : ids) {
        result.append(m_byId.value(id));
    }
    return result;
}

QVector<ExtensionContribution> ExtensionPointRegistry::allContributions() const {
    QVector<ExtensionContribution> result;
    result.reserve(m_byId.size());
    for (auto it = m_byId.cbegin(); it != m_byId.cend(); ++it) {
        result.append(it.value());
    }
    return result;
}
