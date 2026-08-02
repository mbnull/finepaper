#include "features/package_library/package_library_panel.h"

#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"
#include "ui/workbench/workbench_config.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDrag>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

class EndpointPaletteList final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override {
        return {workbench::endpointTypeMime};
    }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        if (items.isEmpty()) {
            return nullptr;
        }
        const QString endpointType =
            items.front()->data(Qt::UserRole).toString();
        if (endpointType.isEmpty()) {
            return nullptr;
        }
        auto* mimeData = new QMimeData;
        mimeData->setData(
            workbench::endpointTypeMime, endpointType.toUtf8());
        return mimeData;
    }

    Qt::DropActions supportedDropActions() const override {
        return Qt::CopyAction;
    }

    void startDrag(Qt::DropActions) override {
        QList<QListWidgetItem*> items = selectedItems();
        if (items.isEmpty() && currentItem()) {
            items.append(currentItem());
        }
        QMimeData* payload = mimeData(items);
        if (!payload) {
            return;
        }
        QDrag drag(this);
        drag.setMimeData(payload);
        drag.exec(Qt::CopyAction, Qt::CopyAction);
    }
};

QString packageDisplayText(const CreationPackageItem& package) {
    return package.name.trimmed().isEmpty() ? package.key() : package.name;
}

} // namespace

PackageLibraryPanel::PackageLibraryPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.packageLibraryPanel"));
    setMinimumWidth(0);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(
        QStringLiteral("finepaper.packageLibraryScroll"));
    m_scroll->setAccessibleName(
        QStringLiteral("NoC IP and Endpoint library content"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(m_scroll);
    content->setObjectName(
        QStringLiteral("finepaper.packageLibraryContent"));
    content->setMinimumWidth(0);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing12);
    contentLayout->setSpacing(ui::UiMetrics::spacing12);

    m_packageGroup = new QGroupBox(
        QStringLiteral("Packages"), content);
    m_packageGroup->setObjectName(
        QStringLiteral("finepaper.packageLibrarySection"));
    m_packageGroup->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* packageLayout = new QVBoxLayout(m_packageGroup);
    packageLayout->setSpacing(ui::UiMetrics::spacing8);

    m_availablePackages = new QLabel(m_packageGroup);
    m_availablePackages->setObjectName(
        QStringLiteral("finepaper.availablePackages"));
    m_availablePackages->setWordWrap(true);
    m_availablePackages->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_availablePackages->setTextFormat(Qt::PlainText);
    m_availablePackages->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_availablePackages->setAccessibleName(
        QStringLiteral("Runnable NoC IP Packages"));
    m_availablePackages->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    packageLayout->addWidget(m_availablePackages);

    auto* creationPackageLabel = new QLabel(
        QStringLiteral("New design Package"), m_packageGroup);
    creationPackageLabel->setObjectName(
        QStringLiteral("finepaper.creationPackageLabel"));
    creationPackageLabel->setWordWrap(true);
    creationPackageLabel->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_creationPackageSelector = new QComboBox(m_packageGroup);
    m_creationPackageSelector->setObjectName(
        QStringLiteral("finepaper.packageSelector"));
    m_creationPackageSelector->setAccessibleName(
        QStringLiteral("Package for new design"));
    m_creationPackageSelector->setAccessibleDescription(QStringLiteral(
        "Sets the initial Package in the New NoC Design dialog. It does not "
        "change the Package bound to the open design."));
    m_creationPackageSelector->setMinimumContentsLength(6);
    m_creationPackageSelector->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_creationPackageSelector->setMinimumWidth(0);
    m_creationPackageSelector->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Fixed);
    creationPackageLabel->setBuddy(m_creationPackageSelector);
    packageLayout->addWidget(creationPackageLabel);
    packageLayout->addWidget(m_creationPackageSelector);

    m_creationPackageDetails = new QLabel(m_packageGroup);
    m_creationPackageDetails->setObjectName(
        QStringLiteral("finepaper.creationPackageDetails"));
    m_creationPackageDetails->setTextFormat(Qt::PlainText);
    m_creationPackageDetails->setWordWrap(true);
    m_creationPackageDetails->setMinimumWidth(0);
    m_creationPackageDetails->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_creationPackageDetails->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    m_creationPackageDetails->setFocusPolicy(Qt::NoFocus);
    m_creationPackageDetails->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    m_creationPackageDetails->setAccessibleName(
        QStringLiteral("Selected Package capabilities"));
    packageLayout->addWidget(m_creationPackageDetails);
    contentLayout->addWidget(m_packageGroup);

    m_currentDesignGroup = new QGroupBox(
        QStringLiteral("Current Design"), content);
    m_currentDesignGroup->setObjectName(
        QStringLiteral("finepaper.currentDesignSection"));
    m_currentDesignGroup->setSizePolicy(
        QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* currentDesignLayout = new QVBoxLayout(m_currentDesignGroup);
    currentDesignLayout->setSpacing(ui::UiMetrics::spacing8);
    m_activePackage = new QLabel(m_currentDesignGroup);
    m_activePackage->setObjectName(
        QStringLiteral("finepaper.activePackage"));
    m_activePackage->setWordWrap(true);
    m_activePackage->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_activePackage->setTextFormat(Qt::PlainText);
    m_activePackage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_activePackage->setProperty(
        "finepaperRole", QStringLiteral("subtitle"));
    currentDesignLayout->addWidget(m_activePackage);

    m_activePackageAvailability = new QLabel(m_currentDesignGroup);
    m_activePackageAvailability->setObjectName(
        QStringLiteral("finepaper.activePackageAvailability"));
    m_activePackageAvailability->setTextFormat(Qt::PlainText);
    m_activePackageAvailability->setWordWrap(true);
    m_activePackageAvailability->setMinimumWidth(0);
    m_activePackageAvailability->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_activePackageAvailability->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    m_activePackageAvailability->setFocusPolicy(Qt::NoFocus);
    m_activePackageAvailability->setProperty(
        "finepaperRole", QStringLiteral("warning"));
    currentDesignLayout->addWidget(m_activePackageAvailability);
    contentLayout->addWidget(m_currentDesignGroup);

    m_endpointGroup = new QGroupBox(
        QStringLiteral("Endpoint Types"), content);
    m_endpointGroup->setObjectName(
        QStringLiteral("finepaper.endpointLibrarySection"));
    auto* endpointLayout = new QVBoxLayout(m_endpointGroup);
    endpointLayout->setSpacing(ui::UiMetrics::spacing8);
    auto* paletteHelp = new QLabel(
        QStringLiteral(
            "Drag a type onto the canvas, or select a Router and press Enter."),
        m_endpointGroup);
    paletteHelp->setObjectName(
        QStringLiteral("finepaper.endpointPaletteHelp"));
    paletteHelp->setWordWrap(true);
    paletteHelp->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    paletteHelp->setTextFormat(Qt::PlainText);
    paletteHelp->setProperty("finepaperRole", QStringLiteral("muted"));
    endpointLayout->addWidget(paletteHelp);

    auto* filterLabel = new QLabel(
        QStringLiteral("Filter Endpoint types"), m_endpointGroup);
    m_endpointFilter = new QLineEdit(m_endpointGroup);
    m_endpointFilter->setObjectName(
        QStringLiteral("finepaper.endpointPaletteFilter"));
    m_endpointFilter->setPlaceholderText(
        QStringLiteral("Type a name or ID…"));
    m_endpointFilter->setClearButtonEnabled(true);
    m_endpointFilter->setAccessibleName(
        QStringLiteral("Filter Endpoint types"));
    filterLabel->setBuddy(m_endpointFilter);
    endpointLayout->addWidget(filterLabel);
    endpointLayout->addWidget(m_endpointFilter);

    m_endpointPalette = new EndpointPaletteList(m_endpointGroup);
    m_endpointPalette->setObjectName(
        QStringLiteral("finepaper.endpointPalette"));
    m_endpointPalette->setAccessibleName(QStringLiteral("Endpoint types"));
    m_endpointPalette->setAccessibleDescription(QStringLiteral(
        "Drag a type to the canvas, or press Enter to add it to the "
        "selected Router."));
    m_endpointPalette->setDragEnabled(true);
    m_endpointPalette->setDragDropMode(QAbstractItemView::DragOnly);
    m_endpointPalette->setDefaultDropAction(Qt::CopyAction);
    m_endpointPalette->setSelectionMode(
        QAbstractItemView::SingleSelection);
    m_endpointPalette->setAlternatingRowColors(true);
    m_endpointPalette->setMinimumHeight(120);
    endpointLayout->addWidget(m_endpointPalette, 1);

    m_endpointHint = new QLabel(m_endpointGroup);
    m_endpointHint->setObjectName(
        QStringLiteral("finepaper.endpointPaletteHint"));
    m_endpointHint->setWordWrap(true);
    m_endpointHint->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_endpointHint->setTextFormat(Qt::PlainText);
    m_endpointHint->setProperty(
        "finepaperRole", QStringLiteral("muted"));
    m_endpointHint->setAccessibleName(
        QStringLiteral("Endpoint quick-add status"));
    endpointLayout->addWidget(m_endpointHint);

    m_addEndpoint = new QPushButton(
        QStringLiteral("Add to selected Router"), m_endpointGroup);
    m_addEndpoint->setObjectName(
        QStringLiteral("finepaper.addEndpointToRouter"));
    m_addEndpoint->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    endpointLayout->addWidget(m_addEndpoint);
    contentLayout->addWidget(m_endpointGroup, 1);

    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);

    auto* actionFooter = new QWidget(this);
    actionFooter->setObjectName(
        QStringLiteral("finepaper.packageMaintenanceActions"));
    actionFooter->setProperty("finepaperRole", QStringLiteral("card"));
    auto* actionLayout = new QVBoxLayout(actionFooter);
    actionLayout->setContentsMargins(
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing8,
        ui::UiMetrics::spacing12,
        ui::UiMetrics::spacing12);
    actionLayout->setSpacing(ui::UiMetrics::spacing8);

    m_createDesign = new QPushButton(
        QStringLiteral("Create Design…"), actionFooter);
    m_createDesign->setObjectName(
        QStringLiteral("finepaper.createDesign"));
    m_createDesign->setAccessibleName(QStringLiteral("Create Design"));
    m_createDesign->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    actionLayout->addWidget(m_createDesign);

    auto* maintenanceLayout = new ui::ResponsiveActionLayout;
    maintenanceLayout->setSpacing(ui::UiMetrics::spacing8);
    m_installPackage = new QPushButton(
        QStringLiteral("Install…"), actionFooter);
    m_installPackage->setObjectName(
        QStringLiteral("finepaper.installPackage"));
    m_installPackage->setAccessibleName(
        QStringLiteral("Install Package"));
    m_reloadPackages = new QPushButton(
        QStringLiteral("Reload"), actionFooter);
    m_reloadPackages->setObjectName(
        QStringLiteral("finepaper.reloadPackages"));
    m_reloadPackages->setAccessibleName(
        QStringLiteral("Reload Packages"));
    m_reloadPackages->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    maintenanceLayout->addWidget(m_installPackage);
    maintenanceLayout->addWidget(m_reloadPackages);
    actionLayout->addLayout(maintenanceLayout);
    root->addWidget(actionFooter);

    connect(m_creationPackageSelector, &QComboBox::currentIndexChanged,
            this, [this] {
                updateCreationPackagePresentation();
                emit creationPackageChanged(
                    selectedCreationPackageKey());
            });
    connect(m_createDesign, &QPushButton::clicked,
            this, [this] {
                emit createDesignRequested(
                    selectedCreationPackageKey());
            });
    connect(m_installPackage, &QPushButton::clicked,
            this, &PackageLibraryPanel::installPackageRequested);
    connect(m_reloadPackages, &QPushButton::clicked,
            this, &PackageLibraryPanel::reloadPackagesRequested);
    connect(m_endpointFilter, &QLineEdit::textChanged,
            this, [this] { applyEndpointFilter(); });
    connect(m_endpointPalette, &QListWidget::itemSelectionChanged,
            this, &PackageLibraryPanel::updateEndpointPresentation);
    connect(m_endpointPalette, &QListWidget::itemActivated,
            this, [this](QListWidgetItem* item) {
                if (item && !item->isHidden()) {
                    emit endpointAddRequested(
                        item->data(Qt::UserRole).toString());
                }
            });
    connect(m_addEndpoint, &QPushButton::clicked,
            this, [this] {
                if (QListWidgetItem* item = selectedVisibleEndpoint()) {
                    emit endpointAddRequested(
                        item->data(Qt::UserRole).toString());
                }
            });

    QWidget::setTabOrder(m_creationPackageSelector, m_endpointFilter);
    QWidget::setTabOrder(m_endpointFilter, m_endpointPalette);
    QWidget::setTabOrder(m_endpointPalette, m_addEndpoint);
    QWidget::setTabOrder(m_addEndpoint, m_createDesign);
    QWidget::setTabOrder(m_createDesign, m_installPackage);
    QWidget::setTabOrder(m_installPackage, m_reloadPackages);

    setState({});
}

void PackageLibraryPanel::setState(PackageLibraryViewState state) {
    const QString selectedKey = selectedCreationPackageKey();
    const QString selectedEndpointId = selectedVisibleEndpoint()
        ? selectedVisibleEndpoint()->data(Qt::UserRole).toString()
        : QString();
    const bool packagesChanged = !m_hasState
        || m_state.runnablePackages != state.runnablePackages;
    const bool endpointTypesChanged = !m_hasState
        || m_state.endpoints.types != state.endpoints.types;

    m_state = std::move(state);
    m_hasState = true;

    if (packagesChanged) {
        const bool selectedStillExists = std::any_of(
            m_state.runnablePackages.cbegin(),
            m_state.runnablePackages.cend(),
            [&](const CreationPackageItem& package) {
                return package.key() == selectedKey;
            });
        const QString preferred = selectedStillExists
            ? selectedKey : m_state.fallbackCreationPackageKey;
        rebuildCreationPackages(preferred);
    } else if (selectedCreationPackageKey().isEmpty()
               && !m_state.fallbackCreationPackageKey.isEmpty()) {
        selectCreationPackage(m_state.fallbackCreationPackageKey);
    }

    if (endpointTypesChanged) {
        rebuildEndpointTypes();
        if (!selectedEndpointId.isEmpty()) {
            for (int row = 0; row < m_endpointPalette->count(); ++row) {
                QListWidgetItem* item = m_endpointPalette->item(row);
                if (item->data(Qt::UserRole).toString()
                    == selectedEndpointId) {
                    m_endpointPalette->setCurrentItem(item);
                    break;
                }
            }
        }
    }

    updateActivePackagePresentation();
    updateCreationPackagePresentation();
    applyEndpointFilter();
    updateInterlocks();
}

QString PackageLibraryPanel::selectedCreationPackageKey() const {
    return m_creationPackageSelector
        ? m_creationPackageSelector->currentData().toString()
        : QString();
}

bool PackageLibraryPanel::selectCreationPackage(const QString& key) {
    if (!m_creationPackageSelector) {
        return false;
    }
    const int index = m_creationPackageSelector->findData(key);
    if (index < 0) {
        return false;
    }
    m_creationPackageSelector->setCurrentIndex(index);
    reveal(m_creationPackageSelector);
    return true;
}

QWidget* PackageLibraryPanel::preferredFocusTarget() {
    QWidget* target = taskFocusTarget();
    reveal(target);
    return target;
}

void PackageLibraryPanel::rebuildCreationPackages(
    const QString& preferredKey) {
    const QSignalBlocker blocker(m_creationPackageSelector);
    m_creationPackageSelector->clear();
    for (const CreationPackageItem& package : m_state.runnablePackages) {
        m_creationPackageSelector->addItem(
            packageDisplayText(package), package.key());
        const int index = m_creationPackageSelector->count() - 1;
        const QString completeText = QStringLiteral("%1 — %2")
            .arg(packageDisplayText(package), package.key());
        m_creationPackageSelector->setItemData(
            index, completeText, Qt::ToolTipRole);
        m_creationPackageSelector->setItemData(
            index, completeText, Qt::AccessibleTextRole);
    }
    if (m_creationPackageSelector->count() == 0) {
        m_creationPackageSelector->addItem(
            QStringLiteral("No runnable Package"), QString());
        return;
    }
    const int preferredIndex =
        m_creationPackageSelector->findData(preferredKey);
    m_creationPackageSelector->setCurrentIndex(
        preferredIndex >= 0 ? preferredIndex : 0);
}

void PackageLibraryPanel::rebuildEndpointTypes() {
    const QSignalBlocker blocker(m_endpointPalette);
    m_endpointPalette->clear();
    for (const EndpointLibraryItem& type : m_state.endpoints.types) {
        auto* item = new QListWidgetItem(
            type.label.trimmed().isEmpty() ? type.id : type.label,
            m_endpointPalette);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setData(Qt::UserRole, type.id);
        item->setToolTip(type.description);
        item->setData(
            Qt::AccessibleTextRole,
            QStringLiteral("%1, %2")
                .arg(item->text())
                .arg(type.id));
    }
}

void PackageLibraryPanel::applyEndpointFilter() {
    const QString filter = m_endpointFilter->text().trimmed();
    for (int row = 0; row < m_endpointPalette->count(); ++row) {
        QListWidgetItem* item = m_endpointPalette->item(row);
        const bool matches = filter.isEmpty()
            || item->text().contains(filter, Qt::CaseInsensitive)
            || item->data(Qt::UserRole).toString().contains(
                filter, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
    updateEndpointPresentation();
}

void PackageLibraryPanel::updateActivePackagePresentation() {
    const ActivePackageViewState& active = m_state.activePackage;
    m_currentDesignGroup->setVisible(
        active.availability != ActivePackageAvailability::NoDesign);
    m_activePackageAvailability->clear();
    m_activePackageAvailability->hide();

    switch (active.availability) {
    case ActivePackageAvailability::NoDesign:
        m_activePackage->setText(QStringLiteral("No design is open."));
        m_activePackage->setToolTip({});
        break;
    case ActivePackageAvailability::RuntimeReady:
        m_activePackage->setText(
            QStringLiteral("%1 — %2")
                .arg(active.name, active.reference.key()));
        m_activePackage->setToolTip(active.metadataRoot);
        break;
    case ActivePackageAvailability::MetadataOnly: {
        m_activePackage->setText(
            QStringLiteral("%1 — %2 (runtime unavailable)")
                .arg(active.name, active.reference.key()));
        const QString recovery = QStringLiteral(
            "Runtime unavailable. Reload or reinstall this exact Package "
            "before Validate or Generate RTL.");
        m_activePackageAvailability->setText(recovery);
        m_activePackageAvailability->setAccessibleDescription(recovery);
        m_activePackageAvailability->show();
        m_activePackage->setToolTip(QStringLiteral(
            "Retained metadata from %1 keeps editing available.")
                .arg(active.metadataRoot));
        break;
    }
    case ActivePackageAvailability::Missing: {
        m_activePackage->setText(
            QStringLiteral("Package not loaded: %1 (design is read-only)")
                .arg(active.reference.key()));
        const QString recovery = QStringLiteral(
            "Install this exact Package ID and version to restore editing.");
        m_activePackageAvailability->setText(recovery);
        m_activePackageAvailability->setAccessibleDescription(recovery);
        m_activePackageAvailability->show();
        m_activePackage->setToolTip(recovery);
        break;
    }
    }
}

void PackageLibraryPanel::updateCreationPackagePresentation() {
    QStringList availablePackages;
    availablePackages.reserve(m_state.runnablePackages.size());
    for (const CreationPackageItem& package : m_state.runnablePackages) {
        availablePackages.append(
            QStringLiteral("%1 — %2")
                .arg(packageDisplayText(package), package.key()));
    }
    if (availablePackages.isEmpty()) {
        m_availablePackages->setText(
            QStringLiteral("No runnable NoC IP Package is available."));
        m_availablePackages->setToolTip(QStringLiteral(
            "Use Install to add or repair a runtime NoC IP Package."));
    } else {
        m_availablePackages->setText(
            availablePackages.size() == 1
                ? QStringLiteral("1 available.")
                : QStringLiteral("%1 available.")
                      .arg(availablePackages.size()));
        m_availablePackages->setAccessibleDescription(
            availablePackages.size() == 1
                ? QStringLiteral("1 runnable NoC IP Package is available.")
                : QStringLiteral("%1 runnable NoC IP Packages are available.")
                      .arg(availablePackages.size()));
        m_availablePackages->setToolTip(
            availablePackages.join(QLatin1Char('\n')));
    }

    const QString selectedKey = selectedCreationPackageKey();
    const auto selected = std::find_if(
        m_state.runnablePackages.cbegin(),
        m_state.runnablePackages.cend(),
        [&](const CreationPackageItem& package) {
            return package.key() == selectedKey;
        });
    if (selected == m_state.runnablePackages.cend()) {
        const QString unavailable = QStringLiteral(
            "Install or repair a runnable Package before creating a design.");
        m_creationPackageDetails->setText(unavailable);
        m_creationPackageDetails->setAccessibleDescription(unavailable);
        m_creationPackageSelector->setToolTip(unavailable);
        return;
    }

    m_creationPackageDetails->setText(selected->capabilitySummary);
    m_creationPackageDetails->setAccessibleDescription(
        QStringLiteral("Selected Package: %1. %2")
            .arg(selected->name)
            .arg(selected->capabilitySummary));
    m_creationPackageSelector->setToolTip(QStringLiteral(
        "%1\n%2\nSets the initial choice for New NoC Design. The open "
        "design keeps its own bound Package.")
            .arg(selected->name, selected->capabilitySummary));
}

void PackageLibraryPanel::updateEndpointPresentation() {
    const EndpointLibraryViewState& endpoints = m_state.endpoints;
    const bool designOpen =
        endpoints.availability != EndpointLibraryAvailability::NoDesign;
    m_endpointGroup->setVisible(designOpen);

    const bool ready =
        endpoints.availability == EndpointLibraryAvailability::Ready;
    const bool interactive = ready && !m_state.interlocks.operationBusy;
    m_endpointFilter->setEnabled(interactive);
    m_endpointPalette->setEnabled(interactive);

    QListWidgetItem* item = selectedVisibleEndpoint();
    const bool canAdd = interactive && endpoints.selectedRouterId && item
        && endpoints.attachmentRejection.isEmpty();
    m_addEndpoint->setEnabled(canAdd);

    QString hint;
    switch (endpoints.availability) {
    case EndpointLibraryAvailability::NoDesign:
        hint = QStringLiteral("Create or open a design to add Endpoints.");
        break;
    case EndpointLibraryAvailability::PackageMissing:
        hint = QStringLiteral(
            "Endpoint editing is unavailable until the design Package is loaded.");
        break;
    case EndpointLibraryAvailability::NoTypes:
        hint = QStringLiteral(
            "The current Package does not declare any Endpoint types.");
        break;
    case EndpointLibraryAvailability::Ready: {
        bool hasVisibleType = false;
        for (int row = 0; row < m_endpointPalette->count(); ++row) {
            if (!m_endpointPalette->item(row)->isHidden()) {
                hasVisibleType = true;
                break;
            }
        }
        if (m_state.interlocks.operationBusy) {
            hint = QStringLiteral(
                "Wait for the current operation to finish.");
        } else if (!m_endpointFilter->text().trimmed().isEmpty()
                   && !hasVisibleType) {
            hint = QStringLiteral(
                "No Endpoint types match \u201c%1\u201d. Clear the filter to see all types.")
                .arg(m_endpointFilter->text().trimmed());
        } else if (!endpoints.selectedRouterId) {
            hint = QStringLiteral(
                "Select a Router to enable keyboard quick-add.");
        } else if (!item) {
            hint = QStringLiteral("Choose an Endpoint type for %1.")
                .arg(*endpoints.selectedRouterId);
        } else if (!endpoints.attachmentRejection.isEmpty()) {
            hint = endpoints.attachmentRejection;
        } else {
            hint = QStringLiteral("Ready to add %1 to %2.")
                .arg(item->text(), *endpoints.selectedRouterId);
        }
        break;
    }
    }
    m_endpointHint->setText(hint);
    m_endpointHint->setAccessibleDescription(hint);
}

void PackageLibraryPanel::updateInterlocks() {
    const bool hasRunnablePackages =
        !m_state.runnablePackages.isEmpty();
    const bool hasDesign = m_state.activePackage.availability
        != ActivePackageAvailability::NoDesign;
    // Keep one reliable creation entry in the pinned footer. The canvas
    // Empty State may be vertically clipped by a small window or a large
    // system font, while this action remains independent of scroll position.
    m_createDesign->show();
    m_createDesign->setText(
        hasDesign ? QStringLiteral("Create Another Design…")
                  : QStringLiteral("Create Design…"));
    m_creationPackageSelector->setEnabled(
        hasRunnablePackages && !m_state.interlocks.operationBusy);
    m_createDesign->setEnabled(
        hasRunnablePackages && !m_state.interlocks.operationBusy);
    m_createDesign->setToolTip(
        hasRunnablePackages
            ? QStringLiteral("Choose a NoC IP and create a new design.")
            : QStringLiteral(
                  "Install or repair a runnable NoC IP Package before creating a design."));

    const bool packageActionsBlocked =
        m_state.interlocks.operationBusy
        || m_state.interlocks.cleanupUnresolved
        || m_state.interlocks.endpointDraftsUnresolved;
    m_installPackage->setEnabled(!packageActionsBlocked);
    m_reloadPackages->setEnabled(!packageActionsBlocked);
    const QString blockedReason = m_state.interlocks.cleanupUnresolved
        ? m_state.interlocks.cleanupBlockedReason
        : m_state.interlocks.endpointDraftsUnresolved
        ? m_state.interlocks.endpointDraftBlockedReason
        : QString();
    m_installPackage->setToolTip(blockedReason);
    m_reloadPackages->setToolTip(blockedReason);
}

QListWidgetItem* PackageLibraryPanel::selectedVisibleEndpoint() {
    QListWidgetItem* item = m_endpointPalette
        ? m_endpointPalette->currentItem() : nullptr;
    return item && !item->isHidden() ? item : nullptr;
}

QWidget* PackageLibraryPanel::taskFocusTarget() {
    const EndpointLibraryAvailability endpointAvailability =
        m_state.endpoints.availability;
    if ((endpointAvailability == EndpointLibraryAvailability::Ready
         || endpointAvailability == EndpointLibraryAvailability::NoTypes)
        && m_endpointFilter->isEnabled()) {
        return m_endpointFilter;
    }
    if (endpointAvailability
            == EndpointLibraryAvailability::PackageMissing
        && m_installPackage->isEnabled()) {
        return m_installPackage;
    }
    if (m_creationPackageSelector->isEnabled()) {
        return m_creationPackageSelector;
    }
    return m_installPackage->isEnabled()
        ? static_cast<QWidget*>(m_installPackage)
        : static_cast<QWidget*>(m_scroll);
}

void PackageLibraryPanel::reveal(QWidget* target) {
    if (!target || !m_scroll || target == m_scroll
        || !m_scroll->widget()
        || (target != m_scroll->widget()
            && !m_scroll->widget()->isAncestorOf(target))) {
        return;
    }
    m_scroll->ensureWidgetVisible(
        target, ui::UiMetrics::spacing12, ui::UiMetrics::spacing12);
}

} // namespace finepaper
