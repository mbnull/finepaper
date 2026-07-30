#pragma once

#include "features/domain/domain_configuration_dialog.h"

#include <QWidget>

#include <functional>

class QLabel;
class QPlainTextEdit;
class QVBoxLayout;

namespace finepaper {

// First-class center Workspace which owns one long-lived instance of the
// existing five-page editor for the current design session. The modal dialog
// remains available only for flows that must complete Domain data before a
// design can exist.
class DomainConfigurationWorkspace final : public QWidget {
public:
    explicit DomainConfigurationWorkspace(QWidget* parent = nullptr);
    ~DomainConfigurationWorkspace() override;

    void setContext(const NocDesign* design,
                    const PackageDefinition* package,
                    const QString& designIdentity,
                    DomainConfigurationValidator validator);
    void setBusy(bool busy);
    [[nodiscard]] bool hasPendingChanges() const;
    void discardPendingChanges();

    std::function<bool(const DesignResult&)> applyRequested;
    std::function<void(bool)> draftStateChanged;

private:
    void clearEditor(const QString& message);
    void showReadOnlySnapshot(const NocDesign& design, const QString& message);
    void updateRuntimeCapabilitySummary(const PackageDefinition* package);
    void createEditor(NocDesign design,
                      PackageDefinition package,
                      DomainConfigurationValidator validator);

    QString m_designIdentity;
    bool m_busy = false;
    QLabel* m_status = nullptr;
    QLabel* m_runtimeCapabilities = nullptr;
    QPlainTextEdit* m_readOnlySnapshot = nullptr;
    QVBoxLayout* m_layout = nullptr;
    DomainConfigurationDialog* m_editor = nullptr;
};

} // namespace finepaper
