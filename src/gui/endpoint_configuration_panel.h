#pragma once

#include "application/endpoint_configuration.h"
#include "application/endpoint_domain_assignment.h"
#include "gui/package_parameter_form.h"
#include "noc/model.h"
#include "package/package.h"

#include <QJsonObject>
#include <QDialog>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QVBoxLayout;

namespace finepaper {

class EndpointDomainAssignmentEditor;
struct EndpointCreationDraft {
    QString id;
    QString type;
    QJsonObject parameters;
    EndpointDomainAssignments domainAssignments;
};

class EndpointCreationDialog final : public QDialog {
public:
    EndpointCreationDialog(
        const NocDesign& design,
        const PackageDefinition& package,
        QString suggestedType,
        QString suggestedId,
        QWidget* parent = nullptr);
    EndpointCreationDialog(
        const NocDesign& design,
        const PackageDefinition& package,
        QString suggestedType,
        QString suggestedId,
        QSet<QString> reservedEndpointIds,
        QWidget* parent = nullptr);

    [[nodiscard]] EndpointCreationDraft draft() const;
    [[nodiscard]] QStringList localErrors() const;

protected:
    void accept() override;

private:
    void rebuildParameters();
    void updateValidation();
    [[nodiscard]] const EndpointTypeDefinition* selectedType() const;

    NocDesign m_design;
    PackageDefinition m_package;
    QSet<QString> m_unavailableEndpointIds;
    EndpointDomainAssignments m_automaticAssignments;
    bool m_updating = false;

    QLineEdit* m_id = nullptr;
    QComboBox* m_type = nullptr;
    PackageParameterForm* m_parameters = nullptr;
    EndpointDomainAssignmentEditor* m_domains = nullptr;
    QLabel* m_domainSummary = nullptr;
    QLabel* m_diagnostics = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_acceptButton = nullptr;
};

// Inspector editor for EndpointInstance parameters.  Endpoint Attachment
// configuration remains intentionally outside this panel and continues to use
// ElementConfigurationPanel when the semantic attachment edge is selected.
class EndpointConfigurationPanel final : public QWidget {
public:
    explicit EndpointConfigurationPanel(QWidget* parent = nullptr);

    void setContext(const NocDesign* design,
                    const PackageDefinition* package,
                    QString designIdentity,
                    std::optional<QString> endpointId,
                    bool busy,
                    quint64 packageCatalogRevision = 0);
    void setBusy(bool busy);
    [[nodiscard]] QWidget* preferredFocusTarget();

    [[nodiscard]] bool hasUnappliedDrafts(
        const QString& designIdentity) const;
    [[nodiscard]] QStringList unappliedDraftEndpointIds(
        const QString& designIdentity) const;
    void discardDraft(const QString& designIdentity,
                      const QString& endpointId);
    void clearDraftsForDesign(const QString& designIdentity);
    void clearDrafts();

    std::function<void()> draftStateChanged;

    std::function<EndpointTypeChangePlan(
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migration,
        QJsonObject parameterPatch)> planTypeChangeRequested;
    std::function<void(QString endpointId, QJsonObject parameters)>
        updateParametersRequested;
    std::function<void(
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migration,
        QJsonObject parameterPatch,
        EndpointTypeChangeImpactConfirmation confirmation)>
        changeTypeRequested;

private:
    struct CachedDraft {
        QString sourceType;
        QJsonObject sourceParameters;
        QString sourceSchemaIdentity;
        QString targetType;
        QString targetSchemaIdentity;
        EndpointParameterMigration migration =
            EndpointParameterMigration::ResetToDefaults;
        QJsonObject desiredParameters;
        PackageParameterDraft editorState;

        bool operator==(const CachedDraft&) const = default;
    };

    void handleTargetSelectionChanged(const QString& selectionName);
    void restoreAcceptedTargetSelection();
    void rebuildTargetParameters();
    void updateValidation();
    void updateTypeChangeSummary(const QJsonObject& desiredParameters);
    void updateStatus();
    void setConflictState(bool conflicted, const QString& details = {});
    void captureCurrentDraft();
    void captureCurrentDraft(
        const PackageParameterEditorSnapshot& snapshot);
    void resetVisibleDraft();
    void notifyDraftStateChanged();
    void apply();
    [[nodiscard]] const EndpointTypeDefinition* selectedType() const;
    [[nodiscard]] EndpointParameterMigration selectedMigration() const;
    [[nodiscard]] QJsonObject typeChangePatch() const;
    [[nodiscard]] QStringList planDiagnostics(
        const EndpointTypeChangePlan& plan) const;

    std::optional<EndpointInstance> m_endpoint;
    std::optional<PackageDefinition> m_package;
    std::optional<EndpointTypeChangePlan> m_baseTypeChangePlan;
    const NocDesign* m_contextDesign = nullptr;
    const PackageDefinition* m_contextPackage = nullptr;
    QString m_contextSchemaIdentity;
    quint64 m_contextCatalogRevision = 0;
    QString m_designIdentity;
    QHash<QString, QString> m_parameterSchemaIdentities;
    QHash<QString, QHash<QString, CachedDraft>> m_drafts;
    bool m_hasContext = false;
    bool m_busy = false;
    bool m_updating = false;
    bool m_restoringDraft = false;
    bool m_conflicted = false;
    bool m_reportedDraftPending = false;
    QString m_activeTargetType;
    EndpointParameterMigration m_activeMigration =
        EndpointParameterMigration::ResetToDefaults;

    QLabel* m_status = nullptr;
    QLabel* m_conflictStatus = nullptr;
    QPushButton* m_discardConflict = nullptr;
    QWidget* m_editor = nullptr;
    QComboBox* m_type = nullptr;
    QLabel* m_migrationLabel = nullptr;
    QComboBox* m_migration = nullptr;
    QLabel* m_typeChangeSummary = nullptr;
    PackageParameterForm* m_parameters = nullptr;
    QLabel* m_diagnostics = nullptr;
    QPushButton* m_apply = nullptr;
};

} // namespace finepaper
