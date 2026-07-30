#include "gui/domain_property_form.h"

#include <QApplication>
#include <QCheckBox>
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

    if (failures == 0) {
        QTextStream(stdout) << "Domain schema editor tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Domain schema editor test(s) failed"
                        << Qt::endl;
    return 1;
}
