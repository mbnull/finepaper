#include "application/endpoint_configuration.h"
#include "features/endpoint_configuration/endpoint_configuration_panel.h"
#include "features/endpoint_configuration/package_parameter_form.h"
#include "ui/common/schema_value_editor.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
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

void respondToParameterSwitchConfirmation(
    QMessageBox::StandardButton response,
    bool* sawTextFirstConfirmation = nullptr) {
    QTimer::singleShot(0, [response, sawTextFirstConfirmation] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (!messageBox
                || messageBox->objectName()
                    != QStringLiteral(
                        "finepaper.endpointConfiguration."
                        "parameterSwitchConfirmation")) {
                continue;
            }
            if (sawTextFirstConfirmation) {
                const QAbstractButton* discard =
                    messageBox->button(QMessageBox::Discard);
                *sawTextFirstConfirmation =
                    messageBox->text().contains(
                        QStringLiteral("unapplied edits"))
                    && discard
                    && discard->text()
                           == QStringLiteral(
                               "Discard Parameter Edits and Switch");
            }
            if (QAbstractButton* button = messageBox->button(response)) {
                button->click();
            } else {
                messageBox->reject();
            }
            return;
        }
    });
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
    const QString semanticIdentity = form.schemaIdentity();
    QApplication::processEvents();

    auto* width = schemaEditor(
        &form, QStringLiteral("finepaper.testParameter.width"));
    auto* widthInput = form.findChild<QLineEdit*>(
        QStringLiteral("finepaper.schemaValue.width.scalar.text"));
    auto* widthPresence = form.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.width.present"));
    auto* widthLabel = form.findChild<QLabel*>(
        QStringLiteral("finepaper.testParameter.label.width"));
    auto* advancedToggle = form.findChild<QToolButton*>(
        QStringLiteral("finepaper.testParameter.advanced.toggle"));
    auto* advancedContent = form.findChild<QWidget*>(
        QStringLiteral("finepaper.testParameter.advanced.content"));
    check(width && widthInput && !widthPresence
              && width->toolTip().contains(QStringLiteral("Package description"))
              && width->toolTip().contains(QStringLiteral("Unit: bits"))
              && width->accessibleDescription()
                  == QStringLiteral("Package description for width")
              && widthInput->accessibleName()
                  == QStringLiteral("Arbitrary width")
              && widthInput->accessibleDescription().contains(
                  QStringLiteral("Unit: bits")),
          QStringLiteral(
              "required Package field is direct and exposes metadata on its actual input"));
    check(widthLabel && widthLabel->text() == QStringLiteral("Arbitrary width (bits)")
              && widthLabel->buddy() == widthInput
              && widthLabel->geometry().bottom() < width->geometry().top(),
          QStringLiteral(
              "Package parameter labels stay visible above their inputs in a narrow Inspector"));
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

    QVector<ParameterDefinition> presentationOnly = schema;
    presentationOnly.front().label = QStringLiteral("Relabeled width");
    presentationOnly.front().description = QStringLiteral("New help copy");
    form.setSchema(
        presentationOnly,
        QJsonObject{{QStringLiteral("width"), 64},
                    {QStringLiteral("depth"), 8}});
    check(form.schemaIdentity() == semanticIdentity,
          QStringLiteral(
              "presentation-only Package changes keep a compatible parameter schema identity"));

    form.setSchema({schema.front()}, {});
    auto* repair = form.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.width.acceptRequired"));
    check(repair && !repair->isHidden()
              && repair->text() == QStringLiteral("Use Package default")
              && !form.values().contains(QStringLiteral("width"))
              && !form.locallyValid() && !form.isModified(),
          QStringLiteral(
              "missing required Package value stays an unchanged invalid source until explicit repair"));
    if (repair) {
        repair->click();
    }
    check(form.locallyValid() && form.isModified()
              && form.values().value(QStringLiteral("width")).toInt() == 64,
          QStringLiteral(
              "required Package repair adopts its declared default as a real draft change"));
    QVector<ParameterDefinition> semanticChange = presentationOnly;
    semanticChange.front().maximum = 32;
    form.setSchema(
        semanticChange,
        QJsonObject{{QStringLiteral("width"), 32},
                    {QStringLiteral("depth"), 8}});
    check(form.schemaIdentity() != semanticIdentity,
          QStringLiteral(
              "type constraints change the shared parameter schema identity"));
}

void creationDialogOwnsIdentityParametersAndAutomaticDomains() {
    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture(package, false);
    EndpointCreationDialog dialog(
        design,
        package,
        QStringLiteral("initiator-any-name"),
        QStringLiteral("new-endpoint"),
        {QStringLiteral("detached-endpoint")});
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
    if (auto* id = dialog.findChild<QLineEdit*>(
            QStringLiteral("finepaper.endpointCreation.id"))) {
        id->setText(QStringLiteral("detached-endpoint"));
        QApplication::processEvents();
    }
    check(accept && !accept->isEnabled()
              && dialog.localErrors().join(QLatin1Char('\n'))
                     .contains(QStringLiteral("already in use")),
          QStringLiteral(
              "creation reserves IDs owned by disconnected canvas Endpoints"));
}

void targetSelectionProtectsDependentParameterDrafts() {
    const PackageDefinition package = packageFixture();
    const NocDesign design = designFixture(package, false);
    EndpointConfigurationPanel panel;
    panel.show();
    panel.planTypeChangeRequested = [&](QString endpointId,
                                        QString targetType,
                                        EndpointParameterMigration migration,
                                        QJsonObject patch) {
        return endpoint_configuration::buildTypeChangePlan(
            design, package, endpointId, targetType, migration, patch);
    };
    panel.setContext(
        &design, &package, QStringLiteral("selection-guard"),
        QStringLiteral("ep-any-name"), false);
    QApplication::processEvents();

    auto* type = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.type"));
    auto* migration = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.migration"));
    auto* width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(type && migration && width,
          QStringLiteral("selection guard fixture exposes dependent controls"));
    if (!type || !migration || !width) {
        return;
    }

    width->setValue(QJsonValue(96));
    if (width->valueChanged) {
        width->valueChanged();
    }
    bool sawTypeConfirmation = false;
    respondToParameterSwitchConfirmation(
        QMessageBox::Cancel, &sawTypeConfirmation);
    type->setCurrentIndex(type->findData(
        QStringLiteral("target-any-name")));
    QApplication::processEvents();
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(sawTypeConfirmation
              && type->currentData().toString()
                  == QStringLiteral("initiator-any-name")
              && width && width->value() && width->value()->toInt() == 96,
          QStringLiteral(
              "cancelled type switch restores the selection and parameter draft"));

    respondToParameterSwitchConfirmation(QMessageBox::Discard);
    type->setCurrentIndex(type->findData(
        QStringLiteral("target-any-name")));
    QApplication::processEvents();
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(type->currentData().toString()
              == QStringLiteral("target-any-name")
              && width && width->value() && width->value()->toInt() == 128,
          QStringLiteral(
              "explicit discard permits a type-dependent parameter rebuild"));

    width->setValue(QJsonValue(256));
    if (width->valueChanged) {
        width->valueChanged();
    }
    const int preserveIndex = migration->findData(static_cast<int>(
        EndpointParameterMigration::PreserveCompatible));
    bool sawMigrationConfirmation = false;
    respondToParameterSwitchConfirmation(
        QMessageBox::Cancel, &sawMigrationConfirmation);
    migration->setCurrentIndex(preserveIndex);
    QApplication::processEvents();
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(sawMigrationConfirmation
              && static_cast<EndpointParameterMigration>(
                     migration->currentData().toInt())
                  == EndpointParameterMigration::ResetToDefaults
              && width && width->value() && width->value()->toInt() == 256,
          QStringLiteral(
              "cancelled migration switch restores its selection and parameter draft"));

    respondToParameterSwitchConfirmation(QMessageBox::Discard);
    migration->setCurrentIndex(preserveIndex);
    QApplication::processEvents();
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(static_cast<EndpointParameterMigration>(
              migration->currentData().toInt())
              == EndpointParameterMigration::PreserveCompatible
              && width && width->value() && width->value()->toInt() == 64,
          QStringLiteral(
              "explicit discard permits a migration-dependent parameter rebuild"));
}

void conflictsPreserveDraftsUntilExplicitDiscard() {
    const PackageDefinition package = packageFixture();
    NocDesign design = designFixture(package, false);
    EndpointConfigurationPanel panel;
    panel.show();
    panel.setContext(
        &design, &package, QStringLiteral("conflict-design"),
        QStringLiteral("ep-any-name"), false);
    QApplication::processEvents();

    auto* width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    if (width) {
        width->setValue(QJsonValue(96));
        if (width->valueChanged) {
            width->valueChanged();
        }
    }
    NocDesign changedSource = design;
    changedSource.endpoints.front().parameters.insert(
        QStringLiteral("width"), 72);
    panel.setContext(
        &changedSource, &package, QStringLiteral("conflict-design"),
        QStringLiteral("ep-any-name"), false);
    QApplication::processEvents();

    auto* conflictStatus = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.endpointConfiguration.conflictStatus"));
    auto* discardConflict = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointConfiguration.discardConflict"));
    auto* type = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.type"));
    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointConfiguration.apply"));
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(panel.hasUnappliedDrafts(QStringLiteral("conflict-design"))
              && conflictStatus && conflictStatus->isVisible()
              && conflictStatus->text().contains(
                  QStringLiteral("durable parameter values changed"))
              && discardConflict && discardConflict->isEnabled()
              && type && !type->isEnabled()
              && width && !width->isEnabled()
              && width->value() && width->value()->toInt() == 72
              && apply && !apply->isEnabled(),
          QStringLiteral(
              "source mismatch preserves the draft while showing durable values read-only"));

    if (discardConflict) {
        discardConflict->click();
        QApplication::processEvents();
    }
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    check(!panel.hasUnappliedDrafts(QStringLiteral("conflict-design"))
              && conflictStatus && !conflictStatus->isVisible()
              && type && type->isEnabled()
              && width && width->isEnabled()
              && width->value() && width->value()->toInt() == 72,
          QStringLiteral(
              "explicit conflict discard resumes editing from the durable source"));

    width->setValue(QJsonValue(80));
    if (width->valueChanged) {
        width->valueChanged();
    }
    PackageDefinition changedSchema = package;
    changedSchema.endpointTypes.front().parameters[1].maximum = 2048;
    panel.setContext(
        &changedSource, &changedSchema, QStringLiteral("conflict-design"),
        QStringLiteral("ep-any-name"), false);
    QApplication::processEvents();
    conflictStatus = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.endpointConfiguration.conflictStatus"));
    discardConflict = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointConfiguration.discardConflict"));
    check(panel.hasUnappliedDrafts(QStringLiteral("conflict-design"))
              && conflictStatus && conflictStatus->isVisible()
              && conflictStatus->text().contains(
                  QStringLiteral("Package parameter schema"))
              && discardConflict && discardConflict->isEnabled(),
          QStringLiteral(
              "schema mismatch preserves the draft instead of silently erasing it"));
    if (discardConflict) {
        discardConflict->click();
    }
}

void catalogRevisionRefreshesPresentationWithoutLosingDraft() {
    PackageDefinition package = packageFixture();
    const NocDesign design = designFixture(package, false);
    const QString designIdentity = QStringLiteral("catalog-refresh");
    const QString endpointId = QStringLiteral("ep-any-name");

    EndpointConfigurationPanel panel;
    panel.show();
    panel.setContext(
        &design, &package, designIdentity, endpointId, false, 7);
    QApplication::processEvents();

    auto* width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    if (width) {
        width->setValue(QJsonValue(96));
        if (width->valueChanged) {
            width->valueChanged();
        }
    }
    check(width && panel.hasUnappliedDrafts(designIdentity),
          QStringLiteral(
              "the catalog-refresh fixture preserves an Endpoint draft"));

    package.endpointTypes.front().label =
        QStringLiteral("Renamed Initiator");
    panel.setContext(
        &design, &package, designIdentity, endpointId, false, 8);
    QApplication::processEvents();

    auto* type = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointConfiguration.type"));
    width = schemaEditor(
        &panel, QStringLiteral("finepaper.endpointParameter.width"));
    const int initiatorIndex = type
        ? type->findData(QStringLiteral("initiator-any-name")) : -1;
    check(type && initiatorIndex >= 0
              && type->itemText(initiatorIndex).contains(
                  QStringLiteral("Renamed Initiator"))
              && width && width->value() && width->value()->toInt() == 96
              && panel.hasUnappliedDrafts(designIdentity),
          QStringLiteral(
              "a new catalog revision refreshes presentation metadata from "
              "the same Package address without losing a compatible draft"));
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
    auto* parameterSection = panel.findChild<QWidget*>(
        QStringLiteral("finepaper.endpointConfiguration.parameters"));
    check(parameterSection
              && parameterSection->accessibleDescription().contains(
                  QStringLiteral("attachment line")),
          QStringLiteral(
              "Inspector keeps Endpoint parameters separate from Attachment configuration without consuming edit space"));
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
    auto* parameterScroll = panel.findChild<QScrollArea*>(
        QStringLiteral("finepaper.endpointConfiguration.parameterScroll"));
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
              && !summaryScroll && !parameterScroll,
          QStringLiteral(
              "type change exposes per-key drop/add/reset details and delegates scrolling to the Inspector host"));
    panel.resize(320, 720);
    QApplication::processEvents();
    check(migration && migrationLabel
              && migrationLabel->wordWrap()
              && !migrationLabel->geometry().intersects(
                  migration->geometry())
              && panel.minimumSizeHint().width() <= panel.width(),
          QStringLiteral(
              "narrow Inspector forms wrap long labels without overlapping their fields"));
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
    targetSelectionProtectsDependentParameterDrafts();
    conflictsPreserveDraftsUntilExplicitDiscard();
    catalogRevisionRefreshesPresentationWithoutLosingDraft();
    inspectorEditsEndpointOnlyAndPreviewsTypeImpact();
    if (failures == 0) {
        QTextStream(stdout)
            << "Endpoint configuration panel tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
