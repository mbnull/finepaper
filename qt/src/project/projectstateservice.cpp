// ProjectStateService stores plugin-owned project state outside Graph.
#include "project/projectstateservice.h"

#include <QSet>

namespace {

void addPluginStateDependencies(ProjectDocument& document,
                                const QVector<ProjectPluginStateRecord>& pluginStates) {
    QSet<QString> pluginIds;
    for (const ProjectPluginRecord& plugin : document.plugins) {
        pluginIds.insert(plugin.id);
    }
    for (const ProjectPluginStateRecord& state : pluginStates) {
        if (state.pluginId.isEmpty() || pluginIds.contains(state.pluginId)) {
            continue;
        }
        document.plugins.push_back(ProjectPluginRecord{state.pluginId, QStringLiteral("1.0")});
        pluginIds.insert(state.pluginId);
    }
}

} // namespace

ProjectStateService::ProjectStateService(QObject* parent)
    : QObject(parent) {}

void ProjectStateService::clear() {
    m_pluginStates.clear();
}

void ProjectStateService::loadFromDocument(const ProjectDocument& document) {
    m_pluginStates = document.pluginStates;
}

void ProjectStateService::writeToDocument(ProjectDocument& document) const {
    document.pluginStates = m_pluginStates;
    addPluginStateDependencies(document, m_pluginStates);
}

bool ProjectStateService::ensurePluginStateRecord(const ProjectPluginStateRecord& record) {
    for (const ProjectPluginStateRecord& existing : m_pluginStates) {
        if (existing.pluginId == record.pluginId && existing.instanceId == record.instanceId) {
            return false;
        }
    }

    m_pluginStates.push_back(record);
    return true;
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
        emit parameterChanged(pluginId, instanceId, section, name);
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
