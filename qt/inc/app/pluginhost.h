#pragma once

#include "app/appcontext.h"

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class IAppPlugin {
public:
    virtual ~IAppPlugin() = default;
    virtual QString id() const = 0;
    virtual void activate(AppContext& context) = 0;
};

struct PluginActivationResult {
    bool success = false;
    QString error;
    QStringList activatedPluginIds;
};

class PluginHost {
public:
    explicit PluginHost(AppContext context);

    bool registerPlugin(std::unique_ptr<IAppPlugin> plugin);
    PluginActivationResult activatePlugins();
    QStringList pluginIds() const;

private:
    bool hasPluginId(const QString& id) const;

    AppContext m_context;
    std::vector<std::unique_ptr<IAppPlugin>> m_plugins;
    QStringList m_activatedPluginIds;
    QString m_activationError;
    bool m_activationFailed = false;
    bool m_activated = false;
};
