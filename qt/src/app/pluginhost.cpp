#include "app/pluginhost.h"

#include <exception>
#include <utility>

namespace {

bool isCanonicalPluginId(const QString& id) {
    return !id.isEmpty() && id == id.trimmed();
}

QString activationFailedError(const QString& pluginId, const QString& reason) {
    if (reason.trimmed().isEmpty()) {
        return QStringLiteral("Plugin activation failed for %1.").arg(pluginId);
    }

    return QStringLiteral("Plugin activation failed for %1: %2").arg(pluginId, reason);
}

} // namespace

PluginHost::PluginHost(AppContext context) : m_context(context) {}

bool PluginHost::registerPlugin(std::unique_ptr<IAppPlugin> plugin) {
    if (!plugin) {
        return false;
    }

    const QString id = plugin->id();
    if (!isCanonicalPluginId(id) || hasPluginId(id) || m_activated || m_activationFailed) {
        return false;
    }

    m_plugins.push_back(std::move(plugin));
    return true;
}

PluginActivationResult PluginHost::activatePlugins() {
    PluginActivationResult result;

    if (m_activationFailed) {
        result.success = false;
        result.error = m_activationError;
        result.activatedPluginIds = m_activatedPluginIds;
        return result;
    }

    if (!m_context.workbench) {
        result.success = false;
        result.error = QStringLiteral("WorkbenchService is required before activating plugins.");
        return result;
    }

    if (m_activated) {
        result.success = true;
        result.activatedPluginIds = m_activatedPluginIds;
        return result;
    }

    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        const QString pluginId = plugin->id();
        try {
            plugin->activate(m_context);
        } catch (const std::exception& exception) {
            m_activationFailed = true;
            m_activationError = activationFailedError(pluginId, QString::fromUtf8(exception.what()));
            result.success = false;
            result.error = m_activationError;
            result.activatedPluginIds = m_activatedPluginIds;
            return result;
        } catch (...) {
            m_activationFailed = true;
            m_activationError = activationFailedError(pluginId, QStringLiteral("unknown exception"));
            result.success = false;
            result.error = m_activationError;
            result.activatedPluginIds = m_activatedPluginIds;
            return result;
        }

        m_activatedPluginIds.append(pluginId);
        result.activatedPluginIds = m_activatedPluginIds;
    }

    m_activated = true;
    result.success = true;
    return result;
}

QStringList PluginHost::pluginIds() const {
    QStringList ids;
    ids.reserve(m_plugins.size());
    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        ids.append(plugin->id());
    }
    return ids;
}

bool PluginHost::hasPluginId(const QString& id) const {
    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        if (plugin->id() == id) {
            return true;
        }
    }
    return false;
}
