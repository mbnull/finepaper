#include "gui/schema_value_editor.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace finepaper {
namespace {

constexpr int invalidChoiceRole = Qt::UserRole + 1;
constexpr int invalidRawChoiceRole = Qt::UserRole + 2;

QString compactJsonValue(const QJsonValue& value) {
    QJsonArray wrapper;
    wrapper.append(value);
    QByteArray json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (json.size() >= 2) {
        json = json.mid(1, json.size() - 2);
    }
    return QString::fromUtf8(json);
}

QString displayChoiceLabel(const SchemaChoice& choice) {
    return choice.label.trimmed().isEmpty() ? choice.value : choice.label;
}

} // namespace

class SchemaValueEditor::ScalarValueWidget final : public QWidget {
public:
    ScalarValueWidget(ParameterDefinition definition,
                      SchemaValueOptions options,
                      QString objectNamePrefix,
                      QWidget* parent = nullptr)
        : QWidget(parent),
          m_definition(std::move(definition)),
          m_options(std::move(options)),
          m_objectNamePrefix(std::move(objectNamePrefix)) {
        setObjectName(m_objectNamePrefix);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        if (m_options.referenceDomainType
            || m_definition.type == ParameterType::Enumeration) {
            m_combo = new QComboBox(this);
            m_combo->installEventFilter(this);
            m_combo->setObjectName(m_objectNamePrefix + QStringLiteral(".choice"));
            if (m_options.referenceDomainType) {
                if (m_options.allowCustomReferences) {
                    m_combo->setEditable(true);
                    m_combo->setInsertPolicy(QComboBox::NoInsert);
                    m_editableComboLineEdit = m_combo->lineEdit();
                    m_editableComboLineEdit->setObjectName(
                        m_objectNamePrefix
                        + QStringLiteral(".choice.customReference"));
                    m_editableComboLineEdit->setPlaceholderText(
                        QStringLiteral("Domain ID"));
                    m_editableComboLineEdit->installEventFilter(this);
                }
                for (const SchemaChoice& choice : m_options.choices) {
                    m_combo->addItem(displayChoiceLabel(choice), choice.value);
                    m_combo->setItemData(m_combo->count() - 1,
                                         !choice.valid,
                                         invalidChoiceRole);
                }
            } else {
                for (const QString& entry : m_definition.values) {
                    m_combo->addItem(entry, entry);
                }
            }
            layout->addWidget(m_combo, 1);
            connect(m_combo, &QComboBox::currentIndexChanged, this, [this] {
                if (m_updating) {
                    return;
                }
                m_preserveInvalidRaw = false;
                notifyChanged();
            });
            if (m_editableComboLineEdit) {
                connect(m_editableComboLineEdit, &QLineEdit::textEdited,
                        this, [this] {
                    if (m_updating) {
                        return;
                    }
                    m_preserveInvalidRaw = false;
                    notifyChanged();
                });
            }
            return;
        }

        if (m_definition.type == ParameterType::Boolean) {
            m_boolean = new QCheckBox(this);
            m_boolean->installEventFilter(this);
            m_boolean->setObjectName(m_objectNamePrefix + QStringLiteral(".boolean"));
            layout->addWidget(m_boolean);
            layout->addStretch();
            connect(m_boolean, &QCheckBox::checkStateChanged, this, [this] {
                if (m_updating) {
                    return;
                }
                m_preserveInvalidRaw = false;
                notifyChanged();
            });
            return;
        }

        m_lineEdit = new QLineEdit(this);
        m_lineEdit->installEventFilter(this);
        m_lineEdit->setObjectName(m_objectNamePrefix + QStringLiteral(".text"));
        if (m_definition.type == ParameterType::Integer) {
            m_lineEdit->setPlaceholderText(QStringLiteral("Integer"));
        } else if (m_definition.type == ParameterType::Number) {
            m_lineEdit->setPlaceholderText(QStringLiteral("Number"));
        }
        layout->addWidget(m_lineEdit, 1);
        connect(m_lineEdit, &QLineEdit::textEdited, this, [this] {
            if (m_updating) {
                return;
            }
            m_preserveInvalidRaw = false;
            notifyChanged();
        });
    }

    void setValue(const QJsonValue& value) {
        m_updating = true;
        m_preserveInvalidRaw = false;
        m_invalidRaw = {};

        if (m_combo) {
            for (int index = m_combo->count() - 1; index >= 0; --index) {
                if (m_combo->itemData(index, invalidRawChoiceRole).toBool()
                    || (m_combo->itemData(index, invalidChoiceRole).toBool()
                        && index >= baseChoiceCount())) {
                    m_combo->removeItem(index);
                }
            }
            if (value.isString()) {
                const QString stored = value.toString();
                int index = m_combo->findData(stored);
                if (index < 0 && m_options.referenceDomainType
                    && m_options.allowCustomReferences) {
                    m_combo->setCurrentIndex(-1);
                    m_combo->setEditText(stored);
                } else if (index < 0) {
                    m_combo->addItem(
                        QStringLiteral("Unsupported: %1").arg(stored), stored);
                    index = m_combo->count() - 1;
                    m_combo->setItemData(index, true, invalidChoiceRole);
                    m_combo->setCurrentIndex(index);
                } else {
                    m_combo->setCurrentIndex(index);
                }
            } else {
                m_invalidRaw = value;
                m_preserveInvalidRaw = true;
                m_combo->addItem(
                    QStringLiteral("Invalid stored value: %1")
                        .arg(compactJsonValue(value)));
                const int index = m_combo->count() - 1;
                m_combo->setItemData(index, true, invalidChoiceRole);
                m_combo->setItemData(index, true, invalidRawChoiceRole);
                m_combo->setCurrentIndex(index);
            }
        } else if (m_boolean) {
            if (value.isBool()) {
                m_boolean->setChecked(value.toBool());
                m_boolean->setToolTip({});
            } else {
                m_invalidRaw = value;
                m_preserveInvalidRaw = true;
                m_boolean->setChecked(false);
                m_boolean->setToolTip(
                    QStringLiteral("Invalid stored value: %1")
                        .arg(compactJsonValue(value)));
            }
        } else if (m_lineEdit) {
            const bool expectedString = m_definition.type == ParameterType::String;
            const bool expectedNumber = m_definition.type == ParameterType::Integer
                || m_definition.type == ParameterType::Number;
            if ((expectedString && value.isString())
                || (expectedNumber && value.isDouble())) {
                m_lineEdit->setText(
                    expectedString
                        ? value.toString()
                        : QString::number(value.toDouble(), 'g', 17));
            } else {
                m_invalidRaw = value;
                m_preserveInvalidRaw = true;
                m_lineEdit->setText(compactJsonValue(value));
            }
        }
        m_updating = false;
    }

    [[nodiscard]] QJsonValue value() const {
        if (m_preserveInvalidRaw) {
            return m_invalidRaw;
        }
        if (m_combo) {
            return comboValue();
        }
        if (m_boolean) {
            return m_boolean->isChecked();
        }
        if (m_definition.type == ParameterType::String) {
            return m_lineEdit ? QJsonValue(m_lineEdit->text()) : QJsonValue();
        }
        bool parsed = false;
        const double number = m_lineEdit
            ? QLocale::c().toDouble(m_lineEdit->text().trimmed(), &parsed)
            : 0.0;
        return parsed && std::isfinite(number)
            ? QJsonValue(number)
            : QJsonValue();
    }

    [[nodiscard]] QStringList localErrors() const {
        if (m_preserveInvalidRaw) {
            return {QStringLiteral("Stored value has the wrong JSON type (%1).")
                        .arg(compactJsonValue(m_invalidRaw))};
        }
        if (m_combo) {
            if (m_options.referenceDomainType
                && m_options.allowCustomReferences) {
                const int currentIndex = m_combo->currentIndex();
                const bool displaysCurrentItem = currentIndex >= 0
                    && m_combo->currentText()
                        == m_combo->itemText(currentIndex);
                if (displaysCurrentItem
                    && m_combo->currentData(invalidChoiceRole).toBool()) {
                    return {QStringLiteral(
                        "Referenced Domain is unavailable or has the wrong type.")};
                }
                if (comboValue().trimmed().isEmpty()) {
                    return {QStringLiteral("Enter a referenced Domain ID.")};
                }
                return {};
            }
            if (m_combo->currentIndex() < 0) {
                return {m_options.referenceDomainType
                            ? QStringLiteral("Choose a referenced Domain.")
                            : QStringLiteral("Choose an enum value.")};
            }
            if (m_combo->currentData(invalidChoiceRole).toBool()) {
                return {m_options.referenceDomainType
                            ? QStringLiteral("Referenced Domain is unavailable or has the wrong type.")
                            : QStringLiteral("Stored enum value is not declared by the Package.")};
            }
            return {};
        }
        if (m_boolean || m_definition.type == ParameterType::String) {
            return {};
        }

        const QString text = m_lineEdit ? m_lineEdit->text().trimmed() : QString();
        bool parsed = false;
        const double number = QLocale::c().toDouble(text, &parsed);
        if (!parsed || !std::isfinite(number)) {
            return {m_definition.type == ParameterType::Integer
                        ? QStringLiteral("Enter a finite integer.")
                        : QStringLiteral("Enter a finite number.")};
        }
        if (m_definition.type == ParameterType::Integer
            && std::floor(number) != number) {
            return {QStringLiteral("Value must be an integer.")};
        }
        if (m_definition.minimum && number < *m_definition.minimum) {
            return {QStringLiteral("Value must be at least %1.")
                        .arg(QString::number(*m_definition.minimum, 'g', 17))};
        }
        if (m_definition.maximum && number > *m_definition.maximum) {
            return {QStringLiteral("Value must be at most %1.")
                        .arg(QString::number(*m_definition.maximum, 'g', 17))};
        }
        return {};
    }

    std::function<void()> changed;
    std::function<void()> activated;

private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if ((watched == m_combo || watched == m_boolean
             || watched == m_lineEdit
             || watched == m_editableComboLineEdit)
            && (event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::FocusIn)
            && activated) {
            activated();
        }
        return QWidget::eventFilter(watched, event);
    }

    [[nodiscard]] int baseChoiceCount() const {
        return m_options.referenceDomainType
            ? m_options.choices.size()
            : m_definition.values.size();
    }

    [[nodiscard]] QString comboValue() const {
        if (!m_combo) {
            return {};
        }
        if (!m_editableComboLineEdit) {
            return m_combo->currentData().toString();
        }
        const QString text = m_combo->currentText();
        const int displayedChoice = m_combo->findText(
            text, Qt::MatchExactly);
        return displayedChoice >= 0
            ? m_combo->itemData(displayedChoice).toString()
            : text;
    }

    void notifyChanged() {
        if (changed) {
            changed();
        }
    }

    ParameterDefinition m_definition;
    SchemaValueOptions m_options;
    QString m_objectNamePrefix;
    bool m_updating = false;
    bool m_preserveInvalidRaw = false;
    QJsonValue m_invalidRaw;
    QLineEdit* m_lineEdit = nullptr;
    QLineEdit* m_editableComboLineEdit = nullptr;
    QComboBox* m_combo = nullptr;
    QCheckBox* m_boolean = nullptr;
};

SchemaValueEditor::SchemaValueEditor(ParameterDefinition definition,
                                     SchemaValueOptions options,
                                     QWidget* parent)
    : QWidget(parent),
      m_definition(std::move(definition)),
      m_options(std::move(options)) {
    setObjectName(objectNamePrefix());
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(4);

    if (!m_options.multiple
        && m_definition.type == ParameterType::Boolean
        && !m_options.referenceDomainType) {
        m_booleanEditor = new QCheckBox(this);
        m_booleanEditor->setObjectName(objectNamePrefix() + QStringLiteral(".boolean"));
        m_booleanEditor->setTristate(true);
        m_booleanEditor->setCheckState(Qt::PartiallyChecked);
        m_rootLayout->addWidget(m_booleanEditor);
        connect(m_booleanEditor, &QCheckBox::checkStateChanged, this, [this] {
            if (m_updating) {
                return;
            }
            if (m_absentSuggestion && m_absentSuggestion->isBool()
                && m_booleanEditor->checkState() != Qt::PartiallyChecked) {
                m_updating = true;
                m_booleanEditor->setCheckState(
                    m_absentSuggestion->toBool()
                        ? Qt::Checked : Qt::Unchecked);
                m_absentSuggestion.reset();
                m_updating = false;
            }
            m_preserveInvalidBooleanValue = false;
            m_booleanEditor->setToolTip({});
            notifyValueChanged();
        });
        return;
    }

    m_presenceToggle = new QCheckBox(QStringLiteral("Set"), this);
    m_presenceToggle->setObjectName(objectNamePrefix() + QStringLiteral(".present"));
    m_rootLayout->addWidget(m_presenceToggle);

    if (!m_options.multiple) {
        SchemaValueOptions scalarOptions = m_options;
        scalarOptions.multiple = false;
        scalarOptions.required = true;
        m_scalarEditor = new ScalarValueWidget(
            m_definition,
            std::move(scalarOptions),
            objectNamePrefix() + QStringLiteral(".scalar"),
            this);
        m_scalarEditor->changed = [this] { notifyValueChanged(); };
        m_scalarEditor->setValue(defaultItemValue());
        m_rootLayout->addWidget(m_scalarEditor);
    } else {
        m_multipleContainer = new QWidget(this);
        m_multipleContainer->setObjectName(
            objectNamePrefix() + QStringLiteral(".multiple"));
        auto* multipleLayout = new QVBoxLayout(m_multipleContainer);
        multipleLayout->setContentsMargins(0, 0, 0, 0);
        multipleLayout->setSpacing(4);

        m_invalidMultipleLabel = new QLabel(m_multipleContainer);
        m_invalidMultipleLabel->setObjectName(
            objectNamePrefix() + QStringLiteral(".invalidStoredValue"));
        m_invalidMultipleLabel->setWordWrap(true);
        m_invalidMultipleLabel->hide();
        multipleLayout->addWidget(m_invalidMultipleLabel);

        m_multipleTable = new QTableWidget(0, 1, m_multipleContainer);
        m_multipleTable->setObjectName(
            objectNamePrefix() + QStringLiteral(".items"));
        m_multipleTable->setHorizontalHeaderLabels({QStringLiteral("Value")});
        m_multipleTable->horizontalHeader()->setStretchLastSection(true);
        m_multipleTable->verticalHeader()->hide();
        m_multipleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_multipleTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_multipleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_multipleTable->setMinimumHeight(112);
        multipleLayout->addWidget(m_multipleTable);

        auto* buttons = new QHBoxLayout;
        m_addButton = new QPushButton(QStringLiteral("Add"), m_multipleContainer);
        m_removeButton = new QPushButton(QStringLiteral("Remove"), m_multipleContainer);
        m_moveUpButton = new QPushButton(QStringLiteral("Up"), m_multipleContainer);
        m_moveDownButton = new QPushButton(QStringLiteral("Down"), m_multipleContainer);
        m_addButton->setObjectName(objectNamePrefix() + QStringLiteral(".add"));
        m_removeButton->setObjectName(objectNamePrefix() + QStringLiteral(".remove"));
        m_moveUpButton->setObjectName(objectNamePrefix() + QStringLiteral(".up"));
        m_moveDownButton->setObjectName(objectNamePrefix() + QStringLiteral(".down"));
        buttons->addWidget(m_addButton);
        buttons->addWidget(m_removeButton);
        buttons->addWidget(m_moveUpButton);
        buttons->addWidget(m_moveDownButton);
        buttons->addStretch();
        multipleLayout->addLayout(buttons);
        m_rootLayout->addWidget(m_multipleContainer);

        connect(m_multipleTable, &QTableWidget::itemSelectionChanged,
                this, [this] { updateMultipleButtons(); });
        connect(m_addButton, &QPushButton::clicked, this, [this] {
            m_preserveInvalidMultipleValue = false;
            m_invalidMultipleLabel->hide();
            addMultipleValue(defaultItemValue());
            m_multipleTable->selectRow(m_multipleTable->rowCount() - 1);
            notifyValueChanged();
        });
        connect(m_removeButton, &QPushButton::clicked, this, [this] {
            const int row = m_multipleTable->currentRow();
            if (row < 0) {
                return;
            }
            m_preserveInvalidMultipleValue = false;
            m_invalidMultipleLabel->hide();
            m_multipleTable->removeRow(row);
            if (m_multipleTable->rowCount() > 0) {
                m_multipleTable->selectRow(
                    std::min(row, m_multipleTable->rowCount() - 1));
            }
            updateMultipleButtons();
            notifyValueChanged();
        });
        connect(m_moveUpButton, &QPushButton::clicked, this, [this] {
            const int row = m_multipleTable->currentRow();
            if (row <= 0) {
                return;
            }
            QVector<QJsonValue> values = multipleValues();
            std::swap(values[row], values[row - 1]);
            rebuildMultipleValues(values, row - 1);
            notifyValueChanged();
        });
        connect(m_moveDownButton, &QPushButton::clicked, this, [this] {
            const int row = m_multipleTable->currentRow();
            if (row < 0 || row + 1 >= m_multipleTable->rowCount()) {
                return;
            }
            QVector<QJsonValue> values = multipleValues();
            std::swap(values[row], values[row + 1]);
            rebuildMultipleValues(values, row + 1);
            notifyValueChanged();
        });
        updateMultipleButtons();
    }

    connect(m_presenceToggle, &QCheckBox::toggled, this, [this](bool present) {
        if (m_scalarEditor) {
            m_scalarEditor->setEnabled(present);
        }
        if (m_multipleContainer) {
            m_multipleContainer->setEnabled(present);
        }
        if (!m_updating) {
            if (present) {
                m_absentSuggestion.reset();
            }
            notifyValueChanged();
        }
    });
    setPresent(false);
}

void SchemaValueEditor::setValue(
    std::optional<QJsonValue> value,
    std::optional<QJsonValue> absentSuggestion) {
    m_updating = true;
    m_absentSuggestion = value ? std::nullopt : std::move(absentSuggestion);
    m_preserveInvalidBooleanValue = false;
    m_invalidBooleanValue = {};
    m_preserveInvalidMultipleValue = false;
    m_invalidMultipleValue = {};

    if (m_booleanEditor) {
        if (!value) {
            m_booleanEditor->setCheckState(Qt::PartiallyChecked);
            m_booleanEditor->setToolTip({});
        } else if (value->isBool()) {
            m_booleanEditor->setCheckState(
                value->toBool() ? Qt::Checked : Qt::Unchecked);
            m_booleanEditor->setToolTip({});
        } else {
            m_invalidBooleanValue = *value;
            m_preserveInvalidBooleanValue = true;
            m_booleanEditor->setCheckState(Qt::PartiallyChecked);
            m_booleanEditor->setToolTip(
                QStringLiteral("Invalid stored value: %1")
                    .arg(compactJsonValue(*value)));
        }
        m_updating = false;
        return;
    }

    setPresent(value.has_value());
    if (value && m_scalarEditor) {
        m_scalarEditor->setValue(*value);
    } else if (value && m_multipleTable) {
        if (value->isArray()) {
            QVector<QJsonValue> items;
            const QJsonArray array = value->toArray();
            items.reserve(array.size());
            for (const QJsonValue& item : array) {
                items.append(item);
            }
            rebuildMultipleValues(items);
            m_invalidMultipleLabel->hide();
        } else {
            m_preserveInvalidMultipleValue = true;
            m_invalidMultipleValue = *value;
            rebuildMultipleValues({});
            m_invalidMultipleLabel->setText(
                QStringLiteral("Invalid stored value (expected an array): %1")
                    .arg(compactJsonValue(*value)));
            m_invalidMultipleLabel->show();
        }
    } else if (!value && m_multipleTable) {
        if (m_absentSuggestion && m_absentSuggestion->isArray()) {
            QVector<QJsonValue> items;
            const QJsonArray array = m_absentSuggestion->toArray();
            items.reserve(array.size());
            for (const QJsonValue& item : array) {
                items.append(item);
            }
            rebuildMultipleValues(items);
        } else {
            rebuildMultipleValues({});
        }
        m_invalidMultipleLabel->hide();
    } else if (!value && m_scalarEditor && m_absentSuggestion) {
        m_scalarEditor->setValue(*m_absentSuggestion);
    }
    m_updating = false;
}

std::optional<QJsonValue> SchemaValueEditor::value() const {
    if (m_booleanEditor) {
        if (m_preserveInvalidBooleanValue) {
            return m_invalidBooleanValue;
        }
        if (m_booleanEditor->checkState() == Qt::PartiallyChecked) {
            return std::nullopt;
        }
        return QJsonValue(m_booleanEditor->checkState() == Qt::Checked);
    }
    if (!isPresent()) {
        return std::nullopt;
    }
    if (m_scalarEditor) {
        return m_scalarEditor->value();
    }
    if (m_preserveInvalidMultipleValue) {
        return m_invalidMultipleValue;
    }
    QJsonArray array;
    for (const QJsonValue& item : multipleValues()) {
        array.append(item);
    }
    return QJsonValue(array);
}

QStringList SchemaValueEditor::localErrors() const {
    const bool requiresPresence =
        m_options.required
        && m_options.validationMode == PropertyValidationMode::Complete;
    if (m_booleanEditor) {
        if (m_preserveInvalidBooleanValue) {
            return {QStringLiteral("Stored value has the wrong JSON type (%1).")
                        .arg(compactJsonValue(m_invalidBooleanValue))};
        }
        if (requiresPresence
            && m_booleanEditor->checkState() == Qt::PartiallyChecked) {
            return {QStringLiteral("Value is required.")};
        }
        return {};
    }

    if (!isPresent()) {
        return requiresPresence
            ? QStringList{QStringLiteral("Value is required.")}
            : QStringList{};
    }
    if (m_scalarEditor) {
        return m_scalarEditor->localErrors();
    }
    if (m_preserveInvalidMultipleValue) {
        return {QStringLiteral("Stored value must be an array.")};
    }
    if (m_options.required && m_multipleTable->rowCount() == 0) {
        return {QStringLiteral("At least one value is required.")};
    }
    QStringList errors;
    for (int row = 0; row < m_multipleTable->rowCount(); ++row) {
        const auto* editor = dynamic_cast<const ScalarValueWidget*>(
            m_multipleTable->cellWidget(row, 0));
        if (!editor) {
            errors.append(QStringLiteral("Item %1 has no editor.").arg(row + 1));
            continue;
        }
        for (const QString& error : editor->localErrors()) {
            errors.append(QStringLiteral("Item %1: %2").arg(row + 1).arg(error));
        }
    }
    return errors;
}

void SchemaValueEditor::setPresent(bool present) {
    if (m_booleanEditor) {
        if (!present) {
            m_booleanEditor->setCheckState(Qt::PartiallyChecked);
        } else if (m_booleanEditor->checkState() == Qt::PartiallyChecked) {
            m_booleanEditor->setCheckState(Qt::Unchecked);
        }
        return;
    }
    if (!m_presenceToggle) {
        return;
    }
    const QSignalBlocker blocker(m_presenceToggle);
    m_presenceToggle->setChecked(present);
    if (m_scalarEditor) {
        m_scalarEditor->setEnabled(present);
    }
    if (m_multipleContainer) {
        m_multipleContainer->setEnabled(present);
    }
}

bool SchemaValueEditor::isPresent() const {
    if (m_booleanEditor) {
        return m_preserveInvalidBooleanValue
            || m_booleanEditor->checkState() != Qt::PartiallyChecked;
    }
    return m_presenceToggle && m_presenceToggle->isChecked();
}

void SchemaValueEditor::notifyValueChanged() {
    if (!m_updating && valueChanged) {
        valueChanged();
    }
}

void SchemaValueEditor::addMultipleValue(const QJsonValue& value) {
    const int row = m_multipleTable->rowCount();
    m_multipleTable->insertRow(row);
    SchemaValueOptions scalarOptions = m_options;
    scalarOptions.multiple = false;
    scalarOptions.required = true;
    auto* editor = new ScalarValueWidget(
        m_definition,
        std::move(scalarOptions),
        QStringLiteral("%1.item.%2")
            .arg(objectNamePrefix())
            .arg(m_nextMultipleItemToken++),
        m_multipleTable);
    editor->setValue(value);
    editor->changed = [this] {
        updateMultipleButtons();
        notifyValueChanged();
    };
    editor->activated = [this, editor] {
        for (int candidateRow = 0;
             candidateRow < m_multipleTable->rowCount(); ++candidateRow) {
            if (m_multipleTable->cellWidget(candidateRow, 0) == editor) {
                m_multipleTable->selectRow(candidateRow);
                break;
            }
        }
    };
    auto* selectionItem = new QTableWidgetItem;
    selectionItem->setFlags(
        Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_multipleTable->setItem(row, 0, selectionItem);
    m_multipleTable->setCellWidget(row, 0, editor);
    m_multipleTable->resizeRowToContents(row);
}

void SchemaValueEditor::rebuildMultipleValues(const QVector<QJsonValue>& values,
                                              int selectedRow) {
    m_multipleTable->setRowCount(0);
    for (const QJsonValue& item : values) {
        addMultipleValue(item);
    }
    if (selectedRow >= 0 && selectedRow < m_multipleTable->rowCount()) {
        m_multipleTable->selectRow(selectedRow);
    }
    updateMultipleButtons();
}

QVector<QJsonValue> SchemaValueEditor::multipleValues() const {
    QVector<QJsonValue> values;
    if (!m_multipleTable) {
        return values;
    }
    values.reserve(m_multipleTable->rowCount());
    for (int row = 0; row < m_multipleTable->rowCount(); ++row) {
        const auto* editor = dynamic_cast<const ScalarValueWidget*>(
            m_multipleTable->cellWidget(row, 0));
        values.append(editor ? editor->value() : QJsonValue());
    }
    return values;
}

void SchemaValueEditor::updateMultipleButtons() {
    if (!m_multipleTable) {
        return;
    }
    const int row = m_multipleTable->currentRow();
    const bool selected = row >= 0;
    bool valuesValid = true;
    for (int candidateRow = 0;
         candidateRow < m_multipleTable->rowCount(); ++candidateRow) {
        const auto* editor = dynamic_cast<const ScalarValueWidget*>(
            m_multipleTable->cellWidget(candidateRow, 0));
        if (!editor || !editor->localErrors().isEmpty()) {
            valuesValid = false;
            break;
        }
    }
    m_removeButton->setEnabled(selected);
    m_moveUpButton->setEnabled(selected && valuesValid && row > 0);
    m_moveDownButton->setEnabled(
        selected && valuesValid
        && row + 1 < m_multipleTable->rowCount());
}

QJsonValue SchemaValueEditor::defaultItemValue() const {
    if (m_options.referenceDomainType) {
        return m_options.choices.isEmpty()
            ? QJsonValue(QString())
            : QJsonValue(m_options.choices.constFirst().value);
    }
    switch (m_definition.type) {
    case ParameterType::Integer:
    case ParameterType::Number: {
        double value = 0.0;
        if (m_definition.minimum && value < *m_definition.minimum) {
            value = *m_definition.minimum;
        }
        if (m_definition.maximum && value > *m_definition.maximum) {
            value = *m_definition.maximum;
        }
        return value;
    }
    case ParameterType::Boolean:
        return false;
    case ParameterType::Enumeration:
        return m_definition.values.isEmpty()
            ? QJsonValue(QString())
            : QJsonValue(m_definition.values.constFirst());
    case ParameterType::String:
        return QString();
    case ParameterType::Invalid:
        return {};
    }
    return {};
}

QString SchemaValueEditor::objectNamePrefix() const {
    const QString id = m_definition.id.trimmed().isEmpty()
        ? QStringLiteral("value")
        : m_definition.id;
    return QStringLiteral("finepaper.schemaValue.%1").arg(id);
}

} // namespace finepaper
