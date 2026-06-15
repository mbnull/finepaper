#include "noc/nocsemanticmodel.h"

#include <QJsonArray>
#include <QJsonValue>

namespace {

bool canonical(const QString& value) {
    return !value.trimmed().isEmpty() && value == value.trimmed();
}

} // namespace

NoCSemanticModel NoCSemanticModel::fromJson(const QJsonObject& descriptor) {
    NoCSemanticModel model;
    const QJsonArray roles = descriptor.value(QStringLiteral("roles")).toArray();
    for (const QJsonValue& roleValue : roles) {
        const QJsonObject role = roleValue.toObject();
        const QString moduleId = role.value(QStringLiteral("module")).toString();
        const QString semantic = role.value(QStringLiteral("semantic")).toString();
        if (canonical(moduleId) && canonical(semantic)) {
            model.m_rolesByModule.insert(moduleId, semantic);
        }
    }
    return model;
}

QString NoCSemanticModel::semanticRoleForModule(const QString& moduleId) const {
    return m_rolesByModule.value(moduleId);
}

bool NoCSemanticModel::isEmpty() const {
    return m_rolesByModule.isEmpty();
}
