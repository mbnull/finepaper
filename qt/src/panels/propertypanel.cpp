// PropertyPanel — shows editable parameters for the currently selected module.
// Each parameter type (QString, int, double, bool) gets a matching Qt widget.
// Changes are pushed through SetParameterCommand so they are undoable.
// blockSignals() prevents feedback loops when the model updates the widget.
#include "panels/propertypanel.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduletypemetadata.h"
#include "plugins/pluginprojectadapter.h"
#include "project/projectstateservice.h"
#include "commands/commandmanager.h"
#include "commands/setpluginstateparametercommand.h"
#include "commands/setparametercommand.h"
#include <cfloat>
#include <climits>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <limits>
#include <utility>

namespace {

QString humanizeIdentifier(const QString& identifier) {
    QString text = identifier;
    text.replace('-', ' ');
    text.replace('_', ' ');

    bool capitalizeNext = true;
    for (int index = 0; index < text.size(); ++index) {
        if (text[index].isSpace()) {
            capitalizeNext = true;
            continue;
        }

        if (capitalizeNext) {
            text[index] = text[index].toUpper();
            capitalizeNext = false;
        }
    }

    return text;
}

const ModuleParameterMetadata* metadataForParameter(const Module* module, const QString& name) {
    return ModuleTypeMetadata::parameterMetadata(module, name);
}

void applySpinBoxMetadata(QSpinBox* spinBox, const ModuleParameterMetadata* metadata) {
    const int minimum = metadata && metadata->minimumValue.has_value()
        ? static_cast<int>(*metadata->minimumValue)
        : INT_MIN;
    const int maximum = metadata && metadata->maximumValue.has_value()
        ? static_cast<int>(*metadata->maximumValue)
        : INT_MAX;
    spinBox->setRange(minimum, maximum);

    if (metadata && !metadata->unit.isEmpty()) {
        spinBox->setSuffix(QStringLiteral(" %1").arg(metadata->unit));
    }
}

void applyDoubleSpinBoxMetadata(QDoubleSpinBox* spinBox, const ModuleParameterMetadata* metadata) {
    const double minimum = metadata && metadata->minimumValue.has_value()
        ? *metadata->minimumValue
        : std::numeric_limits<double>::lowest();
    const double maximum = metadata && metadata->maximumValue.has_value()
        ? *metadata->maximumValue
        : std::numeric_limits<double>::max();
    spinBox->setRange(minimum, maximum);

    if (metadata && !metadata->unit.isEmpty()) {
        spinBox->setSuffix(QStringLiteral(" %1").arg(metadata->unit));
    }
}

void applyReadOnlyMetadata(QWidget* widget, const ModuleParameterMetadata* metadata) {
    if (!widget || !metadata || !metadata->readOnly) {
        return;
    }

    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        lineEdit->setReadOnly(true);
    } else if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
        spinBox->setReadOnly(true);
        spinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
        doubleSpinBox->setReadOnly(true);
        doubleSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    } else {
        widget->setEnabled(false);
    }
}

void syncComboBoxValue(QComboBox* comboBox, const QString& value) {
    if (!comboBox) {
        return;
    }

    int index = comboBox->findData(value);
    if (index < 0) {
        index = comboBox->findText(value);
    }
    if (index < 0) {
        comboBox->addItem(value, value);
        index = comboBox->count() - 1;
    }

    comboBox->setCurrentIndex(index);
}

QJsonValue valueToJson(const Parameter::Value& value) {
    if (const auto* stringValue = std::get_if<QString>(&value)) {
        return *stringValue;
    }
    if (const auto* intValue = std::get_if<int>(&value)) {
        return *intValue;
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return *doubleValue;
    }
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }
    return QJsonValue(QJsonValue::Undefined);
}

QJsonValue resolvedPluginValue(const PluginParameterField& field, const QJsonValue& storedValue) {
    return storedValue.isUndefined() ? valueToJson(field.defaultValue) : storedValue;
}

QString defaultStringValue(const Parameter::Value& value) {
    if (const auto* stringValue = std::get_if<QString>(&value)) {
        return *stringValue;
    }
    if (const auto* intValue = std::get_if<int>(&value)) {
        return QString::number(*intValue);
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return QString::number(*doubleValue);
    }
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue ? QStringLiteral("true") : QStringLiteral("false");
    }
    return QString();
}

int defaultIntValue(const Parameter::Value& value) {
    if (const auto* intValue = std::get_if<int>(&value)) {
        return *intValue;
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return static_cast<int>(*doubleValue);
    }
    return 0;
}

double defaultDoubleValue(const Parameter::Value& value) {
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return *doubleValue;
    }
    if (const auto* intValue = std::get_if<int>(&value)) {
        return static_cast<double>(*intValue);
    }
    return 0.0;
}

bool defaultBoolValue(const Parameter::Value& value) {
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }
    return false;
}

QString pluginValueAsString(const PluginParameterField& field, const QJsonValue& storedValue) {
    const QJsonValue value = resolvedPluginValue(field, storedValue);
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    return defaultStringValue(field.defaultValue);
}

void applyPluginConfigurability(QWidget* widget, bool configurable) {
    if (!widget) {
        return;
    }

    if (configurable) {
        return;
    }

    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        lineEdit->setReadOnly(true);
    } else if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
        spinBox->setReadOnly(true);
        spinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
        doubleSpinBox->setReadOnly(true);
        doubleSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    } else {
        widget->setEnabled(false);
    }
}
} // namespace

PropertyPanel::PropertyPanel(Graph* graph,
                             ProjectStateService* stateService,
                             QVector<IPluginProjectAdapter*> pluginAdapters,
                             CommandManager* commandManager,
                             QWidget* parent)
    : QWidget(parent),
      m_graph(graph),
      m_stateService(stateService),
      m_pluginAdapters(std::move(pluginAdapters)),
      m_commandManager(commandManager) {
    m_layout = new QVBoxLayout(this);
    m_descriptionView = new QPlainTextEdit(this);
    m_descriptionView->setReadOnly(true);
    m_descriptionView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_descriptionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_descriptionView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_descriptionView->setFixedHeight((m_descriptionView->fontMetrics().lineSpacing() * 3) + 12);
    m_descriptionView->setStyleSheet(
        QStringLiteral("QPlainTextEdit { color: #555; font-size: 11px; border: 1px solid #d8d8d8; }"));
    m_descriptionView->hide();
    m_layout->addWidget(m_descriptionView);
    m_formLayout = new QFormLayout();
    m_layout->addLayout(m_formLayout);
    if (m_stateService) {
        connect(m_stateService,
                &ProjectStateService::parameterChanged,
                this,
                &PropertyPanel::onPluginStateParameterChanged);
    }
}

PropertyPanel::PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : PropertyPanel(graph, nullptr, {}, commandManager, parent) {}

QWidget* PropertyPanel::createPluginParameterWidget(const PluginParameterSection&,
                                                    const PluginParameterField& field,
                                                    const QJsonValue& storedValue,
                                                    bool editable) {
    if (!field.choices.isEmpty()) {
        auto* comboBox = new QComboBox(this);
        for (const PluginInstanceParameterChoice& choice : field.choices) {
            comboBox->addItem(choice.label, choice.value);
        }
        syncComboBoxValue(comboBox, pluginValueAsString(field, storedValue));
        applyPluginConfigurability(comboBox, editable);
        return comboBox;
    }

    const QJsonValue value = resolvedPluginValue(field, storedValue);
    if (field.type == QStringLiteral("int")) {
        auto* spinBox = new QSpinBox(this);
        spinBox->setRange(INT_MIN, INT_MAX);
        spinBox->setValue(value.toInt(defaultIntValue(field.defaultValue)));
        applyPluginConfigurability(spinBox, editable);
        return spinBox;
    }
    if (field.type == QStringLiteral("double")) {
        auto* doubleSpinBox = new QDoubleSpinBox(this);
        doubleSpinBox->setRange(std::numeric_limits<double>::lowest(),
                                std::numeric_limits<double>::max());
        doubleSpinBox->setValue(value.toDouble(defaultDoubleValue(field.defaultValue)));
        applyPluginConfigurability(doubleSpinBox, editable);
        return doubleSpinBox;
    }
    if (field.type == QStringLiteral("bool")) {
        auto* checkBox = new QCheckBox(this);
        checkBox->setChecked(value.toBool(defaultBoolValue(field.defaultValue)));
        applyPluginConfigurability(checkBox, editable);
        return checkBox;
    }

    auto* lineEdit = new QLineEdit(pluginValueAsString(field, storedValue), this);
    applyPluginConfigurability(lineEdit, editable);
    return lineEdit;
}

void PropertyPanel::setSelectedModule(QString moduleId) {
    Module* module = moduleId.isEmpty() ? nullptr : m_graph->getModule(moduleId);
    setSelectedModule(module);
}

void PropertyPanel::setSelectedModule(Module* module) {
    if (m_selectedModule) {
        disconnect(m_selectedModule, &Module::parameterChanged, this, &PropertyPanel::onParameterChanged);
    }

    m_selectedModule = module;
    clearPanel();

    if (m_selectedModule) {
        connect(m_selectedModule, &Module::parameterChanged, this, &PropertyPanel::onParameterChanged);
    }
    populatePanel();
}

void PropertyPanel::clearPanel() {
    m_descriptionView->clear();
    m_descriptionView->hide();

    while (m_formLayout->rowCount() > 0) {
        m_formLayout->removeRow(0);
    }
    m_parameterWidgets.clear();
    m_ipParameterWidgets.clear();
}

void PropertyPanel::populatePanel() {
    if (!m_selectedModule) {
        if (!m_stateService) {
            return;
        }

        const auto renderSection = [this](const PluginParameterSection& section,
                                          const QString& instanceId,
                                          bool writableState) {
            const QString baseLabel = section.label.isEmpty() ? section.pluginId : section.label;
            const QString title = instanceId.isEmpty()
                ? baseLabel
                : QStringLiteral("%1 / %2").arg(baseLabel, instanceId);
            auto* header = new QLabel(title, this);
            QFont font = header->font();
            font.setBold(true);
            header->setFont(font);
            m_formLayout->addRow(header);

            for (const PluginParameterField& field : section.fields) {
                const QJsonValue stored = m_stateService->parameter(
                    section.pluginId, instanceId, section.id, field.name);
                const bool editable = field.configurable && writableState;
                QWidget* widget = createPluginParameterWidget(section, field, stored, editable);
                if (!widget) {
                    continue;
                }
                const QString label = field.label.isEmpty() ? humanizeIdentifier(field.name) : field.label;
                QLabel* rowLabel = new QLabel(label, this);
                if (!field.description.isEmpty()) {
                    rowLabel->setToolTip(field.description);
                    widget->setToolTip(field.description);
                }
                if (editable && m_commandManager) {
                    const QString pluginId = section.pluginId;
                    const QString sectionId = section.id;
                    const QString fieldName = field.name;
                    const auto executeCommand = [this,
                                                 pluginId,
                                                 instanceId,
                                                 sectionId,
                                                 fieldName](QJsonValue value) {
                        auto command = std::make_unique<SetPluginStateParameterCommand>(
                            m_stateService, pluginId, instanceId, sectionId, fieldName, std::move(value));
                        m_commandManager->executeCommand(std::move(command));
                    };

                    if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
                        connect(comboBox,
                                QOverload<int>::of(&QComboBox::currentIndexChanged),
                                this,
                                [comboBox, executeCommand](int index) {
                                    if (index < 0) {
                                        return;
                                    }
                                    executeCommand(comboBox->itemData(index).toString());
                                });
                    } else if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
                        connect(spinBox,
                                QOverload<int>::of(&QSpinBox::valueChanged),
                                this,
                                [executeCommand](int value) {
                                    executeCommand(value);
                                });
                    } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
                        connect(doubleSpinBox,
                                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                                this,
                                [executeCommand](double value) {
                                    executeCommand(value);
                                });
                    } else if (auto* checkBox = qobject_cast<QCheckBox*>(widget)) {
                        connect(checkBox, &QCheckBox::toggled, this, [executeCommand](bool checked) {
                            executeCommand(checked);
                        });
                    } else if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
                        connect(lineEdit, &QLineEdit::editingFinished, this, [lineEdit, executeCommand]() {
                            executeCommand(lineEdit->text());
                        });
                    }
                }
                m_formLayout->addRow(rowLabel, widget);
                m_ipParameterWidgets.insert(
                    section.pluginId + QStringLiteral("/") + instanceId +
                        QStringLiteral("/") + section.id + QStringLiteral("/") + field.name,
                    widget);
            }
        };

        for (const IPluginProjectAdapter* adapter : m_pluginAdapters) {
            if (!adapter) {
                continue;
            }
            for (const PluginParameterSection& section : adapter->parameterSections()) {
                bool renderedPersistedState = false;
                for (const ProjectIpInstanceRecord& record : m_stateService->ipInstanceRecords()) {
                    if (record.ipcoreId != section.pluginId) {
                        continue;
                    }
                    renderSection(section,
                                  record.instanceId,
                                  record.state.value(section.id).isObject());
                    renderedPersistedState = true;
                }
                if (!renderedPersistedState) {
                    renderSection(section, section.instanceId, false);
                }
            }
        }
        return;
    }

    const QString moduleDescription = ModuleTypeMetadata::description(m_selectedModule);
    if (!moduleDescription.isEmpty()) {
        m_descriptionView->setPlainText(moduleDescription);
        m_descriptionView->setToolTip(moduleDescription);
        m_descriptionView->verticalScrollBar()->setValue(0);
        m_descriptionView->show();
    }

    const QString moduleId = m_selectedModule->id();
    const auto addParameterRow = [this, &moduleId](const QString& name,
                                                   const QString& label,
                                                   const QString& description) {
        const auto paramIt = m_selectedModule->parameters().find(name);
        if (paramIt == m_selectedModule->parameters().end()) {
            return;
        }

        const Parameter& param = paramIt.value();
        const ModuleParameterMetadata* metadata = metadataForParameter(m_selectedModule, name);
        QWidget* widget = nullptr;

        if (metadata && !metadata->choices.isEmpty() && std::holds_alternative<QString>(param.value())) {
            auto* comboBox = new QComboBox(this);
            for (const ModuleParameterChoice& choice : metadata->choices) {
                comboBox->addItem(choice.label, choice.value);
            }
            syncComboBoxValue(comboBox, std::get<QString>(param.value()));
            if (!metadata->readOnly) {
                connect(comboBox,
                        QOverload<int>::of(&QComboBox::currentIndexChanged),
                        this,
                        [this, moduleId, name, comboBox](int index) {
                            if (index < 0 || !m_graph->getModule(moduleId)) return;
                            auto cmd = std::make_unique<SetParameterCommand>(
                                m_graph, moduleId, name, comboBox->itemData(index).toString());
                            m_commandManager->executeCommand(std::move(cmd));
                        });
            }
            widget = comboBox;
        } else if (std::holds_alternative<QString>(param.value())) {
            auto* lineEdit = new QLineEdit(std::get<QString>(param.value()));
            if (!metadata || !metadata->readOnly) {
                connect(lineEdit, &QLineEdit::editingFinished, this, [this, moduleId, name, lineEdit]() {
                    if (!m_graph->getModule(moduleId)) return;
                    auto cmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, name, lineEdit->text());
                    m_commandManager->executeCommand(std::move(cmd));
                });
            }
            widget = lineEdit;
        } else if (std::holds_alternative<int>(param.value())) {
            auto* spinBox = new QSpinBox();
            applySpinBoxMetadata(spinBox, metadata);
            spinBox->setValue(std::get<int>(param.value()));
            if (!metadata || !metadata->readOnly) {
                connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, moduleId, name](int value) {
                    if (!m_graph->getModule(moduleId)) return;
                    auto cmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, name, value);
                    m_commandManager->executeCommand(std::move(cmd));
                });
            }
            widget = spinBox;
        } else if (std::holds_alternative<double>(param.value())) {
            auto* doubleSpinBox = new QDoubleSpinBox();
            applyDoubleSpinBoxMetadata(doubleSpinBox, metadata);
            doubleSpinBox->setValue(std::get<double>(param.value()));
            if (!metadata || !metadata->readOnly) {
                connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, moduleId, name](double value) {
                    if (!m_graph->getModule(moduleId)) return;
                    auto cmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, name, value);
                    m_commandManager->executeCommand(std::move(cmd));
                });
            }
            widget = doubleSpinBox;
        } else if (std::holds_alternative<bool>(param.value())) {
            auto* checkBox = new QCheckBox();
            checkBox->setChecked(std::get<bool>(param.value()));
            if (!metadata || !metadata->readOnly) {
                connect(checkBox, &QCheckBox::toggled, this, [this, moduleId, name](bool checked) {
                    if (!m_graph->getModule(moduleId)) return;
                    auto cmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, name, checked);
                    m_commandManager->executeCommand(std::move(cmd));
                });
            }
            widget = checkBox;
        }

        if (widget) {
            QLabel* rowLabel = new QLabel(label.isEmpty() ? name : label, this);
            if (!description.isEmpty()) {
                rowLabel->setToolTip(description);
                widget->setToolTip(description);
            }
            applyReadOnlyMetadata(widget, metadata);
            m_formLayout->addRow(rowLabel, widget);
            m_parameterWidgets[name] = widget;
        }
    };

    const QVector<ModuleConfigField>& configFields = ModuleTypeMetadata::configFields(m_selectedModule);
    if (!configFields.isEmpty()) {
        for (const ModuleConfigField& field : configFields) {
            addParameterRow(field.parameterName, field.label, field.description);
        }
        return;
    }

    for (auto it = m_selectedModule->parameters().constBegin(); it != m_selectedModule->parameters().constEnd(); ++it) {
        const QString& name = it.key();
        if (name == "x" || name == "y") continue;
        const ModuleParameterMetadata* metadata = metadataForParameter(m_selectedModule, name);
        addParameterRow(name,
                        metadata && !metadata->label.isEmpty() ? metadata->label : humanizeIdentifier(name),
                        metadata ? metadata->description : QString());
    }
}

void PropertyPanel::onParameterChanged(const QString& name) {
    if (!m_selectedModule) return;

    auto it = m_parameterWidgets.find(name);
    if (it == m_parameterWidgets.end()) return;

    const auto& params = m_selectedModule->parameters();
    auto paramIt = params.find(name);
    if (paramIt == params.end()) return;

    const auto& value = paramIt.value().value();

    if (auto* lineEdit = qobject_cast<QLineEdit*>(it.value())) {
        lineEdit->blockSignals(true);
        lineEdit->setText(std::get<QString>(value));
        lineEdit->blockSignals(false);
    } else if (auto* comboBox = qobject_cast<QComboBox*>(it.value())) {
        comboBox->blockSignals(true);
        syncComboBoxValue(comboBox, std::get<QString>(value));
        comboBox->blockSignals(false);
    } else if (auto* spinBox = qobject_cast<QSpinBox*>(it.value())) {
        spinBox->blockSignals(true);
        spinBox->setValue(std::get<int>(value));
        spinBox->blockSignals(false);
    } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(it.value())) {
        doubleSpinBox->blockSignals(true);
        doubleSpinBox->setValue(std::get<double>(value));
        doubleSpinBox->blockSignals(false);
    } else if (auto* checkBox = qobject_cast<QCheckBox*>(it.value())) {
        checkBox->blockSignals(true);
        checkBox->setChecked(std::get<bool>(value));
        checkBox->blockSignals(false);
    }
}

void PropertyPanel::onPluginStateParameterChanged(const QString& pluginId,
                                                  const QString& instanceId,
                                                  const QString& section,
                                                  const QString& name) {
    if (m_selectedModule || !m_stateService) {
        return;
    }

    const QString key = pluginId + QStringLiteral("/") + instanceId +
        QStringLiteral("/") + section + QStringLiteral("/") + name;
    auto widgetIt = m_ipParameterWidgets.find(key);
    if (widgetIt == m_ipParameterWidgets.end()) {
        return;
    }

    PluginParameterField field;
    bool hasField = false;
    for (const IPluginProjectAdapter* adapter : m_pluginAdapters) {
        if (!adapter) {
            continue;
        }
        for (const PluginParameterSection& candidateSection : adapter->parameterSections()) {
            if (candidateSection.pluginId != pluginId || candidateSection.id != section) {
                continue;
            }
            for (const PluginParameterField& candidateField : candidateSection.fields) {
                if (candidateField.name == name) {
                    field = candidateField;
                    hasField = true;
                    break;
                }
            }
            if (hasField) {
                break;
            }
        }
        if (hasField) {
            break;
        }
    }

    const QJsonValue stored = m_stateService->parameter(pluginId, instanceId, section, name);
    const QJsonValue value = hasField ? resolvedPluginValue(field, stored) : stored;
    QWidget* widget = widgetIt.value();
    if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
        lineEdit->blockSignals(true);
        lineEdit->setText(hasField ? pluginValueAsString(field, stored) : value.toString());
        lineEdit->blockSignals(false);
    } else if (auto* comboBox = qobject_cast<QComboBox*>(widget)) {
        comboBox->blockSignals(true);
        syncComboBoxValue(comboBox, hasField ? pluginValueAsString(field, stored) : value.toString());
        comboBox->blockSignals(false);
    } else if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
        spinBox->blockSignals(true);
        spinBox->setValue(value.toInt(hasField ? defaultIntValue(field.defaultValue) : spinBox->value()));
        spinBox->blockSignals(false);
    } else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget)) {
        doubleSpinBox->blockSignals(true);
        doubleSpinBox->setValue(value.toDouble(hasField ? defaultDoubleValue(field.defaultValue)
                                                        : doubleSpinBox->value()));
        doubleSpinBox->blockSignals(false);
    } else if (auto* checkBox = qobject_cast<QCheckBox*>(widget)) {
        checkBox->blockSignals(true);
        checkBox->setChecked(value.toBool(hasField ? defaultBoolValue(field.defaultValue)
                                                   : checkBox->isChecked()));
        checkBox->blockSignals(false);
    }
}
