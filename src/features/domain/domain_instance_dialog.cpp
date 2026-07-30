#include "features/domain/domain_instance_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {

DomainInstanceDialog::DomainInstanceDialog(
    QVector<DomainTypeDefinition> types,
    QVector<DomainDefinition> draftDomains,
    std::optional<DomainDefinition> existing,
    QString preferredType,
    DomainCandidateValidator validator,
    QWidget* parent)
    : QDialog(parent),
      m_types(std::move(types)),
      m_draftDomains(std::move(draftDomains)),
      m_existing(std::move(existing)),
      m_validator(std::move(validator)) {
    setObjectName(QStringLiteral("finepaper.domainInstanceDialog"));
    setWindowTitle(m_existing
                       ? QStringLiteral("Edit Domain")
                       : QStringLiteral("Add Domain"));
    setModal(true);
    resize(620, 620);

    auto* root = new QVBoxLayout(this);
    auto* introduction = new QLabel(
        m_existing
            ? QStringLiteral("Edit the Package-defined properties of this Domain. "
                             "Its stable ID and Type cannot be changed here.")
            : QStringLiteral("Choose a Package-defined Domain Type, then provide "
                             "the instance identity and properties."),
        this);
    introduction->setObjectName(
        QStringLiteral("finepaper.domainInstance.introduction"));
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    auto* identityForm = new QFormLayout;
    if (m_existing) {
        m_typeDisplay = new QLineEdit(m_existing->type, this);
        m_typeDisplay->setObjectName(QStringLiteral("finepaper.domainInstance.type"));
        m_typeDisplay->setReadOnly(true);
        identityForm->addRow(QStringLiteral("Type"), m_typeDisplay);
    } else {
        m_typeSelector = new QComboBox(this);
        m_typeSelector->setObjectName(QStringLiteral("finepaper.domainInstance.type"));
        for (const DomainTypeDefinition& type : m_types) {
            const QString label = type.label.trimmed().isEmpty()
                ? type.id
                : QStringLiteral("%1 (%2)").arg(type.label, type.id);
            m_typeSelector->addItem(label, type.id);
        }
        const int preferredIndex = m_typeSelector->findData(preferredType);
        if (preferredIndex >= 0) {
            m_typeSelector->setCurrentIndex(preferredIndex);
        }
        m_typeSelector->setEnabled(m_typeSelector->count() > 0);
        identityForm->addRow(QStringLiteral("Type"), m_typeSelector);
    }

    m_idEditor = new QLineEdit(this);
    m_idEditor->setObjectName(QStringLiteral("finepaper.domainInstance.id"));
    m_idEditor->setReadOnly(m_existing.has_value());
    if (m_existing) {
        m_idEditor->setText(m_existing->id);
    }
    identityForm->addRow(QStringLiteral("ID"), m_idEditor);

    m_nameEditor = new QLineEdit(this);
    m_nameEditor->setObjectName(QStringLiteral("finepaper.domainInstance.name"));
    if (m_existing) {
        m_nameEditor->setText(m_existing->name);
    }
    identityForm->addRow(QStringLiteral("Name"), m_nameEditor);
    root->addLayout(identityForm);

    m_typeDescription = new QLabel(this);
    m_typeDescription->setObjectName(
        QStringLiteral("finepaper.domainInstance.typeDescription"));
    m_typeDescription->setWordWrap(true);
    m_typeDescription->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_typeDescription);

    auto* propertyGroup = new QGroupBox(QStringLiteral("Properties"), this);
    propertyGroup->setObjectName(
        QStringLiteral("finepaper.domainInstance.properties"));
    auto* propertyLayout = new QVBoxLayout(propertyGroup);
    m_propertyForm = new DomainPropertyForm(propertyGroup);
    auto* propertyScroll = new QScrollArea(propertyGroup);
    propertyScroll->setObjectName(
        QStringLiteral("finepaper.domainInstance.propertyScroll"));
    propertyScroll->setWidgetResizable(true);
    propertyScroll->setFrameShape(QFrame::NoFrame);
    propertyScroll->setWidget(m_propertyForm);
    propertyLayout->addWidget(propertyScroll);
    root->addWidget(propertyGroup, 1);

    m_diagnostics = new QLabel(this);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.domainInstance.diagnostics"));
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextFormat(Qt::PlainText);
    m_diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_diagnostics);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(QStringLiteral("finepaper.domainInstance.buttons"));
    m_okButton = m_buttons->button(QDialogButtonBox::Ok);
    m_okButton->setObjectName(QStringLiteral("finepaper.domainInstance.ok"));
    m_okButton->setText(m_existing ? QStringLiteral("Save Domain")
                                   : QStringLiteral("Add Domain"));
    root->addWidget(m_buttons);

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(200);
    connect(m_validationTimer, &QTimer::timeout,
            this, [this] { updateValidation(true); });

    connect(m_buttons, &QDialogButtonBox::accepted,
            this, [this] { accept(); });
    connect(m_buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(m_idEditor, &QLineEdit::textChanged,
            this, [this] { scheduleValidation(); });
    connect(m_nameEditor, &QLineEdit::textChanged,
            this, [this] { scheduleValidation(); });
    if (m_typeSelector) {
        connect(m_typeSelector, &QComboBox::currentIndexChanged, this, [this] {
            if (m_updating) {
                return;
            }
            rebuildPropertyForm(PropertyInitialization::CreateWithDefaults);
            updateTypeDescription();
            scheduleValidation();
        });
    }
    m_propertyForm->valuesChanged = [this] { scheduleValidation(); };

    rebuildPropertyForm(m_existing
                            ? PropertyInitialization::ExactValues
                            : PropertyInitialization::CreateWithDefaults);
    updateTypeDescription();
    updateValidation();
}

DomainDefinition DomainInstanceDialog::candidate() const {
    DomainDefinition domain;
    domain.id = m_idEditor ? m_idEditor->text().trimmed() : QString();
    domain.type = selectedTypeId().trimmed();
    domain.name = m_nameEditor ? m_nameEditor->text().trimmed() : QString();
    if (m_propertyForm) {
        domain.properties = m_propertyForm->values();
    }
    return domain;
}

QStringList DomainInstanceDialog::localErrors() const {
    QStringList errors;
    const DomainDefinition domain = candidate();
    if (!selectedType()) {
        errors.append(QStringLiteral("Choose a Domain Type declared by the Package."));
    }
    if (domain.id.isEmpty()) {
        errors.append(QStringLiteral("Domain ID is required."));
    }
    if (domain.name.isEmpty()) {
        errors.append(QStringLiteral("Domain name is required."));
    }

    int matchingIds = 0;
    for (const DomainDefinition& existing : m_draftDomains) {
        if (existing.id == domain.id) {
            ++matchingIds;
        }
    }
    const int allowedMatches = m_existing && m_existing->id == domain.id ? 1 : 0;
    if (!domain.id.isEmpty() && matchingIds > allowedMatches) {
        errors.append(QStringLiteral("Domain ID is already in use."));
    }
    if (m_existing
        && (domain.id != m_existing->id || domain.type != m_existing->type)) {
        errors.append(QStringLiteral("Domain ID and Type are immutable."));
    }
    if (m_propertyForm) {
        errors += m_propertyForm->localErrors();
    }
    return errors;
}

void DomainInstanceDialog::accept() {
    m_validationTimer->stop();
    updateValidation(true);
    if (m_okButton && m_okButton->isEnabled()) {
        QDialog::accept();
    }
}

const DomainTypeDefinition* DomainInstanceDialog::selectedType() const {
    const QString typeId = selectedTypeId();
    const auto it = std::find_if(
        m_types.cbegin(), m_types.cend(), [&](const DomainTypeDefinition& type) {
            return type.id == typeId;
        });
    return it == m_types.cend() ? nullptr : &*it;
}

QString DomainInstanceDialog::selectedTypeId() const {
    if (m_existing) {
        return m_existing->type;
    }
    return m_typeSelector ? m_typeSelector->currentData().toString() : QString();
}

void DomainInstanceDialog::rebuildPropertyForm(
    PropertyInitialization initialization) {
    m_updating = true;
    const DomainTypeDefinition* type = selectedType();
    const QJsonObject initialValues = m_existing
        ? m_existing->properties
        : QJsonObject{};
    m_propertyForm->setSchema(type ? type->properties
                                   : QVector<DomainPropertyDefinition>{},
                              m_draftDomains,
                              initialValues,
                              initialization);
    m_updating = false;
}

void DomainInstanceDialog::scheduleValidation() {
    if (m_updating || !m_validationTimer) {
        return;
    }
    m_validationTimer->start();
    updateValidation(false);
}

void DomainInstanceDialog::updateValidation(
    bool runAuthoritativeValidator) {
    if (m_updating || !m_okButton || !m_diagnostics) {
        return;
    }

    const QStringList local = localErrors();
    m_validationDiagnostics.clear();
    if (local.isEmpty() && m_validator && runAuthoritativeValidator) {
        m_validationDiagnostics = m_validator(candidate());
    }

    QStringList lines;
    for (const QString& error : local) {
        lines.append(QStringLiteral("• %1").arg(error));
    }
    for (const Diagnostic& diagnostic : m_validationDiagnostics) {
        QString line = QStringLiteral("[%1] %2: %3")
                           .arg(diagnostic.severity,
                                diagnostic.code,
                                diagnostic.message);
        if (!diagnostic.path.isEmpty()) {
            line += QStringLiteral(" (%1)").arg(diagnostic.path);
        }
        lines.append(line);
    }
    if (lines.isEmpty()) {
        if (m_validator && !runAuthoritativeValidator) {
            lines.append(QStringLiteral("Local fields are valid; validating design…"));
        } else if (m_validator) {
            lines.append(QStringLiteral("Configuration is valid."));
        } else {
            lines.append(QStringLiteral("Local fields are valid."));
        }
    }
    m_diagnostics->setText(lines.join(QLatin1Char('\n')));
    m_okButton->setEnabled(
        local.isEmpty()
        && (!m_validator || runAuthoritativeValidator)
        && !hasErrors(m_validationDiagnostics));
}

void DomainInstanceDialog::updateTypeDescription() {
    const DomainTypeDefinition* type = selectedType();
    if (!type) {
        m_typeDescription->setText(
            QStringLiteral("No valid Domain Type is available."));
        return;
    }

    QStringList appliesTo;
    for (ElementKind kind : type->appliesTo) {
        appliesTo.append(elementKindId(kind));
    }
    const QString cardinality = type->cardinality == DomainCardinality::Multiple
        ? QStringLiteral("multiple assignments")
        : QStringLiteral("single assignment");
    m_typeDescription->setText(
        QStringLiteral("%1. Applies to: %2. %3; %4.")
            .arg(type->label.trimmed().isEmpty() ? type->id : type->label,
                 appliesTo.isEmpty()
                     ? QStringLiteral("no element kinds")
                     : appliesTo.join(QStringLiteral(", ")),
                 cardinality,
                 type->required
                     ? QStringLiteral("assignment required")
                     : QStringLiteral("assignment optional")));
}

} // namespace finepaper
