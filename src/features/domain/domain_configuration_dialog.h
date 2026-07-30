#pragma once

#include "application/application.h"
#include "application/domain_configuration.h"
#include "package/package.h"

#include <QDialog>

#include <functional>
#include <optional>

class QDialogButtonBox;
class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTimer;

namespace finepaper {

inline constexpr int domainConfigurationRowTokenRole = Qt::UserRole + 60;

using DomainConfigurationValidator =
    std::function<DesignResult(const DomainConfiguration&)>;

enum class DomainConfigurationPresentation {
    ModalDialog,
    EmbeddedWorkspace,
};

// Edits all five persisted Domain arrays as one working copy. Individual rows
// are intentionally allowed to make the draft temporarily invalid; only the
// final Apply crosses the Application boundary and therefore required/cyclic
// configurations can be assembled without exposing partial Design state.
class DomainConfigurationDialog final : public QDialog {
public:
    DomainConfigurationDialog(NocDesign baseDesign,
                              PackageDefinition package,
                              DomainConfiguration configuration,
                              DomainConfigurationValidator validator,
                              QWidget* parent = nullptr,
                              DomainConfigurationPresentation presentation =
                                  DomainConfigurationPresentation::ModalDialog);
    ~DomainConfigurationDialog() override;

    [[nodiscard]] DomainConfiguration configuration() const;
    [[nodiscard]] bool hasPendingChanges() const;
    [[nodiscard]] const DesignResult& validatedResult() const {
        return m_validatedResult;
    }
    void synchronizeContext(NocDesign baseDesign,
                            PackageDefinition package,
                            DomainConfigurationValidator validator);
    void discardDraft();
    void setBusy(bool busy);
    void reject() override;

    // Embedded workspaces keep the editor alive after an atomic Apply. Return
    // true only after the authoritative DesignResult has been adopted.
    std::function<bool(const DesignResult&)> workspaceApplyRequested;
    std::function<void(bool)> draftStateChanged;

protected:
    void accept() override;
    void closeEvent(QCloseEvent* event) override;

private:
    class Impl;

    void rebuildAll();
    void wireActions();
    void scheduleValidation();
    void updateValidation(bool authoritative = true);
    void revertDraft();
    void notifyDraftStateChanged();
    [[nodiscard]] bool confirmDiscardDraft(const QString& action);

    NocDesign m_baseDesign;
    PackageDefinition m_package;
    DomainConfiguration m_initialConfiguration;
    std::optional<DomainConfiguration> m_pendingAuthoritativeConfiguration;
    DomainConfigurationValidator m_validator;
    DesignResult m_validatedResult;
    DomainConfigurationPresentation m_presentation =
        DomainConfigurationPresentation::ModalDialog;
    bool m_updating = false;
    bool m_discardConfirmed = false;
    bool m_busy = false;

    Impl* m_impl = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_domains = nullptr;
    QTableWidget* m_memberships = nullptr;
    QTableWidget* m_relations = nullptr;
    QTableWidget* m_policies = nullptr;
    QTableWidget* m_overrides = nullptr;
    QLabel* m_summary = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_revert = nullptr;
    QTimer* m_validationTimer = nullptr;
};

} // namespace finepaper
