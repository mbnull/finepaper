#include "features/design_extensions/design_extension_editor_dialog.h"
#include "features/design_extensions/design_extensions_workspace.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool waitUntil(const std::function<bool()>& predicate,
               QDeadlineTimer deadline = QDeadlineTimer(
                   std::chrono::seconds{3})) {
    while (!predicate() && !deadline.hasExpired()) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    QApplication::processEvents();
    return predicate();
}

finepaper::DesignExtensionDefinition definitionFor(
    const QString& id,
    const QJsonObject& schema,
    std::optional<QString> editorKind = QStringLiteral("json-schema")) {
    finepaper::DesignExtensionDefinition definition;
    definition.id = id;
    definition.version = 1;
    definition.schemaDocument = schema;
    const finepaper::json_schema::CompileResult compiled =
        finepaper::json_schema::compile(schema);
    definition.schemaStatus = compiled.status;
    definition.compiledSchema = compiled.schema;
    definition.schemaIssues = compiled.issues;
    if (editorKind) {
        definition.editor = finepaper::DesignExtensionEditorDefinition{
            *editorKind};
    }
    return definition;
}

QJsonObject objectSchemaWithDefault() {
    return QJsonObject{
        {QStringLiteral("title"), QStringLiteral("Vendor settings")},
        {QStringLiteral("description"),
         QStringLiteral("A Package-owned test extension.")},
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("required"),
         QJsonArray{QStringLiteral("count")}},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("count"),
              QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("integer")},
                  {QStringLiteral("minimum"), 1}}}}},
        {QStringLiteral("default"),
         QJsonObject{{QStringLiteral("count"), 1}}}};
}

QListWidgetItem* itemForId(QListWidget* list, const QString& id) {
    if (!list) {
        return nullptr;
    }
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toString() == id) {
            return list->item(row);
        }
    }
    return nullptr;
}

finepaper::DesignResult acceptedResult() {
    finepaper::DesignResult result;
    result.success = true;
    return result;
}

void respondToMessageBox(const QString& objectName,
                         QMessageBox::StandardButton button,
                         bool* observed = nullptr,
                         bool* plainText = nullptr) {
    QTimer::singleShot(
        0, [objectName, button, observed, plainText] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (!message || message->objectName() != objectName) {
                    continue;
                }
                if (observed) {
                    *observed = true;
                }
                if (plainText) {
                    *plainText = message->textFormat() == Qt::PlainText;
                }
                if (QAbstractButton* target = message->button(button)) {
                    target->click();
                }
                return;
            }
        });
}

void workspaceStateMatrix() {
    using namespace finepaper;

    DesignExtensionsWorkspace workspace;
    workspace.resize(900, 620);
    workspace.show();

    auto* list = workspace.findChild<QListWidget*>(
        QStringLiteral("finepaper.designExtensions.list"));
    auto* state = workspace.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.workspaceState"));
    auto* status = workspace.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.status"));
    auto* details = workspace.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.statusDetails"));
    auto* open = workspace.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.open"));
    auto* remove = workspace.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.remove"));
    auto* filter = workspace.findChild<QLineEdit*>(
        QStringLiteral("finepaper.designExtensions.filter"));
    check(list && state && status && details && open && remove && filter,
          QStringLiteral("workspace exposes stable accessible controls"));
    if (!list || !state || !status || !details || !open || !remove || !filter) {
        return;
    }

    workspace.setContext(nullptr, nullptr);
    check(list->count() == 0
              && state->text().contains(QStringLiteral("No design"))
              && status->accessibleName().isEmpty()
              && details->accessibleName().isEmpty()
              && !open->isVisible() && !remove->isVisible(),
          QStringLiteral("no-design state is explicit"));
    const auto shortcuts = workspace.findChildren<QShortcut*>();
    check(std::any_of(
              shortcuts.cbegin(), shortcuts.cend(),
              [](const QShortcut* shortcut) {
                  return shortcut->key() == QKeySequence::Find
                      && shortcut->context()
                          == Qt::WidgetWithChildrenShortcut;
              }),
          QStringLiteral(
              "Ctrl+F filters only while the Design Extensions Workspace is active"));

    PackageDefinition package;
    package.id = QStringLiteral("vendor.noc");
    package.version = QStringLiteral("2.4");
    package.designExtensionsDeclared = true;
    package.designExtensions = {
        definitionFor(QStringLiteral("vendor.settings"),
                      objectSchemaWithDefault()),
        definitionFor(QStringLiteral("vendor.noEditor"),
                      QJsonObject{}, std::nullopt),
        definitionFor(QStringLiteral("vendor.future"),
                      QJsonObject{}, QStringLiteral("future-form")),
        definitionFor(
            QStringLiteral("vendor.unsupported"),
            QJsonObject{{QStringLiteral("anyOf"), QJsonArray{QJsonObject{}}}})
    };

    NocDesign design;
    design.package = PackageReference{package.id, package.version};
    workspace.setContext(&design, &package);
    check(list->count() == 4,
          QStringLiteral("all Package-declared extension ids are data-driven"));

    list->setCurrentItem(itemForId(list, QStringLiteral("vendor.settings")));
    QApplication::processEvents();
    check(status->text().contains(QStringLiteral("Not configured"))
              && open->isEnabled()
              && open->text().contains(QStringLiteral("Configure"))
              && !remove->isVisible(),
          QStringLiteral("Ready json-schema extension can be configured"));

    list->setCurrentItem(itemForId(list, QStringLiteral("vendor.noEditor")));
    QApplication::processEvents();
    check(status->text().contains(QStringLiteral("No supported editor"))
              && !open->isEnabled()
              && details->text().contains(QStringLiteral("did not declare")),
          QStringLiteral("missing editor fails closed without namespace inference"));

    list->setCurrentItem(itemForId(list, QStringLiteral("vendor.future")));
    QApplication::processEvents();
    check(!open->isEnabled()
              && details->text().contains(QStringLiteral("future-form")),
          QStringLiteral("unknown editor kind fails closed with a reason"));

    list->setCurrentItem(itemForId(list, QStringLiteral("vendor.unsupported")));
    QApplication::processEvents();
    check(status->text().contains(QStringLiteral("Unsupported schema"))
              && !open->isEnabled(),
          QStringLiteral("unsupported schema cannot create a value"));

    design.packageData.insert(
        QStringLiteral("vendor.future"), QJsonObject{});
    design.packageData.insert(
        QStringLiteral("vendor.unsupported"), QJsonObject{});
    workspace.setContext(&design, &package);
    list->setCurrentItem(itemForId(list, QStringLiteral("vendor.future")));
    QApplication::processEvents();
    check(open->isEnabled() && open->text().contains(QStringLiteral("View"))
              && remove->isVisible() && remove->isEnabled(),
          QStringLiteral("configured unknown editor stays viewable and removable"));
    list->setCurrentItem(
        itemForId(list, QStringLiteral("vendor.unsupported")));
    QApplication::processEvents();
    check(open->isEnabled() && remove->isEnabled(),
          QStringLiteral("configured unsupported schema has a repair removal path"));

    filter->setText(QStringLiteral("future"));
    QApplication::processEvents();
    check(list->count() == 1
              && list->item(0)->data(Qt::UserRole).toString()
                     == QStringLiteral("vendor.future"),
          QStringLiteral("extension filtering matches vendor-defined ids"));
    filter->clear();
    filter->setText(QStringLiteral("does-not-exist"));
    QApplication::processEvents();
    auto* title = workspace.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.title"));
    check(list->count() == 0 && title
              && title->text().contains(QStringLiteral("No matching"))
              && !open->isVisible() && !remove->isVisible(),
          QStringLiteral(
              "an empty filter result has a distinct state without stale actions"));
    filter->clear();

    NocDesign missingPackageDesign = design;
    missingPackageDesign.packageData = QJsonObject{
        {QStringLiteral("legacy.opaque"), false}};
    NocDesign emptyMissingPackageDesign = missingPackageDesign;
    emptyMissingPackageDesign.packageData = QJsonObject{};
    workspace.setContext(&emptyMissingPackageDesign, nullptr);
    check(title && title->text().contains(QStringLiteral("Package unavailable")),
          QStringLiteral(
              "missing Package without retained data is not misreported as an empty declaration"));
    workspace.setContext(&missingPackageDesign, nullptr);
    check(list->count() == 1
              && state->text().contains(QStringLiteral("not loaded")),
          QStringLiteral("missing Package preserves visible opaque data"));
    list->setCurrentRow(0);
    QApplication::processEvents();
    check(open->isEnabled() && !remove->isVisible()
              && status->text().contains(QStringLiteral("Read-only")),
          QStringLiteral("missing Package data is inspectable but immutable"));

    const PackageDefinition packageWithoutDeclarations;
    workspace.setContext(&design, &packageWithoutDeclarations);
    check(list->count() == design.packageData.size(),
          QStringLiteral("undeclared retained keys remain visible"));
    workspace.resize(520, 480);
    QApplication::processEvents();
    auto* splitter = workspace.findChild<QSplitter*>(
        QStringLiteral("finepaper.designExtensions.splitter"));
    check(splitter && splitter->orientation() == Qt::Vertical,
          QStringLiteral(
              "narrow Workspace stacks extension discovery above details"));
    check(list->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
          QStringLiteral(
              "narrow extension discovery wraps text without a horizontal scroll trap"));

    PackageDefinition changingPackage = package;
    changingPackage.designExtensions = {
        definitionFor(QStringLiteral("vendor.a"), QJsonObject{}),
        definitionFor(QStringLiteral("vendor.b"), QJsonObject{})};
    DesignExtensionsWorkspace selectionWorkspace;
    selectionWorkspace.setContext(&design, &changingPackage);
    auto* selectionList = selectionWorkspace.findChild<QListWidget*>(
        QStringLiteral("finepaper.designExtensions.list"));
    if (selectionList) {
        selectionList->setCurrentItem(
            itemForId(selectionList, QStringLiteral("vendor.b")));
    }
    changingPackage.designExtensions = {
        definitionFor(QStringLiteral("vendor.a"), QJsonObject{}),
        definitionFor(QStringLiteral("vendor.c"), QJsonObject{})};
    selectionWorkspace.setContext(&design, &changingPackage);
    check(selectionList && selectionList->currentItem()
              && selectionList->currentItem()->data(Qt::UserRole).toString()
                     == QStringLiteral("vendor.a"),
          QStringLiteral(
              "context rebuild falls back deliberately when the selected stable id disappears"));

    PackageDefinition cachedPackage;
    cachedPackage.id = QStringLiteral("vendor.cached-noc");
    cachedPackage.version = QStringLiteral("1.0");
    cachedPackage.designExtensionsDeclared = true;
    cachedPackage.designExtensions = {definitionFor(
        QStringLiteral("vendor.cached"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}})};
    NocDesign cachedDesign;
    cachedDesign.package = PackageReference{
        cachedPackage.id, cachedPackage.version};
    cachedDesign.packageData.insert(
        QStringLiteral("vendor.cached"),
        QJsonObject{
            {QStringLiteral("domain"), QStringLiteral("missing-power")}});
    DesignExtensionsWorkspace cacheWorkspace;
    cacheWorkspace.setContext(&cachedDesign, &cachedPackage);
    auto* cacheList = cacheWorkspace.findChild<QListWidget*>(
        QStringLiteral("finepaper.designExtensions.list"));
    auto* cacheStatus = cacheWorkspace.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.status"));
    if (cacheList) {
        cacheList->setCurrentRow(0);
    }
    QApplication::processEvents();
    const bool initiallySchemaValid = cacheStatus
        && cacheStatus->text().contains(QStringLiteral("Configured"));

    DesignExtensionDomainReferenceDefinition cachedReference;
    cachedReference.pointerTokens = {QStringLiteral("domain")};
    cachedReference.domainType = QStringLiteral("power");
    cachedPackage.designExtensions[0].domainReferences = {cachedReference};
    cacheWorkspace.setContext(&cachedDesign, &cachedPackage);
    if (cacheList) {
        cacheList->setCurrentRow(0);
    }
    QApplication::processEvents();
    check(initiallySchemaValid && cacheStatus
              && cacheStatus->text().contains(QStringLiteral("Invalid value")),
          QStringLiteral(
              "same-version Package reference declaration changes invalidate cached extension validation"));
}

void dialogJsonAndTransactionSemantics() {
    using namespace finepaper;

    const DesignExtensionDefinition anyValue = definitionFor(
        QStringLiteral("vendor.any"), QJsonObject{});
    const QJsonValue largeInteger = QJsonValue(
        (std::numeric_limits<qint64>::max)());

    DesignExtensionEditorContext scalarContext;
    scalarContext.id = anyValue.id;
    scalarContext.title = QStringLiteral("Any root value");
    scalarContext.value = largeInteger;
    scalarContext.definition = anyValue;
    scalarContext.configured = true;
    scalarContext.editable = true;
    DesignExtensionEditorDialog scalarDialog(scalarContext);
    scalarDialog.show();
    auto* scalarEditor = scalarDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* scalarApply = scalarDialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.apply"));
    check(scalarEditor && scalarApply
              && scalarEditor->toPlainText().trimmed()
                     == QStringLiteral("9223372036854775807"),
          QStringLiteral("root 64-bit integer renders without double rounding"));
    if (!scalarEditor || !scalarApply) {
        return;
    }
    std::optional<QJsonValue> appliedValue = std::nullopt;
    QString appliedId;
    scalarDialog.applyRequested = [&](QString id, QJsonValue value) {
        appliedId = std::move(id);
        appliedValue = value;
        return acceptedResult();
    };
    scalarEditor->setPlainText(QStringLiteral("false"));
    check(waitUntil([&] { return scalarApply->isEnabled(); }),
          QStringLiteral("valid changed root scalar enables Apply after debounce"));
    scalarApply->click();
    check(scalarDialog.result() == QDialog::Accepted && appliedValue
              && appliedValue->isBool() && !appliedValue->toBool()
              && appliedId == QStringLiteral("vendor.any"),
          QStringLiteral("Apply emits the exact id and arbitrary root QJsonValue once"));

    const DesignExtensionDefinition objectDefinition = definitionFor(
        QStringLiteral("vendor.settings"), objectSchemaWithDefault());
    DesignExtensionEditorContext failureContext;
    failureContext.id = objectDefinition.id;
    failureContext.title = QStringLiteral("Vendor settings");
    failureContext.value = objectDefinition.schemaDocument.value(
        QStringLiteral("default"));
    failureContext.definition = objectDefinition;
    failureContext.configured = false;
    failureContext.editable = true;
    DesignExtensionEditorDialog failureDialog(failureContext);
    failureDialog.show();
    QApplication::processEvents();
    auto* failureEditor = failureDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* failureApply = failureDialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.apply"));
    auto* diagnostics = failureDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.diagnostics"));
    auto* validationState = failureDialog.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.validationState"));
    check(failureEditor && failureApply && diagnostics && validationState,
          QStringLiteral("editor dialog exposes validation and action controls"));
    if (!failureEditor || !failureApply || !diagnostics || !validationState) {
        return;
    }
    check(validationState->accessibleName().isEmpty(),
          QStringLiteral(
              "dynamic validation text remains the accessible label name"));

    const QString rawDraft = QStringLiteral("{ \"count\" : 2 }");
    failureEditor->setPlainText(rawDraft);
    check(waitUntil([&] { return failureApply->isEnabled(); }),
          QStringLiteral("schema-valid object draft enables Apply"));
    check(!validationState->text().contains(
              QStringLiteral("Domain reference")),
          QStringLiteral(
              "schema-only extension status does not claim Domain-reference validation"));
    int failedCalls = 0;
    failureDialog.applyRequested = [&](QString, QJsonValue) {
        ++failedCalls;
        DesignResult result;
        result.diagnostics.append(Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("design.semantic_rejection"),
            QStringLiteral("Package semantic rule rejected count"),
            QStringLiteral("/packageData/vendor.settings/count"),
            QStringLiteral("package")});
        return result;
    };
    failureApply->click();
    check(failedCalls == 1 && failureDialog.isVisible()
              && failureEditor->toPlainText() == rawDraft
              && validationState->text().contains(QStringLiteral("unchanged"))
              && diagnostics->toPlainText().contains(
                     QStringLiteral("/packageData/vendor.settings/count")),
          QStringLiteral("failed Apply stays open, preserves raw draft, and shows inline diagnostics"));

    auto* formatButton = failureDialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.format"));
    if (formatButton) {
        formatButton->click();
        failureApply->click();
        check(waitUntil([&] {
                  return failedCalls == 2
                      && diagnostics->toPlainText().contains(
                          QStringLiteral("design.semantic_rejection"));
              })
                  && diagnostics->toPlainText().contains(
                      QStringLiteral("Package semantic rule rejected count")),
              QStringLiteral(
                  "synchronous Format validation cannot leave a timer that erases backend rejection diagnostics"));
    }

    failureEditor->setPlainText(QStringLiteral("{\n  \"count\": 0\n}"));
    check(waitUntil([&] {
              return validationState->text().contains(
                  QStringLiteral("violates"));
          })
              && !failureApply->isEnabled()
              && diagnostics->toPlainText().contains(QStringLiteral("/count")),
          QStringLiteral("schema violations expose the instance JSON Pointer"));

    failureEditor->setPlainText(QStringLiteral("{\n  \"count\": \n}"));
    check(waitUntil([&] {
              return validationState->text().contains(
                  QStringLiteral("syntax"));
          })
              && diagnostics->toPlainText().contains(QStringLiteral("Line"))
              && diagnostics->toPlainText().contains(QStringLiteral("column")),
          QStringLiteral("syntax errors report a line and column"));

    auto* defaultButton = failureDialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.loadDefault"));
    check(defaultButton && defaultButton->isVisible(),
          QStringLiteral("Package-declared default is discoverable"));
    if (defaultButton) {
        bool replacePromptObserved = false;
        respondToMessageBox(
            QStringLiteral(
                "finepaper.designExtensions.replaceDraftConfirmation"),
            QMessageBox::Discard,
            &replacePromptObserved);
        defaultButton->click();
        check(replacePromptObserved
                  && waitUntil([&] { return failureApply->isEnabled(); })
                  && failureEditor->toPlainText().contains(
                         QStringLiteral("\"count\": 1")),
              QStringLiteral(
                  "Package default confirms before replacing only the dialog draft"));
    }

    failureDialog.resize(520, 480);
    QApplication::processEvents();
    check(failureDialog.size() == QSize(520, 480)
              && failureEditor->isVisible() && failureApply->isVisible()
              && failureApply->mapTo(
                     &failureDialog,
                     QPoint(0, failureApply->height())).y()
                     <= failureDialog.contentsRect().bottom()
              && failureEditor->tabChangesFocus(),
          QStringLiteral(
              "520x480 editor keeps JSON and actions reachable without a Tab-key trap"));

    auto* failureButtons = failureDialog.findChild<QDialogButtonBox*>(
        QStringLiteral("finepaper.designExtensions.editorButtons"));
    failureEditor->setPlainText(QStringLiteral("{\"count\": 3}"));
    check(waitUntil([&] { return failureApply->isEnabled(); }),
          QStringLiteral("changed draft is ready before close confirmation"));
    bool discardPromptUsesPlainText = false;
    respondToMessageBox(
        QStringLiteral(
            "finepaper.designExtensions.discardDraftConfirmation"),
        QMessageBox::Cancel,
        nullptr,
        &discardPromptUsesPlainText);
    if (failureButtons) {
        failureButtons->button(QDialogButtonBox::Cancel)->click();
    }
    check(failureDialog.isVisible() && discardPromptUsesPlainText,
          QStringLiteral(
              "Cancel keeps an unapplied draft when discard is not confirmed"));
    respondToMessageBox(
        QStringLiteral(
            "finepaper.designExtensions.discardDraftConfirmation"),
        QMessageBox::Discard);
    if (failureButtons) {
        failureButtons->button(QDialogButtonBox::Cancel)->click();
    }
    check(!failureDialog.isVisible(),
          QStringLiteral("confirmed discard closes the modal draft"));

    DesignExtensionEditorDialog untouchedDialog(failureContext);
    untouchedDialog.show();
    QApplication::processEvents();
    bool untouchedPromptObserved = false;
    respondToMessageBox(
        QStringLiteral(
            "finepaper.designExtensions.discardDraftConfirmation"),
        QMessageBox::Cancel,
        &untouchedPromptObserved);
    untouchedDialog.close();
    QApplication::processEvents();
    check(!untouchedDialog.isVisible() && !untouchedPromptObserved,
          QStringLiteral(
              "an untouched unconfigured Package default closes without a false dirty warning"));
    if (untouchedDialog.isVisible()) {
        respondToMessageBox(
            QStringLiteral(
                "finepaper.designExtensions.discardDraftConfirmation"),
            QMessageBox::Discard);
        untouchedDialog.close();
    }

    DesignExtensionEditorDialog pendingDialog(failureContext);
    pendingDialog.show();
    auto* pendingEditor = pendingDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* pendingState = pendingDialog.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.validationState"));
    if (pendingEditor) {
        pendingEditor->setPlainText(QStringLiteral("{\"count\": 4}"));
    }
    bool pendingPromptObserved = false;
    bool pendingValidationPaused = false;
    QTimer::singleShot(
        std::chrono::milliseconds{250}, [&pendingPromptObserved,
              &pendingValidationPaused,
              pendingState] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (!message
                    || message->objectName()
                        != QStringLiteral(
                            "finepaper.designExtensions.discardDraftConfirmation")) {
                    continue;
                }
                pendingPromptObserved = true;
                pendingValidationPaused = pendingState
                    && pendingState->text().contains(
                        QStringLiteral("Checking"));
                if (QAbstractButton* cancel = message->button(
                        QMessageBox::Cancel)) {
                    cancel->click();
                }
                return;
            }
        });
    pendingDialog.close();
    check(pendingEditor && pendingDialog.isVisible()
              && pendingPromptObserved && pendingValidationPaused,
          QStringLiteral(
              "window close pauses pending validation and preserves the changed draft"));
    respondToMessageBox(
        QStringLiteral(
            "finepaper.designExtensions.discardDraftConfirmation"),
        QMessageBox::Discard);
    pendingDialog.close();

    DesignExtensionEditorDialog invalidDialog(failureContext);
    invalidDialog.show();
    auto* invalidEditor = invalidDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* invalidState = invalidDialog.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.validationState"));
    if (invalidEditor) {
        invalidEditor->setPlainText(QStringLiteral("{\"count\":"));
    }
    check(invalidState
              && waitUntil([invalidState] {
                  return invalidState->text().contains(
                      QStringLiteral("syntax"));
              }),
          QStringLiteral("invalid exit test reaches syntax diagnostics"));
    bool escapePromptObserved = false;
    respondToMessageBox(
        QStringLiteral(
            "finepaper.designExtensions.discardDraftConfirmation"),
        QMessageBox::Cancel,
        &escapePromptObserved);
    QKeyEvent escapePress(
        QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&invalidDialog, &escapePress);
    check(invalidDialog.isVisible() && escapePromptObserved,
          QStringLiteral(
              "Escape preserves an invalid dirty draft without confirmed discard"));
    respondToMessageBox(
        QStringLiteral(
            "finepaper.designExtensions.discardDraftConfirmation"),
        QMessageBox::Discard);
    QApplication::sendEvent(&invalidDialog, &escapePress);
    check(!invalidDialog.isVisible(),
          QStringLiteral("Escape closes after invalid draft discard is confirmed"));

    DesignExtensionEditorContext readOnlyContext = scalarContext;
    readOnlyContext.editable = false;
    DesignExtensionEditorDialog readOnlyDialog(readOnlyContext);
    check(readOnlyDialog.findChild<QPushButton*>(
              QStringLiteral("finepaper.designExtensions.apply")) == nullptr
              && readOnlyDialog.findChild<QPlainTextEdit*>(
                     QStringLiteral("finepaper.designExtensions.json"))
                     ->isReadOnly(),
          QStringLiteral("read-only viewer exposes no mutation action"));

    DesignExtensionEditorContext oversizedContext = scalarContext;
    oversizedContext.title = QStringLiteral("Oversized value");
    oversizedContext.value = QString(
        8 * 1024 * 1024 + 1, QLatin1Char('x'));
    DesignExtensionEditorDialog oversizedDialog(
        std::move(oversizedContext));
    auto* oversizedEditor = oversizedDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* oversizedState = oversizedDialog.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.validationState"));
    check(oversizedEditor && oversizedState && oversizedEditor->isReadOnly()
              && oversizedEditor->toPlainText().size() < 1024
              && oversizedState->text().contains(QStringLiteral("too large")),
          QStringLiteral(
              "oversized stored JSON is preserved without populating the in-app text document"));

    DesignExtensionEditorContext pasteContext = failureContext;
    DesignExtensionEditorDialog pasteDialog(std::move(pasteContext));
    auto* pasteEditor = pasteDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* pasteDiagnostics = pasteDialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.diagnostics"));
    auto* pasteApply = pasteDialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.apply"));
    if (pasteEditor) {
        pasteEditor->setPlainText(QStringLiteral("{\"count\": 2}"));
    }
    const QString pasteOriginal = pasteEditor
        ? pasteEditor->toPlainText() : QString();
    QApplication::clipboard()->setText(
        QString(8 * 1024 * 1024 + 1, QLatin1Char('x')));
    if (pasteEditor) {
        pasteEditor->selectAll();
        pasteEditor->paste();
    }
    const bool rejectedBeforeInsertion = pasteEditor && pasteDiagnostics
              && pasteEditor->toPlainText() == pasteOriginal
              && pasteDiagnostics->toPlainText().contains(
                  QStringLiteral("at most 8 MiB"));
    check(rejectedBeforeInsertion && pasteApply
              && waitUntil([pasteApply] { return pasteApply->isEnabled(); }),
          QStringLiteral(
              "oversized paste is rejected before insertion without cancelling pending validation"));
    QApplication::clipboard()->clear();
}

void dialogDomainReferenceContextAndValidation() {
    using namespace finepaper;

    DesignExtensionDefinition definition = definitionFor(
        QStringLiteral("vendor.domain-aware"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});
    DesignExtensionDomainReferenceDefinition reference;
    reference.pointerTokens = {
        QStringLiteral("bindings"),
        QStringLiteral("*"),
        QStringLiteral("domain")};
    reference.domainType = QStringLiteral("power");
    definition.domainReferences.append(reference);
    DesignExtensionDomainReferenceDefinition spacedReference;
    spacedReference.pointerTokens = {
        QStringLiteral("groups"),
        QStringLiteral("with  spaces"),
        QStringLiteral("*"),
        QStringLiteral("domain")};
    spacedReference.domainType = QStringLiteral("power");
    definition.domainReferences.append(spacedReference);

    const DomainDefinition powerMain{
        QStringLiteral("power-main"),
        QStringLiteral("power"),
        QStringLiteral("Main power"),
        {}};
    const DomainDefinition powerAux{
        QStringLiteral("power-aux"),
        QStringLiteral("power"),
        QStringLiteral("Auxiliary power"),
        {}};
    const DomainDefinition clockMain{
        QStringLiteral("clock-main"),
        QStringLiteral("clock"),
        QStringLiteral("Main clock"),
        {}};

    PackageDefinition package;
    package.id = QStringLiteral("vendor.noc");
    package.version = QStringLiteral("1.0");
    package.designExtensionsDeclared = true;
    package.domainTypes = {
        DomainTypeDefinition{
            .id = QStringLiteral("power"),
            .label = QStringLiteral("Power domain")},
        DomainTypeDefinition{
            .id = QStringLiteral("clock"),
            .label = QStringLiteral("Clock domain")}};
    package.designExtensions = {definition};

    NocDesign design;
    design.package = PackageReference{package.id, package.version};
    design.domains = {powerMain, powerAux, clockMain};
    for (int index = 0; index < 260; ++index) {
        design.domains.append(DomainDefinition{
            QStringLiteral("zz-power-extra-%1")
                .arg(index, 3, 10, QLatin1Char('0')),
            QStringLiteral("power"),
            QStringLiteral("Extra power %1").arg(index),
            {}});
    }
    const QJsonObject configuredBindings{
        {QStringLiteral("bindings"),
         QJsonArray{QJsonObject{
             {QStringLiteral("domain"), QStringLiteral("power-main")}}}}};
    design.packageData.insert(
        definition.id, configuredBindings);

    DesignExtensionsWorkspace workspace;
    workspace.resize(900, 620);
    workspace.show();
    workspace.setContext(&design, &package);
    auto* open = workspace.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.open"));
    bool summaryObserved = false;
    QTimer::singleShot(0, &workspace, [&summaryObserved] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName()
                != QStringLiteral(
                    "finepaper.designExtensions.editorDialog")) {
                continue;
            }
            auto* summary = widget->findChild<QPlainTextEdit*>(
                QStringLiteral(
                    "finepaper.designExtensions.domainReferences"));
            summaryObserved = summary && summary->isReadOnly()
                && summary->tabChangesFocus()
                && summary->toPlainText().contains(
                    QStringLiteral("/bindings/*/domain"))
                && summary->toPlainText().contains(
                    QStringLiteral("/groups/with  spaces/*/domain"))
                && summary->toPlainText().contains(
                    QStringLiteral("power-main — Main power"))
                && summary->toPlainText().contains(
                    QStringLiteral("power-aux — Auxiliary power"))
                && summary->toPlainText().contains(
                    QStringLiteral("zz-power-extra-012 — Extra power 12"))
                && summary->toPlainText().contains(
                    QStringLiteral(
                        "… 6 more; open Domain Configuration for the complete list"))
                && !summary->toPlainText().contains(
                    QStringLiteral("zz-power-extra-259"))
                && !summary->toPlainText().contains(
                    QStringLiteral("clock-main"));
            widget->close();
            return;
        }
    });
    if (open) {
        open->click();
    }
    check(open && summaryObserved,
          QStringLiteral(
              "workspace exposes only matching current-design Domain candidates for Package-declared paths"));

    DesignExtensionEditorContext context;
    context.id = definition.id;
    context.title = QStringLiteral("Domain-aware settings");
    context.value = design.packageData.value(definition.id);
    context.definition = definition;
    context.domainReferenceIndex =
        DesignDomainReferenceIndex::fromDomains(design.domains);
    context.domainReferenceSummary = QStringLiteral(
        "Package-declared JSON paths:\n"
        "  /bindings/*/domain → Power domain (power)\n\n"
        "Available design Domains:\n"
        "  Power domain (power): power-main, power-aux");
    context.configured = true;
    context.editable = true;
    DesignExtensionEditorDialog dialog(std::move(context));
    dialog.show();
    auto* editor = dialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.json"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.apply"));
    auto* state = dialog.findChild<QLabel*>(
        QStringLiteral("finepaper.designExtensions.validationState"));
    auto* diagnostics = dialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.diagnostics"));
    check(editor && apply && state && diagnostics,
          QStringLiteral(
              "Domain-aware editor exposes source, validation, and diagnostics"));
    if (!editor || !apply || !state || !diagnostics) {
        return;
    }

    editor->setPlainText(QStringLiteral(
        R"json({"bindings":[{"domain":"missing-power"}]})json"));
    check(waitUntil([state] {
              return state->text().contains(
                  QStringLiteral("Domain references are invalid"));
          })
              && !apply->isEnabled()
              && diagnostics->toPlainText().contains(
                  QStringLiteral("/bindings/0/domain"))
              && diagnostics->toPlainText().contains(
                  QStringLiteral("missing-power")),
          QStringLiteral(
              "unknown Domain id disables Apply with an exact instance pointer"));

    auto* referenceSummary = dialog.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.designExtensions.domainReferences"));
    auto* format = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.format"));
    dialog.resize(520, 480);
    QApplication::processEvents();
    check(dialog.size() == QSize(520, 480)
              && referenceSummary && referenceSummary->isVisible()
              && referenceSummary->tabChangesFocus()
              && format && referenceSummary->geometry().bottom()
                     < format->geometry().top()
              && format->geometry().bottom() < editor->geometry().top()
              && diagnostics->isVisible() && editor->isVisible()
              && editor->height() > 0 && apply->isVisible()
              && apply->mapTo(
                     &dialog, QPoint(0, apply->height())).y()
                     <= dialog.contentsRect().bottom(),
          QStringLiteral(
              "520x480 keeps Domain candidates, invalid-reference diagnostics, JSON, and actions reachable"));

    editor->setPlainText(QStringLiteral(
        R"json({"bindings":[{"domain":"clock-main"}]})json"));
    check(waitUntil([diagnostics] {
              return diagnostics->toPlainText().contains(
                  QStringLiteral("domain_reference_type_mismatch"));
          })
              && !apply->isEnabled(),
          QStringLiteral(
              "wrong-type Domain id is rejected before the Application mutation"));

    editor->setPlainText(QStringLiteral(
        R"json({"bindings":[{"domain":"power-aux"}]})json"));
    check(waitUntil([apply] { return apply->isEnabled(); }),
          QStringLiteral(
              "matching Domain id enables Apply after live reference validation"));

    editor->setPlainText(QStringLiteral(R"json({"optional":true})json"));
    check(waitUntil([apply] { return apply->isEnabled(); }),
          QStringLiteral(
              "an optional pointer with no matches remains valid for the schema to govern"));
}

void workspaceRemovalIsExplicit() {
    using namespace finepaper;

    PackageDefinition package;
    package.id = QStringLiteral("vendor.noc");
    package.version = QStringLiteral("1.0");
    package.designExtensionsDeclared = true;
    QJsonObject removalSchema = objectSchemaWithDefault();
    removalSchema.insert(
        QStringLiteral("title"),
        QStringLiteral("<b>Vendor</b><br> settings"));
    package.designExtensions = {
        definitionFor(QStringLiteral("vendor.settings"), removalSchema)};
    NocDesign design;
    design.package = PackageReference{package.id, package.version};
    design.packageData.insert(
        QStringLiteral("vendor.settings"),
        QJsonObject{{QStringLiteral("count"), 1}});

    DesignExtensionsWorkspace workspace;
    workspace.resize(800, 600);
    workspace.show();
    workspace.setContext(&design, &package);
    auto* remove = workspace.findChild<QPushButton*>(
        QStringLiteral("finepaper.designExtensions.remove"));
    check(remove && remove->isEnabled(),
          QStringLiteral("configured declared extension exposes removal"));
    if (!remove) {
        return;
    }

    int removeCalls = 0;
    QString removedId;
    workspace.removeRequested = [&](QString id) {
        ++removeCalls;
        removedId = std::move(id);
        return acceptedResult();
    };
    bool confirmationUsesPlainText = false;
    QTimer::singleShot(0, [&confirmationUsesPlainText] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* message = qobject_cast<QMessageBox*>(widget);
            if (message
                && message->objectName()
                       == QStringLiteral(
                           "finepaper.designExtensions.removeConfirmation")) {
                confirmationUsesPlainText =
                    message->textFormat() == Qt::PlainText
                    && message->text().contains(QStringLiteral("<b>Vendor</b>"));
                if (QAbstractButton* yes = message->button(QMessageBox::Yes)) {
                    yes->click();
                }
            }
        }
    });
    remove->click();
    check(removeCalls == 1
              && removedId == QStringLiteral("vendor.settings")
              && confirmationUsesPlainText,
          QStringLiteral("confirmed removal emits exactly the selected declared id"));

    workspace.setBusy(true);
    check(!remove->isEnabled(),
          QStringLiteral("busy workspace disables extension mutations"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    workspaceStateMatrix();
    dialogJsonAndTransactionSemantics();
    dialogDomainReferenceContextAndValidation();
    workspaceRemovalIsExplicit();
    QTextStream(stdout)
        << (failures == 0 ? "design-extensions-ui-tests passed"
                          : "design-extensions-ui-tests failed")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
