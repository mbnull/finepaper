#include "features/domain/domain_property_form.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTextStream>

#include <optional>

namespace {

int failures = 0;

void check(bool condition, const QString& description) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << description << Qt::endl;
}

finepaper::DomainPropertyDefinition property(
    const QString& id,
    finepaper::ParameterType type,
    bool multiple = false,
    std::optional<QJsonValue> defaultValue = std::nullopt) {
    finepaper::DomainPropertyDefinition definition;
    definition.id = id;
    definition.label = id;
    definition.type = type;
    definition.multiple = multiple;
    if (defaultValue) {
        definition.hasDefault = true;
        definition.defaultValue = *defaultValue;
    }
    return definition;
}

QStringList stringItems(const QJsonValue& value) {
    QStringList result;
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray values = value.toArray();
    result.reserve(values.size());
    for (const QJsonValue& item : values) {
        result.append(item.toString());
    }
    return result;
}

void exactMissingPropertyUsesDefaultWhenFirstEnabled() {
    finepaper::DomainPropertyDefinition count = property(
        QStringLiteral("count"),
        finepaper::ParameterType::Integer,
        false,
        QJsonValue(7));

    finepaper::DomainPropertyForm form;
    form.setSchema({count},
                   {},
                   {},
                   finepaper::PropertyInitialization::ExactValues);

    check(!form.values().contains(QStringLiteral("count")),
          QStringLiteral("ExactValues preserves an initially absent property"));

    auto* presence = form.findChild<QCheckBox*>(
        QStringLiteral("finepaper.schemaValue.count.present"));
    check(presence != nullptr,
          QStringLiteral("optional scalar exposes a stable presence control"));
    if (presence) {
        presence->setChecked(true);
        QApplication::processEvents();
    }

    const QJsonObject enabled = form.values();
    check(enabled.contains(QStringLiteral("count"))
              && enabled.value(QStringLiteral("count")).isDouble()
              && enabled.value(QStringLiteral("count")).toInt() == 7,
          QStringLiteral("first enabling an absent property uses its declared default"));
}

void createDefaultsAreOverriddenBySuppliedValues() {
    finepaper::DomainPropertyDefinition count = property(
        QStringLiteral("count"),
        finepaper::ParameterType::Integer,
        false,
        QJsonValue(7));
    finepaper::DomainPropertyDefinition mode = property(
        QStringLiteral("mode"),
        finepaper::ParameterType::Enumeration,
        false,
        QJsonValue(QStringLiteral("safe")));
    mode.values = {QStringLiteral("safe"), QStringLiteral("fast")};

    finepaper::DomainPropertyForm form;
    form.setSchema(
        {count, mode},
        {},
        QJsonObject{
            {QStringLiteral("count"), 11},
            {QStringLiteral("mode"), QStringLiteral("fast")}},
        finepaper::PropertyInitialization::CreateWithDefaults);

    const QJsonObject values = form.values();
    check(values.value(QStringLiteral("count")).toInt() == 11
              && values.value(QStringLiteral("mode")).toString()
                  == QStringLiteral("fast"),
          QStringLiteral("CreateWithDefaults overlays supplied values on Package defaults"));
}

void unknownPropertiesRoundTripWithoutLoss() {
    finepaper::DomainPropertyDefinition count = property(
        QStringLiteral("count"), finepaper::ParameterType::Integer);
    const QJsonObject legacyValue{
        {QStringLiteral("vendor"), QStringLiteral("kept")},
        {QStringLiteral("enabled"), true}};
    const QJsonObject stored{
        {QStringLiteral("count"), 5},
        {QStringLiteral("legacy"), legacyValue}};

    finepaper::DomainPropertyForm form;
    form.setSchema({count},
                   {},
                   stored,
                   finepaper::PropertyInitialization::ExactValues);

    const QJsonObject roundTripped = form.values();
    check(roundTripped.value(QStringLiteral("count")).toInt() == 5
              && roundTripped.contains(QStringLiteral("legacy"))
              && roundTripped.value(QStringLiteral("legacy")).toObject()
                  == legacyValue,
          QStringLiteral("unknown stored properties survive an edit round-trip"));

    QPushButton* removeLegacy = nullptr;
    for (QPushButton* button : form.findChildren<QPushButton*>()) {
        if (button->property("finepaper.propertyId").toString()
            == QStringLiteral("legacy")) {
            removeLegacy = button;
            break;
        }
    }
    check(removeLegacy != nullptr,
          QStringLiteral("undeclared properties expose an explicit recovery action"));
    if (removeLegacy) {
        removeLegacy->click();
    }
    check(!form.values().contains(QStringLiteral("legacy")),
          QStringLiteral("an invalid undeclared property can be removed explicitly"));
}

void explicitEmptyAndFalseValuesRoundTrip() {
    const finepaper::DomainPropertyDefinition flag = property(
        QStringLiteral("flag"), finepaper::ParameterType::Boolean);
    const finepaper::DomainPropertyDefinition zero = property(
        QStringLiteral("zero"), finepaper::ParameterType::Integer);
    const finepaper::DomainPropertyDefinition text = property(
        QStringLiteral("text"), finepaper::ParameterType::String);
    const finepaper::DomainPropertyDefinition tags = property(
        QStringLiteral("tags"),
        finepaper::ParameterType::String,
        true);

    finepaper::DomainPropertyForm form;
    form.setSchema(
        {flag, zero, text, tags},
        {},
        QJsonObject{
            {QStringLiteral("flag"), false},
            {QStringLiteral("zero"), 0},
            {QStringLiteral("text"), QString()},
            {QStringLiteral("tags"), QJsonArray{}}},
        finepaper::PropertyInitialization::ExactValues);

    const QJsonObject values = form.values();
    check(values.contains(QStringLiteral("flag"))
              && values.value(QStringLiteral("flag")).isBool()
              && !values.value(QStringLiteral("flag")).toBool(),
          QStringLiteral("explicit false remains present and false"));
    check(values.contains(QStringLiteral("zero"))
              && values.value(QStringLiteral("zero")).isDouble()
              && values.value(QStringLiteral("zero")).toDouble() == 0.0,
          QStringLiteral("explicit zero remains present and numeric"));
    check(values.contains(QStringLiteral("text"))
              && values.value(QStringLiteral("text")).isString()
              && values.value(QStringLiteral("text")).toString().isEmpty(),
          QStringLiteral("explicit empty string remains present"));
    check(values.contains(QStringLiteral("tags"))
              && values.value(QStringLiteral("tags")).isArray()
              && values.value(QStringLiteral("tags")).toArray().isEmpty(),
          QStringLiteral("explicit empty array remains present"));
}

void multipleRowsAreSelectableAndOperableThroughTheirEditors() {
    const finepaper::DomainPropertyDefinition sequence = property(
        QStringLiteral("sequence"),
        finepaper::ParameterType::String,
        true);

    finepaper::DomainPropertyForm form;
    form.resize(520, 320);
    form.setSchema(
        {sequence},
        {},
        QJsonObject{{QStringLiteral("sequence"),
                     QJsonArray{QStringLiteral("alpha"),
                                QStringLiteral("beta"),
                                QStringLiteral("gamma")}}},
        finepaper::PropertyInitialization::ExactValues);
    form.show();
    QApplication::processEvents();

    auto* table = form.findChild<QTableWidget*>(
        QStringLiteral("finepaper.schemaValue.sequence.items"));
    auto* remove = form.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.sequence.remove"));
    auto* add = form.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.sequence.add"));
    auto* moveUp = form.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.sequence.up"));
    auto* moveDown = form.findChild<QPushButton*>(
        QStringLiteral("finepaper.schemaValue.sequence.down"));
    check(table && remove && add && moveUp && moveDown,
          QStringLiteral("multiple editor exposes stable table and action controls"));
    if (!table || !remove || !add || !moveUp || !moveDown
        || table->rowCount() != 3) {
        form.close();
        return;
    }

    QWidget* secondRow = table->cellWidget(1, 0);
    QLineEdit* secondValue = secondRow
        ? secondRow->findChild<QLineEdit*>()
        : nullptr;
    check(secondValue != nullptr,
          QStringLiteral("multiple string row exposes its scalar child editor"));
    if (secondValue) {
        secondValue->setFocus(Qt::MouseFocusReason);
        QApplication::processEvents();
    }
    check(table->currentRow() == 1 && moveUp->isEnabled()
              && moveDown->isEnabled() && remove->isEnabled(),
          QStringLiteral("focusing a row scalar selects that row for actions"));

    moveUp->click();
    QApplication::processEvents();
    check(stringItems(form.values().value(QStringLiteral("sequence")))
              == QStringList{QStringLiteral("beta"),
                             QStringLiteral("alpha"),
                             QStringLiteral("gamma")},
          QStringLiteral("Up moves the selected multiple value without reordering others"));

    moveDown->click();
    QApplication::processEvents();
    check(stringItems(form.values().value(QStringLiteral("sequence")))
              == QStringList{QStringLiteral("alpha"),
                             QStringLiteral("beta"),
                             QStringLiteral("gamma")},
          QStringLiteral("Down moves the selected multiple value back"));

    remove->click();
    QApplication::processEvents();
    check(stringItems(form.values().value(QStringLiteral("sequence")))
              == QStringList{QStringLiteral("alpha"),
                             QStringLiteral("gamma")},
          QStringLiteral("Remove deletes the row selected through its scalar editor"));

    add->click();
    QApplication::processEvents();
    QSet<QString> itemObjectNames;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (QWidget* editor = table->cellWidget(row, 0)) {
            itemObjectNames.insert(editor->objectName());
        }
    }
    check(itemObjectNames.size() == table->rowCount(),
          QStringLiteral("Remove followed by Add keeps every multiple row objectName unique"));
    form.close();
}

void completeAndPartialModesMatchAggregateValidationSemantics() {
    finepaper::DomainPropertyDefinition requiredCount = property(
        QStringLiteral("count"), finepaper::ParameterType::Integer);
    requiredCount.required = true;

    finepaper::DomainPropertyForm completeForm;
    finepaper::DomainPropertyFormOptions completeOptions;
    completeOptions.initialization =
        finepaper::PropertyInitialization::ExactValues;
    completeOptions.validationMode =
        finepaper::PropertyValidationMode::Complete;
    completeForm.setSchema({requiredCount}, {}, {}, completeOptions);
    check(!completeForm.locallyValid(),
          QStringLiteral("Complete mode requires absent schema-required properties"));

    finepaper::DomainPropertyForm partialForm;
    finepaper::DomainPropertyFormOptions partialOptions;
    partialOptions.initialization =
        finepaper::PropertyInitialization::ExactValues;
    partialOptions.validationMode =
        finepaper::PropertyValidationMode::Partial;
    partialForm.setSchema({requiredCount}, {}, {}, partialOptions);
    check(partialForm.locallyValid()
              && !partialForm.values().contains(QStringLiteral("count")),
          QStringLiteral("Partial mode permits a schema-required property to be absent"));

    finepaper::DomainPropertyDefinition requiredTags = property(
        QStringLiteral("tags"), finepaper::ParameterType::String, true);
    requiredTags.required = true;
    partialForm.setSchema(
        {requiredTags},
        {},
        QJsonObject{{QStringLiteral("tags"), QJsonArray{}}},
        partialOptions);
    check(!partialForm.locallyValid(),
          QStringLiteral("Partial mode still rejects an explicitly set empty required list"));
}

void customReferencesSupportWorkingCopyForwardReferences() {
    finepaper::DomainPropertyDefinition parent = property(
        QStringLiteral("parent"), finepaper::ParameterType::String);
    parent.referenceDomainType = QStringLiteral("clock");

    const QVector<finepaper::DomainDefinition> domains{
        finepaper::DomainDefinition{
            QStringLiteral("clock-a"),
            QStringLiteral("clock"),
            QStringLiteral("Clock A"),
            {}}};
    const QJsonObject forwardReference{
        {QStringLiteral("parent"), QStringLiteral("clock-future")}};

    finepaper::DomainPropertyForm strictForm;
    strictForm.setSchema(
        {parent},
        domains,
        forwardReference,
        finepaper::PropertyInitialization::ExactValues);
    auto* strictChoice = strictForm.findChild<QComboBox*>(
        QStringLiteral("finepaper.schemaValue.parent.scalar.choice"));
    check(strictChoice && !strictChoice->isEditable()
              && !strictForm.locallyValid()
              && strictForm.values().value(QStringLiteral("parent")).toString()
                  == QStringLiteral("clock-future"),
          QStringLiteral("strict quick editor preserves but rejects an unavailable reference"));

    finepaper::DomainPropertyFormOptions workingCopyOptions;
    workingCopyOptions.initialization =
        finepaper::PropertyInitialization::ExactValues;
    workingCopyOptions.allowCustomReferences = true;
    finepaper::DomainPropertyForm workingCopyForm;
    workingCopyForm.setSchema(
        {parent}, domains, forwardReference, workingCopyOptions);
    auto* customChoice = workingCopyForm.findChild<QComboBox*>(
        QStringLiteral("finepaper.schemaValue.parent.scalar.choice"));
    auto* customInput = workingCopyForm.findChild<QLineEdit*>(
        QStringLiteral(
            "finepaper.schemaValue.parent.scalar.choice.customReference"));
    check(customChoice && customChoice->isEditable() && customInput
              && workingCopyForm.locallyValid()
              && workingCopyForm.values().value(QStringLiteral("parent")).toString()
                  == QStringLiteral("clock-future"),
          QStringLiteral("working-copy editor accepts a future reference as an editable Domain ID"));
    if (customInput) {
        customInput->setText(QStringLiteral("clock-mutual"));
    }
    check(workingCopyForm.locallyValid()
              && workingCopyForm.values().value(QStringLiteral("parent")).toString()
                  == QStringLiteral("clock-mutual"),
          QStringLiteral("custom reference input returns the typed stable Domain ID"));

    finepaper::DomainPropertyForm rawRecoveryForm;
    rawRecoveryForm.setSchema(
        {parent},
        domains,
        QJsonObject{{QStringLiteral("parent"), 17}},
        workingCopyOptions);
    check(!rawRecoveryForm.locallyValid()
              && rawRecoveryForm.values().value(QStringLiteral("parent"))
                  == QJsonValue(17),
          QStringLiteral("custom references still preserve and reject a wrong-type raw value"));
    auto* recoveryChoice = rawRecoveryForm.findChild<QComboBox*>(
        QStringLiteral("finepaper.schemaValue.parent.scalar.choice"));
    if (recoveryChoice) {
        recoveryChoice->setCurrentIndex(
            recoveryChoice->findData(QStringLiteral("clock-a")));
    }
    check(rawRecoveryForm.locallyValid()
              && rawRecoveryForm.values().value(QStringLiteral("parent")).toString()
                  == QStringLiteral("clock-a"),
          QStringLiteral("choosing a valid reference explicitly recovers from raw stored data"));
}

} // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QApplication application(argc, argv);

    exactMissingPropertyUsesDefaultWhenFirstEnabled();
    createDefaultsAreOverriddenBySuppliedValues();
    unknownPropertiesRoundTripWithoutLoss();
    explicitEmptyAndFalseValuesRoundTrip();
    multipleRowsAreSelectableAndOperableThroughTheirEditors();
    completeAndPartialModesMatchAggregateValidationSemantics();
    customReferencesSupportWorkingCopyForwardReferences();

    if (failures == 0) {
        QTextStream(stdout) << "Domain schema editor tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Domain schema editor test(s) failed"
                        << Qt::endl;
    return 1;
}
