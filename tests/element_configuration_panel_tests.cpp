#include "features/element_configuration/element_configuration_panel.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTextStream>

#include <atomic>
#include <cstdio>
#include <optional>

namespace {

using namespace finepaper;

int failures = 0;
std::atomic_uint formLayoutWarningCount = 0;
std::atomic<QtMessageHandler> messageHandlerToForward = nullptr;

void captureFormLayoutWarnings(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message) {
    if (type == QtWarningMsg
        && message.startsWith(
            QStringLiteral("QFormLayout::takeAt: Invalid index"))) {
        formLayoutWarningCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (const QtMessageHandler handler = messageHandlerToForward.load(
            std::memory_order_acquire)) {
        handler(type, context, message);
        return;
    }
    std::fprintf(
        stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
}

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

ElementPropertyDefinition integerProperty(const QString& id,
                                          int defaultValue) {
    ElementPropertyDefinition property;
    property.id = id;
    property.label = QStringLiteral("Arbitrary %1").arg(id);
    property.type = ParameterType::Integer;
    property.hasDefault = true;
    property.defaultValue = defaultValue;
    property.minimum = 0;
    property.maximum = 64;
    return property;
}

ElementPropertySetDefinition propertySet(
    const QString& id,
    const QVector<ElementKind>& appliesTo,
    const QStringList& endpointTypes = {}) {
    ElementPropertySetDefinition definition;
    definition.id = id;
    definition.label = QStringLiteral("Schema %1").arg(id);
    definition.appliesTo = appliesTo;
    definition.endpointTypes = endpointTypes;
    definition.properties = {
        integerProperty(QStringLiteral("pipeline"), 2)
    };
    return definition;
}

PackageDefinition packageFixture() {
    PackageDefinition package;
    package.format = QStringLiteral("finepaper.noc-package");
    package.formatVersion = 3;
    package.id = QStringLiteral("test.element-configuration-panel");
    package.name = QStringLiteral("Element Configuration Panel");
    package.version = QStringLiteral("1.0.0");
    package.elementPropertySets = {
        propertySet(QStringLiteral("vendor.router-implementation"),
                    {ElementKind::Router}),
        propertySet(QStringLiteral("vendor.mesh-link"),
                    {ElementKind::RouterLink}),
        propertySet(QStringLiteral("vendor.attachment-common"),
                    {ElementKind::EndpointAttachment}),
        propertySet(QStringLiteral("vendor.client-attachment"),
                    {ElementKind::EndpointAttachment},
                    {QStringLiteral("client")})
    };
    return package;
}

NocDesign designFixture() {
    NocDesign design;
    design.formatVersion = 3;
    design.id = QStringLiteral("element-configuration-panel");
    design.package = PackageReference{
        QStringLiteral("test.element-configuration-panel"),
        QStringLiteral("1.0.0")};
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep-client"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            {}},
        EndpointInstance{
            QStringLiteral("ep-memory"),
            QStringLiteral("memory"),
            EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
            {}}
    };
    design.elementConfigurations = {
        ElementConfiguration{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QStringLiteral("vendor.router-implementation"),
            QJsonObject{{QStringLiteral("pipeline"), 4}}}
    };
    return design;
}

QLineEdit* pipelineEditor(ElementConfigurationPanel& panel) {
    return panel.findChild<QLineEdit*>(
        QStringLiteral("finepaper.schemaValue.pipeline.scalar.text"));
}

ElementConfigurationContextStamp contextStamp(
    const QString& designIdentity = {},
    quint64 designRevision = 1,
    quint64 packageCatalogRevision = 1) {
    return {
        .designIdentity = designIdentity,
        .designRevision = designRevision,
        .packageCatalogRevision = packageCatalogRevision,
    };
}

void editPipeline(ElementConfigurationPanel& panel, const QString& text) {
    QLineEdit* editor = pipelineEditor(panel);
    check(editor != nullptr,
          QStringLiteral("Element draft fixture exposes the pipeline editor"));
    if (!editor) {
        return;
    }
    editor->setText(text);
    QMetaObject::invokeMethod(
        editor,
        "textEdited",
        Qt::DirectConnection,
        Q_ARG(QString, text));
    QApplication::processEvents();
}

void draftsSurviveContextChangesAndFailClosedOnConflicts() {
    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture();
    const ElementRef router{
        ElementKind::Router, QStringLiteral("r-0-0")};
    const ElementRef link{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))};
    const QString sessionA = QStringLiteral("element-session-a");
    const QString sessionB = QStringLiteral("element-session-b");

    ElementConfigurationPanel panel;
    panel.resize(520, 520);
    panel.show();
    panel.setContext(&design, &package, router, contextStamp(sessionA));
    QApplication::processEvents();

    editPipeline(panel, QStringLiteral("6"));
    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    check(apply && apply->isEnabled()
              && panel.hasUnappliedDrafts(sessionA)
              && panel.hasUnappliedDraft(sessionA, router),
          QStringLiteral(
              "a valid Element edit creates a session-scoped Router draft"));

    panel.setContext(&design, &package, link, contextStamp(sessionA));
    QApplication::processEvents();
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("2"),
          QStringLiteral(
              "switching selection shows the selected Link's authoritative value"));
    panel.setContext(&design, &package, router, contextStamp(sessionA));
    QApplication::processEvents();
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("6")
              && apply && apply->isEnabled(),
          QStringLiteral(
              "returning to a Router restores its unapplied Element draft"));

    editPipeline(panel, QStringLiteral("1e"));
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    check(apply && !apply->isEnabled()
              && panel.hasUnappliedDraft(sessionA, router),
          QStringLiteral(
              "an invalid numeric token remains an explicit non-applicable draft"));
    panel.setContext(&design, &package, link, contextStamp(sessionA));
    panel.setContext(&design, &package, router, contextStamp(sessionA));
    QApplication::processEvents();
    auto* draftStatus = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.draftStatus"));
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("1e")
              && draftStatus && draftStatus->isVisible()
              && draftStatus->text().contains(
                  QStringLiteral("validation errors")),
          QStringLiteral(
              "selection changes restore an invalid Element token verbatim"));

    panel.setContext(&design, &package, router, contextStamp(sessionB));
    QApplication::processEvents();
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("4")
              && !panel.hasUnappliedDrafts(sessionB)
              && panel.hasUnappliedDraft(sessionA, router),
          QStringLiteral(
              "a different document session cannot observe the previous session's Element draft"));
    panel.setContext(&design, &package, router, contextStamp(sessionA));
    QApplication::processEvents();
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("1e"),
          QStringLiteral(
              "returning to the original session restores only that session's raw draft"));

    NocDesign changedDesign = design;
    changedDesign.elementConfigurations.front().properties.insert(
        QStringLiteral("pipeline"), 5);
    panel.setContext(
        &changedDesign, &package, router, contextStamp(sessionA));
    QApplication::processEvents();
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    draftStatus = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.draftStatus"));
    auto* discard = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.discardDraft"));
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("1e")
              && panel.hasUnappliedDraft(sessionA, router)
              && apply && !apply->isEnabled()
              && draftStatus && draftStatus->isVisible()
              && draftStatus->property("finepaperRole").toString()
                  == QStringLiteral("error")
              && draftStatus->text().contains(
                  QStringLiteral("Draft conflict"))
              && discard && discard->isVisible() && discard->isEnabled(),
          QStringLiteral(
              "an authoritative source change preserves the draft read-only and exposes an explicit conflict"));

    if (discard) {
        discard->click();
        QApplication::processEvents();
    }
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    draftStatus = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.draftStatus"));
    discard = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.discardDraft"));
    check(!panel.hasUnappliedDrafts(sessionA)
              && pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("5")
              && apply && !apply->isEnabled()
              && draftStatus && !draftStatus->isVisible()
              && discard && !discard->isVisible(),
          QStringLiteral(
              "explicit conflict discard loads the new authoritative value and clears only the session draft"));

    editPipeline(panel, QStringLiteral("7"));
    PackageDefinition changedPackage = package;
    changedPackage.elementPropertySets.front().properties.front().maximum = 5;
    panel.setContext(
        &changedDesign, &changedPackage, router, contextStamp(sessionA));
    QApplication::processEvents();
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    draftStatus = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.draftStatus"));
    discard = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.discardDraft"));
    check(panel.hasUnappliedDraft(sessionA, router)
              && pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("5")
              && apply && !apply->isEnabled()
              && draftStatus && draftStatus->isVisible()
              && draftStatus->text().contains(QStringLiteral("Package schema"))
              && discard && discard->isEnabled(),
          QStringLiteral(
              "a semantic Package schema change preserves the old Element draft as a read-only conflict"));
    if (discard) {
        discard->click();
        QApplication::processEvents();
    }
    check(!panel.hasUnappliedDrafts(sessionA)
              && pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("5"),
          QStringLiteral(
              "discarding a schema-conflicted Element draft accepts the new schema and durable value"));
}

void identicalContextPreservesEditorAndRevisionsForceRefresh() {
    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture();
    const ElementRef router{
        ElementKind::Router, QStringLiteral("r-0-0")};
    const QString session = QStringLiteral("element-stable-context");

    ElementConfigurationPanel panel;
    panel.resize(520, 520);
    panel.show();
    for (int iteration = 0; iteration < 100; ++iteration) {
        panel.setContext(
            &design, &package, router, contextStamp(session, 11, 7));
    }
    QApplication::processEvents();

    QLineEdit* initialEditor = pipelineEditor(panel);
    check(initialEditor != nullptr,
          QStringLiteral("revision fixture exposes the pipeline editor"));
    if (!initialEditor) {
        return;
    }
    panel.activateWindow();
    // The offscreen Qt platform does not honour QWidget::activateWindow().
    // Force an active test window so this regression covers focus retention,
    // not a platform capability that the test backend intentionally lacks.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&panel);
    QT_WARNING_POP
    QApplication::processEvents();
    initialEditor->setFocus(Qt::OtherFocusReason);
    editPipeline(panel, QStringLiteral("1e"));
    initialEditor->setCursorPosition(1);
    QApplication::processEvents();
    check(initialEditor->hasFocus(),
          QStringLiteral("revision fixture can focus the active editor"));

    panel.setContext(
        &design, &package, router, contextStamp(session, 11, 7));
    QApplication::processEvents();
    check(pipelineEditor(panel) == initialEditor
              && initialEditor->text() == QStringLiteral("1e")
              && initialEditor->cursorPosition() == 1
              && initialEditor->hasFocus(),
          QStringLiteral(
              "an identical borrowed context preserves editor identity, focus, cursor and raw draft"));

    QPointer<QLineEdit> designRevisionEditor(initialEditor);
    panel.setContext(
        &design, &package, router, contextStamp(session, 12, 7));
    QApplication::processEvents();
    QLineEdit* refreshedEditor = pipelineEditor(panel);
    check(designRevisionEditor.isNull()
              && refreshedEditor
              && refreshedEditor->text() == QStringLiteral("1e"),
          QStringLiteral(
              "a design revision rebuilds the editor while restoring its protected draft"));

    QPointer<QLineEdit> catalogRevisionEditor(refreshedEditor);
    panel.setContext(
        &design, &package, router, contextStamp(session, 12, 8));
    QApplication::processEvents();
    check(catalogRevisionEditor.isNull()
              && pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("1e"),
          QStringLiteral(
              "a Package catalog revision also forces a schema refresh without losing the draft"));

    const ElementRef link{
        ElementKind::RouterLink, QStringLiteral("r-0-0--r-1-0")};
    for (int iteration = 0; iteration < 100; ++iteration) {
        panel.setContext(
            &design, &package, link, contextStamp(session, 12, 8));
        panel.setContext(
            &design, &package, router, contextStamp(session, 12, 8));
    }
    QApplication::processEvents();
    check(pipelineEditor(panel)
              && pipelineEditor(panel)->text() == QStringLiteral("1e")
              && panel.findChildren<QLineEdit*>().size() == 1,
          QStringLiteral(
              "repeated Router and Link selection rebuilds keep one editor and restore the raw draft"));
}

void resolutionErrorsSurviveBusyTransitions() {
    const PackageDefinition package = packageFixture();
    NocDesign design = designFixture();
    design.elementConfigurations.front().properties.insert(
        QStringLiteral("unknown-property"), 1);
    const ElementRef router{
        ElementKind::Router, QStringLiteral("r-0-0")};

    ElementConfigurationPanel panel;
    panel.resize(520, 520);
    panel.show();
    panel.setContext(
        &design,
        &package,
        router,
        contextStamp(QStringLiteral("element-resolution-failure")));
    QApplication::processEvents();

    auto* status = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.status"));
    auto* form = panel.findChild<QWidget*>(
        QStringLiteral("finepaper.elementConfiguration.form"));
    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    auto* reset = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.reset"));
    check(status && status->isVisible()
              && status->text().contains(
                  QStringLiteral("could not be resolved"))
              && form && !form->isEnabled()
              && !pipelineEditor(panel)
              && apply && !apply->isEnabled()
              && reset && reset->isEnabled(),
          QStringLiteral(
              "an invalid stored Element configuration fails closed while retaining its safe Reset recovery"));

    std::optional<QString> resetPropertySet;
    panel.resetRequested = [&](ElementRef element, QString propertySetId) {
        check(element == router,
              QStringLiteral(
                  "invalid override recovery retains the exact element"));
        resetPropertySet = std::move(propertySetId);
    };
    if (reset) {
        reset->click();
    }
    check(resetPropertySet
              == QStringLiteral("vendor.router-implementation"),
          QStringLiteral(
              "invalid override exposes a working reset-to-default request"));

    panel.setBusy(true);
    panel.setBusy(false);
    QApplication::processEvents();
    check(status && status->isVisible()
              && status->text().contains(
                  QStringLiteral("could not be resolved"))
              && form && !form->isEnabled()
              && !pipelineEditor(panel)
              && apply && !apply->isEnabled()
              && reset && reset->isEnabled(),
          QStringLiteral(
              "the resolution diagnostic and reset recovery survive a busy-to-idle transition"));
}

void sparseOverrideOriginAndDefaultRecoveryAreExplicit() {
    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture();
    const ElementRef router{
        ElementKind::Router, QStringLiteral("r-0-0")};
    const ElementRef link{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))};

    ElementConfigurationPanel panel;
    panel.resize(360, 560);
    panel.show();
    panel.setContext(
        &design,
        &package,
        router,
        contextStamp(QStringLiteral("element-origin")));
    QApplication::processEvents();

    auto* origin = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.origin.pipeline"));
    auto* useDefault = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.useDefault.pipeline"));
    auto* presence = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.pipeline.present"));
    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    QLineEdit* input = pipelineEditor(panel);
    QLabel* fieldLabel = nullptr;
    for (QLabel* label : panel.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Arbitrary pipeline")) {
            fieldLabel = label;
            break;
        }
    }
    check(origin && origin->text() == QStringLiteral("Design override")
              && useDefault && useDefault->isVisible()
              && !presence && input
              && input->accessibleName()
                  == QStringLiteral("Arbitrary pipeline")
              && fieldLabel && fieldLabel->buddy() == input,
          QStringLiteral(
              "Element field exposes its sparse origin, actual input label, and explicit default action without Set"));

    std::optional<QJsonObject> appliedValues;
    panel.applyRequested = [&](ElementRef, QString, QJsonObject values) {
        appliedValues = std::move(values);
    };
    if (useDefault) {
        useDefault->click();
        QApplication::processEvents();
    }
    check(input && input->text() == QStringLiteral("2")
              && origin
              && origin->text() == QStringLiteral("Package default")
              && useDefault && !useDefault->isVisible()
              && apply && apply->isEnabled(),
          QStringLiteral(
              "Use Package default creates one valid removal of the durable override"));
    if (apply) {
        apply->click();
    }
    check(appliedValues
              && appliedValues->value(QStringLiteral("pipeline")).toInt() == 2,
          QStringLiteral(
              "default recovery emits the effective value for sparse backend normalization"));

    panel.setContext(
        &design,
        &package,
        link,
        contextStamp(QStringLiteral("element-origin")));
    QApplication::processEvents();
    origin = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.origin.pipeline"));
    useDefault = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.useDefault.pipeline"));
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    check(origin && origin->text() == QStringLiteral("Package default")
              && useDefault && !useDefault->isVisible()
              && apply && !apply->isEnabled(),
          QStringLiteral(
              "a pure Package default begins clean and cannot manufacture a no-op Apply"));
    editPipeline(panel, QStringLiteral("6"));
    check(origin
              && origin->text()
                  == QStringLiteral("Unapplied Design override")
              && useDefault && useDefault->isVisible()
              && apply && apply->isEnabled(),
          QStringLiteral(
              "editing a default value exposes an unapplied sparse override"));
    useDefault->click();
    QApplication::processEvents();
    check(origin && origin->text() == QStringLiteral("Package default")
              && apply && !apply->isEnabled(),
          QStringLiteral(
              "returning an edit to its Package default restores a clean draft"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    const QtMessageHandler previousMessageHandler =
        qInstallMessageHandler(captureFormLayoutWarnings);
    messageHandlerToForward.store(
        previousMessageHandler, std::memory_order_release);

    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture();
    const ElementRef router{
        ElementKind::Router, QStringLiteral("r-0-0")};
    const ElementRef link{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))};
    const ElementRef clientAttachment{
        ElementKind::EndpointAttachment, QStringLiteral("ep-client")};
    const ElementRef memoryAttachment{
        ElementKind::EndpointAttachment, QStringLiteral("ep-memory")};

    const ElementConfigurationPanelProjection routerProjection =
        projectElementConfigurationPanel(&design, &package, router);
    check(routerProjection.ready()
              && routerProjection.propertySetIds
                  == QStringList{QStringLiteral("vendor.router-implementation")},
          QStringLiteral("Router property sets are projected from arbitrary Package ids"));
    check(projectElementConfigurationPanel(&design, &package, link)
              .propertySetIds
              == QStringList{QStringLiteral("vendor.mesh-link")},
          QStringLiteral("Router Link receives only its Package-declared property sets"));
    check(projectElementConfigurationPanel(
              &design, &package, clientAttachment).propertySetIds
              == QStringList{
                  QStringLiteral("vendor.attachment-common"),
                  QStringLiteral("vendor.client-attachment")},
          QStringLiteral("Endpoint Attachment combines generic and endpoint-type schemas"));
    check(projectElementConfigurationPanel(
              &design, &package, memoryAttachment).propertySetIds
              == QStringList{QStringLiteral("vendor.attachment-common")},
          QStringLiteral("endpointTypes filters Attachment schemas without fixed type names"));
    check(projectElementConfigurationPanel(
              &design,
              &package,
              ElementRef{ElementKind::Endpoint, QStringLiteral("ep-client")})
              .state == ElementConfigurationPanelState::UnsupportedSelection,
          QStringLiteral("Endpoint instances stay outside the element configuration model"));

    NocDesign v2Design = design;
    v2Design.formatVersion = 2;
    check(projectElementConfigurationPanel(&v2Design, &package, router).state
              == ElementConfigurationPanelState::UnsupportedFormat,
          QStringLiteral("V2 Designs expose an explicit read-only capability state"));
    PackageDefinition noSets = package;
    noSets.elementPropertySets.clear();
    check(projectElementConfigurationPanel(&design, &noSets, router).state
              == ElementConfigurationPanelState::NoApplicablePropertySets,
          QStringLiteral("a V3 Package may explicitly expose no applicable set"));

    ElementConfigurationPanel panel;
    panel.resize(520, 520);
    panel.show();
    panel.setContext(&design, &package, router, contextStamp());
    QApplication::processEvents();

    auto* selector = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.elementConfiguration.propertySet"));
    auto* status = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.status"));
    auto* overrideState = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.elementConfiguration.overrideState"));
    auto* pipeline = panel.findChild<QLineEdit*>(
        QStringLiteral("finepaper.schemaValue.pipeline.scalar.text"));
    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    auto* reset = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.reset"));
    check(selector && status && overrideState && pipeline && apply && reset,
          QStringLiteral("Element Configuration exposes stable schema-driven controls"));
    if (!selector || !status || !overrideState || !pipeline || !apply || !reset) {
        return 1;
    }

    check(selector->count() == 1
              && selector->currentData().toString()
                  == QStringLiteral("vendor.router-implementation")
              && pipeline->text() == QStringLiteral("4")
              && overrideState->text().contains(
                  QStringLiteral("1 property overridden")),
          QStringLiteral("Inspector displays Package defaults plus the current sparse override"));
    check(!panel.findChild<QWidget*>(
              QStringLiteral("finepaper.elementConfiguration.scroll")),
          QStringLiteral(
              "Element Configuration delegates scrolling to its Inspector host"));
    check(status->text().contains(QStringLiteral("fixed Mesh"))
              && !apply->isEnabled() && reset->isEnabled(),
          QStringLiteral("Router Inspector keeps the Mesh boundary visible and avoids no-op apply"));

    std::optional<QJsonObject> appliedValues = std::nullopt;
    std::optional<QString> appliedSet = std::nullopt;
    panel.applyRequested = [&](ElementRef element,
                               QString propertySetId,
                               QJsonObject values) {
        check(element == router,
              QStringLiteral("Apply retains the exact semantic Router identity"));
        appliedSet = std::move(propertySetId);
        appliedValues = std::move(values);
    };
    pipeline->setText(QStringLiteral("6"));
    QMetaObject::invokeMethod(
        pipeline,
        "textEdited",
        Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("6")));
    QApplication::processEvents();
    check(apply->isEnabled(),
          QStringLiteral("a valid effective-value edit enables Apply"));
    apply->click();
    check(appliedSet == QStringLiteral("vendor.router-implementation")
              && appliedValues
              && appliedValues->value(QStringLiteral("pipeline")).toInt() == 6,
          QStringLiteral("Apply emits schema-generic effective values for atomic mutation"));

    std::optional<QString> resetSet = std::nullopt;
    panel.resetRequested = [&](ElementRef element, QString propertySetId) {
        check(element == router,
              QStringLiteral("Reset retains the exact semantic Router identity"));
        resetSet = std::move(propertySetId);
    };
    reset->click();
    check(resetSet == QStringLiteral("vendor.router-implementation"),
          QStringLiteral("Reset requests deletion of the selected sparse set"));

    panel.setBusy(true);
    QApplication::processEvents();
    check(!selector->isEnabled() && !apply->isEnabled() && !reset->isEnabled()
              && status->text().contains(QStringLiteral("Read-only")),
          QStringLiteral("busy operations retain visible values in an explicit read-only state"));

    panel.setContext(
        &design,
        &package,
        ElementRef{ElementKind::Endpoint, QStringLiteral("ep-client")},
        contextStamp());
    QApplication::processEvents();
    check(status->text().contains(QStringLiteral("Endpoint instance parameters")),
          QStringLiteral("Endpoint selection points users to its separate parameter path"));
    check(formLayoutWarningCount.load(std::memory_order_relaxed) == 0,
          QStringLiteral(
              "rebuilding the Package-driven form never probes an invalid QFormLayout row"));
    draftsSurviveContextChangesAndFailClosedOnConflicts();
    identicalContextPreservesEditorAndRevisionsForceRefresh();
    resolutionErrorsSurviveBusyTransitions();
    sparseOverrideOriginAndDefaultRecoveryAreExplicit();
    qInstallMessageHandler(previousMessageHandler);
    messageHandlerToForward.store(nullptr, std::memory_order_release);

    if (failures == 0) {
        QTextStream(stdout) << "Element configuration panel tests passed"
                            << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
