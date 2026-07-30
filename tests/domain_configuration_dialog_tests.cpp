#include "gui/domain_configuration_dialog.h"

#include "application/domain_configuration.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <functional>

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

void processEventsFor(int milliseconds = 300) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    QApplication::processEvents();
}

DomainPropertyDefinition integerProperty(const QString& id,
                                         bool required = false) {
    DomainPropertyDefinition property;
    property.id = id;
    property.label = id;
    property.type = ParameterType::Integer;
    property.minimum = 1;
    property.maximum = 8;
    property.required = required;
    return property;
}

PackageDefinition domainPackage() {
    DomainTypeDefinition power;
    power.id = QStringLiteral("power");
    power.label = QStringLiteral("Power domain");
    power.appliesTo = {ElementKind::Router, ElementKind::Endpoint};
    power.cardinality = DomainCardinality::Single;

    DomainRelationDefinition poweredBy;
    poweredBy.id = QStringLiteral("poweredBy");
    poweredBy.label = QStringLiteral("Powered by");
    poweredBy.targetTypes = {QStringLiteral("power")};
    poweredBy.cardinality = DomainCardinality::Single;

    DomainTypeDefinition clock;
    clock.id = QStringLiteral("clock");
    clock.label = QStringLiteral("Clock domain");
    clock.appliesTo = {ElementKind::Router, ElementKind::Endpoint};
    clock.cardinality = DomainCardinality::Multiple;
    clock.relations = {poweredBy};
    DomainPropertyDefinition stages = integerProperty(
        QStringLiteral("stages"), true);
    stages.hasDefault = true;
    stages.defaultValue = 4;
    clock.crossingProperties = {stages};

    PackageDefinition package;
    package.format = QStringLiteral("finepaper.noc-package");
    package.formatVersion = 2;
    package.id = QStringLiteral("test.domain-configuration-dialog");
    package.name = QStringLiteral("Domain configuration dialog test");
    package.version = QStringLiteral("1.0.0");
    package.domainTypes = {power, clock};
    return package;
}

NocDesign baseDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("domain-configuration-dialog");
    design.name = QStringLiteral("Domain configuration dialog");
    design.package = PackageReference{
        QStringLiteral("test.domain-configuration-dialog"),
        QStringLiteral("1.0.0")};
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep0"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            {}}};
    return design;
}

DomainConfiguration completeConfiguration() {
    DomainConfiguration configuration;
    configuration.domains = {
        DomainDefinition{
            QStringLiteral("p0"),
            QStringLiteral("power"),
            QStringLiteral("Main power"),
            {}},
        DomainDefinition{
            QStringLiteral("c0"),
            QStringLiteral("clock"),
            QStringLiteral("Clock zero"),
            {}},
        DomainDefinition{
            QStringLiteral("c1"),
            QStringLiteral("clock"),
            QStringLiteral("Clock one"),
            {}},
        DomainDefinition{
            QStringLiteral("c2"),
            QStringLiteral("clock"),
            QStringLiteral("Clock two"),
            {}}};
    configuration.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("power"), QStringList{QStringLiteral("p0")}},
                {QStringLiteral("clock"), QStringList{QStringLiteral("c0")}}}},
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("power"), QStringList{QStringLiteral("p0")}},
                {QStringLiteral("clock"), QStringList{QStringLiteral("c1")}}}},
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")},
            QHash<QString, QStringList>{
                {QStringLiteral("power"), QStringList{QStringLiteral("p0")}},
                {QStringLiteral("clock"), QStringList{QStringLiteral("c0")}}}}};
    configuration.domainRelations = {
        DomainRelation{
            QStringLiteral("poweredBy"),
            QStringLiteral("c0"),
            QStringLiteral("p0"),
            {}}};
    configuration.crossingPolicies = {
        DomainCrossingPolicy{
            QStringLiteral("c0-to-c1"),
            QStringLiteral("clock"),
            QStringLiteral("c0"),
            QStringLiteral("c1"),
            QJsonObject{{QStringLiteral("stages"), 2}}}};
    configuration.edgeOverrides = {
        DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))},
            QStringLiteral("clock"),
            QStringLiteral("c0-to-c1"),
            {}}};
    return configuration;
}

DesignResult acceptedResult(const NocDesign& base,
                            const DomainConfiguration& configuration) {
    DesignResult result;
    result.success = true;
    result.design = domain_configuration::replace(base, configuration);
    result.design.name = QStringLiteral("validated aggregate");
    return result;
}

bool hasRelation(const DomainConfiguration& configuration,
                 const QString& type,
                 const QString& from,
                 const QString& to) {
    return std::any_of(
        configuration.domainRelations.cbegin(),
        configuration.domainRelations.cend(),
        [&](const DomainRelation& relation) {
            return relation.type == type && relation.from == from
                && relation.to == to;
        });
}

int findComboValue(QComboBox* combo, const QString& value) {
    if (!combo) {
        return -1;
    }
    int index = combo->findData(value);
    if (index >= 0) {
        return index;
    }
    return combo->findText(value, Qt::MatchContains);
}

bool chooseComboValue(QComboBox* combo, const QString& value) {
    const int index = findComboValue(combo, value);
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    return true;
}

void respondToDiscardConfirmation(bool discard) {
    QTimer::singleShot(0, [discard] {
        auto* confirmation = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        check(confirmation
                  && confirmation->objectName()
                      == QStringLiteral(
                          "finepaper.domainConfiguration.discardConfirmation"),
              QStringLiteral("dirty complete drafts use the stable discard confirmation"));
        if (!confirmation) {
            return;
        }
        auto* button = confirmation->findChild<QPushButton*>(
            discard
                ? QStringLiteral(
                      "finepaper.domainConfiguration.discardConfirmation.discard")
                : QStringLiteral(
                      "finepaper.domainConfiguration.discardConfirmation.continue"));
        check(button != nullptr,
              QStringLiteral("the discard confirmation exposes the requested action"));
        if (button) {
            button->click();
        }
    });
}

void fiveSectionsAndInitialConfigurationRoundTrip() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    const DomainConfiguration initial = completeConfiguration();

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.resize(900, 640);
    dialog.show();
    processEventsFor();

    auto* tabs = dialog.findChild<QTabWidget*>(
        QStringLiteral("finepaper.domainConfiguration.tabs"));
    auto* domains = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.domains"));
    auto* memberships = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.memberships"));
    auto* relations = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.relations"));
    auto* policies = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.policies"));
    auto* overrides = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.overrides"));

    check(dialog.objectName() == QStringLiteral("finepaper.domainConfigurationDialog")
              && tabs && domains && memberships && relations && policies
              && overrides,
          QStringLiteral("the complete editor exposes all five stable sections"));
    bool everySectionHasRecordActions = true;
    for (const QString& section : {
             QStringLiteral("domains"),
             QStringLiteral("memberships"),
             QStringLiteral("relations"),
             QStringLiteral("policies"),
             QStringLiteral("overrides")}) {
        const QString prefix = QStringLiteral("finepaper.domainConfiguration.%1")
                                   .arg(section);
        everySectionHasRecordActions = everySectionHasRecordActions
            && dialog.findChild<QPushButton*>(prefix + QStringLiteral(".add"))
            && dialog.findChild<QPushButton*>(prefix + QStringLiteral(".edit"))
            && dialog.findChild<QPushButton*>(prefix + QStringLiteral(".delete"));
    }
    check(everySectionHasRecordActions,
          QStringLiteral("every persisted array has explicit Add, Edit, and Delete working-copy actions"));
    if (tabs && domains && memberships && relations && policies && overrides) {
        check(tabs->count() == 5
                  && domains->rowCount() == initial.domains.size()
                  && memberships->rowCount()
                      == initial.domainMemberships.size()
                  && relations->rowCount() == initial.domainRelations.size()
                  && policies->rowCount() == initial.crossingPolicies.size()
                  && overrides->rowCount() == initial.edgeOverrides.size(),
              QStringLiteral("all five persisted arrays are represented without hidden rows"));
    }
    check(dialog.configuration() == initial,
          QStringLiteral("opening the complete editor round-trips all five arrays exactly"));
    dialog.close();
}

void duplicateDamagedRowsCanBeRemovedIndependently() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    const DomainConfiguration initial = completeConfiguration();
    DomainConfiguration damaged = initial;
    damaged.domains.append(damaged.domains.constFirst());
    damaged.domainMemberships.append(
        damaged.domainMemberships.constFirst());
    damaged.domainRelations.append(damaged.domainRelations.constFirst());
    damaged.crossingPolicies.append(damaged.crossingPolicies.constFirst());
    damaged.edgeOverrides.append(damaged.edgeOverrides.constFirst());

    DomainConfigurationDialog dialog(
        design,
        package,
        damaged,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.show();
    processEventsFor();

    const QStringList sections{
        QStringLiteral("domains"),
        QStringLiteral("memberships"),
        QStringLiteral("relations"),
        QStringLiteral("policies"),
        QStringLiteral("overrides")};
    bool removedEveryDuplicate = true;
    for (const QString& section : sections) {
        auto* table = dialog.findChild<QTableWidget*>(
            QStringLiteral("finepaper.domainConfiguration.%1").arg(section));
        auto* remove = dialog.findChild<QPushButton*>(
            QStringLiteral("finepaper.domainConfiguration.%1.delete")
                .arg(section));
        if (!table || !remove || table->rowCount() < 2) {
            removedEveryDuplicate = false;
            continue;
        }
        const int before = table->rowCount();
        table->selectRow(before - 1);
        QApplication::processEvents();
        remove->click();
        QApplication::processEvents();
        removedEveryDuplicate = removedEveryDuplicate
            && table->rowCount() == before - 1;
    }
    check(removedEveryDuplicate && dialog.configuration() == initial,
          QStringLiteral("stable row tokens let every duplicate damaged record be deleted without removing its identical sibling"));
    respondToDiscardConfirmation(true);
    dialog.close();
}

void meshElementsAreReferencesRatherThanEditableTopology() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    DomainConfiguration initial = completeConfiguration();
    initial.domainMemberships.clear();
    initial.edgeOverrides.clear();

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.show();
    processEventsFor();

    check(!dialog.findChild<QPushButton*>(
              QStringLiteral("finepaper.domainConfiguration.routers.add"))
              && !dialog.findChild<QPushButton*>(
                  QStringLiteral("finepaper.domainConfiguration.routers.delete"))
              && !dialog.findChild<QPushButton*>(
                  QStringLiteral("finepaper.domainConfiguration.links.add"))
              && !dialog.findChild<QPushButton*>(
                  QStringLiteral("finepaper.domainConfiguration.links.delete")),
          QStringLiteral("the Domain editor exposes no Router or Router Link CRUD"));

    auto* addMembership = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.memberships.add"));
    check(addMembership != nullptr,
          QStringLiteral("the Membership section exposes a stable Add action"));
    if (!addMembership) {
        dialog.close();
        return;
    }

    QStringList offeredElements;
    bool sawMembershipDialog = false;
    QTimer::singleShot(0, [&] {
        auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        sawMembershipDialog = editor
            && editor->objectName()
                == QStringLiteral("finepaper.domainConfiguration.membershipDialog");
        if (!editor) {
            return;
        }
        auto* element = editor->findChild<QComboBox*>(
            QStringLiteral("finepaper.domainConfiguration.membershipDialog.element"));
        check(element != nullptr,
              QStringLiteral("Membership Add exposes its derived element selector"));
        if (element) {
            for (int index = 0; index < element->count(); ++index) {
                offeredElements.append(
                    element->itemText(index) + QLatin1Char('|')
                    + element->itemData(index).toString());
            }
        }
        editor->reject();
    });
    addMembership->click();
    QApplication::processEvents();

    const QString offered = offeredElements.join(QLatin1Char('\n'));
    const QString derivedLink = linkId(
        QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
    check(sawMembershipDialog
              && offeredElements.size() == 3
              && offered.contains(QStringLiteral("r-0-0"))
              && offered.contains(QStringLiteral("r-1-0"))
              && offered.contains(QStringLiteral("ep0"))
              && !offered.contains(derivedLink),
          QStringLiteral("Membership choices are exactly Mesh-derived Routers and existing Endpoints, never Links"));
    dialog.close();
}

void aggregateValidationAndTemporaryInvalidDraft() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    const DomainConfiguration initial = completeConfiguration();
    int validationCalls = 0;

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&](const DomainConfiguration& configuration) {
            ++validationCalls;
            if (hasRelation(configuration,
                            QStringLiteral("poweredBy"),
                            QStringLiteral("c1"),
                            QStringLiteral("p0"))) {
                return acceptedResult(design, configuration);
            }
            DesignResult rejected;
            rejected.design = domain_configuration::replace(design, configuration);
            rejected.diagnostics.append(Diagnostic{
                QStringLiteral("error"),
                QStringLiteral("test.required_relation"),
                QStringLiteral("c1 needs a poweredBy relation"),
                QStringLiteral("/domainRelations"),
                QStringLiteral("test")});
            return rejected;
        });
    dialog.show();
    processEventsFor();

    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.apply"));
    auto* relations = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.relations"));
    auto* addRelation = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.relations.add"));
    check(apply && relations && addRelation,
          QStringLiteral("aggregate validation test controls are available"));
    if (!apply || !relations || !addRelation) {
        dialog.close();
        return;
    }

    check(validationCalls > 0 && !apply->isEnabled()
              && dialog.configuration() == initial
              && dialog.result() != QDialog::Accepted,
          QStringLiteral("a rejected aggregate disables Apply while preserving the entire original draft"));

    bool relationEditorCompleted = false;
    QTimer::singleShot(0, [&] {
        auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(editor
                  && editor->objectName()
                      == QStringLiteral("finepaper.domainConfiguration.relationDialog"),
              QStringLiteral("Relation Add opens the generic relation editor"));
        if (!editor) {
            return;
        }
        auto* from = editor->findChild<QComboBox*>(
            QStringLiteral("finepaper.domainConfiguration.relationDialog.from"));
        auto* type = editor->findChild<QComboBox*>(
            QStringLiteral("finepaper.domainConfiguration.relationDialog.type"));
        auto* to = editor->findChild<QComboBox*>(
            QStringLiteral("finepaper.domainConfiguration.relationDialog.to"));
        check(from && type && to,
              QStringLiteral("Relation editor exposes source, schema, and target controls"));
        if (!from || !type || !to) {
            editor->reject();
            return;
        }
        const bool selectedFrom = chooseComboValue(from, QStringLiteral("c1"));
        const bool selectedType = chooseComboValue(type, QStringLiteral("poweredBy"));
        const bool selectedTo = chooseComboValue(to, QStringLiteral("p0"));
        auto* buttons = editor->findChild<QDialogButtonBox*>();
        QPushButton* ok = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        check(selectedFrom && selectedType && selectedTo && ok && ok->isEnabled(),
              QStringLiteral("a relation can be assembled against the complete working copy"));
        if (ok && ok->isEnabled()) {
            relationEditorCompleted = true;
            ok->click();
        } else {
            editor->reject();
        }
    });
    addRelation->click();
    processEventsFor();

    const DomainConfiguration repaired = dialog.configuration();
    check(relationEditorCompleted
              && relations->rowCount() == initial.domainRelations.size() + 1
              && hasRelation(repaired,
                             QStringLiteral("poweredBy"),
                             QStringLiteral("c1"),
                             QStringLiteral("p0"))
              && apply->isEnabled(),
          QStringLiteral("a temporarily invalid draft becomes atomically valid after adding its dependent relation"));

    apply->click();
    QApplication::processEvents();
    const DesignResult& validated = dialog.validatedResult();
    check(dialog.result() == QDialog::Accepted
              && validated.success
              && validated.design.name == QStringLiteral("validated aggregate")
              && domain_configuration::fromDesign(validated.design) == repaired,
          QStringLiteral("Apply accepts only the validated aggregate and returns that authoritative DesignResult"));
}

void validationCannotFailOpenOrTruncateDiagnostics() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    const DomainConfiguration initial = completeConfiguration();

    DomainConfigurationDialog missingValidator(
        design, package, initial, {});
    missingValidator.show();
    processEventsFor();
    auto* missingApply = missingValidator.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.apply"));
    auto* missingDiagnostics = missingValidator.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.domainConfiguration.diagnostics"));
    check(missingApply && !missingApply->isEnabled()
              && missingDiagnostics
              && missingDiagnostics->toPlainText().contains(
                  QStringLiteral("domain_configuration.validator_missing")),
          QStringLiteral("a missing Application validator fails closed and explains why Apply is disabled"));
    missingValidator.close();

    DomainConfigurationDialog manyDiagnostics(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            DesignResult result;
            result.design = domain_configuration::replace(design, configuration);
            for (int index = 0; index < 650; ++index) {
                result.diagnostics.append(Diagnostic{
                    QStringLiteral("error"),
                    QStringLiteral("test.diagnostic.%1").arg(index),
                    QStringLiteral("diagnostic-%1").arg(index, 3, 10, QLatin1Char('0')),
                    QStringLiteral("/domains/%1").arg(index),
                    QStringLiteral("test")});
            }
            return result;
        });
    manyDiagnostics.show();
    processEventsFor();
    auto* completeDiagnostics = manyDiagnostics.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.domainConfiguration.diagnostics"));
    const QString text = completeDiagnostics
        ? completeDiagnostics->toPlainText() : QString();
    check(completeDiagnostics
              && text.contains(QStringLiteral("diagnostic-000"))
              && text.contains(QStringLiteral("diagnostic-649"))
              && completeDiagnostics->document()->blockCount() >= 650,
          QStringLiteral("the complete DRC view retains both the first root cause and the final diagnostic"));
    manyDiagnostics.close();
}

void dirtyDraftRequiresExplicitDiscardOrRevert() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    const DomainConfiguration initial = completeConfiguration();

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.show();
    processEventsFor();

    auto* domains = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.domains"));
    auto* removeDomain = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.domains.delete"));
    auto* revert = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.revert"));
    check(domains && removeDomain && revert,
          QStringLiteral("dirty draft confirmation controls are available"));
    if (!domains || !removeDomain || !revert) {
        dialog.close();
        return;
    }

    domains->selectRow(domains->rowCount() - 1);
    removeDomain->click();
    QApplication::processEvents();
    check(dialog.configuration() != initial && revert->isEnabled(),
          QStringLiteral("a working-copy deletion marks the complete draft as changed"));

    respondToDiscardConfirmation(false);
    dialog.close();
    check(dialog.isVisible() && dialog.configuration() != initial,
          QStringLiteral("Continue editing cancels the window close without losing the draft"));

    respondToDiscardConfirmation(false);
    dialog.reject();
    check(dialog.isVisible() && dialog.configuration() != initial,
          QStringLiteral("Continue editing also cancels the dialog Cancel action"));

    respondToDiscardConfirmation(false);
    revert->click();
    check(dialog.configuration() != initial,
          QStringLiteral("Continue editing also cancels a destructive Revert"));

    respondToDiscardConfirmation(true);
    revert->click();
    check(dialog.configuration() == initial && !revert->isEnabled(),
          QStringLiteral("explicit confirmation restores the entire five-array draft atomically"));
    dialog.close();
}

void overridePropertiesUsePartialValidation() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    const DomainConfiguration initial = completeConfiguration();

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.show();
    processEventsFor();

    auto* overrides = dialog.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainConfiguration.overrides"));
    auto* editOverride = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.overrides.edit"));
    check(overrides && editOverride && overrides->rowCount() == 1,
          QStringLiteral("the initial singleton crossing override is editable"));
    if (!overrides || !editOverride || overrides->rowCount() != 1) {
        dialog.close();
        return;
    }
    overrides->selectRow(0);
    QApplication::processEvents();

    bool partialOverrideAccepted = false;
    QTimer::singleShot(0, [&] {
        auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(editor
                  && editor->objectName()
                      == QStringLiteral("finepaper.domainConfiguration.overrideDialog"),
              QStringLiteral("Override Edit opens the crossing override editor"));
        if (!editor) {
            return;
        }
        auto* buttons = editor->findChild<QDialogButtonBox*>();
        QPushButton* ok = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        check(ok && ok->isEnabled(),
              QStringLiteral("required crossing properties remain optional in an edge override"));
        if (ok && ok->isEnabled()) {
            partialOverrideAccepted = true;
            ok->click();
        } else {
            editor->reject();
        }
    });
    editOverride->click();
    processEventsFor();

    check(partialOverrideAccepted
              && dialog.configuration().edgeOverrides.size() == 1
              && dialog.configuration().edgeOverrides.constFirst()
                     .properties.isEmpty(),
          QStringLiteral("an override round-trips with an absent required policy property under Partial validation"));
    dialog.close();
}

void newOverrideDoesNotCopyPolicyDefaults() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    DomainConfiguration initial = completeConfiguration();
    initial.edgeOverrides.clear();

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.show();
    processEventsFor();

    auto* addOverride = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.overrides.add"));
    check(addOverride && addOverride->isEnabled(),
          QStringLiteral("a singleton crossing can open the Override Add editor"));
    if (!addOverride || !addOverride->isEnabled()) {
        dialog.close();
        return;
    }

    bool overrideAdded = false;
    QTimer::singleShot(0, [&] {
        auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(editor
                  && editor->objectName()
                      == QStringLiteral("finepaper.domainConfiguration.overrideDialog"),
              QStringLiteral("Override Add opens the generic crossing editor"));
        if (!editor) {
            return;
        }
        auto* buttons = editor->findChild<QDialogButtonBox*>();
        QPushButton* ok = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        check(ok && ok->isEnabled(),
              QStringLiteral("a new partial override is valid without setting a required policy property"));
        if (ok && ok->isEnabled()) {
            overrideAdded = true;
            ok->click();
        } else {
            editor->reject();
        }
    });
    addOverride->click();
    processEventsFor();

    check(overrideAdded
              && dialog.configuration().edgeOverrides.size() == 1
              && dialog.configuration().edgeOverrides.constFirst()
                     .properties.isEmpty(),
          QStringLiteral("new overrides remain true partials and never copy crossing schema defaults"));
    respondToDiscardConfirmation(true);
    dialog.close();
}

void setValuedCrossingsAreVisibleButCannotBeOverridden() {
    const NocDesign design = baseDesign();
    const PackageDefinition package = domainPackage();
    DomainConfiguration initial = completeConfiguration();
    initial.edgeOverrides.clear();
    initial.domainMemberships[0].assignments.insert(
        QStringLiteral("clock"),
        QStringList{QStringLiteral("c0"), QStringLiteral("c2")});

    DomainConfigurationDialog dialog(
        design,
        package,
        initial,
        [&design](const DomainConfiguration& configuration) {
            return acceptedResult(design, configuration);
        });
    dialog.show();
    processEventsFor();

    auto* addOverride = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainConfiguration.overrides.add"));
    check(addOverride && addOverride->isEnabled(),
          QStringLiteral("the Override section can inspect projected crossing candidates"));
    if (!addOverride || !addOverride->isEnabled()) {
        dialog.close();
        return;
    }

    bool sawOverrideDialog = false;
    bool allCandidatesAreSetValued = true;
    bool allCandidatesAreDisabled = true;
    int candidateCount = 0;
    bool saveDisabled = false;
    QTimer::singleShot(0, [&] {
        auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        sawOverrideDialog = editor
            && editor->objectName()
                == QStringLiteral("finepaper.domainConfiguration.overrideDialog");
        if (!editor) {
            return;
        }
        auto* crossings = editor->findChild<QComboBox*>(
            QStringLiteral("finepaper.domainConfiguration.overrideDialog.edge"));
        check(crossings != nullptr,
              QStringLiteral("Override Add exposes the projected directed crossings"));
        if (crossings) {
            candidateCount = crossings->count();
            for (int index = 0; index < crossings->count(); ++index) {
                allCandidatesAreSetValued = allCandidatesAreSetValued
                    && crossings->itemText(index).contains(
                        QStringLiteral("[set-valued; override unavailable]"));
                allCandidatesAreDisabled = allCandidatesAreDisabled
                    && !(crossings->model()->flags(
                             crossings->model()->index(index, 0))
                         & Qt::ItemIsEnabled);
            }
        }
        auto* buttons = editor->findChild<QDialogButtonBox*>();
        QPushButton* ok = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
        saveDisabled = ok && !ok->isEnabled();
        editor->reject();
    });
    addOverride->click();
    QApplication::processEvents();

    check(sawOverrideDialog && candidateCount >= 1
              && allCandidatesAreSetValued && allCandidatesAreDisabled
              && saveDisabled
              && dialog.configuration().edgeOverrides.isEmpty(),
          QStringLiteral("set-valued crossings remain visible but cannot create an ambiguous single-policy override"));
    dialog.close();
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    fiveSectionsAndInitialConfigurationRoundTrip();
    duplicateDamagedRowsCanBeRemovedIndependently();
    meshElementsAreReferencesRatherThanEditableTopology();
    aggregateValidationAndTemporaryInvalidDraft();
    validationCannotFailOpenOrTruncateDiagnostics();
    dirtyDraftRequiresExplicitDiscardOrRevert();
    overridePropertiesUsePartialValidation();
    newOverrideDoesNotCopyPolicyDefaults();
    setValuedCrossingsAreVisibleButCannotBeOverridden();

    if (failures == 0) {
        QTextStream(stdout) << "All Domain configuration dialog tests passed"
                            << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
