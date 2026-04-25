// PluginRegistry discovers directory-based Finepaper plugins at startup.
#pragma once

#include "plugins/plugindescriptor.h"

#include <QList>
#include <QString>
#include <QStringList>

class PluginRegistry {
public:
    static PluginRegistry& instance();

    static QList<PluginDescriptor> discover(const QStringList& roots);
    static QStringList defaultPluginRoots();

    const QList<PluginDescriptor>& plugins() const;
    const PluginDescriptor* plugin(const QString& pluginId) const;

private:
    PluginRegistry();

    QList<PluginDescriptor> m_plugins;
};
