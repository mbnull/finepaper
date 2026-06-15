#pragma once

#include "ipcore/ipcatalogservice.h"
#include "ipcraft/compositionmodel.h"
#include "ipcraft/packagespec.h"
#include "project/ipinstancestate.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace ProjectFlowSupport {

struct PackageFlowContext {
    enum class ErrorKind {
        None,
        MissingPackageRoot,
        SpecReadFailed,
        MissingFlow,
    };

    bool ok = false;
    ErrorKind errorKind = ErrorKind::None;
    ipcraft::PackageSpec package;
    QString packageRoot;
    QString error;
};

QStringList defaultFrameworkToolSearchPaths();
QString designNameForProject(const QString& projectPath, const QString& explicitDesignName);
QString withInstanceContext(const ProjectIpInstanceRecord& instance, const QString& message);
const IpCatalogEntry* findCatalogEntry(const QList<IpCatalogEntry>& entries, const QString& ipcoreId);
PackageFlowContext packageFlowContextForEntry(const IpCatalogEntry& entry, const QString& flowId);
std::optional<ipcraft::GraphConfig> graphConfigForInstance(
    const ProjectIpInstanceRecord& instance);
QString readTextFileIfPresent(const QString& path);
bool isSafeInstanceOutputKey(const QString& instanceId);
bool isReservedInstanceOutputKey(const QString& instanceId);

} // namespace ProjectFlowSupport
