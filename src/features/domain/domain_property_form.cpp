#include "features/domain/domain_property_form.h"

#include "ui/layouts/responsive_action_layout.h"
#include "ui/theme/ui_tokens.h"

#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

#include <utility>

namespace finepaper {
namespace {

QString compactJsonValue(const QJsonValue& value) {
    QJsonArray wrapper;
    wrapper.append(value);
    QByteArray json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (json.size() >= 2) {
        json = json.mid(1, json.size() - 2);
    }
    return QString::fromUtf8(json);
}

} // namespace

DomainPropertyForm::DomainPropertyForm(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.domainPropertyForm"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_form = new QFormLayout(this);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form->setHorizontalSpacing(ui::UiMetrics::spacing8);
    m_form->setVerticalSpacing(ui::UiMetrics::spacing8);
}

void DomainPropertyForm::setSchema(
    const QVector<DomainPropertyDefinition>& definitions,
    const QVector<DomainDefinition>& draftDomains,
    const QJsonObject& values,
    DomainPropertyFormOptions formOptions) {
    clearRows();
    m_rows.reserve(definitions.size());
    m_passthroughValues = values;
    for (const DomainPropertyDefinition& definition : definitions) {
        m_passthroughValues.remove(definition.id);
    }

    if (!m_passthroughValues.isEmpty()) {
        auto* retained = new QLabel(
            QStringLiteral(
                "These stored properties are not declared by the current "
                "Package schema. They are preserved unless explicitly removed."),
            this);
        retained->setObjectName(
            QStringLiteral("finepaper.domainPropertyForm.passthrough"));
        retained->setWordWrap(true);
        retained->setMinimumWidth(0);
        m_form->addRow(retained);
        const QStringList passthroughKeys = m_passthroughValues.keys();
        for (const QString& key : passthroughKeys) {
            auto* row = new QWidget(this);
            row->setObjectName(
                QStringLiteral("finepaper.domainPropertyForm.passthrough.%1")
                    .arg(QString::fromLatin1(key.toUtf8().toHex())));
            row->setMinimumWidth(0);
            row->setSizePolicy(
                QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto* layout = new ui::ResponsiveActionLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(ui::UiMetrics::spacing8);
            auto* valueLabel = new QLabel(
                QStringLiteral("%1 = %2")
                    .arg(key, compactJsonValue(
                                  m_passthroughValues.value(key))),
                row);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valueLabel->setWordWrap(true);
            valueLabel->setMinimumWidth(0);
            valueLabel->setSizePolicy(
                QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto* remove = new QPushButton(QStringLiteral("Remove"), row);
            remove->setObjectName(
                row->objectName() + QStringLiteral(".remove"));
            remove->setProperty("finepaper.propertyId", key);
            layout->addWidget(valueLabel);
            layout->addWidget(remove);
            connect(remove, &QPushButton::clicked, this, [this, key, row] {
                m_passthroughValues.remove(key);
                row->hide();
                if (valuesChanged) {
                    valuesChanged();
                }
            });
            m_form->addRow(row);
        }
    }

    if (definitions.isEmpty()) {
        auto* empty = new QLabel(
            QStringLiteral("This Domain type has no instance properties."), this);
        empty->setObjectName(QStringLiteral("finepaper.domainPropertyForm.empty"));
        empty->setWordWrap(true);
        m_form->addRow(empty);
        return;
    }

    for (const DomainPropertyDefinition& definition : definitions) {
        SchemaValueOptions options;
        options.multiple = definition.multiple;
        options.required = definition.required;
        options.validationMode = formOptions.validationMode;
        options.referenceDomainType = definition.referenceDomainType;
        options.allowCustomReferences = formOptions.allowCustomReferences;
        if (definition.referenceDomainType) {
            for (const DomainDefinition& domain : draftDomains) {
                if (domain.type != *definition.referenceDomainType) {
                    continue;
                }
                SchemaChoice choice;
                choice.value = domain.id;
                choice.label = domain.name.trimmed().isEmpty()
                    ? domain.id
                    : QStringLiteral("%1 — %2").arg(domain.name, domain.id);
                options.choices.append(std::move(choice));
            }
        }

        auto* editor = new SchemaValueEditor(
            static_cast<const ParameterDefinition&>(definition),
            std::move(options),
            this);
        editor->setObjectName(
            QStringLiteral("finepaper.domainProperty.%1").arg(definition.id));

        std::optional<QJsonValue> initialValue;
        if (values.contains(definition.id)) {
            initialValue = values.value(definition.id);
        } else if (formOptions.initialization
                       == PropertyInitialization::CreateWithDefaults
                   && definition.hasDefault) {
            initialValue = definition.defaultValue;
        }
        const std::optional<QJsonValue> absentSuggestion =
            !initialValue && definition.hasDefault
            ? std::optional<QJsonValue>(definition.defaultValue)
            : std::nullopt;
        editor->setValue(
            std::move(initialValue), absentSuggestion);
        editor->valueChanged = [this] {
            if (valuesChanged) {
                valuesChanged();
            }
        };

        QString label = definition.label.trimmed().isEmpty()
            ? definition.id
            : definition.label;
        if (definition.required
            && formOptions.validationMode == PropertyValidationMode::Complete) {
            label += QStringLiteral(" *");
        }
        auto* labelWidget = new QLabel(label);
        labelWidget->setTextFormat(Qt::PlainText);
        labelWidget->setWordWrap(true);
        labelWidget->setMinimumWidth(0);
        QSizePolicy labelPolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        labelPolicy.setHeightForWidth(true);
        labelWidget->setSizePolicy(labelPolicy);
        labelWidget->setBuddy(editor->primaryInput());
        m_form->addRow(labelWidget, editor);
        m_rows.append(PropertyRow{definition, editor});
    }
}

void DomainPropertyForm::setSchema(
    const QVector<DomainPropertyDefinition>& definitions,
    const QVector<DomainDefinition>& draftDomains,
    const QJsonObject& values,
    PropertyInitialization initialization) {
    DomainPropertyFormOptions options;
    options.initialization = initialization;
    setSchema(definitions, draftDomains, values, options);
}

QJsonObject DomainPropertyForm::values() const {
    QJsonObject result = m_passthroughValues;
    for (const PropertyRow& row : m_rows) {
        const std::optional<QJsonValue> value = row.editor->value();
        if (value) {
            result.insert(row.definition.id, *value);
        }
    }
    return result;
}

QStringList DomainPropertyForm::localErrors() const {
    QStringList errors;
    for (const PropertyRow& row : m_rows) {
        const QString label = row.definition.label.trimmed().isEmpty()
            ? row.definition.id
            : row.definition.label;
        for (const QString& error : row.editor->localErrors()) {
            errors.append(QStringLiteral("%1: %2").arg(label, error));
        }
    }
    return errors;
}

void DomainPropertyForm::clearRows() {
    while (m_form->rowCount() > 0) {
        m_form->removeRow(m_form->rowCount() - 1);
    }
    m_rows.clear();
}

} // namespace finepaper
