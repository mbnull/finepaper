// PropertyPanel — shows editable parameters for the currently selected module.
// Each parameter type (QString, int, double, bool) gets a matching Qt widget.
// Changes are pushed through SetParameterCommand so they are undoable.
// blockSignals() prevents feedback loops when the model updates the widget.
#include "propertypanel.h"
#include "graph.h"
#include "module.h"
#include "commandmanager.h"
#include "commands/setparametercommand.h"
#include <cfloat>
#include <climits>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>

PropertyPanel::PropertyPanel(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent), m_graph(graph), m_commandManager(commandManager) {
    m_layout = new QVBoxLayout(this);
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
    QLayoutItem* item;
    while ((item = m_layout->takeAt(0))) {
        if (item->widget()) {
            delete item->widget();
        } else if (QLayout* sub = item->layout()) {
            // Delete all widgets inside the sub-layout (e.g. QFormLayout rows)
            // QLayoutItem* subItem;
            // while ((subItem = sub->takeAt(0))) {
            //     if (subItem->widget()) delete subItem->widget();
            //     delete subItem;
            // }
            // delete sub;
        }
        delete item;
    }
    m_parameterWidgets.clear();
}

void PropertyPanel::populatePanel() {
    auto formLayout = new QFormLayout();

    for (auto it = m_selectedModule->parameters().constBegin(); it != m_selectedModule->parameters().constEnd(); ++it) {
        const QString& name = it.key();
        const Parameter& param = it.value();
        QWidget* widget = nullptr;

        if (std::holds_alternative<QString>(param.value())) {
            auto* lineEdit = new QLineEdit(std::get<QString>(param.value()));
            connect(lineEdit, &QLineEdit::editingFinished, [this, name, lineEdit]() {
                auto cmd = std::make_unique<SetParameterCommand>(m_graph, m_selectedModule->id(), name, lineEdit->text());
                m_commandManager->executeCommand(std::move(cmd));
            });
            widget = lineEdit;
        } else if (std::holds_alternative<int>(param.value())) {
            auto* spinBox = new QSpinBox();
            spinBox->setRange(INT_MIN, INT_MAX);
            spinBox->setValue(std::get<int>(param.value()));
            connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), [this, name](int value) {
                auto cmd = std::make_unique<SetParameterCommand>(m_graph, m_selectedModule->id(), name, value);
                m_commandManager->executeCommand(std::move(cmd));
            });
            widget = spinBox;
        } else if (std::holds_alternative<double>(param.value())) {
            auto* doubleSpinBox = new QDoubleSpinBox();
            doubleSpinBox->setRange(-DBL_MAX, DBL_MAX);
            doubleSpinBox->setValue(std::get<double>(param.value()));
            connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, name](double value) {
                auto cmd = std::make_unique<SetParameterCommand>(m_graph, m_selectedModule->id(), name, value);
                m_commandManager->executeCommand(std::move(cmd));
            });
            widget = doubleSpinBox;
        } else if (std::holds_alternative<bool>(param.value())) {
            auto* checkBox = new QCheckBox();
            checkBox->setChecked(std::get<bool>(param.value()));
            connect(checkBox, &QCheckBox::toggled, [this, name](bool checked) {
                auto cmd = std::make_unique<SetParameterCommand>(m_graph, m_selectedModule->id(), name, checked);
                m_commandManager->executeCommand(std::move(cmd));
            });
            widget = checkBox;
        }

        if (widget) {
            formLayout->addRow(name, widget);
            m_parameterWidgets[name] = widget;
        }
    }

    m_layout->addLayout(formLayout);
}

void PropertyPanel::onParameterChanged(const QString& name) {
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
