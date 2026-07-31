#pragma once

#include "application/application.h"
#include "application/design_extension_references.h"
#include "package/package.h"

#include <QDialog>
#include <QJsonValue>

#include <functional>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace finepaper {

struct DesignExtensionEditorContext {
    QString id;
    QString title;
    QString description;
    QString editorMessage;
    QJsonValue value;
    std::optional<DesignExtensionDefinition> definition = std::nullopt;
    std::optional<DesignDomainReferenceIndex> domainReferenceIndex =
        std::nullopt;
    QString domainReferenceSummary;
    bool configured = false;
    bool editable = false;
};

// A transactional, schema-aware JSON source editor. The dialog owns the draft:
// closing or rejecting it never changes the design, and a successful callback
// is required before the dialog accepts.
class DesignExtensionEditorDialog final : public QDialog {
public:
    explicit DesignExtensionEditorDialog(
        DesignExtensionEditorContext context,
        QWidget* parent = nullptr);

    std::function<DesignResult(QString, QJsonValue)> applyRequested;

protected:
    void reject() override;

private:
    [[nodiscard]] bool hasUnappliedDraft() const;
    void validateDraft();
    void applyDraft();
    void formatDraft();
    void loadPackageDefault();
    void showApplicationDiagnostics(const QVector<Diagnostic>& diagnostics);
    void setDiagnosticsText(const QString& text);
    void updateApplyState();

    DesignExtensionEditorContext m_context;
    std::optional<DesignDomainReferenceIndex> m_domainReferenceIndex =
        std::nullopt;
    QPlainTextEdit* m_editor = nullptr;
    QLabel* m_validationState = nullptr;
    QLabel* m_diagnosticsHeading = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
    QPushButton* m_formatButton = nullptr;
    QPushButton* m_defaultButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QTimer* m_validationTimer = nullptr;
    QString m_initialSource;
    QJsonValue m_parsedValue;
    bool m_syntaxValid = false;
    bool m_schemaValid = false;
    bool m_domainReferencesValid = false;
    bool m_applyInProgress = false;
    bool m_valueTooLarge = false;
    bool m_draftTouched = false;
};

} // namespace finepaper
