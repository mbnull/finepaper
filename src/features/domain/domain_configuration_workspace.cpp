#include "features/domain/domain_configuration_workspace.h"

#include "application/domain_configuration.h"

#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include <utility>

namespace finepaper {

DomainConfigurationWorkspace::DomainConfigurationWorkspace(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.domainConfigurationWorkspace"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(12, 12, 12, 12);

    m_runtimeCapabilities = new QLabel(this);
    m_runtimeCapabilities->setObjectName(QStringLiteral(
        "finepaper.domainConfigurationWorkspace.runtimeCapabilities"));
    m_runtimeCapabilities->setWordWrap(true);
    m_runtimeCapabilities->setTextFormat(Qt::RichText);
    m_layout->addWidget(m_runtimeCapabilities);

    m_status = new QLabel(
        QStringLiteral(
            "Create or open a Package-defined Domain design to use this Workspace."),
        this);
    m_status->setObjectName(
        QStringLiteral("finepaper.domainConfigurationWorkspace.status"));
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_layout->addWidget(m_status);

    m_readOnlySnapshot = new QPlainTextEdit(this);
    m_readOnlySnapshot->setObjectName(QStringLiteral(
        "finepaper.domainConfigurationWorkspace.readOnlySnapshot"));
    m_readOnlySnapshot->setReadOnly(true);
    m_readOnlySnapshot->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_readOnlySnapshot->hide();
    m_layout->addWidget(m_readOnlySnapshot, 1);
}

DomainConfigurationWorkspace::~DomainConfigurationWorkspace() {
    if (m_editor) {
        m_editor->workspaceApplyRequested = {};
        m_editor->draftStateChanged = {};
    }
}

void DomainConfigurationWorkspace::setContext(
    const NocDesign* design,
    const PackageDefinition* package,
    const QString& designIdentity,
    DomainConfigurationValidator validator) {
    if (!design) {
        updateRuntimeCapabilitySummary(nullptr);
        m_designIdentity = designIdentity;
        clearEditor(QStringLiteral(
            "Create or open a design before configuring Domains."));
        return;
    }
    if (!formatVersionSupportsDomains(design->formatVersion)) {
        updateRuntimeCapabilitySummary(nullptr);
        m_designIdentity = designIdentity;
        clearEditor(QStringLiteral(
            "This Design version does not expose Domain configuration."));
        return;
    }
    if (!package) {
        updateRuntimeCapabilitySummary(nullptr);
        m_designIdentity = designIdentity;
        showReadOnlySnapshot(
            *design,
            QStringLiteral(
                "The exact design Package is unavailable. Domain data remains "
                "inspectable below as a read-only five-plane snapshot; restore "
                "the Package to regain schema-driven editing."));
        return;
    }
    if (!formatVersionSupportsDomains(package->formatVersion)) {
        updateRuntimeCapabilitySummary(nullptr);
        m_designIdentity = designIdentity;
        clearEditor(QStringLiteral(
            "This Package version does not expose Domain configuration."));
        return;
    }
    if (package->domainTypes.isEmpty()) {
        updateRuntimeCapabilitySummary(nullptr);
        m_designIdentity = designIdentity;
        clearEditor(QStringLiteral(
            "This Package explicitly declares no Domain types."));
        return;
    }

    if (m_editor && m_designIdentity == designIdentity) {
        updateRuntimeCapabilitySummary(package);
        m_editor->synchronizeContext(*design, *package, std::move(validator));
        m_editor->setBusy(m_busy);
        return;
    }

    clearEditor({});
    m_designIdentity = designIdentity;
    updateRuntimeCapabilitySummary(package);
    createEditor(*design, *package, std::move(validator));
}

void DomainConfigurationWorkspace::setBusy(bool busy) {
    m_busy = busy;
    if (m_editor) {
        m_editor->setBusy(m_busy);
    }
}

bool DomainConfigurationWorkspace::hasPendingChanges() const {
    return m_editor && m_editor->hasPendingChanges();
}

void DomainConfigurationWorkspace::discardPendingChanges() {
    if (m_editor) {
        m_editor->discardDraft();
    }
}

void DomainConfigurationWorkspace::clearEditor(const QString& message) {
    if (m_editor) {
        m_editor->workspaceApplyRequested = {};
        m_editor->draftStateChanged = {};
        m_layout->removeWidget(m_editor);
        delete m_editor;
        m_editor = nullptr;
    }
    m_readOnlySnapshot->clear();
    m_readOnlySnapshot->hide();
    m_status->setText(message.isEmpty()
                          ? QStringLiteral(
                                "Domain Configuration is not available for the current design.")
                          : message);
    m_status->show();
}

void DomainConfigurationWorkspace::showReadOnlySnapshot(
    const NocDesign& design,
    const QString& message) {
    clearEditor(message);
    const QJsonObject snapshot = domain_configuration::toJson(
        domain_configuration::fromDesign(design));
    m_readOnlySnapshot->setPlainText(QString::fromUtf8(
        QJsonDocument(snapshot).toJson(QJsonDocument::Indented)));
    m_readOnlySnapshot->show();
}

void DomainConfigurationWorkspace::updateRuntimeCapabilitySummary(
    const PackageDefinition* package) {
    if (!package) {
        m_runtimeCapabilities->clear();
        m_runtimeCapabilities->hide();
        return;
    }

    const auto& declared = package->runtimeCapabilities.domainConfiguration;
    if (!declared) {
        m_runtimeCapabilities->setText(QStringLiteral(
            "<b>Runtime consumption is not declared.</b> Domain planes remain "
            "visible for inspection, but validation and generation must fail "
            "closed rather than silently ignore populated data."));
        m_runtimeCapabilities->show();
        return;
    }

    QStringList unsupported;
    const auto appendUnsupported = [&unsupported](bool supported,
                                                   const QString& label) {
        if (!supported) {
            unsupported.append(label);
        }
    };
    appendUnsupported(declared->domains, QStringLiteral("Domains"));
    appendUnsupported(declared->memberships, QStringLiteral("Memberships"));
    appendUnsupported(declared->relations, QStringLiteral("Relations"));
    appendUnsupported(declared->crossingPolicies,
                      QStringLiteral("Crossing Policies"));
    appendUnsupported(declared->edgeOverrides,
                      QStringLiteral("Edge Overrides"));

    if (unsupported.isEmpty()) {
        m_runtimeCapabilities->setText(QStringLiteral(
            "<b>Runtime capability:</b> all five Domain configuration planes "
            "are validated and materialized by this Package runtime."));
    } else {
        m_runtimeCapabilities->setText(QStringLiteral(
            "<b>Runtime consumption gap:</b> %1 %2 editable here, but if any "
            "of these planes contains data, Validate and Generate fail closed "
            "instead of silently ignoring it.")
            .arg(unsupported.join(QStringLiteral(", ")),
                 unsupported.size() == 1 ? QStringLiteral("remains")
                                         : QStringLiteral("remain")));
    }
    m_runtimeCapabilities->show();
}

void DomainConfigurationWorkspace::createEditor(
    NocDesign design,
    PackageDefinition package,
    DomainConfigurationValidator validator) {
    m_status->hide();
    m_readOnlySnapshot->hide();
    m_editor = new DomainConfigurationDialog(
        design,
        std::move(package),
        domain_configuration::fromDesign(design),
        std::move(validator),
        this,
        DomainConfigurationPresentation::EmbeddedWorkspace);
    m_editor->workspaceApplyRequested = [this](const DesignResult& result) {
        return applyRequested && applyRequested(result);
    };
    m_editor->draftStateChanged = [this](bool pending) {
        if (draftStateChanged) {
            draftStateChanged(pending);
        }
    };
    m_editor->setBusy(m_busy);
    m_layout->addWidget(m_editor, 1);
    m_editor->show();
}

} // namespace finepaper
