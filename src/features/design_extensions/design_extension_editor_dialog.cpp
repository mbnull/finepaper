#include "features/design_extensions/design_extension_editor_dialog.h"

#include "application/design_extension_references.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

constexpr qsizetype maximumDraftBytes = 8 * 1024 * 1024;

QByteArray serializedJson(const QJsonValue& value) {
    if (value.isObject()) {
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented);
    }
    if (value.isArray()) {
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented);
    }
    if (value.isUndefined()) {
        return {};
    }
    QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(
        QJsonDocument::Compact);
    return wrapped.size() >= 2 ? wrapped.sliced(1, wrapped.size() - 2)
                               : QByteArray{};
}

QString pointerLabel(const QString& pointer) {
    return pointer.isEmpty() ? QStringLiteral("/") : pointer;
}

QString issueText(const json_schema::Issue& issue) {
    QString text = QStringLiteral("%1: %2")
                       .arg(pointerLabel(issue.instancePointer), issue.message);
    if (!issue.schemaPointer.isEmpty()) {
        text += QStringLiteral("\n  Schema: #%1").arg(issue.schemaPointer);
    }
    return text;
}

QString diagnosticsText(const QVector<Diagnostic>& diagnostics) {
    QStringList lines;
    lines.reserve(diagnostics.size());
    for (const Diagnostic& diagnostic : diagnostics) {
        QString line = QStringLiteral("%1: %2")
                           .arg(pointerLabel(diagnostic.path), diagnostic.message);
        if (!diagnostic.code.isEmpty()) {
            line += QStringLiteral(" [%1]").arg(diagnostic.code);
        }
        lines.append(std::move(line));
    }
    return lines.join(QLatin1Char('\n'));
}

QString parseErrorText(const QByteArray& source,
                       qsizetype errorOffset,
                       const QString& errorMessage) {
    const qsizetype offset = std::clamp<qsizetype>(
        errorOffset, 0, source.size());
    const QString prefix = QString::fromUtf8(source.left(offset));
    const qsizetype lastLineBreak = prefix.lastIndexOf(QLatin1Char('\n'));
    const qsizetype line = prefix.count(QLatin1Char('\n')) + 1;
    const qsizetype column = prefix.size()
        - (lastLineBreak < 0 ? 0 : lastLineBreak + 1) + 1;
    return QStringLiteral("Line %1, column %2: %3")
        .arg(line)
        .arg(column)
        .arg(errorMessage);
}

struct ParsedJson {
    bool success = false;
    QJsonValue value;
    QString error;
};

ParsedJson parseJsonValue(const QByteArray& source) {
    QByteArray wrapped;
    wrapped.reserve(source.size() + 2);
    wrapped.append('[');
    wrapped.append(source);
    wrapped.append(']');

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        wrapped, &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        return ParsedJson{
            false,
            {},
            parseErrorText(
                source,
                (std::max)(
                    qsizetype{0},
                    static_cast<qsizetype>(parseError.offset) - 1),
                parseError.errorString())};
    }
    if (!document.isArray() || document.array().size() != 1) {
        return ParsedJson{
            false,
            {},
            QStringLiteral("Line 1, column 1: a single JSON value is required")};
    }
    return ParsedJson{true, document.array().at(0), {}};
}

class BoundedJsonEditor final : public QPlainTextEdit {
public:
    explicit BoundedJsonEditor(qsizetype maximumBytes, QWidget* parent)
        : QPlainTextEdit(parent), m_maximumBytes(maximumBytes) {}

    std::function<void()> oversizedInputRejected;

protected:
    void insertFromMimeData(const QMimeData* source) override {
        if (source && source->hasText()) {
            QString remaining = toPlainText();
            const QTextCursor cursor = textCursor();
            if (cursor.hasSelection()) {
                remaining.remove(cursor.selectionStart(),
                                 cursor.selectionEnd()
                                     - cursor.selectionStart());
            }
            const qsizetype remainingBytes = remaining.toUtf8().size();
            const qsizetype availableBytes = remainingBytes > m_maximumBytes
                ? 0
                : m_maximumBytes - remainingBytes;
            const QString insertedText = source->text();
            const bool definitelyTooLarge = insertedText.size()
                > availableBytes;
            const bool encodedTooLarge = !definitelyTooLarge
                && insertedText.toUtf8().size() > availableBytes;
            if (remainingBytes > m_maximumBytes || definitelyTooLarge
                || encodedTooLarge) {
                if (oversizedInputRejected) {
                    oversizedInputRejected();
                }
                return;
            }
        }
        QPlainTextEdit::insertFromMimeData(source);
    }

private:
    qsizetype m_maximumBytes = 0;
};

} // namespace

DesignExtensionEditorDialog::DesignExtensionEditorDialog(
    DesignExtensionEditorContext context,
    QWidget* parent)
    : QDialog(parent),
      m_context(std::move(context)),
      m_domainReferenceIndex(m_context.domainReferenceIndex) {
    if (m_context.definition
        && !m_context.definition->domainReferences.isEmpty()
        && !m_domainReferenceIndex) {
        m_domainReferenceIndex =
            DesignDomainReferenceIndex::fromDomains(
                QVector<DomainDefinition>{});
    }
    const QByteArray initialSource = serializedJson(m_context.value);
    m_valueTooLarge = initialSource.size() > maximumDraftBytes;
    const QString initialText = m_valueTooLarge
        ? QString()
        : QString::fromUtf8(initialSource);
    if (m_valueTooLarge) {
        m_context.editable = false;
    } else if (m_context.editable) {
        m_initialSource = initialText;
    }
    setObjectName(QStringLiteral("finepaper.designExtensions.editorDialog"));
    setWindowTitle(
        m_context.editable
            ? QStringLiteral("Configure %1").arg(m_context.title)
            : QStringLiteral("View %1").arg(m_context.title));
    setModal(true);
    setSizeGripEnabled(true);
    resize(860, 680);
    setMinimumSize(520, 480);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto* heading = new QLabel(m_context.title, this);
    heading->setObjectName(
        QStringLiteral("finepaper.designExtensions.editorTitle"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(18);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    heading->setTextFormat(Qt::PlainText);
    layout->addWidget(heading);

    auto* identity = new QLabel(
        QStringLiteral("Namespace: %1").arg(m_context.id), this);
    identity->setObjectName(
        QStringLiteral("finepaper.designExtensions.editorNamespace"));
    identity->setTextFormat(Qt::PlainText);
    identity->setTextInteractionFlags(Qt::TextSelectableByKeyboard
                                      | Qt::TextSelectableByMouse);
    layout->addWidget(identity);

    if (!m_context.description.isEmpty()) {
        auto* description = new QLabel(m_context.description, this);
        description->setObjectName(
            QStringLiteral("finepaper.designExtensions.editorDescription"));
        description->setWordWrap(true);
        description->setTextFormat(Qt::PlainText);
        description->setTextInteractionFlags(Qt::TextSelectableByKeyboard
                                             | Qt::TextSelectableByMouse);
        layout->addWidget(description);
    }

    QString editorNoteText;
    if (m_valueTooLarge) {
        editorNoteText = QStringLiteral(
            "Read-only summary. The stored JSON exceeds the in-app editor "
            "limit and remains preserved unchanged in the design.");
    } else if (m_context.editable && m_context.definition
               && !m_context.definition->domainReferences.isEmpty()) {
        editorNoteText = QStringLiteral(
            "Schema-aware JSON source editor. Finepaper checks the "
            "Package schema and Package-declared Domain references here; "
            "deeper Package semantic checks still run during Validate / DRC "
            "and generation.");
    } else if (m_context.editable) {
        editorNoteText = QStringLiteral(
            "Schema-aware JSON source editor. Finepaper checks the "
            "Package schema here; Package semantic checks still run "
            "during Validate / DRC and generation.");
    } else {
        editorNoteText = QStringLiteral(
            "Read-only JSON source. This Finepaper build will not infer "
            "an editor from the extension namespace.");
    }
    if (!m_context.editorMessage.isEmpty()) {
        editorNoteText += QStringLiteral("\n%1").arg(m_context.editorMessage);
    }
    auto* editorNote = new QLabel(editorNoteText, this);
    editorNote->setObjectName(
        QStringLiteral("finepaper.designExtensions.editorMode"));
    editorNote->setWordWrap(true);
    editorNote->setTextFormat(Qt::PlainText);
    layout->addWidget(editorNote);

    if (!m_context.domainReferenceSummary.isEmpty()) {
        auto* referenceHeading = new QLabel(
            QStringLiteral("Design Domain references"), this);
        referenceHeading->setObjectName(
            QStringLiteral(
                "finepaper.designExtensions.domainReferencesHeading"));
        QFont referenceHeadingFont = referenceHeading->font();
        referenceHeadingFont.setBold(true);
        referenceHeading->setFont(referenceHeadingFont);
        layout->addWidget(referenceHeading);

        auto* references = new QPlainTextEdit(this);
        references->setObjectName(
            QStringLiteral("finepaper.designExtensions.domainReferences"));
        references->setAccessibleName(
            QStringLiteral("Available design Domain references"));
        references->setAccessibleDescription(
            QStringLiteral(
                "Package-declared JSON paths and matching Domain ids from the current design"));
        references->setReadOnly(true);
        references->setTabChangesFocus(true);
        references->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        references->setMinimumHeight(44);
        references->setMaximumHeight(112);
        references->setPlainText(m_context.domainReferenceSummary);
        referenceHeading->setBuddy(references);
        layout->addWidget(references);
    }

    auto* toolRow = new QHBoxLayout;
    m_formatButton = new QPushButton(QStringLiteral("&Format JSON"), this);
    m_formatButton->setObjectName(
        QStringLiteral("finepaper.designExtensions.format"));
    m_formatButton->setToolTip(
        QStringLiteral("Reformat the current syntactically valid JSON draft."));
    m_defaultButton = new QPushButton(
        QStringLiteral("Load Package &Default"), this);
    m_defaultButton->setObjectName(
        QStringLiteral("finepaper.designExtensions.loadDefault"));
    m_defaultButton->setToolTip(
        QStringLiteral("Replace this dialog's draft with the default declared by the Package schema."));
    toolRow->addWidget(m_formatButton);
    toolRow->addWidget(m_defaultButton);
    toolRow->addStretch(1);
    layout->addLayout(toolRow);

    auto* boundedEditor = new BoundedJsonEditor(maximumDraftBytes, this);
    m_editor = boundedEditor;
    m_editor->setObjectName(QStringLiteral("finepaper.designExtensions.json"));
    m_editor->setAccessibleName(QStringLiteral("Design extension JSON"));
    m_editor->setAccessibleDescription(
        m_context.editable
            ? QStringLiteral("Editable JSON value for the selected Package extension")
            : QStringLiteral("Read-only JSON value for the selected Package extension"));
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setTabChangesFocus(true);
    m_editor->setReadOnly(!m_context.editable);
    m_editor->setPlainText(
        m_valueTooLarge
            ? QStringLiteral(
                  "Stored JSON exceeds the 8 MiB in-app viewing limit. "
                  "Finepaper will preserve the value unchanged.")
            : initialText);
    layout->addWidget(m_editor, 1);

    m_validationState = new QLabel(this);
    m_validationState->setObjectName(
        QStringLiteral("finepaper.designExtensions.validationState"));
    m_validationState->setWordWrap(true);
    m_validationState->setTextFormat(Qt::PlainText);
    layout->addWidget(m_validationState);

    m_diagnosticsHeading = new QLabel(QStringLiteral("Problems"), this);
    QFont diagnosticsHeadingFont = m_diagnosticsHeading->font();
    diagnosticsHeadingFont.setBold(true);
    m_diagnosticsHeading->setFont(diagnosticsHeadingFont);
    m_diagnosticsHeading->setObjectName(
        QStringLiteral("finepaper.designExtensions.diagnosticsHeading"));
    layout->addWidget(m_diagnosticsHeading);

    m_diagnostics = new QPlainTextEdit(this);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.designExtensions.diagnostics"));
    m_diagnostics->setAccessibleName(
        QStringLiteral("Design extension diagnostics"));
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setTabChangesFocus(true);
    m_diagnostics->setMaximumBlockCount(512);
    m_diagnostics->setMinimumHeight(60);
    m_diagnostics->setMaximumHeight(170);
    layout->addWidget(m_diagnostics);
    setDiagnosticsText({});

    auto* buttons = new QDialogButtonBox(this);
    buttons->setObjectName(
        QStringLiteral("finepaper.designExtensions.editorButtons"));
    if (m_context.editable) {
        m_applyButton = buttons->addButton(
            QStringLiteral("&Apply"), QDialogButtonBox::ApplyRole);
        m_applyButton->setObjectName(
            QStringLiteral("finepaper.designExtensions.apply"));
        buttons->addButton(QDialogButtonBox::Cancel);
    } else {
        buttons->addButton(QDialogButtonBox::Close);
    }
    layout->addWidget(buttons);

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(180);
    boundedEditor->oversizedInputRejected = [this] {
        m_validationState->setText(
            QStringLiteral("Paste rejected; the JSON draft is unchanged."));
        setDiagnosticsText(
            QStringLiteral("The editor accepts at most %1 MiB of UTF-8 JSON text.")
                .arg(maximumDraftBytes / (1024 * 1024)));
    };

    connect(m_validationTimer, &QTimer::timeout,
            this, &DesignExtensionEditorDialog::validateDraft);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        m_draftTouched = true;
        setDiagnosticsText({});
        m_validationState->setText(QStringLiteral("Checking JSON…"));
        m_syntaxValid = false;
        m_schemaValid = false;
        m_domainReferencesValid = false;
        updateApplyState();
        m_validationTimer->start();
    });
    connect(m_formatButton, &QPushButton::clicked,
            this, &DesignExtensionEditorDialog::formatDraft);
    connect(m_defaultButton, &QPushButton::clicked,
            this, &DesignExtensionEditorDialog::loadPackageDefault);
    if (m_applyButton) {
        connect(m_applyButton, &QPushButton::clicked,
                this, &DesignExtensionEditorDialog::applyDraft);
    }
    connect(buttons, &QDialogButtonBox::rejected,
            this, &DesignExtensionEditorDialog::reject);

    const bool hasPackageDefault = m_context.definition
        && m_context.definition->schemaDocument.contains(
            QStringLiteral("default"));
    m_defaultButton->setVisible(m_context.editable && hasPackageDefault);
    m_formatButton->setVisible(m_context.editable);

    validateDraft();
    m_editor->setFocus(Qt::OtherFocusReason);
}

void DesignExtensionEditorDialog::reject() {
    if (!hasUnappliedDraft()) {
        QDialog::reject();
        return;
    }

    const bool validationPending = m_validationTimer->isActive();
    m_validationTimer->stop();
    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Discard JSON Draft"),
        QStringLiteral(
            "Discard the unapplied JSON changes in this dialog?\n\n"
            "The NoC design is unchanged, but this draft cannot be recovered."),
        QMessageBox::Discard | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.designExtensions.discardDraftConfirmation"));
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() == QMessageBox::Discard) {
        QDialog::reject();
    } else if (validationPending) {
        m_validationTimer->start();
    }
}

bool DesignExtensionEditorDialog::hasUnappliedDraft() const {
    if (!m_context.editable || m_valueTooLarge || !m_draftTouched) {
        return false;
    }
    if (m_syntaxValid) {
        return m_parsedValue != m_context.value;
    }
    return m_editor->toPlainText() != m_initialSource;
}

void DesignExtensionEditorDialog::validateDraft() {
    m_validationTimer->stop();
    m_syntaxValid = false;
    m_schemaValid = false;
    m_domainReferencesValid = false;
    setDiagnosticsText({});

    if (m_valueTooLarge) {
        m_validationState->setText(
            QStringLiteral("Stored JSON is too large for the in-app viewer."));
        setDiagnosticsText(
            QStringLiteral(
                "The value exceeds %1 MiB. It remains preserved unchanged in the design.")
                .arg(maximumDraftBytes / (1024 * 1024)));
        updateApplyState();
        return;
    }

    const QByteArray source = m_editor->toPlainText().toUtf8();

    if (source.size() > maximumDraftBytes) {
        m_validationState->setText(QStringLiteral("JSON draft is too large."));
        setDiagnosticsText(
            QStringLiteral("The editor accepts at most %1 MiB of UTF-8 JSON text.")
                .arg(maximumDraftBytes / (1024 * 1024)));
        updateApplyState();
        return;
    }

    const ParsedJson parsed = parseJsonValue(source);
    if (!parsed.success) {
        m_validationState->setText(QStringLiteral("Invalid JSON syntax."));
        setDiagnosticsText(parsed.error);
        updateApplyState();
        return;
    }

    m_parsedValue = parsed.value;
    m_syntaxValid = true;
    m_formatButton->setEnabled(m_context.editable);

    if (!m_context.definition
        || m_context.definition->schemaStatus
               != json_schema::CompileStatus::Ready
        || !m_context.definition->compiledSchema) {
        m_validationState->setText(
            m_context.editable
                ? QStringLiteral("No supported Package schema is available.")
                : QStringLiteral("Read-only JSON; schema validation is unavailable."));
        if (m_context.definition
            && !m_context.definition->schemaIssues.isEmpty()) {
            QStringList issues;
            issues.reserve(m_context.definition->schemaIssues.size());
            for (const json_schema::Issue& issue
                 : m_context.definition->schemaIssues) {
                issues.append(issueText(issue));
            }
            setDiagnosticsText(issues.join(QLatin1Char('\n')));
        }
        updateApplyState();
        return;
    }

    const json_schema::ValidationResult validation = json_schema::validate(
        *m_context.definition->compiledSchema, parsed.value);
    m_schemaValid = validation.success;
    if (!validation.success) {
        QStringList issues;
        issues.reserve(validation.issues.size());
        for (const json_schema::Issue& issue : validation.issues) {
            issues.append(issueText(issue));
        }
        if (issues.isEmpty()) {
            issues.append(QStringLiteral(
                "/: The JSON value does not satisfy the Package schema."));
        }
        m_validationState->setText(
            QStringLiteral("JSON is well-formed but violates the Package schema."));
        setDiagnosticsText(issues.join(QLatin1Char('\n')));
        updateApplyState();
        return;
    }

    QVector<Diagnostic> domainReferenceDiagnostics;
    if (!m_context.definition->domainReferences.isEmpty()) {
        Q_ASSERT(m_domainReferenceIndex);
        domainReferenceDiagnostics =
            validateDesignExtensionDomainReferences(
                parsed.value,
                *m_context.definition,
                *m_domainReferenceIndex);
    }
    m_domainReferencesValid = !hasErrors(domainReferenceDiagnostics);
    if (!m_domainReferencesValid) {
        m_validationState->setText(
            QStringLiteral(
                "Structure valid, but one or more Domain references are invalid."));
        setDiagnosticsText(diagnosticsText(domainReferenceDiagnostics));
        updateApplyState();
        return;
    }

    const bool changed = !m_context.configured
        || parsed.value != m_context.value;
    const bool hasDomainReferences =
        !m_context.definition->domainReferences.isEmpty();
    if (!m_context.editable) {
        m_validationState->setText(hasDomainReferences
            ? QStringLiteral(
                  "Read-only; the stored value satisfies the Package schema and Domain references.")
            : QStringLiteral(
                  "Read-only; the stored value satisfies the Package schema."));
    } else if (!changed) {
        m_validationState->setText(hasDomainReferences
            ? QStringLiteral(
                  "Structure and Domain references valid. No changes to apply.")
            : QStringLiteral("Structure valid. No changes to apply."));
    } else {
        m_validationState->setText(hasDomainReferences
            ? QStringLiteral(
                  "Structure and Domain references valid. Changes are ready to apply.")
            : QStringLiteral("Structure valid. Changes are ready to apply."));
    }
    updateApplyState();
}

void DesignExtensionEditorDialog::applyDraft() {
    if (!m_applyButton || !m_applyButton->isEnabled()
        || !m_syntaxValid || !m_schemaValid || !m_domainReferencesValid
        || !applyRequested) {
        if (!applyRequested && m_syntaxValid && m_schemaValid
            && m_domainReferencesValid) {
            m_validationState->setText(
                QStringLiteral("The editor is not connected to a design mutation."));
        }
        return;
    }

    m_applyInProgress = true;
    updateApplyState();
    const DesignResult result = applyRequested(m_context.id, m_parsedValue);
    m_applyInProgress = false;
    if (!result.success) {
        m_validationState->setText(
            QStringLiteral("Apply rejected; the design and JSON draft are unchanged."));
        showApplicationDiagnostics(result.diagnostics);
        updateApplyState();
        return;
    }
    accept();
}

void DesignExtensionEditorDialog::formatDraft() {
    if (!m_syntaxValid) {
        validateDraft();
    }
    if (!m_syntaxValid) {
        return;
    }
    const int cursorPosition = m_editor->textCursor().position();
    const QByteArray formatted = serializedJson(m_parsedValue);
    if (formatted.size() > maximumDraftBytes) {
        m_validationState->setText(
            QStringLiteral("Formatted JSON would exceed the editor limit."));
        setDiagnosticsText(
            QStringLiteral("Keep the current compact draft or reduce its size."));
        return;
    }
    m_editor->setPlainText(QString::fromUtf8(formatted));
    QTextCursor cursor = m_editor->textCursor();
    cursor.setPosition((std::min)(
        cursorPosition,
        static_cast<int>(m_editor->toPlainText().size())));
    m_editor->setTextCursor(cursor);
    validateDraft();
}

void DesignExtensionEditorDialog::loadPackageDefault() {
    if (!m_context.editable || !m_context.definition
        || !m_context.definition->schemaDocument.contains(
            QStringLiteral("default"))) {
        return;
    }
    const QByteArray packageDefault = serializedJson(
        m_context.definition->schemaDocument.value(QStringLiteral("default")));
    if (packageDefault.size() > maximumDraftBytes) {
        m_validationState->setText(
            QStringLiteral("Package default exceeds the editor limit."));
        setDiagnosticsText(
            QStringLiteral("The Package default was not loaded."));
        return;
    }
    if (hasUnappliedDraft()) {
        const bool validationPending = m_validationTimer->isActive();
        m_validationTimer->stop();
        QMessageBox confirmation(
            QMessageBox::Warning,
            QStringLiteral("Load Package Default"),
            QStringLiteral(
                "Replace the current JSON draft with the Package default?\n\n"
                "Unapplied changes in this dialog will be discarded."),
            QMessageBox::Discard | QMessageBox::Cancel,
            this);
        confirmation.setObjectName(
            QStringLiteral(
                "finepaper.designExtensions.replaceDraftConfirmation"));
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Discard) {
            if (validationPending) {
                m_validationTimer->start();
            }
            return;
        }
    }
    m_editor->setPlainText(QString::fromUtf8(packageDefault));
    validateDraft();
}

void DesignExtensionEditorDialog::showApplicationDiagnostics(
    const QVector<Diagnostic>& diagnostics) {
    const QString text = diagnosticsText(diagnostics);
    setDiagnosticsText(
        text.isEmpty()
            ? QStringLiteral("The design mutation was rejected without a diagnostic.")
            : text);
}

void DesignExtensionEditorDialog::setDiagnosticsText(const QString& text) {
    const bool visible = !text.isEmpty();
    m_diagnostics->setPlainText(text);
    m_diagnostics->setVisible(visible);
    m_diagnosticsHeading->setVisible(visible);
}

void DesignExtensionEditorDialog::updateApplyState() {
    m_formatButton->setEnabled(m_context.editable && m_syntaxValid
                               && !m_applyInProgress);
    if (!m_applyButton) {
        return;
    }
    const bool changed = m_syntaxValid
        && (!m_context.configured || m_parsedValue != m_context.value);
    m_applyButton->setEnabled(m_context.editable && m_syntaxValid
                              && m_schemaValid && m_domainReferencesValid
                              && changed
                              && !m_applyInProgress);
}

} // namespace finepaper
