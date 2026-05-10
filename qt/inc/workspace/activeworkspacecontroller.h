// Active workspace controller exposes the selected IP instance as a read model.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

class IpCatalogService;
class ProjectIpService;

struct ActiveWorkspaceState {
    bool hasActiveIp = false;
    QString ipcoreId;
    QString instanceId;
    QString label;
    QString kind;
    QStringList moduleTypes;
    QVector<TopologyPresetDescriptor> topologyPresets;
};

struct ActiveWorkspaceContext {
    ActiveWorkspaceState workspace;
    IpCatalogEntry entry;
    ProjectIpInstanceRecord record;
};

class ActiveWorkspaceController : public QObject {
    Q_OBJECT

public:
    ActiveWorkspaceController(ProjectIpService* projectIpService,
                              const IpCatalogService* catalogService,
                              QObject* parent = nullptr);

    const ActiveWorkspaceState& state() const;
    std::optional<ActiveWorkspaceContext> activeContext() const;

signals:
    void activeWorkspaceChanged();

private:
    void recompute();

    ProjectIpService* m_projectIpService = nullptr;
    const IpCatalogService* m_catalogService = nullptr;
    ActiveWorkspaceState m_state;
};
