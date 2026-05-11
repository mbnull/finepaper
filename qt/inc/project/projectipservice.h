// Project IP service owns add/select workflows for project IP instances.
#pragma once

#include "ipcore/ipcatalogservice.h"
#include "project/ipinstancestate.h"

#include <QObject>
#include <QString>
#include <optional>

class ProjectStateService;
struct ProjectDocument;

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

    void loadFromDocument(const ProjectDocument& document);
    void clear();
    ProjectIpServiceResult createInstanceForIpcore(const IpCatalogEntry& entry);
    bool selectInstance(const QString& ipcoreId, const QString& instanceId);
    std::optional<ProjectIpInstanceRef> selectedIpInstance() const;
    std::optional<ProjectIpInstanceRecord> selectedIpInstanceRecord() const;
    enum class SelectionFallbackPolicy {
        PreserveCurrentOrFirst,
        ExactOrClear,
    };
    void handleIpInstanceRecordsMutated(
        std::optional<ProjectIpInstanceRef> preferredSelection,
        SelectionFallbackPolicy fallbackPolicy = SelectionFallbackPolicy::PreserveCurrentOrFirst);

signals:
    void ipInstancesChanged();
    void selectedIpInstanceChanged();

private:
    const ProjectIpInstanceRecord* findRecord(const QString& ipcoreId,
                                              const QString& instanceId) const;
    QString nextInstanceIdForIpcore(const QString& ipcoreId) const;
    void setSelectedInstance(std::optional<ProjectIpInstanceRef> selection);

    ProjectStateService* m_stateService = nullptr;
    std::optional<ProjectIpInstanceRef> m_selectedIpInstance;
};
