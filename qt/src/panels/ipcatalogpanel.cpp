// IP catalog panel implementation.
#include "panels/ipcatalogpanel.h"

#include "ipcore/internalmodulelibrarymodel.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/iptoolsmodel.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "widgets/collapsiblesection.h"
#include "workspace/activeworkspacecontroller.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDrag>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMap>
#include <QMimeData>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <optional>
#include <utility>

namespace {

constexpr int InstanceIdRole = Qt::UserRole + 1;
constexpr int IpcoreIdRole = Qt::UserRole + 2;
constexpr int ActiveInstanceIdRole = Qt::UserRole + 3;
constexpr auto ScopedModuleMime = "application/x-finepaper-module";

QString catalogLabel(const IpCatalogEntry& entry) {
    const QString label = entry.name.trimmed().isEmpty() ? entry.id : entry.name.trimmed();
    return entry.kind.trimmed().isEmpty()
        ? label
        : label + QStringLiteral(" (") + entry.kind.trimmed() + QStringLiteral(")");
}

bool catalogMatchesFilter(const IpCatalogEntry& entry, const QString& filter) {
    if (filter.trimmed().isEmpty()) {
        return true;
    }

    const QString needle = filter.trimmed();
    return entry.id.contains(needle, Qt::CaseInsensitive)
        || entry.name.contains(needle, Qt::CaseInsensitive)
        || entry.kind.contains(needle, Qt::CaseInsensitive);
}

QString projectInstanceLabel(const ProjectIpInstanceRecord& record) {
    return record.instanceId.trimmed().isEmpty()
        ? record.ipcoreId
        : record.instanceId + QStringLiteral(" - ") + record.ipcoreId;
}

QString humanizeCategory(const QString& category) {
    const QString trimmed = category.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("Uncategorized");
    }
    if (trimmed.compare(QStringLiteral("noc"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("NoC");
    }

    QString text = trimmed;
    text.replace('-', ' ');
    text.replace('_', ' ');

    bool capitalizeNext = true;
    for (int index = 0; index < text.size(); ++index) {
        if (text[index].isSpace()) {
            capitalizeNext = true;
            continue;
        }
        if (capitalizeNext) {
            text[index] = text[index].toUpper();
            capitalizeNext = false;
        }
    }
    return text;
}

QWidget* contentWidget(QWidget* parent) {
    auto* content = new QWidget(parent);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    return content;
}

CollapsibleSection* section(const QString& title,
                            const QString& objectName,
                            QWidget* content,
                            QWidget* parent) {
    auto* result = new CollapsibleSection(title, parent);
    result->setObjectName(objectName);
    result->toggleButton()->setObjectName(objectName + QStringLiteral("Toggle"));
    content->setObjectName(objectName + QStringLiteral("Content"));
    result->setContentWidget(content);
    return result;
}

class ModuleListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

    void setDragStartedCallback(std::function<void(const QString&)> callback) {
        m_dragStartedCallback = std::move(callback);
    }

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        QListWidgetItem* item = currentItem();
        if (!item) {
            return;
        }

        const QString moduleType = item->data(Qt::UserRole).toString();
        const QString ipcoreId = item->data(IpcoreIdRole).toString();
        const QString instanceId = item->data(ActiveInstanceIdRole).toString();
        if (moduleType.trimmed().isEmpty() ||
            ipcoreId.trimmed().isEmpty() ||
            instanceId.trimmed().isEmpty()) {
            return;
        }

        auto* drag = new QDrag(this);
        auto* mimeData = new QMimeData;
        QJsonObject object;
        object.insert(QStringLiteral("ipcore"), ipcoreId);
        object.insert(QStringLiteral("instance"), instanceId);
        object.insert(QStringLiteral("type"), moduleType);
        mimeData->setData(ScopedModuleMime,
                          QJsonDocument(object).toJson(QJsonDocument::Compact));
        drag->setMimeData(mimeData);

        if (m_dragStartedCallback) {
            m_dragStartedCallback(moduleType);
        }
        Q_UNUSED(supportedActions);
        drag->exec(Qt::CopyAction);
    }

private:
    std::function<void(const QString&)> m_dragStartedCallback;
};

} // namespace

IpCatalogPanel::IpCatalogPanel(const IpCatalogService* catalogService,
                               ProjectStateService* stateService,
                               ProjectIpService* projectIpService,
                               ActiveWorkspaceController* workspaceController,
                               QWidget* parent)
    : QWidget(parent),
      m_catalogService(catalogService),
      m_stateService(stateService),
      m_projectIpService(projectIpService),
      m_workspaceController(workspaceController) {
    setObjectName(QStringLiteral("ipCatalogPanel"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("ipCatalogSearch"));
    layout->addWidget(m_search);

    auto* catalogContent = contentWidget(this);
    auto* catalogLayout = qobject_cast<QVBoxLayout*>(catalogContent->layout());
    m_catalogList = new QTreeWidget(catalogContent);
    m_catalogList->setObjectName(QStringLiteral("ipCatalogList"));
    m_catalogList->setHeaderHidden(true);
    m_catalogList->setItemsExpandable(true);
    m_catalogList->setRootIsDecorated(true);
    m_catalogList->setMinimumHeight(130);
    catalogLayout->addWidget(m_catalogList);
    layout->addWidget(section(QStringLiteral("IP Cores"),
                              QStringLiteral("ipCatalogIpCoresSection"),
                              catalogContent,
                              this));

    auto* projectContent = contentWidget(this);
    auto* projectLayout = qobject_cast<QVBoxLayout*>(projectContent->layout());
    m_projectIpList = new QListWidget(projectContent);
    m_projectIpList->setObjectName(QStringLiteral("projectIpList"));
    m_projectIpList->setMinimumHeight(80);
    m_projectIpList->setContextMenuPolicy(Qt::CustomContextMenu);
    projectLayout->addWidget(m_projectIpList);
    layout->addWidget(section(QStringLiteral("Project Instances"),
                              QStringLiteral("ipCatalogProjectInstancesSection"),
                              projectContent,
                              this));
    auto* removeProjectInstanceAction =
        new QAction(QStringLiteral("Delete Instance"), m_projectIpList);
    removeProjectInstanceAction->setObjectName(QStringLiteral("projectIpRemoveAction"));
    removeProjectInstanceAction->setShortcut(QKeySequence::Delete);
    removeProjectInstanceAction->setShortcutContext(Qt::WidgetShortcut);
    m_projectIpList->addAction(removeProjectInstanceAction);

    auto* modulesContent = contentWidget(this);
    auto* modulesLayout = qobject_cast<QVBoxLayout*>(modulesContent->layout());
    auto* activeModuleList = new ModuleListWidget(modulesContent);
    activeModuleList->setObjectName(QStringLiteral("activeModuleList"));
    activeModuleList->setDragEnabled(true);
    activeModuleList->setDragDropMode(QAbstractItemView::DragOnly);
    activeModuleList->setDefaultDropAction(Qt::CopyAction);
    activeModuleList->setMinimumHeight(80);
    activeModuleList->setDragStartedCallback([this](const QString& moduleType) {
        emit moduleDragStarted(moduleType);
    });
    m_activeModuleList = activeModuleList;
    modulesLayout->addWidget(m_activeModuleList);
    layout->addWidget(section(QStringLiteral("Workspace Modules"),
                              QStringLiteral("ipCatalogWorkspaceModulesSection"),
                              modulesContent,
                              this));

    auto* toolsContent = contentWidget(this);
    auto* toolsLayout = qobject_cast<QVBoxLayout*>(toolsContent->layout());
    m_activeToolList = new QListWidget(toolsContent);
    m_activeToolList->setObjectName(QStringLiteral("activeToolList"));
    m_activeToolList->setMinimumHeight(80);
    toolsLayout->addWidget(m_activeToolList);
    layout->addWidget(section(QStringLiteral("Workspace Tools"),
                              QStringLiteral("ipCatalogWorkspaceToolsSection"),
                              toolsContent,
                              this));
    layout->addStretch(1);

    connect(m_search, &QLineEdit::textChanged, this, [this] { refreshCatalog(); });
    connect(m_catalogList, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        emitAddRequest(item);
    });
    connect(m_catalogList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        emitAddRequest(item);
    });
    connect(m_projectIpList,
            &QListWidget::currentItemChanged,
            this,
            [this](QListWidgetItem* current, QListWidgetItem*) { emitSelectRequest(current); });
    connect(m_projectIpList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emitSelectRequest(item);
    });
    connect(m_projectIpList,
            &QListWidget::customContextMenuRequested,
            this,
            [this](const QPoint& pos) {
                if (!m_projectIpList) {
                    return;
                }

                QListWidgetItem* item = m_projectIpList->itemAt(pos);
                if (!item) {
                    return;
                }
                const QString ipcoreId = item->data(Qt::UserRole).toString();
                const QString instanceId = item->data(InstanceIdRole).toString();

                QMenu menu;
                QAction* removeAction = menu.addAction(QStringLiteral("Delete Instance"));
                connect(removeAction, &QAction::triggered, this, [this, ipcoreId, instanceId] {
                    emitRemoveRequest(ipcoreId, instanceId);
                });
                menu.exec(m_projectIpList->viewport()->mapToGlobal(pos));
            });
    connect(removeProjectInstanceAction, &QAction::triggered, this, [this] {
        emitRemoveRequest(m_projectIpList ? m_projectIpList->currentItem() : nullptr);
    });
    connect(m_activeToolList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emitWorkspaceToolRequest(item);
    });

    if (m_stateService) {
        connect(m_stateService,
                &ProjectStateService::ipInstanceRecordsChanged,
                this,
                [this] {
                    refreshProjectInstances();
                    scheduleProjectSelectionSync();
                });
    }
    if (m_projectIpService) {
        connect(m_projectIpService,
                &ProjectIpService::selectedIpInstanceChanged,
                this,
                &IpCatalogPanel::scheduleProjectSelectionSync);
    }
    if (m_workspaceController) {
        connect(m_workspaceController,
                &ActiveWorkspaceController::activeWorkspaceChanged,
                this,
                &IpCatalogPanel::refreshActiveWorkspace);
    }

    refreshCatalog();
    refreshProjectInstances();
    refreshActiveWorkspace();
    scheduleProjectSelectionSync();
}

void IpCatalogPanel::refreshCatalog() {
    if (!m_catalogList) {
        return;
    }

    m_catalogList->clear();
    if (!m_catalogService) {
        return;
    }

    const QString filter = m_search ? m_search->text() : QString();
    QMap<QString, QTreeWidgetItem*> categories;
    for (const IpCatalogEntry& entry : m_catalogService->selectableEntries()) {
        if (!catalogMatchesFilter(entry, filter)) {
            continue;
        }

        const QString category = humanizeCategory(entry.kind);
        QTreeWidgetItem* categoryItem = categories.value(category, nullptr);
        if (!categoryItem) {
            categoryItem = new QTreeWidgetItem(QStringList{category});
            categoryItem->setFlags(Qt::ItemIsEnabled);
            m_catalogList->addTopLevelItem(categoryItem);
            categories.insert(category, categoryItem);
        }

        auto* item = new QTreeWidgetItem(categoryItem, QStringList{catalogLabel(entry)});
        item->setData(0, Qt::UserRole, entry.id);
        item->setToolTip(0, entry.id);
    }

    for (auto it = categories.cbegin(); it != categories.cend(); ++it) {
        m_catalogList->expandItem(it.value());
    }
}

void IpCatalogPanel::refreshProjectInstances() {
    if (!m_projectIpList) {
        return;
    }

    m_projectIpList->clear();
    if (!m_stateService) {
        return;
    }

    for (const ProjectIpInstanceRecord& record : m_stateService->ipInstanceRecords()) {
        auto* item = new QListWidgetItem(projectInstanceLabel(record));
        item->setData(Qt::UserRole, record.ipcoreId);
        item->setData(InstanceIdRole, record.instanceId);
        m_projectIpList->addItem(item);
    }
}

void IpCatalogPanel::refreshActiveWorkspace() {
    if (m_activeModuleList) {
        m_activeModuleList->clear();
    }
    if (m_activeToolList) {
        m_activeToolList->clear();
    }
    if (!m_workspaceController) {
        return;
    }

    const ActiveWorkspaceState& state = m_workspaceController->state();
    InternalModuleLibraryModel moduleModel;
    const QVector<InternalModuleLibraryEntry> moduleEntries =
        moduleModel.entriesForModuleTypes(state.moduleTypes);

    QHash<QString, InternalModuleLibraryEntry> entriesByType;
    for (const InternalModuleLibraryEntry& entry : moduleEntries) {
        entriesByType.insert(entry.moduleType, entry);
    }

    if (m_activeModuleList) {
        for (const QString& moduleType : state.moduleTypes) {
            InternalModuleLibraryEntry entry = entriesByType.value(moduleType);
            if (entry.moduleType.isEmpty()) {
                entry.moduleType = moduleType;
                entry.label = moduleType;
            }
            auto* item = new QListWidgetItem(entry.label);
            item->setData(Qt::UserRole, entry.moduleType);
            item->setData(IpcoreIdRole, state.ipcoreId);
            item->setData(ActiveInstanceIdRole, state.instanceId);
            item->setToolTip(entry.description);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            m_activeModuleList->addItem(item);
        }
    }

    if (!m_catalogService || !m_activeToolList || !state.hasActiveIp) {
        return;
    }

    const std::optional<IpCatalogEntry> entry = m_catalogService->entry(state.ipcoreId);
    if (!entry.has_value()) {
        return;
    }

    const IpToolsModel toolsModel;
    for (const IpToolEntry& tool : toolsModel.entriesForWorkspace(state, *entry)) {
        auto* item = new QListWidgetItem(tool.label);
        item->setData(Qt::UserRole, tool.id);
        item->setData(IpcoreIdRole, state.ipcoreId);
        item->setData(ActiveInstanceIdRole, state.instanceId);
        m_activeToolList->addItem(item);
    }
}

void IpCatalogPanel::emitAddRequest(QTreeWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString ipcoreId = item->data(0, Qt::UserRole).toString();
    if (!ipcoreId.trimmed().isEmpty()) {
        emit addIpcoreRequested(ipcoreId);
    }
}

void IpCatalogPanel::emitSelectRequest(QListWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString ipcoreId = item->data(Qt::UserRole).toString();
    const QString instanceId = item->data(InstanceIdRole).toString();
    if (!ipcoreId.trimmed().isEmpty() && !instanceId.trimmed().isEmpty()) {
        emit selectIpInstanceRequested(ipcoreId, instanceId);
    }
}

void IpCatalogPanel::emitRemoveRequest(const QString& ipcoreId, const QString& instanceId) {
    if (!ipcoreId.trimmed().isEmpty() && !instanceId.trimmed().isEmpty()) {
        emit removeIpInstanceRequested(ipcoreId, instanceId);
    }
}

void IpCatalogPanel::emitRemoveRequest(QListWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString ipcoreId = item->data(Qt::UserRole).toString();
    const QString instanceId = item->data(InstanceIdRole).toString();
    emitRemoveRequest(ipcoreId, instanceId);
}

void IpCatalogPanel::emitWorkspaceToolRequest(QListWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString toolId = item->data(Qt::UserRole).toString();
    const QString ipcoreId = item->data(IpcoreIdRole).toString();
    const QString instanceId = item->data(ActiveInstanceIdRole).toString();
    if (!toolId.trimmed().isEmpty() &&
        !ipcoreId.trimmed().isEmpty() &&
        !instanceId.trimmed().isEmpty()) {
        emit workspaceToolRequested(toolId, ipcoreId, instanceId);
    }
}

void IpCatalogPanel::scheduleProjectSelectionSync() {
    if (m_projectSelectionSyncPending) {
        return;
    }

    m_projectSelectionSyncPending = true;
    QTimer::singleShot(0, this, [this] {
        m_projectSelectionSyncPending = false;
        syncProjectSelection();
    });
}

void IpCatalogPanel::syncProjectSelection() {
    if (!m_projectIpList || !m_projectIpService) {
        return;
    }

    const std::optional<ProjectIpInstanceRef> selection = m_projectIpService->selectedIpInstance();
    const QSignalBlocker blocker(m_projectIpList);
    if (!selection.has_value()) {
        m_projectIpList->setCurrentItem(nullptr);
        return;
    }

    for (int row = 0; row < m_projectIpList->count(); ++row) {
        QListWidgetItem* item = m_projectIpList->item(row);
        if (!item) {
            continue;
        }
        if (item->data(Qt::UserRole).toString() == selection->ipcoreId
            && item->data(InstanceIdRole).toString() == selection->instanceId) {
            m_projectIpList->setCurrentItem(item);
            return;
        }
    }

    m_projectIpList->setCurrentItem(nullptr);
}
