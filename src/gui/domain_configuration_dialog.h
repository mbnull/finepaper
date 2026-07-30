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
                              QWidget* parent = nullptr);
    ~DomainConfigurationDialog() override;

    [[nodiscard]] DomainConfiguration configuration() const;
    [[nodiscard]] const DesignResult& validatedResult() const {
        return m_validatedResult;
    }
    void reject() override;

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
    [[nodiscard]] bool confirmDiscardDraft(const QString& action);

    NocDesign m_baseDesign;
    PackageDefinition m_package;
    DomainConfiguration m_initialConfiguration;
    DomainConfigurationValidator m_validator;
    DesignResult m_validatedResult;
    bool m_updating = false;
    bool m_discardConfirmed = false;

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
