// ProjectStateService stores plugin-owned project state outside Graph.
#include "project/projectstateservice.h"

void ProjectStateService::clear() {
    m_pluginStates.clear();
}

void ProjectStateService::loadFromDocument(const ProjectDocument& document) {
    m_pluginStates = document.pluginStates;
}

void ProjectStateService::writeToDocument(ProjectDocument& document) const {
    document.pluginStates = m_pluginStates;
}

bool ProjectStateService::setParameter(const QString& pluginId,
                                       const QString& instanceId,
                                       const QString& section,
                                       const QString& name,
                                       const QJsonValue& value) {
    for (ProjectPluginStateRecord& record : m_pluginStates) {
        if (record.pluginId != pluginId || record.instanceId != instanceId) {
            continue;
        }

        const QJsonValue sectionValue = record.state.value(section);
        if (!sectionValue.isObject()) {
            return false;
        }

        QJsonObject sectionObject = sectionValue.toObject();
        sectionObject.insert(name, value);
        record.state.insert(section, sectionObject);
        return true;
    }
    return false;
}

QJsonValue ProjectStateService::parameter(const QString& pluginId,
                                          const QString& instanceId,
                                          const QString& section,
                                          const QString& name) const {
    for (const ProjectPluginStateRecord& record : m_pluginStates) {
        if (record.pluginId == pluginId && record.instanceId == instanceId) {
            const QJsonValue sectionValue = record.state.value(section);
            if (!sectionValue.isObject()) {
                return QJsonValue(QJsonValue::Undefined);
            }
            return sectionValue.toObject().value(name);
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}
