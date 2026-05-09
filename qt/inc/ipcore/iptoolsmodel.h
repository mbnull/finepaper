// Read-only model for tools available in the active IP workspace.
#pragma once

#include <QString>
#include <QVector>

struct ActiveWorkspaceState;
struct IpCatalogEntry;

struct IpToolEntry {
    QString id;
    QString label;
    QString kind;
};

class IpToolsModel {
public:
    QVector<IpToolEntry> entriesForWorkspace(const ActiveWorkspaceState& state,
                                             const IpCatalogEntry& entry) const;
};
