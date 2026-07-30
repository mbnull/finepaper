#include "gui/endpoint_domain_assignment_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QString typeLabel(const DomainTypeDefinition& type) {
    return type.label.trimmed().isEmpty() ? type.id : type.label;
}

QString domainLabel(const DomainDefinition& domain) {
    return domain.name.trimmed().isEmpty()
        ? domain.id
        : QStringLiteral("%1 (%2)").arg(domain.name, domain.id);
}

QString cardinalityText(DomainCardinality cardinality) {
    if (cardinality == DomainCardinality::Single) {
        return QStringLiteral("one Domain");
    }
    if (cardinality == DomainCardinality::Multiple) {
        return QStringLiteral("one or more Domains");
    }
    return QStringLiteral("a valid assignment");
}

bool containsAvailableChoice(const EndpointDomainAssignmentGroup& group,
                             const QString& domainId) {
    return std::any_of(
        group.choices.cbegin(), group.choices.cend(),
        [&](const EndpointDomainChoice& choice) {
            return choice.id == domainId && choice.available;
        });
}

QString editorObjectName(const QString& domainType, const QString& suffix) {
    return QStringLiteral("finepaper.endpointDomainAssignment.%1.%2")
        .arg(domainType, suffix);
}

} // namespace

QVector<EndpointDomainAssignmentGroup> buildEndpointDomainAssignmentGroups(
    const NocDesign& design,
    const PackageDefinition& package,
    const EndpointDomainAssignments& initialAssignments) {
    const EndpointDomainAssignments normalizedInitial =
        normalizeEndpointDomainAssignments(initialAssignments);
    QVector<EndpointDomainAssignmentGroup> groups;

    for (const DomainTypeDefinition& type : package.domainTypes) {
        if (!type.appliesTo.contains(ElementKind::Endpoint)) {
            continue;
        }

        EndpointDomainAssignmentGroup group;
        group.domainType = type.id;
        group.domainTypeLabel = typeLabel(type);
        group.cardinality = type.cardinality;
        group.required = type.required;
        group.assignmentProvided =
            normalizedInitial.contains(type.id)
            && !normalizedInitial.value(type.id).isEmpty();
        group.selectedDomainIds = normalizedInitial.value(type.id);

        QSet<QString> seenIds;
        for (const DomainDefinition& domain : design.domains) {
            if (domain.type != type.id || domain.id.trimmed().isEmpty()
                || seenIds.contains(domain.id)) {
                continue;
            }
            seenIds.insert(domain.id);
            group.choices.append(
                EndpointDomainChoice{domain.id, domainLabel(domain), true});
        }
        std::sort(
            group.choices.begin(), group.choices.end(),
            [](const EndpointDomainChoice& lhs,
               const EndpointDomainChoice& rhs) { return lhs.id < rhs.id; });

        // Preserve stale initial values visibly.  A detached Endpoint can stay
        // on the canvas while Domains are edited, so silently discarding such
        // assignments would turn reconnect into a data-loss operation.
        for (const QString& selectedId : std::as_const(group.selectedDomainIds)) {
            if (!seenIds.contains(selectedId)) {
                group.choices.append(EndpointDomainChoice{
                    selectedId,
                    QStringLiteral("Missing Domain (%1)").arg(selectedId),
                    false});
            }
        }

        const qsizetype availableCount = std::count_if(
            group.choices.cbegin(), group.choices.cend(),
            [](const EndpointDomainChoice& choice) { return choice.available; });
        if (group.required && group.selectedDomainIds.isEmpty()
            && availableCount == 1) {
            const auto available = std::find_if(
                group.choices.cbegin(), group.choices.cend(),
                [](const EndpointDomainChoice& choice) {
                    return choice.available;
                });
            group.selectedDomainIds = {available->id};
        }
        groups.append(std::move(group));
    }
    return groups;
}

bool endpointDomainAssignmentsRequireUserDecision(
    const QVector<EndpointDomainAssignmentGroup>& groups,
    EndpointDomainAssignmentDecisionMode mode) {
    for (const EndpointDomainAssignmentGroup& group : groups) {
        const qsizetype availableCount = std::count_if(
            group.choices.cbegin(), group.choices.cend(),
            [](const EndpointDomainChoice& choice) { return choice.available; });
        const bool hasStaleSelection = std::any_of(
            group.selectedDomainIds.cbegin(), group.selectedDomainIds.cend(),
            [&](const QString& domainId) {
                return !containsAvailableChoice(group, domainId);
            });
        const bool invalidCardinality =
            group.cardinality != DomainCardinality::Single
            && group.cardinality != DomainCardinality::Multiple;
        const bool tooManyForSingle =
            group.cardinality == DomainCardinality::Single
            && group.selectedDomainIds.size() > 1;
        const bool missingRequired =
            group.required
            && (group.selectedDomainIds.isEmpty()
                || (mode == EndpointDomainAssignmentDecisionMode::Restore
                    && !group.assignmentProvided));
        if (invalidCardinality || tooManyForSingle || hasStaleSelection
            || missingRequired || (group.required && availableCount == 0)) {
            return true;
        }
        if (mode == EndpointDomainAssignmentDecisionMode::Creation
            && (availableCount > 1
                || (!group.required && availableCount > 0))) {
            return true;
        }
    }
    return false;
}

EndpointDomainAssignments endpointDomainAssignmentsFromGroups(
    const QVector<EndpointDomainAssignmentGroup>& groups) {
    EndpointDomainAssignments result;
    for (const EndpointDomainAssignmentGroup& group : groups) {
        if (!group.selectedDomainIds.isEmpty()) {
            result.insert(group.domainType, group.selectedDomainIds);
        }
    }
    return normalizeEndpointDomainAssignments(result);
}

EndpointDomainAssignmentEditor::EndpointDomainAssignmentEditor(
    const NocDesign& design,
    const PackageDefinition& package,
    EndpointDomainAssignments initialAssignments,
    QWidget* parent)
    : QWidget(parent),
      m_groups(buildEndpointDomainAssignmentGroups(
          design, package, initialAssignments)) {
    setObjectName(QStringLiteral("finepaper.endpointDomainAssignmentEditor"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* introduction = new QLabel(
        QStringLiteral(
            "Choose the Domain membership of this Endpoint. Every row comes "
            "from the selected NoC Package; Router topology is not editable "
            "here."),
        this);
    introduction->setObjectName(
        QStringLiteral("finepaper.endpointDomainAssignment.introduction"));
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(
        QStringLiteral("finepaper.endpointDomainAssignment.scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    if (m_groups.isEmpty()) {
        auto* empty = new QLabel(
            QStringLiteral(
                "This Package declares no Domain Types that apply to Endpoints."),
            content);
        empty->setObjectName(
            QStringLiteral("finepaper.endpointDomainAssignment.empty"));
        empty->setWordWrap(true);
        contentLayout->addWidget(empty);
    }

    m_editors.reserve(m_groups.size());
    for (const EndpointDomainAssignmentGroup& group : std::as_const(m_groups)) {
        GroupEditor editor;
        editor.group = group;

        const QString heading = group.domainTypeLabel == group.domainType
            ? group.domainType
            : QStringLiteral("%1 (%2)")
                  .arg(group.domainTypeLabel, group.domainType);
        auto* box = new QGroupBox(heading, content);
        box->setObjectName(editorObjectName(group.domainType,
                                            QStringLiteral("group")));
        box->setProperty("finepaper.domainType", group.domainType);
        auto* boxLayout = new QVBoxLayout(box);

        const QString requirement = group.required
            ? QStringLiteral("Required: choose %1.")
                  .arg(cardinalityText(group.cardinality))
            : QStringLiteral("Optional: choose %1, or leave it unassigned.")
                  .arg(cardinalityText(group.cardinality));
        auto* description = new QLabel(requirement, box);
        description->setObjectName(
            editorObjectName(group.domainType, QStringLiteral("description")));
        description->setWordWrap(true);
        boxLayout->addWidget(description);

        if (group.cardinality == DomainCardinality::Single) {
            editor.single = new QComboBox(box);
            editor.single->setObjectName(
                editorObjectName(group.domainType, QStringLiteral("single")));
            editor.single->setProperty("finepaper.domainType", group.domainType);
            editor.single->addItem(
                group.required ? QStringLiteral("Choose a Domain…")
                               : QStringLiteral("Unassigned"),
                QString());
            for (const EndpointDomainChoice& choice : group.choices) {
                editor.single->addItem(choice.label, choice.id);
                const int index = editor.single->count() - 1;
                editor.single->setItemData(
                    index, choice.available, Qt::UserRole + 1);
                if (!choice.available) {
                    editor.single->setItemData(
                        index,
                        QStringLiteral(
                            "This Domain no longer exists. Choose another "
                            "Domain or leave this optional type unassigned."),
                        Qt::ToolTipRole);
                }
            }
            if (!group.selectedDomainIds.isEmpty()) {
                const int selected = editor.single->findData(
                    group.selectedDomainIds.constFirst());
                if (selected >= 0) {
                    editor.single->setCurrentIndex(selected);
                }
            }
            boxLayout->addWidget(editor.single);
            connect(editor.single, &QComboBox::currentIndexChanged,
                    this, [this] { updateValidation(); });
        } else {
            editor.multiple = new QListWidget(box);
            editor.multiple->setObjectName(
                editorObjectName(group.domainType, QStringLiteral("multiple")));
            editor.multiple->setProperty("finepaper.domainType", group.domainType);
            editor.multiple->setSelectionMode(QAbstractItemView::NoSelection);
            editor.multiple->setMinimumHeight(96);
            const QSet<QString> selected(
                group.selectedDomainIds.cbegin(),
                group.selectedDomainIds.cend());
            for (const EndpointDomainChoice& choice : group.choices) {
                auto* item = new QListWidgetItem(choice.label, editor.multiple);
                item->setData(Qt::UserRole, choice.id);
                item->setData(Qt::UserRole + 1, choice.available);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(choice.id)
                                        ? Qt::Checked
                                        : Qt::Unchecked);
                if (!choice.available) {
                    item->setToolTip(
                        QStringLiteral(
                            "This Domain no longer exists. Uncheck it before "
                            "continuing."));
                }
            }
            boxLayout->addWidget(editor.multiple);
            connect(editor.multiple, &QListWidget::itemChanged,
                    this, [this] { updateValidation(); });
        }

        const bool hasAvailable = std::any_of(
            group.choices.cbegin(), group.choices.cend(),
            [](const EndpointDomainChoice& choice) {
                return choice.available;
            });
        if (!hasAvailable) {
            auto* unavailable = new QLabel(
                QStringLiteral(
                    "No instances of this Domain Type exist. Create one in "
                    "Domain Manager before adding the Endpoint."),
                box);
            unavailable->setObjectName(
                editorObjectName(group.domainType,
                                 QStringLiteral("unavailable")));
            unavailable->setWordWrap(true);
            boxLayout->addWidget(unavailable);
        }

        contentLayout->addWidget(box);
        m_editors.append(std::move(editor));
    }
    contentLayout->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    m_diagnostics = new QLabel(this);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.endpointDomainAssignment.diagnostics"));
    m_diagnostics->setWordWrap(true);
    m_diagnostics->setTextFormat(Qt::PlainText);
    m_diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_diagnostics);
    updateValidation();
}

EndpointDomainAssignments EndpointDomainAssignmentEditor::assignments() const {
    EndpointDomainAssignments result;
    for (const GroupEditor& editor : m_editors) {
        QStringList selected;
        if (editor.single) {
            const QString domainId =
                editor.single->currentData().toString().trimmed();
            if (!domainId.isEmpty()) {
                selected.append(domainId);
            }
        } else if (editor.multiple) {
            for (int index = 0; index < editor.multiple->count(); ++index) {
                const QListWidgetItem* item = editor.multiple->item(index);
                if (item->checkState() == Qt::Checked) {
                    selected.append(item->data(Qt::UserRole).toString());
                }
            }
        }
        if (!selected.isEmpty()) {
            result.insert(editor.group.domainType, std::move(selected));
        }
    }
    return normalizeEndpointDomainAssignments(result);
}

QStringList EndpointDomainAssignmentEditor::localErrors() const {
    const EndpointDomainAssignments selected = assignments();
    QStringList errors;
    for (const EndpointDomainAssignmentGroup& group : m_groups) {
        const QStringList domainIds = selected.value(group.domainType);
        if (group.cardinality != DomainCardinality::Single
            && group.cardinality != DomainCardinality::Multiple) {
            errors.append(
                QStringLiteral("%1 has an invalid Package cardinality.")
                    .arg(group.domainTypeLabel));
            continue;
        }
        if (group.required && domainIds.isEmpty()) {
            const bool hasAvailable = std::any_of(
                group.choices.cbegin(), group.choices.cend(),
                [](const EndpointDomainChoice& choice) {
                    return choice.available;
                });
            errors.append(
                hasAvailable
                    ? QStringLiteral("Choose %1 for required Domain Type %2.")
                          .arg(cardinalityText(group.cardinality),
                               group.domainTypeLabel)
                    : QStringLiteral(
                          "Required Domain Type %1 has no instances. Create "
                          "one in Domain Manager first.")
                          .arg(group.domainTypeLabel));
        }
        if (group.cardinality == DomainCardinality::Single
            && domainIds.size() > 1) {
            errors.append(
                QStringLiteral("%1 accepts only one Domain.")
                    .arg(group.domainTypeLabel));
        }
        for (const QString& domainId : domainIds) {
            if (!containsAvailableChoice(group, domainId)) {
                errors.append(
                    QStringLiteral(
                        "%1 references missing or incompatible Domain %2.")
                        .arg(group.domainTypeLabel, domainId));
            }
        }
    }
    return errors;
}

void EndpointDomainAssignmentEditor::updateValidation() {
    if (!m_diagnostics) {
        return;
    }
    const QStringList errors = localErrors();
    m_diagnostics->setText(
        errors.isEmpty()
            ? QStringLiteral("Assignments satisfy the Package requirements.")
            : errors.join(QLatin1Char('\n')));
    if (validationChanged) {
        validationChanged();
    }
}

EndpointDomainAssignmentDialog::EndpointDomainAssignmentDialog(
    const NocDesign& design,
    const PackageDefinition& package,
    EndpointDomainAssignments initialAssignments,
    QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("finepaper.endpointDomainAssignmentDialog"));
    setWindowTitle(QStringLiteral("Endpoint Domain Assignments"));
    setModal(true);
    resize(620, 600);

    auto* root = new QVBoxLayout(this);
    m_editor = new EndpointDomainAssignmentEditor(
        design, package, std::move(initialAssignments), this);
    root->addWidget(m_editor, 1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(
        QStringLiteral("finepaper.endpointDomainAssignment.buttons"));
    m_acceptButton = m_buttons->button(QDialogButtonBox::Ok);
    m_acceptButton->setObjectName(
        QStringLiteral("finepaper.endpointDomainAssignment.accept"));
    m_acceptButton->setText(QStringLiteral("Use Assignments"));
    root->addWidget(m_buttons);

    m_editor->validationChanged = [this] { updateValidation(); };
    connect(m_buttons, &QDialogButtonBox::accepted,
            this, [this] { accept(); });
    connect(m_buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    updateValidation();
}

EndpointDomainAssignments EndpointDomainAssignmentDialog::assignments() const {
    return m_editor ? m_editor->assignments() : EndpointDomainAssignments{};
}

QStringList EndpointDomainAssignmentDialog::localErrors() const {
    return m_editor ? m_editor->localErrors() : QStringList{};
}

const QVector<EndpointDomainAssignmentGroup>&
EndpointDomainAssignmentDialog::groups() const {
    static const QVector<EndpointDomainAssignmentGroup> empty;
    return m_editor ? m_editor->groups() : empty;
}

void EndpointDomainAssignmentDialog::accept() {
    updateValidation();
    if (m_acceptButton && m_acceptButton->isEnabled()) {
        QDialog::accept();
    }
}

void EndpointDomainAssignmentDialog::updateValidation() {
    if (!m_acceptButton) {
        return;
    }
    const QStringList errors = localErrors();
    m_acceptButton->setEnabled(errors.isEmpty());
}

} // namespace finepaper
