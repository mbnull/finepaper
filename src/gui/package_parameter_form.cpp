#include "gui/package_parameter_form.h"

#include "gui/schema_value_editor.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QJsonValue>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace finepaper {
namespace {

QString displayLabel(const ParameterDefinition& definition) {
    const QString label = definition.label.trimmed().isEmpty()
        ? definition.id : definition.label;
    return definition.unit.trimmed().isEmpty()
        ? label
        : QStringLiteral("%1 (%2)").arg(label, definition.unit);
}

QString categoryLabel(const ParameterDefinition& definition) {
    return definition.category.trimmed().isEmpty()
        ? QStringLiteral("General") : definition.category;
}

} // namespace

PackageParameterForm::PackageParameterForm(
    QString objectNamePrefix,
    QWidget* parent)
    : QWidget(parent),
      m_objectNamePrefix(std::move(objectNamePrefix)) {
    setObjectName(m_objectNamePrefix + QStringLiteral(".form"));
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(8);
}

void PackageParameterForm::setSchema(
    const QVector<ParameterDefinition>& definitions,
    const QJsonObject& values) {
    m_definitions = definitions;
    rebuild(values);
    m_baselineValues = this->values();
}

void PackageParameterForm::setValues(const QJsonObject& values) {
    loadValues(values, true);
}

void PackageParameterForm::setDraftValues(
    const PackageParameterDraft& values) {
    for (Control& control : m_controls) {
        if (!control.editor) {
            continue;
        }
        const auto draft = values.constFind(control.definition.id);
        if (draft != values.cend()) {
            control.editor->setDraftState(*draft);
            continue;
        }
        control.editor->setValue(
            std::nullopt,
            control.definition.hasDefault
                ? std::optional<QJsonValue>(control.definition.defaultValue)
                : std::nullopt);
    }
    notifyValueChanged();
}

void PackageParameterForm::loadValues(const QJsonObject& values,
                                      bool resetBaseline) {
    for (Control& control : m_controls) {
        if (!control.editor) {
            continue;
        }
        const QJsonValue stored = values.value(control.definition.id);
        const std::optional<QJsonValue> suggestion =
            control.definition.hasDefault
            ? std::optional<QJsonValue>(control.definition.defaultValue)
            : std::nullopt;
        control.editor->setValue(
            stored.isUndefined()
                ? std::nullopt
                : std::optional<QJsonValue>(stored),
            suggestion);
    }
    if (resetBaseline) {
        m_baselineValues = this->values();
    }
    notifyValueChanged();
}

QJsonObject PackageParameterForm::values() const {
    QJsonObject result;
    for (const Control& control : m_controls) {
        if (!control.editor) {
            continue;
        }
        const std::optional<QJsonValue> value = control.editor->value();
        if (value) {
            result.insert(control.definition.id, *value);
        }
    }
    return result;
}

PackageParameterDraft PackageParameterForm::draftValues() const {
    PackageParameterDraft result;
    for (const Control& control : m_controls) {
        if (control.editor) {
            result.insert(
                control.definition.id, control.editor->draftState());
        }
    }
    return result;
}

QStringList PackageParameterForm::localErrors() const {
    QStringList errors;
    for (const Control& control : m_controls) {
        if (!control.editor) {
            continue;
        }
        const QString label = displayLabel(control.definition);
        for (const QString& error : control.editor->localErrors()) {
            errors.append(QStringLiteral("%1: %2").arg(label, error));
        }
    }
    return errors;
}

void PackageParameterForm::rebuild(const QJsonObject& values) {
    m_controls.clear();
    if (m_content) {
        m_rootLayout->removeWidget(m_content);
        delete m_content;
    }
    m_content = new QWidget(this);
    m_content->setObjectName(m_objectNamePrefix + QStringLiteral(".content"));
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);

    if (m_definitions.isEmpty()) {
        auto* empty = new QLabel(
            QStringLiteral("This Package declares no parameters here."),
            m_content);
        empty->setObjectName(m_objectNamePrefix + QStringLiteral(".empty"));
        empty->setWordWrap(true);
        contentLayout->addWidget(empty);
        m_rootLayout->addWidget(m_content);
        return;
    }

    auto* standardContainer = new QWidget(m_content);
    standardContainer->setObjectName(
        m_objectNamePrefix + QStringLiteral(".standard"));
    auto* standardLayout = new QVBoxLayout(standardContainer);
    standardLayout->setContentsMargins(0, 0, 0, 0);
    standardLayout->setSpacing(8);

    auto* advancedContainer = new QWidget(m_content);
    advancedContainer->setObjectName(
        m_objectNamePrefix + QStringLiteral(".advanced.content"));
    auto* advancedLayout = new QVBoxLayout(advancedContainer);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(8);

    QHash<QString, QFormLayout*> standardForms;
    QHash<QString, QFormLayout*> advancedForms;
    int advancedCount = 0;

    auto formFor = [&](const ParameterDefinition& definition) {
        QHash<QString, QFormLayout*>& forms = definition.advanced
            ? advancedForms : standardForms;
        QVBoxLayout* sectionLayout = definition.advanced
            ? advancedLayout : standardLayout;
        const QString category = categoryLabel(definition);
        if (QFormLayout* existing = forms.value(category, nullptr)) {
            return existing;
        }
        auto* group = new QGroupBox(category,
                                    definition.advanced
                                        ? advancedContainer
                                        : standardContainer);
        group->setObjectName(
            QStringLiteral("%1.category.%2.%3")
                .arg(m_objectNamePrefix,
                     definition.advanced
                         ? QStringLiteral("advanced")
                         : QStringLiteral("standard"),
                     category));
        auto* form = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        sectionLayout->addWidget(group);
        forms.insert(category, form);
        return form;
    };

    m_controls.reserve(m_definitions.size());
    for (const ParameterDefinition& definition : std::as_const(m_definitions)) {
        SchemaValueOptions options;
        options.required = true;
        options.validationMode = PropertyValidationMode::Complete;
        auto* editor = new SchemaValueEditor(definition, options, m_content);
        editor->setObjectName(
            QStringLiteral("%1.%2").arg(m_objectNamePrefix, definition.id));
        editor->setProperty("finepaper.parameterId", definition.id);
        editor->valueChanged = [this] { notifyValueChanged(); };
        const QJsonValue stored = values.value(definition.id);
        editor->setValue(
            stored.isUndefined()
                ? std::nullopt
                : std::optional<QJsonValue>(stored),
            definition.hasDefault
                ? std::optional<QJsonValue>(definition.defaultValue)
                : std::nullopt);
        formFor(definition)->addRow(displayLabel(definition), editor);
        m_controls.append(Control{definition, editor});
        if (definition.advanced) {
            ++advancedCount;
        }
    }

    if (!standardForms.isEmpty()) {
        standardLayout->addStretch(1);
        contentLayout->addWidget(standardContainer);
    } else {
        delete standardContainer;
    }

    if (advancedCount > 0) {
        auto* toggle = new QToolButton(m_content);
        toggle->setObjectName(
            m_objectNamePrefix + QStringLiteral(".advanced.toggle"));
        toggle->setText(QStringLiteral("Advanced (%1)").arg(advancedCount));
        toggle->setCheckable(true);
        toggle->setChecked(false);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        advancedContainer->hide();
        connect(toggle, &QToolButton::toggled,
                advancedContainer, [toggle, advancedContainer](bool expanded) {
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            advancedContainer->setVisible(expanded);
        });
        advancedLayout->addStretch(1);
        contentLayout->addWidget(toggle);
        contentLayout->addWidget(advancedContainer);
    } else {
        delete advancedContainer;
    }
    contentLayout->addStretch(1);
    m_rootLayout->addWidget(m_content);
}

void PackageParameterForm::notifyValueChanged() {
    if (valueChanged) {
        valueChanged();
    }
}

} // namespace finepaper
