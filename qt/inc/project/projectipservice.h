// Project IP service owns add/select/remove workflows for project IP instances.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/pluginstate.h"

#include <QObject>
#include <QString>
#include <optional>

class ProjectStateService;

struct ProjectIpInstanceRef {
    QString ipcoreId;
    QString instanceId;
};

struct ProjectIpServiceResult {
    bool success = false;
    QString error;
    ProjectIpInstanceRecord record;
};

class ProjectIpService : public QObject {
    Q_OBJECT

public:
    explicit ProjectIpService(ProjectStateService* stateService, QObject* parent = nullptr);

    ProjectIpServiceResult ensureInstanceForIpcore(const IpCatalogEntry& entry);
    bool selectInstance(const QString& ipcoreId, const QString& instanceId);
    bool removeInstance(const QString& ipcoreId, const QString& instanceId);
    std::optional<ProjectIpInstanceRef> selectedIpInstance() const;

signals:
    void ipInstancesChanged();
    void selectedIpInstanceChanged();

private:
    const ProjectIpInstanceRecord* findRecord(const QString& ipcoreId,
                                              const QString& instanceId) const;
    const ProjectIpInstanceRecord* firstRecordForIpcore(const QString& ipcoreId) const;
    void setSelectedInstance(std::optional<ProjectIpInstanceRef> selection);

    ProjectStateService* m_stateService = nullptr;
    std::optional<ProjectIpInstanceRef> m_selectedIpInstance;
};
