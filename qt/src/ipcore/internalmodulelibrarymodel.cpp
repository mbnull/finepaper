// Internal module library model implementation.
#include "ipcore/internalmodulelibrarymodel.h"

#include "modules/moduleregistry.h"
#include "modules/moduletypemetadata.h"

InternalModuleLibraryModel::InternalModuleLibraryModel(const ModuleRegistry* moduleRegistry)
    : m_moduleRegistry(moduleRegistry) {}

QVector<InternalModuleLibraryEntry>
InternalModuleLibraryModel::entriesForModuleTypes(const QStringList& moduleTypes) const {
    const ModuleRegistry* registry = m_moduleRegistry ? m_moduleRegistry : &ModuleRegistry::instance();
    QVector<InternalModuleLibraryEntry> entries;
    entries.reserve(moduleTypes.size());

    for (const QString& moduleType : moduleTypes) {
        const ModuleType* type = registry->getType(moduleType);
        if (!type) {
            continue;
        }

        InternalModuleLibraryEntry entry;
        entry.moduleType = type->name;
        entry.label = ModuleTypeMetadata::paletteLabel(type);
        entry.description = ModuleTypeMetadata::description(type);
        entry.graphGroup = type->graphGroup;
        entries.push_back(entry);
    }

    return entries;
}
