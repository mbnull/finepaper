// IP catalog panel implementation.
#include "panels/ipcatalogpanel.h"

#include "ipcore/internalmodulelibrarymodel.h"
#include "ipcore/ipcatalogservice.h"
#include "ipcore/iptoolsmodel.h"
#include "project/projectipservice.h"
#include "project/projectstateservice.h"
#include "workspace/activeworkspacecontroller.h"

#include <QAbstractItemView>
#include <QDrag>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <optional>
#include <utility>

namespace {

constexpr int InstanceIdRole = Qt::UserRole + 1;
constexpr auto ModuleTypeMime = "application/x-moduletype";

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
        if (moduleType.trimmed().isEmpty()) {
            return;
        }

        auto* drag = new QDrag(this);
        auto* mimeData = new QMimeData;
        mimeData->setData(ModuleTypeMime, moduleType.toUtf8());
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

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

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

    layout->addWidget(sectionLabel(QStringLiteral("IP Cores"), this));
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("ipCatalogSearch"));
    layout->addWidget(m_search);

    m_catalogList = new QListWidget(this);
    m_catalogList->setObjectName(QStringLiteral("ipCatalogList"));
    layout->addWidget(m_catalogList, 2);

    layout->addWidget(sectionLabel(QStringLiteral("Project Instances"), this));
    m_projectIpList = new QListWidget(this);
    m_projectIpList->setObjectName(QStringLiteral("projectIpList"));
    layout->addWidget(m_projectIpList, 1);

    layout->addWidget(sectionLabel(QStringLiteral("Workspace Modules"), this));
    auto* activeModuleList = new ModuleListWidget(this);
    activeModuleList->setObjectName(QStringLiteral("activeModuleList"));
    activeModuleList->setDragEnabled(true);
    activeModuleList->setDragDropMode(QAbstractItemView::DragOnly);
    activeModuleList->setDefaultDropAction(Qt::CopyAction);
    activeModuleList->setDragStartedCallback([this](const QString& moduleType) {
        emit moduleDragStarted(moduleType);
    });
    m_activeModuleList = activeModuleList;
    layout->addWidget(m_activeModuleList, 1);

    layout->addWidget(sectionLabel(QStringLiteral("Workspace Tools"), this));
    m_activeToolList = new QListWidget(this);
    m_activeToolList->setObjectName(QStringLiteral("activeToolList"));
    layout->addWidget(m_activeToolList, 1);

    connect(m_search, &QLineEdit::textChanged, this, [this] { refreshCatalog(); });
    connect(m_catalogList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emitAddRequest(item);
    });
    connect(m_catalogList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emitAddRequest(item);
    });
    connect(m_projectIpList,
            &QListWidget::currentItemChanged,
            this,
            [this](QListWidgetItem* current, QListWidgetItem*) { emitSelectRequest(current); });
    connect(m_projectIpList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emitSelectRequest(item);
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
    for (const IpCatalogEntry& entry : m_catalogService->selectableEntries()) {
        if (!catalogMatchesFilter(entry, filter)) {
            continue;
        }

        auto* item = new QListWidgetItem(catalogLabel(entry));
        item->setData(Qt::UserRole, entry.id);
        m_catalogList->addItem(item);
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
        item->setData(InstanceIdRole, tool.kind);
        m_activeToolList->addItem(item);
    }
}

void IpCatalogPanel::emitAddRequest(QListWidgetItem* item) {
    if (!item) {
        return;
    }

    const QString ipcoreId = item->data(Qt::UserRole).toString();
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
