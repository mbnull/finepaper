// IP catalog service exposes discovered runtime IP cores as editor-ready entries.
#pragma once

#include "ipcore/ipcoreruntimedescriptor.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

class ModuleRegistry;

struct IpCatalogEntry {
    QString id;
    QString name;
    QString version;
    QString kind;
    QString runtimeRootPath;
    QString sourceRootPath;
    QString modulesPath;
    QString graphicsPath;
    QStringList moduleTypes;
    QHash<QString, IpCoreInstanceParameterDescriptor> instanceParameters;
    IpCoreCommandDescriptor generator;
    IpCoreCommandDescriptor drc;
    QVector<TopologyPresetDescriptor> topologyPresets;

    bool hasModules() const;
    bool isSelectable() const;
};

class IpCatalogService {
public:
    IpCatalogService(QList<IpCoreRuntimeDescriptor> descriptors,
                     const ModuleRegistry* moduleRegistry);

    static IpCatalogService fromRuntimeRegistries();

    QList<IpCatalogEntry> entries() const;
    QList<IpCatalogEntry> selectableEntries() const;
    std::optional<IpCatalogEntry> entry(const QString& id) const;

private:
    QList<IpCatalogEntry> m_entries;
};
