#include "gui/endpoint_configuration_panel.h"

#include "features/domain/endpoint_domain_assignment_dialog.h"
#include "gui/package_parameter_form.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QJsonObject parameterDefaults(const EndpointTypeDefinition& type) {
    QJsonObject result;
    for (const ParameterDefinition& definition : type.parameters) {
        if (definition.hasDefault) {
            result.insert(definition.id, definition.defaultValue);
        }
    }
    return result;
}

QString endpointTypeLabel(const EndpointTypeDefinition& type) {
    return type.label.trimmed().isEmpty() ? type.id : type.label;
}

QString diagnosticLine(const Diagnostic& diagnostic) {
    return diagnostic.path.trimmed().isEmpty()
        ? QStringLiteral("%1: %2").arg(diagnostic.code, diagnostic.message)
        : QStringLiteral("%1: %2 (%3)")
              .arg(diagnostic.code, diagnostic.message, diagnostic.path);
}

QString configurationLine(const ElementConfiguration& configuration) {
    return QStringLiteral("%1 / %2 = %3")
        .arg(configuration.element.id,
             configuration.propertySet,
             QString::fromUtf8(
                 QJsonDocument(configuration.properties)
                     .toJson(QJsonDocument::Compact)));
}

bool sameEndpoint(const std::optional<EndpointInstance>& lhs,
                  const std::optional<EndpointInstance>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    return lhs->id == rhs->id
        && lhs->type == rhs->type
        && lhs->parameters == rhs->parameters;
}

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

EndpointCreationDialog::EndpointCreationDialog(
    const NocDesign& design,
    const PackageDefinition& package,
    QString suggestedType,
    QString suggestedId,
    QWidget* parent)
    : QDialog(parent),
      m_design(design),
      m_package(package) {
    setObjectName(QStringLiteral("finepaper.endpointCreationDialog"));
    setWindowTitle(QStringLiteral("Configure Endpoint"));
    setModal(true);
    resize(720, 720);

    auto* root = new QVBoxLayout(this);
    auto* introduction = new QLabel(
        QStringLiteral(
            "Configure the Endpoint instance before it is added. Identity, "
            "type, parameters and Domain membership are Package-driven; the "
            "Router attachment is chosen on the canvas."),
        this);
    introduction->setObjectName(
        QStringLiteral("finepaper.endpointCreation.introduction"));
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("finepaper.endpointCreation.tabs"));
    auto* endpointPage = new QWidget(tabs);
    auto* endpointLayout = new QVBoxLayout(endpointPage);
    auto* identity = new QGroupBox(QStringLiteral("Endpoint Identity"), endpointPage);
    auto* identityForm = new QFormLayout(identity);
    m_id = new QLineEdit(std::move(suggestedId), identity);
    m_id->setObjectName(QStringLiteral("finepaper.endpointCreation.id"));
    m_id->setPlaceholderText(QStringLiteral("Unique Endpoint ID"));
    identityForm->addRow(QStringLiteral("Endpoint ID / name"), m_id);
    m_type = new QComboBox(identity);
    m_type->setObjectName(QStringLiteral("finepaper.endpointCreation.type"));
    for (const EndpointTypeDefinition& type : m_package.endpointTypes) {
        m_type->addItem(
            QStringLiteral("%1 (%2)").arg(endpointTypeLabel(type), type.id),
            type.id);
    }
    const int suggestedIndex = m_type->findData(suggestedType);
    if (suggestedIndex >= 0) {
        m_type->setCurrentIndex(suggestedIndex);
    }
    identityForm->addRow(QStringLiteral("Endpoint type"), m_type);
    endpointLayout->addWidget(identity);

    auto* parameterGroup = new QGroupBox(
        QStringLiteral("Endpoint Parameters"), endpointPage);
    auto* parameterLayout = new QVBoxLayout(parameterGroup);
    auto* parameterNote = new QLabel(
        QStringLiteral(
            "These values belong to the Endpoint instance. Attachment "
            "configuration is edited separately by selecting its connection."),
        parameterGroup);
    parameterNote->setObjectName(
        QStringLiteral("finepaper.endpointCreation.parameterNote"));
    parameterNote->setWordWrap(true);
    parameterLayout->addWidget(parameterNote);
    m_parameters = new PackageParameterForm(
        QStringLiteral("finepaper.endpointCreation.parameter"), parameterGroup);
    auto* parameterScroll = new QScrollArea(parameterGroup);
    parameterScroll->setObjectName(
        QStringLiteral("finepaper.endpointCreation.parameterScroll"));
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setMinimumHeight(260);
    parameterScroll->setWidget(m_parameters);
    parameterLayout->addWidget(parameterScroll, 1);
    endpointLayout->addWidget(parameterGroup, 1);
    tabs->addTab(endpointPage, QStringLiteral("Endpoint"));

    const QVector<EndpointDomainAssignmentGroup> domainGroups =
        buildEndpointDomainAssignmentGroups(m_design, m_package);
    if (endpointDomainAssignmentsRequireUserDecision(domainGroups)) {
        auto* domainPage = new QWidget(tabs);
        auto* domainLayout = new QVBoxLayout(domainPage);
        m_domains = new EndpointDomainAssignmentEditor(
            m_design, m_package, {}, domainPage);
        domainLayout->addWidget(m_domains, 1);
        tabs->addTab(domainPage, QStringLiteral("Domains"));
        m_domains->validationChanged = [this] { updateValidation(); };
    } else {
        m_automaticAssignments = endpointDomainAssignmentsFromGroups(domainGroups);
        m_domainSummary = new QLabel(
            domainGroups.isEmpty()
                ? QStringLiteral(
                      "This Package declares no Endpoint Domain assignment.")
                : QStringLiteral(
                      "Required Endpoint Domains have one available instance "
                      "and will be assigned automatically."),
            endpointPage);
        m_domainSummary->setObjectName(
            QStringLiteral("finepaper.endpointCreation.automaticDomains"));
        m_domainSummary->setWordWrap(true);
        endpointLayout->addWidget(m_domainSummary);
    }
    root->addWidget(tabs, 1);

    m_diagnostics = new QLabel(this);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.endpointCreation.diagnostics"));
    m_diagnostics->setTextFormat(Qt::PlainText);
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_diagnostics);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(
        QStringLiteral("finepaper.endpointCreation.buttons"));
    m_acceptButton = m_buttons->button(QDialogButtonBox::Ok);
    m_acceptButton->setObjectName(
        QStringLiteral("finepaper.endpointCreation.accept"));
    m_acceptButton->setText(QStringLiteral("Add Endpoint"));
    root->addWidget(m_buttons);

    connect(m_id, &QLineEdit::textChanged,
            this, [this] { updateValidation(); });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] {
        rebuildParameters();
        updateValidation();
    });
    m_parameters->valueChanged = [this] { updateValidation(); };
    connect(m_buttons, &QDialogButtonBox::accepted,
            this, [this] { accept(); });
    connect(m_buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    rebuildParameters();
    updateValidation();
}

EndpointCreationDraft EndpointCreationDialog::draft() const {
    EndpointCreationDraft result;
    result.id = m_id ? m_id->text().trimmed() : QString();
    result.type = m_type ? m_type->currentData().toString() : QString();
    result.parameters = m_parameters ? m_parameters->values() : QJsonObject{};
    result.domainAssignments = m_domains
        ? m_domains->assignments() : m_automaticAssignments;
    return result;
}

QStringList EndpointCreationDialog::localErrors() const {
    QStringList errors;
    const EndpointCreationDraft value = draft();
    if (value.id.isEmpty()) {
        errors.append(QStringLiteral("Enter a non-empty Endpoint ID / name."));
    } else if (std::any_of(
                   m_design.endpoints.cbegin(), m_design.endpoints.cend(),
                   [&](const EndpointInstance& endpoint) {
                       return endpoint.id == value.id;
                   })) {
        errors.append(QStringLiteral("Endpoint ID %1 is already in use.")
                          .arg(value.id));
    }
    if (!selectedType()) {
        errors.append(QStringLiteral("Choose an Endpoint type declared by the Package."));
    }
    if (m_parameters) {
        errors += m_parameters->localErrors();
    }
    if (m_domains) {
        errors += m_domains->localErrors();
    }
    return errors;
}

void EndpointCreationDialog::accept() {
    updateValidation();
    if (m_acceptButton && m_acceptButton->isEnabled()) {
        QDialog::accept();
    }
}

void EndpointCreationDialog::rebuildParameters() {
    if (m_updating || !m_parameters) {
        return;
    }
    m_updating = true;
    if (const EndpointTypeDefinition* type = selectedType()) {
        m_parameters->setSchema(type->parameters, parameterDefaults(*type));
    } else {
        m_parameters->setSchema({}, {});
    }
    m_updating = false;
}

void EndpointCreationDialog::updateValidation() {
    if (m_updating || !m_diagnostics || !m_acceptButton) {
        return;
    }
    const QStringList errors = localErrors();
    m_acceptButton->setEnabled(errors.isEmpty());
    m_diagnostics->setText(
        errors.isEmpty()
            ? QStringLiteral("Endpoint configuration is ready to add.")
            : errors.join(QLatin1Char('\n')));
}

const EndpointTypeDefinition* EndpointCreationDialog::selectedType() const {
    return m_type
        ? m_package.endpointType(m_type->currentData().toString())
        : nullptr;
}

EndpointConfigurationPanel::EndpointConfigurationPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.endpointConfigurationPanel"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_status = new QLabel(this);
    m_status->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.status"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::RichText);
    root->addWidget(m_status);

    m_editor = new QWidget(this);
    m_editor->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.editor"));
    auto* editorLayout = new QVBoxLayout(m_editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);

    auto* identity = new QGroupBox(QStringLiteral("Endpoint Identity"), m_editor);
    auto* identityForm = new QFormLayout(identity);
    identityForm->setFieldGrowthPolicy(
        QFormLayout::AllNonFixedFieldsGrow);
    identityForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_id = new QLabel(identity);
    m_id->setObjectName(QStringLiteral("finepaper.endpointConfiguration.id"));
    m_id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identityForm->addRow(QStringLiteral("Stable ID / name"), m_id);
    m_type = new QComboBox(identity);
    m_type->setObjectName(QStringLiteral("finepaper.endpointConfiguration.type"));
    m_type->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_type->setMinimumContentsLength(12);
    identityForm->addRow(QStringLiteral("Endpoint type"), m_type);
    m_migration = new QComboBox(identity);
    m_migration->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.migration"));
    m_migration->addItem(
        QStringLiteral("Reset target parameters to Package defaults"),
        static_cast<int>(EndpointParameterMigration::ResetToDefaults));
    m_migration->addItem(
        QStringLiteral("Preserve compatible values, default the rest"),
        static_cast<int>(EndpointParameterMigration::PreserveCompatible));
    m_migration->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_migration->setMinimumContentsLength(12);
    identityForm->addRow(QStringLiteral("Type-change migration"), m_migration);
    m_migrationLabel = qobject_cast<QLabel*>(
        identityForm->labelForField(m_migration));
    if (m_migrationLabel) {
        m_migrationLabel->setObjectName(
            QStringLiteral("finepaper.endpointConfiguration.migrationLabel"));
        m_migrationLabel->setWordWrap(true);
    }
    editorLayout->addWidget(identity);

    m_typeChangeSummary = new QLabel(m_editor);
    m_typeChangeSummary->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.typeChangeSummary"));
    m_typeChangeSummary->setWordWrap(true);
    m_typeChangeSummary->setTextFormat(Qt::RichText);
    m_typeChangeSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* summaryScroll = new QScrollArea(m_editor);
    summaryScroll->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.typeChangeSummaryScroll"));
    summaryScroll->setWidgetResizable(true);
    summaryScroll->setFrameShape(QFrame::NoFrame);
    summaryScroll->setMaximumHeight(180);
    summaryScroll->setWidget(m_typeChangeSummary);
    editorLayout->addWidget(summaryScroll);

    auto* parameters = new QGroupBox(
        QStringLiteral("Endpoint Parameters"), m_editor);
    auto* parameterLayout = new QVBoxLayout(parameters);
    auto* note = new QLabel(
        QStringLiteral(
            "Only Endpoint-instance parameters are edited here. Select the "
            "Endpoint attachment line to edit Package-defined attachment properties."),
        parameters);
    note->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.attachmentNote"));
    note->setWordWrap(true);
    parameterLayout->addWidget(note);
    m_parameters = new PackageParameterForm(
        QStringLiteral("finepaper.endpointParameter"), parameters);
    auto* parameterScroll = new QScrollArea(parameters);
    parameterScroll->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.parameterScroll"));
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setMinimumHeight(220);
    parameterScroll->setWidget(m_parameters);
    parameterLayout->addWidget(parameterScroll, 1);
    editorLayout->addWidget(parameters, 1);

    m_diagnostics = new QLabel(m_editor);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.diagnostics"));
    m_diagnostics->setTextFormat(Qt::PlainText);
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    editorLayout->addWidget(m_diagnostics);
    m_apply = new QPushButton(QStringLiteral("Apply Endpoint Configuration"), m_editor);
    m_apply->setObjectName(
        QStringLiteral("finepaper.endpointConfiguration.apply"));
    editorLayout->addWidget(m_apply);
    root->addWidget(m_editor);

    connect(m_type, &QComboBox::currentIndexChanged,
            this, [this] { rebuildTargetParameters(); });
    connect(m_migration, &QComboBox::currentIndexChanged,
            this, [this] { rebuildTargetParameters(); });
    m_parameters->valueChanged = [this] { updateValidation(); };
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    setContext(nullptr, nullptr, {}, std::nullopt, false);
}

void EndpointConfigurationPanel::setContext(
    const NocDesign* design,
    const PackageDefinition* package,
    QString designIdentity,
    std::optional<QString> endpointId,
    bool busy) {
    captureCurrentDraft();

    std::optional<EndpointInstance> nextEndpoint;
    if (design && endpointId) {
        const auto endpoint = std::find_if(
            design->endpoints.cbegin(), design->endpoints.cend(),
            [&](const EndpointInstance& value) {
                return value.id == *endpointId;
            });
        if (endpoint != design->endpoints.cend()) {
            nextEndpoint = *endpoint;
        }
    }
    if (m_hasContext && m_designIdentity == designIdentity
        && m_contextPackage == package
        && sameEndpoint(m_endpoint, nextEndpoint)) {
        m_contextDesign = design;
        if (nextEndpoint) {
            // Attachment moves do not change the parameter-editing source and
            // must not rebuild or discard the visible draft.
            m_endpoint->attachment = nextEndpoint->attachment;
        }
        setBusy(busy);
        return;
    }

    m_contextDesign = design;
    m_contextPackage = package;
    m_designIdentity = std::move(designIdentity);
    m_hasContext = true;
    m_busy = busy;
    m_endpoint = std::move(nextEndpoint);
    m_package = package
        ? std::optional<PackageDefinition>(*package) : std::nullopt;
    m_baseTypeChangePlan.reset();

    if (!design) {
        m_status->setText(QStringLiteral(
            "Create or open a design to edit Endpoint parameters."));
        m_editor->hide();
        return;
    }
    if (!package) {
        m_status->setText(QStringLiteral(
            "<b>Read-only</b><br>The design Package is not loaded."));
        m_editor->hide();
        return;
    }
    if (!endpointId) {
        m_status->setText(QStringLiteral(
            "Select one Endpoint node to edit its own parameters. "
            "Selecting its connection edits attachment configuration separately."));
        m_editor->hide();
        return;
    }
    if (!m_endpoint) {
        m_status->setText(QStringLiteral(
            "The selected Endpoint is no longer present in the design."));
        m_editor->hide();
        return;
    }

    m_status->setText(
        busy
            ? QStringLiteral("Read-only while another operation is running.")
            : QStringLiteral("Editing Endpoint <b>%1</b>.")
                  .arg(m_endpoint->id.toHtmlEscaped()));
    m_editor->show();
    m_editor->setEnabled(!busy);
    m_id->setText(m_endpoint->id);

    std::optional<CachedDraft> cachedDraft;
    bool removedStaleDraft = false;
    auto designDrafts = m_drafts.find(m_designIdentity);
    if (designDrafts != m_drafts.end()) {
        const auto draft = designDrafts->find(m_endpoint->id);
        if (draft != designDrafts->end()) {
            if (draft->sourceType == m_endpoint->type
                && draft->sourceParameters == m_endpoint->parameters) {
                cachedDraft = *draft;
            } else {
                designDrafts->erase(draft);
                removedStaleDraft = true;
                if (designDrafts->isEmpty()) {
                    m_drafts.erase(designDrafts);
                }
            }
        }
    }

    m_updating = true;
    m_type->clear();
    for (const EndpointTypeDefinition& type : m_package->endpointTypes) {
        m_type->addItem(
            QStringLiteral("%1 (%2)").arg(endpointTypeLabel(type), type.id),
            type.id);
    }
    int typeIndex = m_type->findData(m_endpoint->type);
    if (typeIndex < 0) {
        m_type->addItem(
            QStringLiteral("Unsupported current type (%1)").arg(m_endpoint->type),
            m_endpoint->type);
        typeIndex = m_type->count() - 1;
    }
    m_type->setCurrentIndex(typeIndex);
    m_migration->setCurrentIndex(0);
    if (cachedDraft) {
        const int targetIndex = m_type->findData(cachedDraft->targetType);
        if (targetIndex >= 0) {
            m_type->setCurrentIndex(targetIndex);
        } else {
            cachedDraft.reset();
            auto drafts = m_drafts.find(m_designIdentity);
            if (drafts != m_drafts.end()) {
                drafts->remove(m_endpoint->id);
                if (drafts->isEmpty()) {
                    m_drafts.erase(drafts);
                }
                removedStaleDraft = true;
            }
        }
    }
    if (cachedDraft) {
        const int migrationIndex = m_migration->findData(
            static_cast<int>(cachedDraft->migration));
        if (migrationIndex >= 0) {
            m_migration->setCurrentIndex(migrationIndex);
        }
    }
    m_updating = false;
    m_restoringDraft = cachedDraft.has_value();
    rebuildTargetParameters();
    if (cachedDraft) {
        m_updating = true;
        m_parameters->setDraftValues(cachedDraft->editorState);
        m_updating = false;
        m_restoringDraft = false;
        updateValidation();
    } else {
        m_restoringDraft = false;
    }
    if (removedStaleDraft) {
        notifyDraftStateChanged();
    }
}

void EndpointConfigurationPanel::setBusy(bool busy) {
    m_busy = busy;
    if (m_editor) {
        m_editor->setEnabled(!busy);
    }
    if (m_endpoint && m_status) {
        m_status->setText(
            busy
                ? QStringLiteral("Read-only while another operation is running.")
                : QStringLiteral("Editing Endpoint <b>%1</b>.")
                      .arg(m_endpoint->id.toHtmlEscaped()));
    }
    updateValidation();
}

bool EndpointConfigurationPanel::hasUnappliedDrafts(
    const QString& designIdentity) const {
    const auto designDrafts = m_drafts.constFind(designIdentity);
    return designDrafts != m_drafts.cend() && !designDrafts->isEmpty();
}

QStringList EndpointConfigurationPanel::unappliedDraftEndpointIds(
    const QString& designIdentity) const {
    const auto designDrafts = m_drafts.constFind(designIdentity);
    if (designDrafts == m_drafts.cend()) {
        return {};
    }
    QStringList endpointIds = designDrafts->keys();
    std::sort(endpointIds.begin(), endpointIds.end());
    return endpointIds;
}

void EndpointConfigurationPanel::discardDraft(
    const QString& designIdentity,
    const QString& endpointId) {
    auto designDrafts = m_drafts.find(designIdentity);
    if (designDrafts == m_drafts.end()
        || designDrafts->remove(endpointId) == 0) {
        return;
    }
    if (designDrafts->isEmpty()) {
        m_drafts.erase(designDrafts);
    }
    if (m_designIdentity == designIdentity && m_endpoint
        && m_endpoint->id == endpointId) {
        resetVisibleDraft();
    }
    notifyDraftStateChanged();
}

void EndpointConfigurationPanel::clearDraftsForDesign(
    const QString& designIdentity) {
    if (m_drafts.remove(designIdentity) == 0) {
        return;
    }
    if (m_designIdentity == designIdentity) {
        resetVisibleDraft();
    }
    notifyDraftStateChanged();
}

void EndpointConfigurationPanel::clearDrafts() {
    if (m_drafts.isEmpty()) {
        return;
    }
    m_drafts.clear();
    resetVisibleDraft();
    notifyDraftStateChanged();
}

void EndpointConfigurationPanel::rebuildTargetParameters() {
    if (m_updating || !m_endpoint || !m_package || !m_parameters) {
        return;
    }
    m_updating = true;
    m_baseTypeChangePlan.reset();
    const EndpointTypeDefinition* type = selectedType();
    const QString targetType = m_type->currentData().toString();
    const bool changingType = targetType != m_endpoint->type;
    m_migration->setVisible(changingType);
    if (m_migrationLabel) {
        m_migrationLabel->setVisible(changingType);
    }
    if (!type) {
        m_parameters->setSchema({}, {});
    } else if (!changingType) {
        m_parameters->setSchema(type->parameters, m_endpoint->parameters);
    } else if (planTypeChangeRequested) {
        EndpointTypeChangePlan plan = planTypeChangeRequested(
            m_endpoint->id, targetType, selectedMigration(), {});
        m_baseTypeChangePlan = plan;
        m_parameters->setSchema(type->parameters, plan.parameters);
    } else {
        m_parameters->setSchema(type->parameters, parameterDefaults(*type));
    }
    m_updating = false;
    updateValidation();
}

void EndpointConfigurationPanel::updateValidation() {
    if (m_updating || !m_diagnostics || !m_apply) {
        return;
    }
    QStringList errors;
    if (m_parameters) {
        errors += m_parameters->localErrors();
    }
    const bool changingType = m_endpoint && m_type
        && m_type->currentData().toString() != m_endpoint->type;
    if (changingType) {
        if (!m_baseTypeChangePlan) {
            errors.append(QStringLiteral("Type-change preview is unavailable."));
        } else {
            errors += planDiagnostics(*m_baseTypeChangePlan);
        }
    }
    m_apply->setEnabled(
        m_endpoint.has_value() && m_package.has_value() && !m_busy
        && errors.isEmpty()
        && (changingType || (m_parameters && m_parameters->isModified())));
    m_diagnostics->setText(
        errors.isEmpty()
            ? QStringLiteral("Endpoint parameters satisfy the Package schema.")
            : errors.join(QLatin1Char('\n')));
    updateTypeChangeSummary();
    captureCurrentDraft();
}

void EndpointConfigurationPanel::updateTypeChangeSummary() {
    if (!m_typeChangeSummary || !m_endpoint || !m_type || !m_parameters) {
        return;
    }
    const QString targetType = m_type->currentData().toString();
    if (targetType == m_endpoint->type) {
        m_typeChangeSummary->setText(QStringLiteral(
            "The Endpoint ID and attachment stay unchanged. Applying replaces "
            "the complete Endpoint parameter object atomically."));
        return;
    }
    if (!selectedType()) {
        m_typeChangeSummary->setText(
            QStringLiteral("The selected type is not declared by the Package."));
        return;
    }
    if (!m_baseTypeChangePlan) {
        m_typeChangeSummary->setText(
            QStringLiteral("Type-change preview is unavailable."));
        return;
    }

    const QJsonObject before = m_endpoint->parameters;
    const QJsonObject after = m_parameters->values();
    QSet<QString> keySet;
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        keySet.insert(it.key());
    }
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
        keySet.insert(it.key());
    }
    QStringList keys = keySet.values();
    std::sort(keys.begin(), keys.end());

    QStringList changes;
    int preservedCount = 0;
    for (const QString& key : keys) {
        const bool hadOld = before.contains(key);
        const bool hasNew = after.contains(key);
        const QString escapedKey = key.toHtmlEscaped();
        if (hadOld && !hasNew) {
            changes.append(
                QStringLiteral("<li><b>Drop</b> <code>%1</code>: "
                               "<code>%2</code> → <i>removed</i></li>")
                    .arg(escapedKey,
                         compactJsonValue(before.value(key)).toHtmlEscaped()));
        } else if (!hadOld && hasNew) {
            changes.append(
                QStringLiteral("<li><b>Add/default</b> <code>%1</code>: "
                               "<i>absent</i> → <code>%2</code></li>")
                    .arg(escapedKey,
                         compactJsonValue(after.value(key)).toHtmlEscaped()));
        } else if (before.value(key) == after.value(key)) {
            ++preservedCount;
        } else {
            changes.append(
                QStringLiteral("<li><b>Change/reset</b> <code>%1</code>: "
                               "<code>%2</code> → <code>%3</code></li>")
                    .arg(escapedKey,
                         compactJsonValue(before.value(key)).toHtmlEscaped(),
                         compactJsonValue(after.value(key)).toHtmlEscaped()));
        }
    }

    m_typeChangeSummary->setText(
        QStringLiteral(
            "<b>Type migration: <code>%1</code> → <code>%2</code></b>"
            "<br>Strategy: %3"
            "<ul>%4</ul>"
            "%5 unchanged parameter(s); %6 retained Domain membership(s); "
            "%7 incompatible Endpoint "
            "Attachment configuration record(s).")
            .arg(m_endpoint->type.toHtmlEscaped(),
                 targetType.toHtmlEscaped(),
                 selectedMigration()
                         == EndpointParameterMigration::PreserveCompatible
                     ? QStringLiteral("preserve compatible values")
                     : QStringLiteral("reset to Package defaults"),
                 changes.join(QString()),
                 QString::number(
                     preservedCount),
                 QString::number(
                     m_baseTypeChangePlan->retainedDomainMemberships.size()),
                 QString::number(
                     m_baseTypeChangePlan
                         ->removedAttachmentConfigurations.size())));
}

void EndpointConfigurationPanel::captureCurrentDraft() {
    if (m_updating || m_restoringDraft || m_designIdentity.isEmpty()
        || !m_endpoint || !m_type || !m_parameters) {
        return;
    }

    CachedDraft draft;
    draft.sourceType = m_endpoint->type;
    draft.sourceParameters = m_endpoint->parameters;
    draft.targetType = m_type->currentData().toString();
    draft.migration = selectedMigration();
    draft.desiredParameters = m_parameters->values();
    draft.editorState = m_parameters->draftValues();
    const bool hasChange = draft.targetType != draft.sourceType
        || draft.desiredParameters != draft.sourceParameters;

    auto designDrafts = m_drafts.find(m_designIdentity);
    if (!hasChange) {
        if (designDrafts == m_drafts.end()
            || designDrafts->remove(m_endpoint->id) == 0) {
            return;
        }
        if (designDrafts->isEmpty()) {
            m_drafts.erase(designDrafts);
        }
        notifyDraftStateChanged();
        return;
    }

    if (designDrafts == m_drafts.end()) {
        designDrafts = m_drafts.insert(
            m_designIdentity, QHash<QString, CachedDraft>{});
    }
    const auto existing = designDrafts->constFind(m_endpoint->id);
    if (existing != designDrafts->cend() && *existing == draft) {
        return;
    }
    designDrafts->insert(m_endpoint->id, std::move(draft));
    notifyDraftStateChanged();
}

void EndpointConfigurationPanel::resetVisibleDraft() {
    if (!m_endpoint || !m_type || !m_migration) {
        return;
    }
    m_restoringDraft = true;
    m_updating = true;
    const int currentType = m_type->findData(m_endpoint->type);
    if (currentType >= 0) {
        m_type->setCurrentIndex(currentType);
    }
    const int resetMigration = m_migration->findData(
        static_cast<int>(EndpointParameterMigration::ResetToDefaults));
    if (resetMigration >= 0) {
        m_migration->setCurrentIndex(resetMigration);
    }
    m_updating = false;
    rebuildTargetParameters();
    m_restoringDraft = false;
    updateValidation();
}

void EndpointConfigurationPanel::notifyDraftStateChanged() {
    if (draftStateChanged) {
        draftStateChanged();
    }
}

void EndpointConfigurationPanel::apply() {
    if (m_busy || !m_endpoint || !m_package || !m_parameters
        || !m_parameters->locallyValid()) {
        return;
    }
    const QString targetType = m_type->currentData().toString();
    if (targetType == m_endpoint->type) {
        if (updateParametersRequested) {
            updateParametersRequested(m_endpoint->id, m_parameters->values());
        }
        return;
    }
    if (!planTypeChangeRequested || !changeTypeRequested) {
        return;
    }

    const QJsonObject patch = typeChangePatch();
    const EndpointTypeChangePlan plan = planTypeChangeRequested(
        m_endpoint->id, targetType, selectedMigration(), patch);
    const QStringList errors = planDiagnostics(plan);
    if (!errors.isEmpty()) {
        m_diagnostics->setText(errors.join(QLatin1Char('\n')));
        m_apply->setEnabled(false);
        return;
    }

    EndpointTypeChangeImpactConfirmation confirmation;
    if (plan.requiresImpactConfirmation()) {
        QStringList records;
        for (const ElementConfiguration& configuration
             : plan.removedAttachmentConfigurations) {
            records.append(configurationLine(configuration));
        }
        QMessageBox warning(
            QMessageBox::Warning,
            QStringLiteral("Change Endpoint Type"),
            QStringLiteral(
                "Changing %1 from %2 to %3 makes the following exact "
                "Endpoint Attachment configuration records incompatible:\n\n%4\n\n"
                "Continue and remove only these records?")
                .arg(plan.endpointId,
                     plan.currentType,
                     plan.targetType,
                     records.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel,
            this);
        warning.setObjectName(
            QStringLiteral("finepaper.endpointTypeChangeImpact"));
        warning.setDefaultButton(QMessageBox::Cancel);
        if (warning.exec() != QMessageBox::Yes) {
            return;
        }
        confirmation.removedAttachmentConfigurations =
            plan.removedAttachmentConfigurations;
    }
    changeTypeRequested(
        m_endpoint->id,
        targetType,
        selectedMigration(),
        patch,
        std::move(confirmation));
}

const EndpointTypeDefinition* EndpointConfigurationPanel::selectedType() const {
    return m_type && m_package
        ? m_package->endpointType(m_type->currentData().toString())
        : nullptr;
}

EndpointParameterMigration EndpointConfigurationPanel::selectedMigration() const {
    return m_migration
        ? static_cast<EndpointParameterMigration>(m_migration->currentData().toInt())
        : EndpointParameterMigration::ResetToDefaults;
}

QJsonObject EndpointConfigurationPanel::typeChangePatch() const {
    QJsonObject patch;
    if (!m_parameters) {
        return patch;
    }
    const QJsonObject desired = m_parameters->values();
    const QJsonObject base = m_baseTypeChangePlan
        ? m_baseTypeChangePlan->parameters : QJsonObject{};
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it) {
        if (!base.contains(it.key()) || base.value(it.key()) != it.value()) {
            patch.insert(it.key(), it.value());
        }
    }
    return patch;
}

QStringList EndpointConfigurationPanel::planDiagnostics(
    const EndpointTypeChangePlan& plan) const {
    QStringList errors;
    for (const Diagnostic& diagnostic : plan.diagnostics) {
        if (diagnostic.severity == QStringLiteral("error")) {
            errors.append(diagnosticLine(diagnostic));
        }
    }
    return errors;
}

} // namespace finepaper
