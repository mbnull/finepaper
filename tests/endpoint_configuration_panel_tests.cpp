#include "application/endpoint_configuration.h"
#include "gui/endpoint_configuration_panel.h"
#include "gui/package_parameter_form.h"
#include "ui/common/schema_value_editor.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextStream>
#include <QToolButton>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& description) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << description << Qt::endl;
}

SchemaValueEditor* schemaEditor(QObject* parent, const QString& objectName) {
    return parent
        ? static_cast<SchemaValueEditor*>(
              parent->findChild<QWidget*>(objectName))
        : nullptr;
}

ParameterDefinition integerParameter(
    const QString& id,
    int defaultValue,
    const QString& category = {},
    bool advanced = false) {
    ParameterDefinition definition;
    definition.id = id;
    definition.label = QStringLiteral("Arbitrary %1").arg(id);
    definition.description = QStringLiteral("Package description for %1").arg(id);
    definition.unit = QStringLiteral("bits");
    definition.category = category;
    definition.advanced = advanced;
    definition.type = ParameterType::Integer;
    definition.hasDefault = true;
    definition.defaultValue = defaultValue;
    definition.minimum = 1;
    definition.maximum = 1024;
    return definition;
}

ParameterDefinition enumParameter(const QString& id,
                                  const QString& defaultValue) {
    ParameterDefinition definition;
    definition.id = id;
    definition.label = QStringLiteral("Arbitrary %1").arg(id);
    definition.type = ParameterType::Enumeration;
    definition.values = {QStringLiteral("axi4"), QStringLiteral("chi")};
    definition.hasDefault = true;
    definition.defaultValue = defaultValue;
    return definition;
}

DomainTypeDefinition requiredEndpointDomainType() {
    DomainTypeDefinition type;
    type.id = QStringLiteral("clock-any-name");
    type.label = QStringLiteral("Clock selection");
    type.appliesTo = {ElementKind::Endpoint};
    type.cardinality = DomainCardinality::Single;
    type.required = true;
    return type;
}

PackageDefinition packageFixture() {
    PackageDefinition package;
    package.format = QStringLiteral("finepaper.noc-package");
    package.formatVersion = 3;
    package.id = QStringLiteral("test.endpoint-ui");
    package.name = QStringLiteral("Endpoint UI Fixture");
    package.version = QStringLiteral("1.0.0");

    EndpointTypeDefinition initiator;
    initiator.id = QStringLiteral("initiator-any-name");
    initiator.label = QStringLiteral("Initiator");
    initiator.parameters = {
        enumParameter(QStringLiteral("protocol"), QStringLiteral("axi4")),
        integerParameter(QStringLiteral("width"),
                         64,
                         QStringLiteral("Interface")),
        integerParameter(QStringLiteral("queue-limit"),
                         8,
                         QStringLiteral("Buffering"),
                         true)};

    EndpointTypeDefinition target;
    target.id = QStringLiteral("target-any-name");
    target.label = QStringLiteral("Target");
    target.parameters = {
        enumParameter(QStringLiteral("protocol"), QStringLiteral("chi")),
        integerParameter(QStringLiteral("width"),
                         128,
                         QStringLiteral("Interface")),
        integerParameter(QStringLiteral("response-depth"),
                         4,
                         QStringLiteral("Buffering"),
                         true)};
    package.endpointTypes = {initiator, target};
    package.domainTypes = {requiredEndpointDomainType()};

    ElementPropertyDefinition attachmentProperty;
    static_cast<ParameterDefinition&>(attachmentProperty) =
        integerParameter(QStringLiteral("lanes"), 1);
    attachmentProperty.multiple = false;
    ElementPropertySetDefinition initiatorAttachment;
    initiatorAttachment.id = QStringLiteral("attachment.initiator-only");
    initiatorAttachment.label = QStringLiteral("Initiator attachment");
    initiatorAttachment.appliesTo = {ElementKind::EndpointAttachment};
    initiatorAttachment.endpointTypes = {initiator.id};
    initiatorAttachment.properties = {attachmentProperty};
    package.elementPropertySets = {initiatorAttachment};
    return package;
}

NocDesign designFixture(const PackageDefinition& package,
                        bool withAttachmentConfiguration = true) {
    NocDesign design;
    design.format = QStringLiteral("finepaper.noc-design");
    design.formatVersion = 3;
    design.name = QStringLiteral("endpoint-ui");
    design.package = {package.id, package.version};
    design.topology = {QStringLiteral("mesh"), 1, 1};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep-any-name"),
            QStringLiteral("initiator-any-name"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            QJsonObject{
                {QStringLiteral("protocol"), QStringLiteral("axi4")},
                {QStringLiteral("width"), 64},
                {QStringLiteral("queue-limit"), 8}}}};
    design.domains = {
        DomainDefinition{
            QStringLiteral("clock-only"),
            QStringLiteral("clock-any-name"),
            QStringLiteral("Only clock"),
            {}}};
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep-any-name")},
            {{QStringLiteral("clock-any-name"),
              {QStringLiteral("clock-only")}}}}};
    if (withAttachmentConfiguration) {
        design.elementConfigurations = {
            ElementConfiguration{
                ElementRef{ElementKind::EndpointAttachment,
                           QStringLiteral("ep-any-name")},
                QStringLiteral("attachment.initiator-only"),
                QJsonObject{{QStringLiteral("lanes"), 2}}}};
    }
    return design;
}

void genericFormUsesSchemaMetadataAndTracksDrafts() {
    PackageParameterForm form(QStringLiteral("finepaper.testParameter"));
    form.show();
    const QVector<ParameterDefinition> schema{
        integerParameter(QStringLiteral("width"),
                         64,
                         QStringLiteral("Interface")),
        integerParameter(QStringLiteral("depth"),
                         8,
                         QStringLiteral("Buffering"),
                         true)};
    form.setSchema(
        schema,
        QJsonObject{{QStringLiteral("width"), 64},
                    {QStringLiteral("depth"), 8}});
    QApplication::processEvents();

    auto* width = schemaEditor(
        &form, QStringLiteral("finepaper.testParameter.width"));
    auto* advancedToggle = form.findChild<QToolButton*>(
        QStringLiteral("finepaper.testParameter.advanced.toggle"));
    auto* advancedContent = form.findChild<QWidget*>(
        QStringLiteral("finepaper.testParameter.advanced.content"));
    check(width
              && width->toolTip().contains(QStringLiteral("Package description"))
              && width->toolTip().contains(QStringLiteral("Unit: bits"))
              && width->accessibleDescription()
                  == QStringLiteral("Package description for width"),
          QStringLiteral("SchemaValueEditor exposes Package description and unit metadata"));
    check(form.findChild<QGroupBox*>(
              QStringLiteral("finepaper.testParameter.category.standard.Interface"))
              && advancedToggle && advancedContent && !advancedContent->isVisible(),
          QStringLiteral("generic form groups normal fields and collapses Advanced fields"));
    if (advancedToggle && advancedContent) {
        advancedToggle->click();
        QApplication::processEvents();
        check(advancedContent->isVisible(),
              QStringLiteral("Advanced Package fields can be expanded without id knowledge"));
    }
    check(form.locallyValid() && !form.isModified(),
          QStringLiteral("loaded complete values begin valid and unchanged"));
    if (width) {
        width->setValue(QJsonValue(96));
        if (width->valueChanged) {
            width->valueChanged();
        }
    }
    check(form.values().value(QStringLiteral("width")).toInt() == 96
              && form.isModified(),
          QStringLiteral("generic form returns typed JSON and detects a real draft change"));
}

void creationDialogOwnsIdentityParametersAndAutomaticDomains() {
    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture(package, false);
    EndpointCreationDialog dialog(
        design,
        package,
        QStringLiteral("initiator-any-name"),
        QStringLiteral("new-endpoint"));
    auto* tabs = dialog.findChild<QTabWidget*>(
        QStringLiteral("finepaper.endpointCreation.tabs"));
    auto* type = dialog.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointCreation.type"));
    auto* accept = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointCreation.accept"));
    check(tabs && tabs->count() == 1
              && dialog.findChild<QLabel*>(
                     QStringLiteral("finepaper.endpointCreation.automaticDomains")),
          QStringLiteral("one required Domain instance is auto-assigned without a second modal"));
    check(accept && accept->isEnabled(),
          QStringLiteral("Package defaults produce a valid Endpoint creation draft"));
    EndpointCreationDraft draft = dialog.draft();
    check(draft.id == QStringLiteral("new-endpoint")
              && draft.type == QStringLiteral("initiator-any-name")
              && draft.parameters.value(QStringLiteral("width")).toInt() == 64
              && draft.domainAssignments
                     .value(QStringLiteral("clock-any-name"))
                  == QStringList{QStringLiteral("clock-only")},
          QStringLiteral("creation draft includes id, type, parameters and Domain assignments"));

    if (type) {
        type->setCurrentIndex(type->findData(
            QStringLiteral("target-any-name")));
        QApplication::processEvents();
    }
    draft = dialog.draft();
    check(draft.type == QStringLiteral("target-any-name")
              && draft.parameters.value(QStringLiteral("protocol")).toString()
                  == QStringLiteral("chi")
              && draft.parameters.value(QStringLiteral("response-depth")).toInt() == 4
              && !draft.parameters.contains(QStringLiteral("queue-limit")),
          QStringLiteral("creation type selection rebuilds parameters from the selected Package schema"));

    if (auto* id = dialog.findChild<QLineEdit*>(
            QStringLiteral("finepaper.endpointCreation.id"))) {
        id->setText(QStringLiteral("ep-any-name"));
        QApplication::processEvents();
    }
    check(accept && !accept->isEnabled()
              && dialog.localErrors().join(QLatin1Char('\n'))
                     .contains(QStringLiteral("already in use")),
          QStringLiteral("creation validates the editable Endpoint ID before mutation"));
}

void inspectorEditsEndpointOnlyAndPreviewsTypeImpact() {
    const PackageDefinition package = packageFixture();
    NocDesign design = designFixture(package);
    EndpointConfigurationPanel panel;
    panel.show();
    panel.planTypeChangeRequested = [&](
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migration,
        QJsonObject patch) {
        return endpoint_configuration::buildTypeChangePlan(
            design, package, endpointId, targetType, migration, patch);
    };
    QString updatedEndpoint;
    QJsonObject updatedParameters;
    panel.updateParametersRequested = [&](QString endpointId,
                                          QJsonObject parameters) {
        updatedEndpoint = std::move(endpointId);
        updatedParameters = std::move(parameters);
    };
    bool changedType = false;
    panel.changeTypeRequested = [&](QString,
                                    QString,
                                    EndpointParameterMigration,
                                    QJsonObject,
                                    EndpointTypeChangeImpactConfirmation) {
        changedType = true;
    };
    panel.setContext(
        &design,
        &package,
        QStringLiteral("design-a"),
        QStringLiteral("ep-any-name"),
        false);
    QApplication::processEvents();

    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointConfiguration.apply"));
    auto* width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(apply && !apply->isEnabled(),
          QStringLiteral("unchanged Endpoint values do not create a dirty no-op"));
    auto* attachmentNote = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.endpointConfiguration.attachmentNote"));
    check(attachmentNote
              && attachmentNote->text().contains(QStringLiteral("attachment line")),
          QStringLiteral("Inspector keeps Endpoint parameters separate from Attachment configuration"));
    if (width) {
        width->setValue(QJsonValue(96));
        if (width->valueChanged) {
            width->valueChanged();
        }
    }
    check(apply && apply->isEnabled(),
          QStringLiteral("a valid Endpoint parameter draft enables atomic Apply"));
    check(panel.hasUnappliedDrafts(QStringLiteral("design-a"))
              && panel.unappliedDraftEndpointIds(QStringLiteral("design-a"))
                  == QStringList{QStringLiteral("ep-any-name")},
          QStringLiteral("Endpoint edits become explicit design-scoped drafts"));

    panel.setBusy(true);
    panel.setBusy(false);
    panel.setContext(
        &design,
        &package,
        QStringLiteral("design-a"),
        std::nullopt,
        false);
    panel.setContext(
        &design,
        &package,
        QStringLiteral("design-a"),
        QStringLiteral("ep-any-name"),
        false);
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(width && width->value() && width->value()->toInt() == 96,
          QStringLiteral("selection Endpoint to Router and back restores the unapplied draft"));

    NocDesign moved = design;
    moved.endpoints.front().attachment.router = RouterPosition{7, 9};
    panel.setContext(
        &moved,
        &package,
        QStringLiteral("design-a"),
        QStringLiteral("ep-any-name"),
        false);
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(width && width->value() && width->value()->toInt() == 96,
          QStringLiteral("attachment moves do not rebuild or erase an Endpoint parameter draft"));
    if (apply) {
        apply->click();
    }
    check(updatedEndpoint == QStringLiteral("ep-any-name")
              && updatedParameters.value(QStringLiteral("width")).toInt() == 96
              && updatedParameters.contains(QStringLiteral("protocol"))
              && updatedParameters.contains(QStringLiteral("queue-limit")),
          QStringLiteral("same-type Apply emits the complete effective Endpoint parameter object"));
    check(panel.hasUnappliedDrafts(QStringLiteral("design-a")),
          QStringLiteral("a failed or non-adopted Apply callback preserves the draft"));
    panel.discardDraft(
        QStringLiteral("design-a"), QStringLiteral("ep-any-name"));
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(!panel.hasUnappliedDrafts(QStringLiteral("design-a"))
              && width && width->value() && width->value()->toInt() == 64
              && apply && !apply->isEnabled(),
          QStringLiteral("explicit successful-adoption discard clears cache and resets the visible draft"));

    auto* widthText = panel.findChild<QLineEdit*>(
        QStringLiteral("finepaper.schemaValue.width.scalar.text"));
    check(widthText && width,
          QStringLiteral("numeric Endpoint editor exposes its raw text control"));
    if (widthText && width) {
        widthText->setText(QStringLiteral("1e"));
        if (width->valueChanged) {
            width->valueChanged();
        }
    }
    check(widthText && widthText->text() == QStringLiteral("1e"),
          QStringLiteral("test enters an invalid numeric token"));
    check(panel.hasUnappliedDrafts(QStringLiteral("design-a")),
          QStringLiteral("invalid numeric input is captured before selection changes"));
    panel.setContext(
        &design,
        &package,
        QStringLiteral("design-a"),
        std::nullopt,
        false);
    panel.setContext(
        &design,
        &package,
        QStringLiteral("design-a"),
        QStringLiteral("ep-any-name"),
        false);
    widthText = panel.findChild<QLineEdit*>(
        QStringLiteral("finepaper.schemaValue.width.scalar.text"));
    check(widthText && widthText->text() == QStringLiteral("1e"),
          QStringLiteral("selection changes preserve an invalid numeric token verbatim"));
    check(panel.hasUnappliedDrafts(QStringLiteral("design-a")),
          QStringLiteral("an invalid numeric token remains an explicit Endpoint draft"));
    check(apply && !apply->isEnabled(),
          QStringLiteral("an invalid restored Endpoint draft cannot be applied"));
    panel.discardDraft(
        QStringLiteral("design-a"), QStringLiteral("ep-any-name"));

    NocDesign refreshed = design;
    panel.planTypeChangeRequested = [&](
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migration,
        QJsonObject patch) {
        return endpoint_configuration::buildTypeChangePlan(
            refreshed, package, endpointId, targetType, migration, patch);
    };
    panel.setContext(
        &refreshed,
        &package,
        QStringLiteral("design-b"),
        QStringLiteral("ep-any-name"),
        false);
    auto* type = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.type"));
    if (type) {
        type->setCurrentIndex(type->findData(
            QStringLiteral("target-any-name")));
        QApplication::processEvents();
    }
    auto* migration = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.migration"));
    auto* migrationLabel = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.endpointConfiguration.migrationLabel"));
    auto* summary = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.endpointConfiguration.typeChangeSummary"));
    auto* summaryScroll = panel.findChild<QScrollArea*>(
        QStringLiteral("finepaper.endpointConfiguration.typeChangeSummaryScroll"));
    check(migration && migration->isVisible()
              && migrationLabel && migrationLabel->isVisible()
              && summary
              && summary->text().contains(QStringLiteral("Drop"))
              && summary->text().contains(QStringLiteral("queue-limit"))
              && summary->text().contains(QStringLiteral("Add/default"))
              && summary->text().contains(QStringLiteral("response-depth"))
              && summary->text().contains(QStringLiteral("Change/reset"))
              && summary->text().contains(QStringLiteral("protocol"))
              && !summary->text().contains(QStringLiteral("<b>Preserve</b>"))
              && summaryScroll && summaryScroll->maximumHeight() <= 180,
          QStringLiteral("type change exposes bounded per-key drop/add/reset details without listing every preserved value"));
    check(apply && apply->isEnabled() && !changedType,
          QStringLiteral("a valid type-change preview is actionable but not applied implicitly"));

    NocDesign noImpact = designFixture(package, false);
    panel.planTypeChangeRequested = [&](
        QString endpointId,
        QString targetType,
        EndpointParameterMigration migrationValue,
        QJsonObject patch) {
        return endpoint_configuration::buildTypeChangePlan(
            noImpact,
            package,
            endpointId,
            targetType,
            migrationValue,
            patch);
    };
    panel.setContext(
        &noImpact,
        &package,
        QStringLiteral("design-c"),
        QStringLiteral("ep-any-name"),
        false);
    type = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.type"));
    if (type) {
        type->setCurrentIndex(type->findData(
            QStringLiteral("target-any-name")));
        QApplication::processEvents();
    }
    apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointConfiguration.apply"));
    if (apply) {
        apply->click();
    }
    check(changedType,
          QStringLiteral("impact-free type change reaches the Application callback directly"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    genericFormUsesSchemaMetadataAndTracksDrafts();
    creationDialogOwnsIdentityParametersAndAutomaticDomains();
    inspectorEditsEndpointOnlyAndPreviewsTypeImpact();
    if (failures == 0) {
        QTextStream(stdout)
            << "Endpoint configuration panel tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
