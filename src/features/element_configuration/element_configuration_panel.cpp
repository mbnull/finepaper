#include "features/element_configuration/element_configuration_panel.h"

#include "application/element_configuration.h"
#include "package/parameter_schema_identity.h"
#include "ui/common/focus_target.h"
#include "ui/common/schema_value_editor.h"
#include "ui/theme/ui_tokens.h"

#include <QComboBox>
#include <QCollator>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
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
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ui::UiMetrics::spacing8);

    m_heading = new QLabel(QStringLiteral("Element properties"), this);
    m_heading->setProperty("finepaperRole", QStringLiteral("subtitle"));
    layout->addWidget(m_heading);

    m_status = new QLabel(this);
    m_status->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.status"));
    m_status->setWordWrap(true);
    m_status->setMinimumWidth(0);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    m_propertySetSelector = new QComboBox(this);
    m_propertySetSelector->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.propertySet"));
    m_propertySetSelector->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto* propertySetForm = new QFormLayout;
    propertySetForm->setContentsMargins(0, 0, 0, 0);
    propertySetForm->setFieldGrowthPolicy(
        QFormLayout::AllNonFixedFieldsGrow);
    propertySetForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    propertySetForm->addRow(
        QStringLiteral("Property set"), m_propertySetSelector);
    layout->addLayout(propertySetForm);

    m_overrideState = new QLabel(this);
    m_overrideState->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.overrideState"));
    m_overrideState->setWordWrap(true);
    m_overrideState->setMinimumWidth(0);
    m_overrideState->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_overrideState);

    m_draftStatus = new QLabel(this);
    m_draftStatus->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.draftStatus"));
    m_draftStatus->setProperty(
        "finepaperRole", QStringLiteral("warning"));
    m_draftStatus->setWordWrap(true);
    m_draftStatus->setMinimumWidth(0);
    m_draftStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_draftStatus->hide();
    layout->addWidget(m_draftStatus);

    m_formContent = new QWidget(this);
    m_formContent->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.form"));
    m_formContent->setMinimumWidth(0);
    m_formContent->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_form = new QFormLayout(m_formContent);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form->setHorizontalSpacing(ui::UiMetrics::spacing8);
    m_form->setVerticalSpacing(ui::UiMetrics::spacing8);
    layout->addWidget(m_formContent);

    auto* buttons = new QVBoxLayout;
    buttons->setSpacing(ui::UiMetrics::spacing8);
    m_apply = new QPushButton(QStringLiteral("Apply"), this);
    m_apply->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    m_apply->setProperty(
        "finepaperRole", QStringLiteral("primary"));
    m_reset = new QPushButton(QStringLiteral("Reset to Package Defaults"), this);
    m_reset->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.reset"));
    m_reset->setText(QStringLiteral("Reset all to Package defaults"));
    m_reset->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    m_discardDraft = new QPushButton(
        QStringLiteral("Discard Unapplied Changes"), this);
    m_discardDraft->setObjectName(
        QStringLiteral("finepaper.elementConfiguration.discardDraft"));
    m_discardDraft->setProperty(
        "finepaperRole", QStringLiteral("quiet"));
    m_discardDraft->hide();
    buttons->addWidget(m_apply);
    buttons->addWidget(m_reset);
    buttons->addWidget(m_discardDraft);
    layout->addLayout(buttons);

    connect(m_propertySetSelector, &QComboBox::currentIndexChanged,
            this, [this] {
                captureCurrentDraft();
                rebuildForm();
            });
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
    connect(m_discardDraft, &QPushButton::clicked, this, [this] {
        if (!m_projection.element || m_currentPropertySetId.isEmpty()) {
            return;
        }
        discardDraft(
            m_contextStamp.designIdentity,
            *m_projection.element,
            m_currentPropertySetId);
    });

    setContext(nullptr, nullptr, std::nullopt, {});
}

void ElementConfigurationPanel::setContext(
    const NocDesign* design,
    const PackageDefinition* package,
    std::optional<ElementRef> selection,
    ElementConfigurationContextStamp stamp,
    bool busy) {
    if (m_hasContext && m_design == design && m_package == package
        && m_contextStamp == stamp && m_projection.element == selection) {
        setBusy(busy);
        return;
    }

    captureCurrentDraft();
    const QString preferredPropertySet =
        m_propertySetSelector->currentData().toString();
    m_design = design;
    m_package = package;
    m_contextStamp = std::move(stamp);
    m_hasContext = true;
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

QWidget* ElementConfigurationPanel::preferredFocusTarget() {
    QWidget* firstValue = m_rows.isEmpty() || !m_rows.front().editor
        ? nullptr : m_rows.front().editor->primaryInput();
    return ui::firstAvailableFocusTarget(
        this,
        {m_discardDraft, firstValue, m_propertySetSelector, m_apply, m_reset});
}

bool ElementConfigurationPanel::hasUnappliedDrafts(
    const QString& designIdentity) const {
    const auto drafts = m_drafts.constFind(designIdentity);
    return drafts != m_drafts.cend() && !drafts->isEmpty();
}

bool ElementConfigurationPanel::hasUnappliedDraft(
    const QString& designIdentity,
    const ElementRef& element) const {
    const auto drafts = m_drafts.constFind(designIdentity);
    return drafts != m_drafts.cend()
        && std::any_of(
            drafts->cbegin(), drafts->cend(),
            [&element](const CachedDraft& draft) {
                return draft.element == element;
            });
}

QStringList ElementConfigurationPanel::unappliedDraftDescriptions(
    const QString& designIdentity) const {
    QStringList descriptions;
    const auto drafts = m_drafts.constFind(designIdentity);
    if (drafts == m_drafts.cend()) {
        return descriptions;
    }
    descriptions.reserve(drafts->size());
    for (const CachedDraft& draft : *drafts) {
        QString propertySetLabel = draft.propertySetId;
        if (m_package) {
            if (const ElementPropertySetDefinition* propertySet =
                    m_package->elementPropertySet(draft.propertySetId)) {
                propertySetLabel = propertySet->label.trimmed().isEmpty()
                    ? propertySet->id : propertySet->label;
            }
        }
        descriptions.append(
            QStringLiteral("%1 %2 — %3")
                .arg(elementKindLabel(draft.element.kind),
                     draft.element.id,
                     propertySetLabel));
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(
        descriptions.begin(), descriptions.end(),
        [&collator](const QString& left, const QString& right) {
            const int comparison = collator.compare(left, right);
            return comparison == 0 ? left < right : comparison < 0;
        });
    return descriptions;
}

void ElementConfigurationPanel::discardDraft(
    const QString& designIdentity,
    const ElementRef& element,
    const QString& propertySetId) {
    auto drafts = m_drafts.find(designIdentity);
    if (drafts == m_drafts.end()) {
        return;
    }
    const qsizetype removed = drafts->removeIf(
        [&element, &propertySetId](const CachedDraft& draft) {
            return draft.element == element
                && draft.propertySetId == propertySetId;
        });
    if (removed == 0) {
        return;
    }
    if (drafts->isEmpty()) {
        m_drafts.erase(drafts);
    }
    if (m_contextStamp.designIdentity == designIdentity
        && m_projection.element
        && *m_projection.element == element
        && m_currentPropertySetId == propertySetId) {
        rebuildForm();
    }
    notifyDraftStateChanged();
}

void ElementConfigurationPanel::discardDraftsForElement(
    const QString& designIdentity,
    const ElementRef& element) {
    auto drafts = m_drafts.find(designIdentity);
    if (drafts == m_drafts.end()) {
        return;
    }
    const qsizetype removed = drafts->removeIf(
        [&element](const CachedDraft& draft) {
            return draft.element == element;
        });
    if (removed == 0) {
        return;
    }
    if (drafts->isEmpty()) {
        m_drafts.erase(drafts);
    }
    if (m_contextStamp.designIdentity == designIdentity
        && m_projection.element
        && *m_projection.element == element) {
        rebuildForm();
    }
    notifyDraftStateChanged();
}

void ElementConfigurationPanel::clearDraftsForDesign(
    const QString& designIdentity) {
    if (m_drafts.remove(designIdentity) == 0) {
        return;
    }
    if (m_contextStamp.designIdentity == designIdentity) {
        rebuildForm();
    }
    notifyDraftStateChanged();
}

void ElementConfigurationPanel::clearDrafts() {
    if (m_drafts.isEmpty()) {
        return;
    }
    m_drafts.clear();
    rebuildForm();
    notifyDraftStateChanged();
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
    m_resolutionFailureStatus.clear();
    if (m_projection.ready()) {
        // A previous property set may have exposed a resolution error. Reset
        // the status before resolving the newly selected set so stale errors
        // do not remain visible after a successful rebuild.
        showProjectionMessage();
    }
    m_resolved = false;
    m_draftConflict = false;
    m_currentPropertySetId.clear();
    m_currentSchemaIdentity.clear();
    m_initialEffectiveValues = {};
    m_overrideValues = {};
    m_initialEditorState.clear();

    const ElementPropertySetDefinition* propertySet = currentPropertySet();
    if (!m_projection.ready() || !m_projection.element || !m_design
        || !m_package || !propertySet) {
        updateButtons();
        return;
    }

    const ResolvedElementConfiguration resolved = resolveElementConfiguration(
        *m_design, *m_package, *m_projection.element, propertySet->id);
    m_currentPropertySetId = propertySet->id;
    m_currentSchemaIdentity = elementPropertySchemaIdentity(
        propertySet->properties);
    m_initialEffectiveValues = resolved.properties;
    m_overrideValues = resolved.overrideProperties;
    if (!resolved.success()) {
        m_resolutionFailureStatus = QStringLiteral(
            "<b>Read-only:</b> this stored configuration could not be "
            "resolved. %1 Reset all to Package defaults remains available "
            "when a stored override can be removed safely.")
            .arg(diagnosticSummary(resolved.diagnostics).toHtmlEscaped());
        m_overrideState->setText(
            m_overrideValues.isEmpty()
                ? QStringLiteral("No removable Design override was found.")
                : QStringLiteral(
                      "The invalid Design override can be reset to Package defaults."));
        showProjectionMessage();
        updateButtons();
        return;
    }

    m_resolved = true;
    m_rows.reserve(propertySet->properties.size());
    for (const ElementPropertyDefinition& definition : propertySet->properties) {
        SchemaValueOptions options;
        options.multiple = definition.multiple;
        options.required = true;
        options.validationMode = PropertyValidationMode::Complete;
        options.presenceSemantics =
            ValuePresenceSemantics::SparseOverride;
        auto* field = new QWidget(m_formContent);
        field->setObjectName(
            QStringLiteral("finepaper.elementConfiguration.field.%1")
                .arg(definition.id));
        field->setMinimumWidth(0);
        field->setSizePolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(ui::UiMetrics::spacing4);
        auto* editor = new SchemaValueEditor(
            static_cast<const ParameterDefinition&>(definition), options,
            field);
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
        auto* origin = new QLabel(field);
        origin->setObjectName(
            QStringLiteral("finepaper.elementConfiguration.origin.%1")
                .arg(definition.id));
        origin->setProperty("finepaperRole", QStringLiteral("muted"));
        origin->setTextFormat(Qt::PlainText);
        origin->setWordWrap(true);
        origin->setMinimumWidth(0);
        auto* usePackageDefault = new QPushButton(
            QStringLiteral("Use Package default"), field);
        usePackageDefault->setObjectName(
            QStringLiteral("finepaper.elementConfiguration.useDefault.%1")
                .arg(definition.id));
        usePackageDefault->setProperty(
            "finepaperRole", QStringLiteral("quiet"));
        usePackageDefault->setAccessibleDescription(
            QStringLiteral(
                "Remove this property's Design override and use the Package default."));
        fieldLayout->addWidget(origin);
        fieldLayout->addWidget(editor);
        fieldLayout->addWidget(usePackageDefault);
        connect(usePackageDefault, &QPushButton::clicked,
                this, [this, editor, defaultValue = definition.defaultValue] {
                    editor->setValue(defaultValue);
                    updateButtons();
                });
        auto* label = new QLabel(initialLabel);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMinimumWidth(0);
        QSizePolicy labelPolicy(
            QSizePolicy::Ignored, QSizePolicy::Preferred);
        labelPolicy.setHeightForWidth(true);
        label->setSizePolicy(labelPolicy);
        label->setBuddy(editor->primaryInput());
        m_form->addRow(label, field);
        m_rows.append(PropertyRow{
            definition, editor, origin, usePackageDefault});
    }

    m_overrideState->setText(
        m_overrideValues.isEmpty()
            ? QStringLiteral("Using Package defaults.")
            : QStringLiteral(
                  "%1 %2 overridden. Reset removes this property-set override.")
                  .arg(m_overrideValues.size())
                  .arg(m_overrideValues.size() == 1
                           ? QStringLiteral("property")
                           : QStringLiteral("properties")));
    m_initialEditorState = currentEditorState();
    restoreCachedDraft();
    updateButtons();
}

void ElementConfigurationPanel::clearForm() {
    while (m_form->rowCount() > 0) {
        m_form->removeRow(m_form->rowCount() - 1);
    }
    m_rows.clear();
    m_overrideState->clear();
    if (m_draftStatus) {
        m_draftStatus->clear();
        m_draftStatus->hide();
    }
}

void ElementConfigurationPanel::updateButtons() {
    const bool editable = m_projection.ready() && m_resolved && !m_busy;
    const QStringList errors = localErrors();
    const QHash<QString, SchemaValueEditorDraft> editorState =
        currentEditorState();
    const bool changed = editable
        && editorState != m_initialEditorState;
    for (const PropertyRow& row : std::as_const(m_rows)) {
        if (!row.editor || !row.origin || !row.usePackageDefault) {
            continue;
        }
        const std::optional<QJsonValue> current = row.editor->value();
        const bool usesPackageDefault = current
            && row.definition.hasDefault
            && *current == row.definition.defaultValue;
        const auto initial = m_initialEditorState.constFind(
            row.definition.id);
        const bool rowChanged = initial == m_initialEditorState.cend()
            || row.editor->draftState() != *initial;
        row.origin->setText(
            usesPackageDefault
                ? QStringLiteral("Package default")
                : rowChanged
                ? QStringLiteral("Unapplied Design override")
                : QStringLiteral("Design override"));
        row.origin->setAccessibleDescription(
            usesPackageDefault
                ? QStringLiteral(
                      "This effective value comes from the Package default.")
                : QStringLiteral(
                      "This effective value is stored as a sparse Design override."));
        row.usePackageDefault->setVisible(!usesPackageDefault);
        row.usePackageDefault->setEnabled(
            editable && !m_draftConflict && !usesPackageDefault);
    }
    m_propertySetSelector->setEnabled(m_projection.ready() && !m_busy);
    m_formContent->setEnabled(editable && !m_draftConflict);
    m_apply->setEnabled(
        editable && !m_draftConflict && errors.isEmpty() && changed);
    m_reset->setEnabled(
        m_projection.ready() && !m_busy && !m_draftConflict
        && !m_overrideValues.isEmpty());
    m_discardDraft->setVisible(changed || m_draftConflict);
    m_discardDraft->setEnabled(!m_busy && (changed || m_draftConflict));
    if (editable && !errors.isEmpty()) {
        m_apply->setToolTip(errors.join(QLatin1Char('\n')));
    } else if (editable && !changed) {
        m_apply->setToolTip(QStringLiteral("No effective value has changed."));
    } else {
        m_apply->setToolTip({});
    }
    if (m_draftStatus) {
        m_draftStatus->setVisible(changed || m_draftConflict);
        if (m_draftConflict) {
            m_draftStatus->setProperty(
                "finepaperRole", QStringLiteral("error"));
            m_draftStatus->setText(QStringLiteral(
                "Draft conflict: the durable element configuration or Package "
                "schema changed after this draft was created. The draft is "
                "preserved read-only; discard it before editing the new source "
                "values."));
        } else if (changed) {
            m_draftStatus->setProperty(
                "finepaperRole", QStringLiteral("warning"));
            m_draftStatus->setText(
                errors.isEmpty()
                    ? QStringLiteral(
                          "Unapplied element changes. Apply them before Save, "
                          "Validate, or Generate RTL, or explicitly discard "
                          "the draft when prompted.")
                    : QStringLiteral(
                          "This unapplied element draft has validation errors. "
                          "Correct them or discard the draft before continuing."));
        }
    }
    captureCurrentDraft(editorState);
}

void ElementConfigurationPanel::captureCurrentDraft() {
    if (m_updating || m_draftConflict
        || m_contextStamp.designIdentity.isEmpty()
        || !m_projection.element || m_currentPropertySetId.isEmpty()
        || !m_resolved) {
        return;
    }
    captureCurrentDraft(currentEditorState());
}

void ElementConfigurationPanel::captureCurrentDraft(
    const QHash<QString, SchemaValueEditorDraft>& editorState) {
    if (m_updating || m_draftConflict
        || m_contextStamp.designIdentity.isEmpty()
        || !m_projection.element || m_currentPropertySetId.isEmpty()
        || !m_resolved) {
        return;
    }

    QVector<CachedDraft>* designDrafts = nullptr;
    auto existingDesign = m_drafts.find(m_contextStamp.designIdentity);
    if (existingDesign != m_drafts.end()) {
        designDrafts = &*existingDesign;
    }
    const auto matchingDraft = [&](const CachedDraft& draft) {
        return draft.element == *m_projection.element
            && draft.propertySetId == m_currentPropertySetId;
    };

    if (editorState == m_initialEditorState) {
        if (!designDrafts) {
            return;
        }
        const qsizetype removed = designDrafts->removeIf(matchingDraft);
        if (removed == 0) {
            return;
        }
        if (designDrafts->isEmpty()) {
            m_drafts.erase(existingDesign);
        }
        notifyDraftStateChanged();
        return;
    }

    CachedDraft draft = {
        *m_projection.element,
        m_currentPropertySetId,
        m_currentSchemaIdentity,
        m_initialEffectiveValues,
        m_overrideValues,
        editorState,
    };
    if (!designDrafts) {
        existingDesign = m_drafts.insert(
            m_contextStamp.designIdentity, QVector<CachedDraft>{});
        designDrafts = &*existingDesign;
    }
    const auto existing = std::find_if(
        designDrafts->begin(), designDrafts->end(), matchingDraft);
    if (existing != designDrafts->end()) {
        if (*existing == draft) {
            return;
        }
        *existing = std::move(draft);
    } else {
        designDrafts->append(std::move(draft));
    }
    notifyDraftStateChanged();
}

void ElementConfigurationPanel::restoreCachedDraft() {
    if (m_contextStamp.designIdentity.isEmpty() || !m_projection.element
        || m_currentPropertySetId.isEmpty()) {
        return;
    }
    auto designDrafts = m_drafts.find(m_contextStamp.designIdentity);
    if (designDrafts == m_drafts.end()) {
        return;
    }
    const auto draft = std::find_if(
        designDrafts->begin(), designDrafts->end(),
        [this](const CachedDraft& value) {
            return value.element == *m_projection.element
                && value.propertySetId == m_currentPropertySetId;
        });
    if (draft == designDrafts->end()) {
        return;
    }
    bool schemaCompatible = !m_currentSchemaIdentity.isEmpty()
        && draft->sourceSchemaIdentity == m_currentSchemaIdentity
        && draft->editorState.size() == m_initialEditorState.size();
    if (schemaCompatible) {
        for (auto state = m_initialEditorState.cbegin();
             state != m_initialEditorState.cend(); ++state) {
            if (!draft->editorState.contains(state.key())) {
                schemaCompatible = false;
                break;
            }
        }
    }
    m_draftConflict = draft->sourceEffectiveValues
            != m_initialEffectiveValues
        || draft->sourceOverrideValues != m_overrideValues
        || !schemaCompatible;
    if (schemaCompatible) {
        m_updating = true;
        for (PropertyRow& row : m_rows) {
            const auto state = draft->editorState.constFind(row.definition.id);
            if (state != draft->editorState.cend()) {
                row.editor->setDraftState(*state);
            }
        }
        m_updating = false;
    }
}

void ElementConfigurationPanel::notifyDraftStateChanged() {
    const bool draftPending = !m_contextStamp.designIdentity.isEmpty()
        && hasUnappliedDrafts(m_contextStamp.designIdentity);
    if (m_reportedDraftPending == draftPending) {
        return;
    }
    m_reportedDraftPending = draftPending;
    if (draftStateChanged) {
        draftStateChanged();
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

QHash<QString, SchemaValueEditorDraft>
ElementConfigurationPanel::currentEditorState() const {
    QHash<QString, SchemaValueEditorDraft> state;
    state.reserve(m_rows.size());
    for (const PropertyRow& row : m_rows) {
        if (row.editor) {
            state.insert(row.definition.id, row.editor->draftState());
        }
    }
    return state;
}

bool ElementConfigurationPanel::isModified() const {
    return currentEditorState() != m_initialEditorState;
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
    const bool hasPropertySetUi = m_projection.ready();
    m_heading->setVisible(hasPropertySetUi);
    m_propertySetSelector->setVisible(hasPropertySetUi);
    m_overrideState->setVisible(hasPropertySetUi);
    m_formContent->setVisible(hasPropertySetUi);
    m_apply->setVisible(hasPropertySetUi);
    m_reset->setVisible(hasPropertySetUi);
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
        if (!m_resolutionFailureStatus.isEmpty()) {
            m_status->setText(m_resolutionFailureStatus);
        } else if (m_busy) {
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
    m_status->setVisible(
        !hasPropertySetUi || m_busy
        || !m_resolutionFailureStatus.isEmpty());
}

} // namespace finepaper
