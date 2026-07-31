#pragma once

#include "application/application.h"
#include "application/design_extension_references.h"
#include "package/package.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QSplitter;

namespace finepaper {

class DesignExtensionsWorkspace final : public QWidget {
public:
    explicit DesignExtensionsWorkspace(QWidget* parent = nullptr);

    void setContext(const NocDesign* design,
                    const PackageDefinition* package);
    void setBusy(bool busy);

    std::function<DesignResult(QString, QJsonValue)> applyRequested;
    std::function<DesignResult(QString)> removeRequested;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Entry {
        enum class StatusKind {
            Normal,
            Configured,
            ReadOnly,
            Warning,
        };

        QString id;
        QString title;
        QString description;
        QString status;
        QString statusDetails;
        QJsonValue value;
        std::optional<DesignExtensionDefinition> definition = std::nullopt;
        bool configured = false;
        bool editable = false;
        bool removable = false;
        StatusKind statusKind = StatusKind::Normal;
    };

    void rebuildEntries();
    void rebuildList();
    void updateDetails();
    void openSelected();
    void removeSelected();
    [[nodiscard]] QString selectedExtensionId() const;
    [[nodiscard]] int selectedEntryIndex() const;
    [[nodiscard]] bool packageDeclaredValueValid(
        const DesignExtensionDefinition& definition,
        const QJsonValue& value);
    [[nodiscard]] const DesignDomainReferenceIndex& domainReferenceIndex();
    [[nodiscard]] static QJsonValue initialValue(
        const DesignExtensionDefinition& definition);

    QVector<DesignExtensionDefinition> m_definitions;

    struct ValidationCacheEntry {
        std::shared_ptr<const json_schema::CompiledSchema> schema;
        QVector<DesignExtensionDomainReferenceDefinition> domainReferences;
        QJsonValue value;
        bool valid = false;
    };

    QHash<QString, ValidationCacheEntry> m_validationCache;
    QHash<QString, QString> m_domainTypeLabels;
    QString m_validationCachePackage;
    QVector<DomainDefinition> m_validationCacheDomains;
    std::optional<DesignDomainReferenceIndex> m_domainReferenceIndex =
        std::nullopt;
    QJsonObject m_packageData;
    QVector<DomainDefinition> m_designDomains;
    QVector<Entry> m_entries;
    QString m_requiredPackage;
    bool m_hasDesign = false;
    bool m_hasPackage = false;
    bool m_busy = false;

    QLabel* m_workspaceState = nullptr;
    QLineEdit* m_filter = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_namespace = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_statusDetails = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QSplitter* m_splitter = nullptr;
};

} // namespace finepaper
