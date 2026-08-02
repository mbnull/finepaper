#pragma once

#include "noc/model.h"

#include <QVector>
#include <QWidget>

#include <optional>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;

namespace finepaper {

enum class ActivePackageAvailability {
    NoDesign,
    RuntimeReady,
    MetadataOnly,
    Missing,
};

enum class EndpointLibraryAvailability {
    NoDesign,
    PackageMissing,
    NoTypes,
    Ready,
};

struct ActivePackageViewState final {
    ActivePackageAvailability availability =
        ActivePackageAvailability::NoDesign;
    PackageReference reference;
    QString name;
    QString metadataRoot;

    bool operator==(const ActivePackageViewState&) const = default;
};

// A deliberately small projection. The panel never borrows a catalog Package
// and therefore remains valid while MainWindow atomically replaces a catalog.
struct CreationPackageItem final {
    PackageReference reference;
    QString name;
    QString capabilitySummary;

    [[nodiscard]] QString key() const { return reference.key(); }
    bool operator==(const CreationPackageItem&) const = default;
};

struct EndpointLibraryItem final {
    QString id;
    QString label;
    QString description;

    bool operator==(const EndpointLibraryItem&) const = default;
};

struct EndpointLibraryViewState final {
    EndpointLibraryAvailability availability =
        EndpointLibraryAvailability::NoDesign;
    QVector<EndpointLibraryItem> types;
    std::optional<QString> selectedRouterId = std::nullopt;
    QString attachmentRejection;

    bool operator==(const EndpointLibraryViewState&) const = default;
};

struct PackageLibraryInterlocks final {
    bool operationBusy = false;
    bool cleanupUnresolved = false;
    bool endpointDraftsUnresolved = false;
    QString cleanupBlockedReason;
    QString endpointDraftBlockedReason;

    bool operator==(const PackageLibraryInterlocks&) const = default;
};

struct PackageLibraryViewState final {
    ActivePackageViewState activePackage;
    QVector<CreationPackageItem> runnablePackages;
    QString fallbackCreationPackageKey;
    EndpointLibraryViewState endpoints;
    PackageLibraryInterlocks interlocks;

    bool operator==(const PackageLibraryViewState&) const = default;
};

class PackageLibraryPanel final : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PackageLibraryPanel)

public:
    explicit PackageLibraryPanel(QWidget* parent = nullptr);

    void setState(PackageLibraryViewState state);
    [[nodiscard]] QString selectedCreationPackageKey() const;
    bool selectCreationPackage(const QString& key);
    [[nodiscard]] QWidget* preferredFocusTarget();

signals:
    void installPackageRequested();
    void reloadPackagesRequested();
    void createDesignRequested(const QString& preferredPackageKey);
    void creationPackageChanged(const QString& key);
    void endpointAddRequested(const QString& endpointTypeId);

private:
    void rebuildCreationPackages(const QString& preferredKey);
    void rebuildEndpointTypes();
    void applyEndpointFilter();
    void updateActivePackagePresentation();
    void updateCreationPackagePresentation();
    void updateEndpointPresentation();
    void updateInterlocks();
    [[nodiscard]] QListWidgetItem* selectedVisibleEndpoint();
    [[nodiscard]] QWidget* taskFocusTarget();
    void reveal(QWidget* target);

    PackageLibraryViewState m_state;
    bool m_hasState = false;

    QScrollArea* m_scroll = nullptr;
    QGroupBox* m_packageGroup = nullptr;
    QLabel* m_availablePackages = nullptr;
    QComboBox* m_creationPackageSelector = nullptr;
    QLabel* m_creationPackageDetails = nullptr;
    QGroupBox* m_currentDesignGroup = nullptr;
    QLabel* m_activePackage = nullptr;
    QLabel* m_activePackageAvailability = nullptr;
    QGroupBox* m_endpointGroup = nullptr;
    QLineEdit* m_endpointFilter = nullptr;
    QListWidget* m_endpointPalette = nullptr;
    QLabel* m_endpointHint = nullptr;
    QPushButton* m_addEndpoint = nullptr;
    QPushButton* m_createDesign = nullptr;
    QPushButton* m_installPackage = nullptr;
    QPushButton* m_reloadPackages = nullptr;
};

} // namespace finepaper
