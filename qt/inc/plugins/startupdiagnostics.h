// StartupDiagnostics formats plugin/IP load state for the Qt startup log.
#pragma once

#include "modules/moduleregistry.h"
#include "plugins/plugindescriptor.h"

#include <QList>
#include <QStringList>

namespace StartupDiagnostics {

QStringList pluginLogLines(const QList<PluginDescriptor>& plugins);
QStringList ipLogLines(const ModuleRegistry& registry);
QStringList logLines(const QList<PluginDescriptor>& plugins, const ModuleRegistry& registry);

} // namespace StartupDiagnostics
