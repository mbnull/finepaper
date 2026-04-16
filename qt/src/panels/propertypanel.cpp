// PropertyPanel — shows editable parameters for the currently selected module.
// Each parameter type (QString, int, double, bool) gets a matching Qt widget.
// Changes are pushed through SetParameterCommand so they are undoable.
// blockSignals() prevents feedback loops when the model updates the widget.
#include "panels/propertypanel.h"
#include "graph/graph.h"
#include "graph/module.h"
#include "modules/moduletypemetadata.h"
#include "commands/commandmanager.h"
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
} // namespace

PropertyPanel::PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent), m_graph(graph), m_commandManager(commandManager) {
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
        populatePanel();
    }
}

void PropertyPanel::clearPanel() {
    m_descriptionView->clear();
    m_descriptionView->hide();

    while (m_formLayout->rowCount() > 0) {
        m_formLayout->removeRow(0);
    }
    m_parameterWidgets.clear();
}

void PropertyPanel::populatePanel() {
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
