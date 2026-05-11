// IpCoreRuntimeDiagnostics formats IP core runtime load state for the Qt startup log.
#pragma once

#include "ipcore/ipcoreruntimedescriptor.h"
#include "modules/moduleregistry.h"

#include <QList>
#include <QStringList>

namespace IpCoreRuntimeDiagnostics {

QStringList runtimeLogLines(const QList<IpCoreRuntimeDescriptor>& runtimes);
QStringList ipLogLines(const ModuleRegistry& registry);
QStringList logLines(const QList<IpCoreRuntimeDescriptor>& runtimes, const ModuleRegistry& registry);

} // namespace IpCoreRuntimeDiagnostics
