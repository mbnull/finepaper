#include "gui/element_configuration_panel.h"

#include "application/element_configuration.h"
#include "ui/common/schema_value_editor.h"

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

bool propertySetAppliesTo(
    const ElementPropertySetDefinition& propertySet,
    const NocDesign& design,
    const ElementRef& element) {
    if (!propertySet.appliesTo.contains(element.kind)) {
        return false;
    }
    if (element.kind != ElementKind::EndpointAttachment
        || propertySet.endpointTypes.isEmpty()) {
        return true;
    }
    const auto endpoint = std::find_if(
        design.endpoints.cbegin(), design.endpoints.cend(),
        [&element](const EndpointInstance& candidate) {
            return candidate.id == element.id;
        });
    return endpoint != design.endpoints.cend()
        && propertySet.endpointTypes.contains(endpoint->type);
}

QString elementKindLabel(ElementKind kind) {
    switch (kind) {
    case ElementKind::Router:
        return QStringLiteral("Router");
    case ElementKind::RouterLink:
        return QStringLiteral("Router Link");
    case ElementKind::EndpointAttachment:
        return QStringLiteral("Endpoint Attachment");
    case ElementKind::Endpoint:
        return QStringLiteral("Endpoint");
    case ElementKind::Invalid:
        return QStringLiteral("Element");
    }
    return QStringLiteral("Element");
}

QString diagnosticSummary(const QVector<Diagnostic>& diagnostics) {
    QStringList messages;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == QStringLiteral("error")) {
            messages.append(diagnostic.message);
        }
    }
    if (messages.isEmpty()) {
        for (const Diagnostic& diagnostic : diagnostics) {
            messages.append(diagnostic.message);
        }
    }
    messages.removeDuplicates();
    return messages.join(QStringLiteral(" "));
}

} // namespace

ElementConfigurationPanelProjection projectElementConfigurationPanel(
    const NocDesign* design,
    const PackageDefinition* package,
    std::optional<ElementRef> selection) {
    ElementConfigurationPanelProjection projection;
    projection.element = std::move(selection);
    if (!design) {
        projection.state = ElementConfigurationPanelState::NoDesign;
        return projection;
    }
    if (!package) {
        projection.state =
            ElementConfigurationPanelState::PackageUnavailable;
        return projection;
    }
    if (!formatVersionSupportsElementConfigurations(design->formatVersion)
        || !formatVersionSupportsElementConfigurations(package->formatVersion)) {
        projection.state = ElementConfigurationPanelState::UnsupportedFormat;
        return projection;
    }
    if (!projection.element) {
        projection.state = ElementConfigurationPanelState::NoSelection;
        return projection;
    }
    if (!isElementConfigurationTargetKind(projection.element->kind)) {
        projection.state =
            ElementConfigurationPanelState::UnsupportedSelection;
        return projection;
    }
    if (!designReferenceExists(*design, *projection.element)) {
        projection.state = ElementConfigurationPanelState::MissingElement;
        return projection;
    }
    for (const ElementPropertySetDefinition& propertySet
         : package->elementPropertySets) {
        if (propertySetAppliesTo(propertySet, *design, *projection.element)) {
            projection.propertySetIds.append(propertySet.id);
        }
    }
    projection.state = projection.propertySetIds.isEmpty()
        ? ElementConfigurationPanelState::NoApplicablePropertySets
        : ElementConfigurationPanelState::Ready;
    return projection;
}

ElementConfigurationPanel::ElementConfigurationPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.elementConfiguration"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_status = new QLabel(this);
    m_status->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.status"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    m_target = new QLabel(this);
    m_target->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.target"));
    m_target->setWordWrap(true);
    m_target->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_target);

    m_propertySetSelector = new QComboBox(this);
    m_propertySetSelector->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.propertySet"));
    m_propertySetSelector->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    layout->addWidget(m_propertySetSelector);

    m_overrideState = new QLabel(this);
    m_overrideState->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.overrideState"));
    m_overrideState->setWordWrap(true);
    m_overrideState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_overrideState);

    m_formContent = new QWidget(this);
    m_formContent->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.form"));
    m_form = new QFormLayout(m_formContent);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.scroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setMinimumHeight(100);
    m_scroll->setMaximumHeight(280);
    m_scroll->setWidget(m_formContent);
    layout->addWidget(m_scroll);

    auto* buttons = new QHBoxLayout;
    m_apply = new QPushButton(QStringLiteral("Apply"), this);
    m_apply->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    m_reset = new QPushButton(QStringLiteral("Reset to Package Defaults"), this);
    m_reset->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.reset"));
    buttons->addWidget(m_apply);
    buttons->addWidget(m_reset);
    layout->addLayout(buttons);

    connect(m_propertySetSelector, &QComboBox::currentIndexChanged,
            this, [this] { rebuildForm(); });
    connect(m_apply, &QPushButton::clicked, this, [this] {
        const ElementPropertySetDefinition* propertySet = currentPropertySet();
        if (!m_projection.element || !propertySet || !applyRequested) {
            return;
        }
        applyRequested(*m_projection.element,
                       propertySet->id,
                       effectiveValues());
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        const ElementPropertySetDefinition* propertySet = currentPropertySet();
        if (!m_projection.element || !propertySet || !resetRequested) {
            return;
        }
        resetRequested(*m_projection.element, propertySet->id);
    });

    setContext(nullptr, nullptr, std::nullopt);
}

void ElementConfigurationPanel::setContext(
    const NocDesign* design,
    const PackageDefinition* package,
    std::optional<ElementRef> selection,
    bool busy) {
    const QString preferredPropertySet =
        m_propertySetSelector->currentData().toString();
    m_design = design;
    m_package = package;
    m_busy = busy;
    m_projection = projectElementConfigurationPanel(
        design, package, std::move(selection));
    rebuildPropertySetSelector(preferredPropertySet);
    showProjectionMessage();
    rebuildForm();
}

void ElementConfigurationPanel::setBusy(bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    showProjectionMessage();
    updateButtons();
}

void ElementConfigurationPanel::rebuildPropertySetSelector(
    const QString& preferredPropertySet) {
    const QSignalBlocker blocker(m_propertySetSelector);
    m_propertySetSelector->clear();
    if (!m_projection.ready() || !m_package) {
        return;
    }
    for (const QString& propertySetId : m_projection.propertySetIds) {
        const ElementPropertySetDefinition* propertySet =
            m_package->elementPropertySet(propertySetId);
        if (!propertySet) {
            continue;
        }
        const QString label = propertySet->label.trimmed().isEmpty()
            ? propertySet->id
            : propertySet->label;
        m_propertySetSelector->addItem(label, propertySet->id);
    }
    const int preferredIndex =
        m_propertySetSelector->findData(preferredPropertySet);
    if (preferredIndex >= 0) {
        m_propertySetSelector->setCurrentIndex(preferredIndex);
    }
}

void ElementConfigurationPanel::rebuildForm() {
    clearForm();
    m_resolved = false;
    m_initialEffectiveValues = {};
    m_overrideValues = {};

    const ElementPropertySetDefinition* propertySet = currentPropertySet();
    if (!m_projection.ready() || !m_projection.element || !m_design
        || !m_package || !propertySet) {
        updateButtons();
        return;
    }

    const ResolvedElementConfiguration resolved = resolveElementConfiguration(
        *m_design, *m_package, *m_projection.element, propertySet->id);
    if (!resolved.success()) {
        m_status->setText(
            QStringLiteral("<b>Read-only:</b> this stored configuration could "
                           "not be resolved. %1")
                .arg(diagnosticSummary(resolved.diagnostics).toHtmlEscaped()));
        updateButtons();
        return;
    }

    m_resolved = true;
    m_initialEffectiveValues = resolved.properties;
    m_overrideValues = resolved.overrideProperties;
    m_rows.reserve(propertySet->properties.size());
    for (const ElementPropertyDefinition& definition : propertySet->properties) {
        SchemaValueOptions options;
        options.multiple = definition.multiple;
        auto* editor = new SchemaValueEditor(
            static_cast<const ParameterDefinition&>(definition), options,
            m_formContent);
        const QString initialLabel = definition.label.trimmed().isEmpty()
            ? definition.id
            : definition.label;
        editor->setValue(
            resolved.properties.contains(definition.id)
                ? std::optional<QJsonValue>(
                      resolved.properties.value(definition.id))
                : std::nullopt,
            definition.hasDefault
                ? std::optional<QJsonValue>(definition.defaultValue)
                : std::nullopt);
        editor->valueChanged = [this] { updateButtons(); };
        m_form->addRow(initialLabel, editor);
        m_rows.append(PropertyRow{definition, editor});
    }

    m_overrideState->setText(
        m_overrideValues.isEmpty()
            ? QStringLiteral(
                  "Showing Package defaults. No sparse override is stored for this set.")
            : QStringLiteral(
                  "Showing Package defaults plus %1 overridden %2. Reset removes the "
                  "entire sparse override for this set.")
                  .arg(m_overrideValues.size())
                  .arg(m_overrideValues.size() == 1
                           ? QStringLiteral("property")
                           : QStringLiteral("properties")));
    updateButtons();
}

void ElementConfigurationPanel::clearForm() {
    while (QLayoutItem* item = m_form->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_rows.clear();
    m_overrideState->clear();
}

void ElementConfigurationPanel::updateButtons() {
    const bool editable = m_projection.ready() && m_resolved && !m_busy;
    const QStringList errors = localErrors();
    const bool changed = editable
        && effectiveValues() != m_initialEffectiveValues;
    m_propertySetSelector->setEnabled(m_projection.ready() && !m_busy);
    m_scroll->setEnabled(editable);
    m_apply->setEnabled(editable && errors.isEmpty() && changed);
    m_reset->setEnabled(editable && !m_overrideValues.isEmpty());
    if (editable && !errors.isEmpty()) {
        m_apply->setToolTip(errors.join(QLatin1Char('\n')));
    } else if (editable && !changed) {
        m_apply->setToolTip(QStringLiteral("No effective value has changed."));
    } else {
        m_apply->setToolTip({});
    }
}

QJsonObject ElementConfigurationPanel::effectiveValues() const {
    QJsonObject values;
    for (const PropertyRow& row : m_rows) {
        const std::optional<QJsonValue> value = row.editor->value();
        if (value) {
            values.insert(row.definition.id, *value);
        } else if (row.definition.hasDefault) {
            values.insert(row.definition.id, row.definition.defaultValue);
        }
    }
    return values;
}

QStringList ElementConfigurationPanel::localErrors() const {
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

const ElementPropertySetDefinition*
ElementConfigurationPanel::currentPropertySet() const {
    return m_package
        ? m_package->elementPropertySet(
              m_propertySetSelector->currentData().toString())
        : nullptr;
}

void ElementConfigurationPanel::showProjectionMessage() {
    m_target->clear();
    m_target->setVisible(m_projection.element.has_value());
    const bool hasPropertySetUi = m_projection.ready();
    m_propertySetSelector->setVisible(hasPropertySetUi);
    m_overrideState->setVisible(hasPropertySetUi);
    m_scroll->setVisible(hasPropertySetUi);
    m_apply->setVisible(hasPropertySetUi);
    m_reset->setVisible(hasPropertySetUi);
    if (m_projection.element) {
        m_target->setText(
            QStringLiteral("<b>%1</b><br><code>%2</code>")
                .arg(elementKindLabel(m_projection.element->kind).toHtmlEscaped(),
                     m_projection.element->id.toHtmlEscaped()));
    }

    switch (m_projection.state) {
    case ElementConfigurationPanelState::NoDesign:
        m_status->setText(QStringLiteral(
            "Open or create a design to inspect element implementation properties."));
        break;
    case ElementConfigurationPanelState::PackageUnavailable:
        m_status->setText(QStringLiteral(
            "<b>Read-only:</b> the design Package is not loaded, so its arbitrary "
            "element property schemas are unavailable."));
        break;
    case ElementConfigurationPanelState::UnsupportedFormat:
        m_status->setText(QStringLiteral(
            "<b>Read-only:</b> per-element configuration requires both Design and "
            "Package formatVersion 3."));
        break;
    case ElementConfigurationPanelState::NoSelection:
        m_status->setText(QStringLiteral(
            "Select one Router, Router Link, or Endpoint Attachment to edit its "
            "Package-defined implementation properties."));
        break;
    case ElementConfigurationPanelState::UnsupportedSelection:
        m_status->setText(QStringLiteral(
            "This selection has no element property sets. Endpoint instance parameters "
            "remain a separate Endpoint-owned configuration and are not mixed into this panel."));
        break;
    case ElementConfigurationPanelState::MissingElement:
        m_status->setText(QStringLiteral(
            "<b>Read-only:</b> the selected semantic element is not present in the "
            "current Mesh projection."));
        break;
    case ElementConfigurationPanelState::NoApplicablePropertySets:
        m_status->setText(QStringLiteral(
            "<b>Read-only:</b> the Package declares no property set applicable to "
            "this element kind or Endpoint type."));
        break;
    case ElementConfigurationPanelState::Ready:
        if (m_busy) {
            m_status->setText(QStringLiteral(
                "<b>Read-only:</b> element configuration is locked while validation or "
                "generation is running."));
        } else if (m_projection.element
                   && m_projection.element->kind == ElementKind::Router) {
            m_status->setText(QStringLiteral(
                "Edit Package-defined Router implementation properties here. Router "
                "identity, creation, deletion, and links remain generated by the fixed Mesh."));
        } else if (m_projection.element
                   && m_projection.element->kind == ElementKind::RouterLink) {
            m_status->setText(QStringLiteral(
                "Edit Package-defined implementation properties only. This Router Link "
                "remains generated by the fixed Mesh and cannot be rewired manually."));
        } else {
            m_status->setText(QStringLiteral(
                "Edit Package-defined properties for this semantic Endpoint Attachment. "
                "Endpoint instance parameters remain separate."));
        }
        break;
    }
}

} // namespace finepaper
