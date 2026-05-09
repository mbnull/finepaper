// IP catalog service implementation.
#include "ipcore/ipcatalogservice.h"

#include "modules/moduleregistry.h"
#include "plugins/pluginregistry.h"

#include <algorithm>

namespace {

QString catalogSortLabel(const IpCatalogEntry& entry) {
    return entry.name.trimmed().isEmpty() ? entry.id : entry.name;
}

IpCatalogEntry catalogEntryFromDescriptor(const PluginDescriptor& descriptor,
                                          const ModuleRegistry* moduleRegistry) {
    IpCatalogEntry entry;
    entry.id = descriptor.id;
    entry.name = descriptor.name;
    entry.version = descriptor.version;
    entry.kind = descriptor.kind;
    entry.runtimeRootPath = descriptor.runtimeRootPath;
    entry.sourceRootPath = descriptor.sourceRootPath;
    entry.modulesPath = descriptor.modulesPath;
    entry.graphicsPath = descriptor.graphicsPath;
    entry.instanceParameters = descriptor.instanceParameters;
    entry.generator = descriptor.generator;
    entry.drc = descriptor.drc;
    entry.topologyPresets = descriptor.topologyPresets;
    if (moduleRegistry) {
        entry.moduleTypes = moduleRegistry->availableTypesForPlugin(descriptor.id);
    }
    return entry;
}

} // namespace

bool IpCatalogEntry::hasModules() const {
    return !modulesPath.isEmpty();
}

bool IpCatalogEntry::isSelectable() const {
    return !moduleTypes.isEmpty();
}

IpCatalogService::IpCatalogService(QList<PluginDescriptor> descriptors,
                                   const ModuleRegistry* moduleRegistry) {
    m_entries.reserve(descriptors.size());
    for (const PluginDescriptor& descriptor : descriptors) {
        m_entries.append(catalogEntryFromDescriptor(descriptor, moduleRegistry));
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const IpCatalogEntry& left, const IpCatalogEntry& right) {
                  const int labelCompare =
                      QString::compare(catalogSortLabel(left),
                                       catalogSortLabel(right),
                                       Qt::CaseInsensitive);
                  if (labelCompare != 0) {
                      return labelCompare < 0;
                  }
                  return QString::compare(left.id, right.id, Qt::CaseInsensitive) < 0;
              });
}

IpCatalogService IpCatalogService::fromRuntimeRegistries() {
    return IpCatalogService(PluginRegistry::instance().plugins(), &ModuleRegistry::instance());
}

QList<IpCatalogEntry> IpCatalogService::entries() const {
    return m_entries;
}

QList<IpCatalogEntry> IpCatalogService::selectableEntries() const {
    QList<IpCatalogEntry> selectable;
    for (const IpCatalogEntry& catalogEntry : m_entries) {
        if (catalogEntry.isSelectable()) {
            selectable.append(catalogEntry);
        }
    }
    return selectable;
}

std::optional<IpCatalogEntry> IpCatalogService::entry(const QString& id) const {
    for (const IpCatalogEntry& catalogEntry : m_entries) {
        if (catalogEntry.id == id) {
            return catalogEntry;
        }
    }
    return std::nullopt;
}
