// IP catalog panel: read-only catalog/workspace UI with intent signals.
#pragma once

#include <QWidget>
#include <QString>

class ActiveWorkspaceController;
class IpCatalogService;
class PackageService;
class PluginInteractionRegistry;
class ProjectIpService;
class ProjectStateService;
class QEvent;
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
                   const PluginInteractionRegistry* interactions = nullptr,
                   const PackageService* packageService = nullptr,
                   QWidget* parent = nullptr);

signals:
    void addIpcoreRequested(const QString& ipcoreId);
    void selectIpInstanceRequested(const QString& ipcoreId, const QString& instanceId);
    void removeIpInstanceRequested(const QString& ipcoreId, const QString& instanceId);
    void workspaceToolRequested(const QString& toolId,
                                const QString& ipcoreId,
                                const QString& instanceId);
    void moduleDragStarted(const QString& moduleType);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void refreshCatalog();
    void refreshProjectInstances();
    void refreshActiveWorkspace();
    void emitAddRequest(QTreeWidgetItem* item);
    void emitSelectRequest(QListWidgetItem* item);
    void emitRemoveRequest(const QString& ipcoreId, const QString& instanceId);
    void emitRemoveRequest(QListWidgetItem* item);
    void emitWorkspaceToolRequest(QListWidgetItem* item);
    void scheduleProjectSelectionSync();
    void syncProjectSelection();

    const IpCatalogService* m_catalogService = nullptr;
    ProjectStateService* m_stateService = nullptr;
    ProjectIpService* m_projectIpService = nullptr;
    ActiveWorkspaceController* m_workspaceController = nullptr;
    const PluginInteractionRegistry* m_interactions = nullptr;
    const PackageService* m_packageService = nullptr;
    QLineEdit* m_search = nullptr;
    QTreeWidget* m_catalogList = nullptr;
    QListWidget* m_projectIpList = nullptr;
    QListWidget* m_activeModuleList = nullptr;
    QListWidget* m_activeToolList = nullptr;
    QListWidget* m_packageInspectorList = nullptr;
    bool m_projectSelectionSyncPending = false;
};
