// IP catalog panel: read-only catalog/workspace UI with intent signals.
#pragma once

#include <QWidget>
#include <QString>

class ActiveWorkspaceController;
class IpCatalogService;
class ProjectIpService;
class ProjectStateService;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

class IpCatalogPanel : public QWidget {
    Q_OBJECT

public:
    IpCatalogPanel(const IpCatalogService* catalogService,
                   ProjectStateService* stateService,
                   ProjectIpService* projectIpService,
                   ActiveWorkspaceController* workspaceController,
                   QWidget* parent = nullptr);

signals:
    void addIpcoreRequested(const QString& ipcoreId);
    void selectIpInstanceRequested(const QString& ipcoreId, const QString& instanceId);
    void moduleDragStarted(const QString& moduleType);

private:
    void refreshCatalog();
    void refreshProjectInstances();
    void refreshActiveWorkspace();
    void emitAddRequest(QTreeWidgetItem* item);
    void emitSelectRequest(QListWidgetItem* item);
    void scheduleProjectSelectionSync();
    void syncProjectSelection();

    const IpCatalogService* m_catalogService = nullptr;
    ProjectStateService* m_stateService = nullptr;
    ProjectIpService* m_projectIpService = nullptr;
    ActiveWorkspaceController* m_workspaceController = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeWidget* m_catalogList = nullptr;
    QListWidget* m_projectIpList = nullptr;
    QListWidget* m_activeModuleList = nullptr;
    QListWidget* m_activeToolList = nullptr;
    bool m_projectSelectionSyncPending = false;
};
