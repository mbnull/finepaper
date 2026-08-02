#include "features/endpoint_configuration/package_parameter_form.h"

#include "package/parameter_schema_identity.h"
#include "ui/common/schema_value_editor.h"
#include "ui/theme/ui_tokens.h"

#include <QGroupBox>
#include <QJsonValue>
#include <QLabel>
#include <QSizePolicy>
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
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(ui::UiMetrics::spacing8);
}

void PackageParameterForm::setSchema(
    const QVector<ParameterDefinition>& definitions,
    const QJsonObject& values) {
    m_schemaIdentity = parameterSchemaIdentity(definitions);
    m_definitions = definitions;
    rebuild(values);
    m_baselineDraftValues = draftValues();
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
        m_baselineDraftValues = draftValues();
    }
    notifyValueChanged();
}

bool PackageParameterForm::isModified() const {
    return draftValues() != m_baselineDraftValues;
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

PackageParameterEditorSnapshot
PackageParameterForm::editorSnapshot() const {
    PackageParameterEditorSnapshot snapshot;
    for (const Control& control : m_controls) {
        if (!control.editor) {
            continue;
        }
        const std::optional<QJsonValue> value = control.editor->value();
        if (value) {
            snapshot.values.insert(control.definition.id, *value);
        }
        snapshot.draftValues.insert(
            control.definition.id, control.editor->draftState());
        const QString label = displayLabel(control.definition);
        for (const QString& error : control.editor->localErrors()) {
            snapshot.localErrors.append(
                QStringLiteral("%1: %2").arg(label, error));
        }
    }
    snapshot.modified =
        snapshot.draftValues != m_baselineDraftValues;
    return snapshot;
}

void PackageParameterForm::rebuild(const QJsonObject& values) {
    m_controls.clear();
    if (m_content) {
        m_rootLayout->removeWidget(m_content);
        delete m_content;
    }
    m_content = new QWidget(this);
    m_content->setObjectName(m_objectNamePrefix + QStringLiteral(".content"));
    m_content->setMinimumWidth(0);
    m_content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(ui::UiMetrics::spacing8);

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
    standardContainer->setMinimumWidth(0);
    standardContainer->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* standardLayout = new QVBoxLayout(standardContainer);
    standardLayout->setContentsMargins(0, 0, 0, 0);
    standardLayout->setSpacing(ui::UiMetrics::spacing8);

    auto* advancedContainer = new QWidget(m_content);
    advancedContainer->setObjectName(
        m_objectNamePrefix + QStringLiteral(".advanced.content"));
    advancedContainer->setMinimumWidth(0);
    advancedContainer->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* advancedLayout = new QVBoxLayout(advancedContainer);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(ui::UiMetrics::spacing8);

    QHash<QString, QVBoxLayout*> standardLayouts;
    QHash<QString, QVBoxLayout*> advancedLayouts;
    int advancedCount = 0;

    auto layoutFor = [&](const ParameterDefinition& definition) {
        QHash<QString, QVBoxLayout*>& layouts = definition.advanced
            ? advancedLayouts : standardLayouts;
        QVBoxLayout* sectionLayout = definition.advanced
            ? advancedLayout : standardLayout;
        const QString category = categoryLabel(definition);
        if (QVBoxLayout* existing = layouts.value(category, nullptr)) {
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
        group->setMinimumWidth(0);
        group->setSizePolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* form = new QVBoxLayout(group);
        form->setContentsMargins(
            ui::UiMetrics::spacing8,
            ui::UiMetrics::spacing8,
            ui::UiMetrics::spacing8,
            ui::UiMetrics::spacing8);
        form->setSpacing(ui::UiMetrics::spacing8);
        sectionLayout->addWidget(group);
        layouts.insert(category, form);
        return form;
    };

    m_controls.reserve(m_definitions.size());
    for (const ParameterDefinition& definition : std::as_const(m_definitions)) {
        SchemaValueOptions options;
        options.required = true;
        options.validationMode = PropertyValidationMode::Complete;
        options.presenceSemantics =
            ValuePresenceSemantics::RequiredValue;
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
        QVBoxLayout* form = layoutFor(definition);
        auto* field = new QWidget(m_content);
        field->setObjectName(
            QStringLiteral("%1.field.%2")
                .arg(m_objectNamePrefix, definition.id));
        field->setMinimumWidth(0);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(ui::UiMetrics::spacing4);
        auto* label = new QLabel(displayLabel(definition), field);
        label->setObjectName(
            QStringLiteral("%1.label.%2")
                .arg(m_objectNamePrefix, definition.id));
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        QSizePolicy labelPolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        labelPolicy.setHeightForWidth(true);
        label->setSizePolicy(labelPolicy);
        label->setBuddy(editor->primaryInput());
        fieldLayout->addWidget(label);
        fieldLayout->addWidget(editor);
        form->addWidget(field);
        m_controls.append(Control{definition, editor});
        if (definition.advanced) {
            ++advancedCount;
        }
    }

    if (!standardLayouts.isEmpty()) {
        standardLayout->addStretch(1);
        contentLayout->addWidget(standardContainer);
    } else {
        delete standardContainer;
    }

    if (advancedCount > 0) {
        auto* toggle = new QToolButton(m_content);
        toggle->setObjectName(
            m_objectNamePrefix + QStringLiteral(".advanced.toggle"));
        toggle->setText(
            QStringLiteral("Show advanced parameters (%1)")
                .arg(advancedCount));
        toggle->setCheckable(true);
        toggle->setChecked(false);
        toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
        toggle->setArrowType(Qt::NoArrow);
        advancedContainer->hide();
        connect(toggle, &QToolButton::toggled,
                advancedContainer,
                [toggle, advancedContainer, advancedCount](bool expanded) {
            toggle->setText(
                expanded
                    ? QStringLiteral("Hide advanced parameters (%1)")
                          .arg(advancedCount)
                    : QStringLiteral("Show advanced parameters (%1)")
                          .arg(advancedCount));
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
