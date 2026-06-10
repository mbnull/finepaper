#include "app/pluginhost.h"

#include <utility>

namespace {

bool isValidPluginId(const QString& id) {
    return !id.trimmed().isEmpty();
}

} // namespace

PluginHost::PluginHost(AppContext context) : m_context(context) {}

bool PluginHost::registerPlugin(std::unique_ptr<IAppPlugin> plugin) {
    if (!plugin) {
        return false;
    }

    const QString id = plugin->id();
    if (!isValidPluginId(id) || hasPluginId(id) || m_activated) {
        return false;
    }

    m_plugins.push_back(std::move(plugin));
    return true;
}

PluginActivationResult PluginHost::activatePlugins() {
    PluginActivationResult result;

    if (!m_context.workbench) {
        result.success = false;
        result.error = QStringLiteral("WorkbenchService is required before activating plugins.");
        return result;
    }

    if (m_activated) {
        result.success = true;
        result.activatedPluginIds = pluginIds();
        return result;
    }

    for (const std::unique_ptr<IAppPlugin>& plugin : m_plugins) {
        plugin->activate(m_context);
        result.activatedPluginIds.append(plugin->id());
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
