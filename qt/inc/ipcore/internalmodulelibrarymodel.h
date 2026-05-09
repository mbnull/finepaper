// Read-only model for module types available in the active IP workspace.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class ModuleRegistry;

struct InternalModuleLibraryEntry {
    QString moduleType;
    QString label;
    QString description;
    QString graphGroup;
};

class InternalModuleLibraryModel {
public:
    explicit InternalModuleLibraryModel(const ModuleRegistry* moduleRegistry = nullptr);

    QVector<InternalModuleLibraryEntry> entriesForModuleTypes(const QStringList& moduleTypes) const;

private:
    const ModuleRegistry* m_moduleRegistry = nullptr;
};
