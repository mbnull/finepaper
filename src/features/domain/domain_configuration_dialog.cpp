#include "features/domain/domain_configuration_dialog.h"

#include "application/domain_configuration_draft.h"
#include "features/domain/domain_property_form.h"
#include "features/domain/presentation/domain_text.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QString compactObject(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString domainText(const DomainDefinition& domain) {
    return domain_text::domainInstanceDisplayText(domain);
}

QString typeText(const DomainTypeDefinition& type) {
    return domain_text::domainTypeDisplayText(type);
}

QString relationText(const DomainRelationDefinition& relation) {
    return relation.label.trimmed().isEmpty()
        ? relation.id
        : QStringLiteral("%1 (%2)").arg(relation.label, relation.id);
}

QString safeSuffix(const QString& value) {
    return QString::fromLatin1(value.toUtf8().toHex());
}

QString elementKey(const ElementRef& element) {
    return elementKindId(element.kind) + QChar(0x1f) + element.id;
}

ElementRef elementFromKey(const QString& key) {
    const qsizetype separator = key.indexOf(QChar(0x1f));
    if (separator < 0) {
        return {};
    }
    return ElementRef{elementKindFromId(key.left(separator)),
                      key.mid(separator + 1)};
}

QString elementText(const ElementRef& element) {
    return QStringLiteral("%1 %2").arg(elementKindId(element.kind), element.id);
}

QVector<DomainDefinition> domainValues(const DomainConfigurationDraft& draft) {
    QVector<DomainDefinition> domains;
    domains.reserve(draft.domains().size());
    for (const DomainConfigurationDraft::DomainRow& row : draft.domains()) {
        domains.append(row.value);
    }
    return domains;
}

const DomainDefinition* findDomain(const QVector<DomainDefinition>& domains,
                                   const QString& id) {
    const auto found = std::find_if(domains.cbegin(), domains.cend(),
                                    [&](const DomainDefinition& domain) {
                                        return domain.id == id;
                                    });
    return found == domains.cend() ? nullptr : &*found;
}

const DomainRelationDefinition* findRelation(
    const DomainTypeDefinition* type,
    const QString& id) {
    if (!type) {
        return nullptr;
    }
    const auto found = std::find_if(
        type->relations.cbegin(), type->relations.cend(),
        [&](const DomainRelationDefinition& relation) {
            return relation.id == id;
        });
    return found == type->relations.cend() ? nullptr : &*found;
}

void addStoredComboValue(QComboBox* combo,
                         const QString& value,
                         const QString& label = QString()) {
    if (value.isEmpty() || combo->findData(value) >= 0) {
        return;
    }
    combo->insertItem(0,
                      label.isEmpty()
                          ? QStringLiteral("[stored / unavailable] %1").arg(value)
                          : label,
                      value);
}

void selectComboData(QComboBox* combo, const QString& value) {
    int index = combo->findData(value);
    if (index < 0 && combo->count() > 0) {
        index = 0;
    }
    combo->setCurrentIndex(index);
}

QStringList dialogErrorsText(const QStringList& errors) {
    QStringList lines;
    for (const QString& error : errors) {
        lines.append(QStringLiteral("• %1").arg(error));
    }
    return lines;
}

class DomainRecordDialog final : public QDialog {
public:
    DomainRecordDialog(PackageDefinition package,
                       QVector<DomainDefinition> domains,
                       std::optional<DomainDefinition> existing,
                       QWidget* parent)
        : QDialog(parent),
          package_(std::move(package)),
          domains_(std::move(domains)),
          existing_(std::move(existing)) {
        setObjectName(QStringLiteral("finepaper.domainConfiguration.domainDialog"));
        setWindowTitle(existing_ ? QStringLiteral("Edit Domain")
                                 : QStringLiteral("Add Domain"));
        setModal(true);
        resize(620, 620);

        auto* root = new QVBoxLayout(this);
        auto* note = new QLabel(
            QStringLiteral("Domain instances are Package-defined design data. "
                           "References may point to other rows in this working copy, "
                           "including the instance itself."), this);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* identity = new QFormLayout;
        type_ = new QComboBox(this);
        type_->setObjectName(objectName() + QStringLiteral(".type"));
        for (const DomainTypeDefinition& type : package_.domainTypes) {
            type_->addItem(typeText(type), type.id);
        }
        if (existing_) {
            addStoredComboValue(type_, existing_->type);
            selectComboData(type_, existing_->type);
            type_->setEnabled(false);
        }
        identity->addRow(QStringLiteral("Type"), type_);

        id_ = new QLineEdit(this);
        id_->setObjectName(objectName() + QStringLiteral(".id"));
        id_->setReadOnly(existing_.has_value());
        name_ = new QLineEdit(this);
        name_->setObjectName(objectName() + QStringLiteral(".name"));
        if (existing_) {
            id_->setText(existing_->id);
            name_->setText(existing_->name);
        }
        identity->addRow(QStringLiteral("ID"), id_);
        identity->addRow(QStringLiteral("Name"), name_);
        root->addLayout(identity);

        auto* propertyGroup = new QGroupBox(QStringLiteral("Properties"), this);
        auto* propertyLayout = new QVBoxLayout(propertyGroup);
        properties_ = new DomainPropertyForm(propertyGroup);
        properties_->setObjectName(objectName() + QStringLiteral(".properties"));
        auto* scroll = new QScrollArea(propertyGroup);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(properties_);
        propertyLayout->addWidget(scroll);
        root->addWidget(propertyGroup, 1);

        diagnostics_ = new QLabel(this);
        diagnostics_->setObjectName(objectName() + QStringLiteral(".diagnostics"));
        diagnostics_->setWordWrap(true);
        diagnostics_->setTextFormat(Qt::PlainText);
        root->addWidget(diagnostics_);

        buttons_ = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        ok_ = buttons_->button(QDialogButtonBox::Ok);
        ok_->setObjectName(objectName() + QStringLiteral(".save"));
        ok_->setText(existing_ ? QStringLiteral("Save Domain")
                               : QStringLiteral("Add Domain"));
        root->addWidget(buttons_);

        connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
            refresh();
            if (ok_->isEnabled()) {
                QDialog::accept();
            }
        });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(id_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(name_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(type_, &QComboBox::currentIndexChanged, this, [this] {
            if (updating_) {
                return;
            }
            rebuildProperties({}, PropertyInitialization::CreateWithDefaults);
            refresh();
        });
        properties_->valuesChanged = [this] { refresh(); };

        rebuildProperties(existing_ ? existing_->properties : QJsonObject{},
                          existing_ ? PropertyInitialization::ExactValues
                                    : PropertyInitialization::CreateWithDefaults);
        refresh();
    }

    DomainDefinition value() const {
        return DomainDefinition{id_->text().trimmed(),
                                type_->currentData().toString().trimmed(),
                                name_->text().trimmed(),
                                properties_->values()};
    }

private:
    const DomainTypeDefinition* selectedType() const {
        return package_.domainType(type_->currentData().toString());
    }

    void rebuildProperties(const QJsonObject& values,
                           PropertyInitialization initialization) {
        updating_ = true;
        DomainPropertyFormOptions options;
        options.initialization = initialization;
        options.validationMode = PropertyValidationMode::Complete;
        options.allowCustomReferences = true;
        const DomainTypeDefinition* type = selectedType();
        properties_->setSchema(type ? type->properties
                                    : QVector<DomainPropertyDefinition>{},
                               domains_, values, options);
        updating_ = false;
    }

    QStringList localErrors() const {
        QStringList errors;
        if (!selectedType()) {
            errors.append(QStringLiteral("Choose a Domain Type declared by the Package."));
        }
        if (id_->text().trimmed().isEmpty()) {
            errors.append(QStringLiteral("Domain ID is required."));
        }
        if (name_->text().trimmed().isEmpty()) {
            errors.append(QStringLiteral("Domain name is required."));
        }
        if (existing_ && (id_->text().trimmed() != existing_->id
                          || type_->currentData().toString() != existing_->type)) {
            errors.append(QStringLiteral("Existing Domain ID and Type are immutable."));
        }
        errors += properties_->localErrors();
        return errors;
    }

    void refresh() {
        if (updating_) {
            return;
        }
        const QStringList errors = localErrors();
        diagnostics_->setText(errors.isEmpty()
                                  ? QStringLiteral("Local fields are valid. The complete "
                                                   "working copy is checked after Save.")
                                  : dialogErrorsText(errors).join(QLatin1Char('\n')));
        ok_->setEnabled(errors.isEmpty());
    }

    PackageDefinition package_;
    QVector<DomainDefinition> domains_;
    std::optional<DomainDefinition> existing_;
    bool updating_ = false;
    QComboBox* type_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLineEdit* name_ = nullptr;
    DomainPropertyForm* properties_ = nullptr;
    QLabel* diagnostics_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

template<typename Row>
const Row* rowByToken(const QVector<Row>& rows,
                      DomainConfigurationDraft::Token token) {
    const auto found = std::find_if(
        rows.cbegin(), rows.cend(), [&](const Row& row) {
            return row.token == token;
        });
    return found == rows.cend() ? nullptr : &*found;
}

QTableWidgetItem* recordItem(
    const QString& text,
    DomainConfigurationDraft::Token token) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setData(domainConfigurationRowTokenRole,
                  QVariant::fromValue<qulonglong>(token));
    return item;
}

QString assignmentText(const DomainMembership& membership) {
    QStringList types = membership.assignments.keys();
    std::sort(types.begin(), types.end());
    QStringList values;
    for (const QString& type : types) {
        values.append(QStringLiteral("%1 = %2")
                          .arg(type,
                               membership.assignments.value(type).join(
                                   QStringLiteral(" + "))));
    }
    return values.join(QStringLiteral("; "));
}

QString diagnosticsText(const QVector<Diagnostic>& diagnostics) {
    QStringList lines;
    for (const Diagnostic& diagnostic : diagnostics) {
        QString line = QStringLiteral("[%1] %2: %3")
                           .arg(diagnostic.severity,
                                diagnostic.code,
                                diagnostic.message);
        if (!diagnostic.path.isEmpty()) {
            line += QStringLiteral(" (%1)").arg(diagnostic.path);
        }
        lines.append(std::move(line));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

class DomainConfigurationDialog::Impl {
public:
    struct Actions {
        QPushButton* add = nullptr;
        QPushButton* edit = nullptr;
        QPushButton* remove = nullptr;
    };

    explicit Impl(const DomainConfiguration& configuration)
        : draft(configuration) {}

    DomainConfigurationDraft draft;
    Actions domainActions;
    Actions membershipActions;
    Actions relationActions;
    Actions policyActions;
    Actions overrideActions;
};

DomainConfigurationDialog::DomainConfigurationDialog(
    NocDesign baseDesign,
    PackageDefinition package,
    DomainConfiguration configuration,
    DomainConfigurationValidator validator,
    QWidget* parent,
    DomainConfigurationPresentation presentation)
    : QDialog(parent),
      m_baseDesign(std::move(baseDesign)),
      m_package(std::move(package)),
      m_initialConfiguration(configuration),
      m_validator(std::move(validator)),
      m_presentation(presentation),
      m_impl(new Impl(configuration)) {
    const bool embedded = m_presentation
        == DomainConfigurationPresentation::EmbeddedWorkspace;
    setObjectName(embedded
                      ? QStringLiteral(
                            "finepaper.domainConfigurationWorkspace.editor")
                      : QStringLiteral("finepaper.domainConfigurationDialog"));
    setWindowTitle(QStringLiteral("Complete Domain Configuration"));
    setModal(!embedded);
    if (embedded) {
        setWindowFlags(Qt::Widget);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    resize(1120, 780);

    auto* root = new QVBoxLayout(this);
    auto* introduction = new QLabel(
        QStringLiteral(
            "Edit all Package-defined Domain data in one working copy. Rows may "
            "be temporarily invalid while required references and relations are "
            "assembled. Apply is atomic. Routers and Router Links remain derived "
            "from the fixed Mesh and have no CRUD controls here."),
        this);
    introduction->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.introduction"));
    introduction->setWordWrap(true);
    root->addWidget(introduction);

    m_summary = new QLabel(this);
    m_summary->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.summary"));
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_summary);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.tabs"));
    m_tabs->setDocumentMode(true);
    root->addWidget(m_tabs, 1);

    const auto createPage = [this](QTableWidget*& table,
                                   Impl::Actions& actions,
                                   const QString& id,
                                   const QStringList& headers) {
        auto* page = new QWidget(m_tabs);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(6, 6, 6, 6);
        table = new QTableWidget(page);
        table->setObjectName(
            QStringLiteral("finepaper.domainConfiguration.%1").arg(id));
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
        table->verticalHeader()->setToolTip(QStringLiteral(
            "Zero-based row index used by diagnostic paths."));
        table->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table, 1);

        auto* buttons = new QHBoxLayout;
        actions.add = new QPushButton(QStringLiteral("Add…"), page);
        actions.edit = new QPushButton(QStringLiteral("Edit…"), page);
        actions.remove = new QPushButton(QStringLiteral("Delete"), page);
        actions.add->setObjectName(table->objectName() + QStringLiteral(".add"));
        actions.edit->setObjectName(table->objectName() + QStringLiteral(".edit"));
        actions.remove->setObjectName(table->objectName() + QStringLiteral(".delete"));
        actions.remove->setToolTip(QStringLiteral(
            "Remove only this working-copy row. Dependent rows are not silently "
            "cascaded; aggregate diagnostics identify every reference that must "
            "be repaired before Apply."));
        buttons->addWidget(actions.add);
        buttons->addWidget(actions.edit);
        buttons->addWidget(actions.remove);
        buttons->addStretch();
        layout->addLayout(buttons);

        Impl::Actions* actionSet = &actions;
        const auto updateActions = [table, actionSet] {
            const bool selected = table->currentRow() >= 0;
            actionSet->edit->setEnabled(selected);
            actionSet->remove->setEnabled(selected);
        };
        connect(table, &QTableWidget::itemSelectionChanged,
                this, updateActions);
        connect(table, &QTableWidget::cellDoubleClicked,
                this, [actionSet](int, int) {
                    if (actionSet->edit->isEnabled()) {
                        actionSet->edit->click();
                    }
                });
        updateActions();
        return page;
    };

    m_tabs->addTab(
        createPage(m_domains, m_impl->domainActions,
                   QStringLiteral("domains"),
                   {QStringLiteral("Type"), QStringLiteral("Name"),
                    QStringLiteral("ID"), QStringLiteral("Properties")}),
        QStringLiteral("Domains"));
    m_tabs->addTab(
        createPage(m_memberships, m_impl->membershipActions,
                   QStringLiteral("memberships"),
                   {QStringLiteral("Element"), QStringLiteral("Assignments")}),
        QStringLiteral("Memberships"));
    m_tabs->addTab(
        createPage(m_relations, m_impl->relationActions,
                   QStringLiteral("relations"),
                   {QStringLiteral("Type"), QStringLiteral("From"),
                    QStringLiteral("To"), QStringLiteral("Properties")}),
        QStringLiteral("Relations"));
    m_tabs->addTab(
        createPage(m_policies, m_impl->policyActions,
                   QStringLiteral("policies"),
                   {QStringLiteral("ID"), QStringLiteral("Domain Type"),
                    QStringLiteral("Boundary Orientation"), QStringLiteral("Properties")}),
        QStringLiteral("Crossing Policies"));
    m_tabs->addTab(
        createPage(m_overrides, m_impl->overrideActions,
                   QStringLiteral("overrides"),
                   {QStringLiteral("Derived Edge"), QStringLiteral("Domain Type"),
                    QStringLiteral("Default Policy"), QStringLiteral("Partial Properties")}),
        QStringLiteral("Edge Overrides"));

    m_diagnostics = new QPlainTextEdit(this);
    m_diagnostics->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.diagnostics"));
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_diagnostics->setMaximumHeight(150);
    root->addWidget(m_diagnostics);

    const QDialogButtonBox::StandardButtons standardButtons = embedded
        ? QDialogButtonBox::Reset
        : QDialogButtonBox::Ok | QDialogButtonBox::Cancel
              | QDialogButtonBox::Reset;
    m_buttons = new QDialogButtonBox(standardButtons, this);
    m_buttons->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.buttons"));
    m_apply = embedded
        ? m_buttons->addButton(
              QStringLiteral("Apply complete configuration"),
              QDialogButtonBox::AcceptRole)
        : m_buttons->button(QDialogButtonBox::Ok);
    m_apply->setText(QStringLiteral("Apply complete configuration"));
    m_apply->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.apply"));
    m_revert = m_buttons->button(QDialogButtonBox::Reset);
    m_revert->setText(QStringLiteral("Revert draft"));
    m_revert->setObjectName(
        QStringLiteral("finepaper.domainConfiguration.revert"));
    root->addWidget(m_buttons);

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(200);
    connect(m_validationTimer, &QTimer::timeout,
            this, [this] { updateValidation(true); });
    connect(m_buttons, &QDialogButtonBox::accepted,
            this, [this] { accept(); });
    if (!embedded) {
        connect(m_buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
    }
    connect(m_revert, &QPushButton::clicked,
            this, [this] { revertDraft(); });

    wireActions();
    rebuildAll();
    updateValidation(true);
}

namespace {

class MembershipRecordDialog final : public QDialog {
public:
    MembershipRecordDialog(NocDesign baseDesign,
                           PackageDefinition package,
                           QVector<DomainDefinition> domains,
                           std::optional<DomainMembership> existing,
                           QWidget* parent)
        : QDialog(parent),
          baseDesign_(std::move(baseDesign)),
          package_(std::move(package)),
          domains_(std::move(domains)),
          existing_(std::move(existing)) {
        setObjectName(
            QStringLiteral("finepaper.domainConfiguration.membershipDialog"));
        setWindowTitle(existing_ ? QStringLiteral("Edit Membership")
                                 : QStringLiteral("Add Membership"));
        setModal(true);
        resize(700, 620);

        auto* root = new QVBoxLayout(this);
        auto* note = new QLabel(
            QStringLiteral("Memberships can target only Routers projected from "
                           "the fixed Mesh or Endpoints already present in the design. "
                           "This editor never creates, deletes, or rewires Routers."),
            this);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* form = new QFormLayout;
        element_ = new QComboBox(this);
        element_->setObjectName(objectName() + QStringLiteral(".element"));
        const TopologyProjection topology = projectTopology(baseDesign_);
        for (const RouterView& router : topology.routers) {
            const ElementRef ref{ElementKind::Router, router.id};
            element_->addItem(
                QStringLiteral("Router %1 (Mesh %2, %3)")
                    .arg(router.id,
                         QString::number(router.position.x),
                         QString::number(router.position.y)),
                elementKey(ref));
        }
        for (const EndpointInstance& endpoint : baseDesign_.endpoints) {
            const ElementRef ref{ElementKind::Endpoint, endpoint.id};
            element_->addItem(QStringLiteral("Endpoint %1").arg(endpoint.id),
                              elementKey(ref));
        }
        if (existing_) {
            addStoredComboValue(element_, elementKey(existing_->element),
                                QStringLiteral("[stored / unavailable] %1")
                                    .arg(elementText(existing_->element)));
            selectComboData(element_, elementKey(existing_->element));
            assignments_ = existing_->assignments;
        }
        form->addRow(QStringLiteral("Element"), element_);
        root->addLayout(form);

        assignmentsTable_ = new QTableWidget(this);
        assignmentsTable_->setObjectName(objectName()
                                         + QStringLiteral(".assignments"));
        assignmentsTable_->setColumnCount(2);
        assignmentsTable_->setHorizontalHeaderLabels(
            {QStringLiteral("Domain Type"), QStringLiteral("Assignment")});
        assignmentsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        assignmentsTable_->setSelectionMode(QAbstractItemView::NoSelection);
        assignmentsTable_->verticalHeader()->hide();
        assignmentsTable_->verticalHeader()->setSectionResizeMode(
            QHeaderView::ResizeToContents);
        assignmentsTable_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
        assignmentsTable_->horizontalHeader()->setStretchLastSection(true);
        root->addWidget(assignmentsTable_, 1);

        diagnostics_ = new QLabel(this);
        diagnostics_->setObjectName(objectName() + QStringLiteral(".diagnostics"));
        diagnostics_->setWordWrap(true);
        diagnostics_->setTextFormat(Qt::PlainText);
        root->addWidget(diagnostics_);

        buttons_ = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        ok_ = buttons_->button(QDialogButtonBox::Ok);
        ok_->setObjectName(objectName() + QStringLiteral(".save"));
        ok_->setText(existing_ ? QStringLiteral("Save Membership")
                               : QStringLiteral("Add Membership"));
        root->addWidget(buttons_);

        connect(element_, &QComboBox::currentIndexChanged, this, [this] {
            if (!updating_) {
                captureAssignments();
                rebuildAssignments();
            }
        });
        connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
            captureAssignments();
            refresh();
            if (ok_->isEnabled()) {
                QDialog::accept();
            }
        });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

        rebuildAssignments();
        refresh();
    }

    DomainMembership value() {
        captureAssignments();
        return DomainMembership{elementFromKey(element_->currentData().toString()),
                                assignments_};
    }

private:
    struct Editor {
        QString type;
        QComboBox* single = nullptr;
        QListWidget* multiple = nullptr;
    };

    ElementRef selectedElement() const {
        return elementFromKey(element_->currentData().toString());
    }

    void captureAssignments() {
        if (updating_) {
            return;
        }
        for (const Editor& editor : std::as_const(editors_)) {
            QStringList ids;
            if (editor.single) {
                const QString id = editor.single->currentData().toString();
                if (!id.isEmpty()) {
                    ids.append(id);
                }
            } else if (editor.multiple) {
                for (int index = 0; index < editor.multiple->count(); ++index) {
                    QListWidgetItem* item = editor.multiple->item(index);
                    if (item->checkState() == Qt::Checked) {
                        ids.append(item->data(Qt::UserRole).toString());
                    }
                }
            }
            if (ids.isEmpty()) {
                assignments_.remove(editor.type);
            } else {
                assignments_.insert(editor.type, ids);
            }
        }
    }

    void addDomainItems(QComboBox* combo,
                        const QString& type,
                        const QStringList& stored,
                        const DomainAssignmentRule& assignmentRule) {
        combo->addItem(
            assignmentRule.requiresAssignment()
                ? QStringLiteral("Unassigned (below minimum %1)")
                      .arg(assignmentRule.minimumAssignments)
                : QStringLiteral("Unassigned"),
            QString());
        for (const DomainDefinition& domain : domains_) {
            if (domain.type == type) {
                combo->addItem(domainText(domain), domain.id);
            }
        }
        for (const QString& id : stored) {
            addStoredComboValue(combo, id);
        }
        selectComboData(combo, stored.value(0));
    }

    void addDomainItems(QListWidget* list,
                        const QString& type,
                        const QStringList& stored) {
        QStringList seen;
        for (const DomainDefinition& domain : domains_) {
            if (domain.type != type) {
                continue;
            }
            auto* item = new QListWidgetItem(domainText(domain), list);
            item->setData(Qt::UserRole, domain.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(stored.contains(domain.id) ? Qt::Checked
                                                           : Qt::Unchecked);
            seen.append(domain.id);
        }
        for (const QString& id : stored) {
            if (seen.contains(id)) {
                continue;
            }
            auto* item = new QListWidgetItem(
                QStringLiteral("[stored / unavailable] %1").arg(id), list);
            item->setData(Qt::UserRole, id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    }

    void rebuildAssignments() {
        updating_ = true;
        assignmentsTable_->setRowCount(0);
        editors_.clear();
        const ElementRef element = selectedElement();
        QSet<QString> rendered;

        for (const DomainTypeDefinition& type : package_.domainTypes) {
            const std::optional<DomainAssignmentRule> assignmentRule =
                type.assignmentRule(element.kind);
            if (!assignmentRule) {
                continue;
            }
            rendered.insert(type.id);
            const int row = assignmentsTable_->rowCount();
            assignmentsTable_->insertRow(row);
            assignmentsTable_->setItem(
                row, 0,
                new QTableWidgetItem(
                    QStringLiteral("%1\n%2")
                        .arg(typeText(type),
                             domain_text::domainAssignmentConstraintText(
                                 *assignmentRule))));
            const QStringList stored = assignments_.value(type.id);
            Editor editor;
            editor.type = type.id;
            if (!assignmentRule->isSingleAssignment()
                || stored.size() > 1) {
                editor.multiple = new QListWidget(assignmentsTable_);
                editor.multiple->setObjectName(
                    objectName() + QStringLiteral(".assignment.")
                    + safeSuffix(type.id));
                addDomainItems(editor.multiple, type.id, stored);
                assignmentsTable_->setCellWidget(row, 1, editor.multiple);
                connect(editor.multiple, &QListWidget::itemChanged,
                        this, [this] {
                            if (!updating_) {
                                captureAssignments();
                                refresh();
                            }
                        });
            } else {
                editor.single = new QComboBox(assignmentsTable_);
                editor.single->setObjectName(
                    objectName() + QStringLiteral(".assignment.")
                    + safeSuffix(type.id));
                addDomainItems(
                    editor.single, type.id, stored, *assignmentRule);
                assignmentsTable_->setCellWidget(row, 1, editor.single);
                connect(editor.single, &QComboBox::currentIndexChanged,
                        this, [this] {
                            if (!updating_) {
                                captureAssignments();
                                refresh();
                            }
                        });
            }
            editors_.append(editor);
        }

        const QStringList storedTypes = assignments_.keys();
        for (const QString& type : storedTypes) {
            if (rendered.contains(type)) {
                continue;
            }
            const int row = assignmentsTable_->rowCount();
            assignmentsTable_->insertRow(row);
            assignmentsTable_->setItem(
                row, 0,
                new QTableWidgetItem(
                    QStringLiteral("[stored / unavailable] %1").arg(type)));
            auto* holder = new QWidget(assignmentsTable_);
            auto* layout = new QHBoxLayout(holder);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* value = new QLabel(assignments_.value(type).join(", "), holder);
            auto* remove = new QPushButton(QStringLiteral("Remove stored value"), holder);
            remove->setObjectName(objectName() + QStringLiteral(".removeUnknown.")
                                  + safeSuffix(type));
            layout->addWidget(value, 1);
            layout->addWidget(remove);
            assignmentsTable_->setCellWidget(row, 1, holder);
            connect(remove, &QPushButton::clicked, this, [this, type] {
                assignments_.remove(type);
                rebuildAssignments();
                refresh();
            });
        }
        updating_ = false;
        refresh();
    }

    QStringList localErrors() const {
        QStringList errors;
        const ElementRef element = selectedElement();
        if (!isDomainMembershipElementKind(element.kind)
            || !designReferenceExists(baseDesign_, element)) {
            errors.append(QStringLiteral("Choose an existing Mesh Router or Endpoint."));
        }
        if (assignments_.isEmpty()) {
            errors.append(QStringLiteral("Choose at least one Domain assignment."));
        }
        return errors;
    }

    void refresh() {
        if (updating_) {
            return;
        }
        const QStringList errors = localErrors();
        diagnostics_->setText(errors.isEmpty()
                                  ? QStringLiteral(
                                        "Local fields are valid. Per-kind minimum/maximum "
                                        "assignments and references are checked for the "
                                        "complete draft.")
                                  : dialogErrorsText(errors).join(QLatin1Char('\n')));
        ok_->setEnabled(errors.isEmpty());
    }

    NocDesign baseDesign_;
    PackageDefinition package_;
    QVector<DomainDefinition> domains_;
    std::optional<DomainMembership> existing_;
    QHash<QString, QStringList> assignments_;
    QVector<Editor> editors_;
    bool updating_ = false;
    QComboBox* element_ = nullptr;
    QTableWidget* assignmentsTable_ = nullptr;
    QLabel* diagnostics_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

class RelationRecordDialog final : public QDialog {
public:
    RelationRecordDialog(PackageDefinition package,
                         QVector<DomainDefinition> domains,
                         std::optional<DomainRelation> existing,
                         QWidget* parent)
        : QDialog(parent),
          package_(std::move(package)),
          domains_(std::move(domains)),
          existing_(std::move(existing)) {
        setObjectName(
            QStringLiteral("finepaper.domainConfiguration.relationDialog"));
        setWindowTitle(existing_ ? QStringLiteral("Edit Domain Relation")
                                 : QStringLiteral("Add Domain Relation"));
        setModal(true);
        resize(650, 620);

        auto* root = new QVBoxLayout(this);
        auto* note = new QLabel(
            QStringLiteral("Relations are declared by the source Domain Type. "
                           "Self references and cycles are allowed in this working "
                           "copy; all required/cardinality rules are checked together."),
            this);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* form = new QFormLayout;
        from_ = new QComboBox(this);
        from_->setObjectName(objectName() + QStringLiteral(".from"));
        for (const DomainDefinition& domain : domains_) {
            from_->addItem(domainText(domain), domain.id);
        }
        type_ = new QComboBox(this);
        type_->setObjectName(objectName() + QStringLiteral(".type"));
        to_ = new QComboBox(this);
        to_->setObjectName(objectName() + QStringLiteral(".to"));
        form->addRow(QStringLiteral("From Domain"), from_);
        form->addRow(QStringLiteral("Relation Type"), type_);
        form->addRow(QStringLiteral("To Domain"), to_);
        root->addLayout(form);

        auto* propertyGroup = new QGroupBox(QStringLiteral("Properties"), this);
        auto* propertyLayout = new QVBoxLayout(propertyGroup);
        properties_ = new DomainPropertyForm(propertyGroup);
        properties_->setObjectName(objectName() + QStringLiteral(".properties"));
        auto* scroll = new QScrollArea(propertyGroup);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(properties_);
        propertyLayout->addWidget(scroll);
        root->addWidget(propertyGroup, 1);

        diagnostics_ = new QLabel(this);
        diagnostics_->setObjectName(objectName() + QStringLiteral(".diagnostics"));
        diagnostics_->setWordWrap(true);
        diagnostics_->setTextFormat(Qt::PlainText);
        root->addWidget(diagnostics_);

        buttons_ = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        ok_ = buttons_->button(QDialogButtonBox::Ok);
        ok_->setObjectName(objectName() + QStringLiteral(".save"));
        ok_->setText(existing_ ? QStringLiteral("Save Relation")
                               : QStringLiteral("Add Relation"));
        root->addWidget(buttons_);

        const QString storedFrom = existing_ ? existing_->from : QString();
        addStoredComboValue(from_, storedFrom);
        selectComboData(from_, storedFrom);
        rebuildType(existing_ ? existing_->type : QString(),
                    existing_ ? existing_->to : QString(),
                    existing_ ? existing_->properties : QJsonObject{},
                    existing_ ? PropertyInitialization::ExactValues
                              : PropertyInitialization::CreateWithDefaults);

        connect(from_, &QComboBox::currentIndexChanged, this, [this] {
            if (updating_) {
                return;
            }
            rebuildType(type_->currentData().toString(),
                        to_->currentData().toString(),
                        properties_->values(),
                        PropertyInitialization::CreateWithDefaults);
        });
        connect(type_, &QComboBox::currentIndexChanged, this, [this] {
            if (updating_) {
                return;
            }
            rebuildTarget(to_->currentData().toString(),
                          properties_->values(),
                          PropertyInitialization::CreateWithDefaults);
        });
        connect(to_, &QComboBox::currentIndexChanged, this,
                [this] { refresh(); });
        properties_->valuesChanged = [this] { refresh(); };
        connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
            refresh();
            if (ok_->isEnabled()) {
                QDialog::accept();
            }
        });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refresh();
    }

    DomainRelation value() const {
        return DomainRelation{type_->currentData().toString().trimmed(),
                              from_->currentData().toString().trimmed(),
                              to_->currentData().toString().trimmed(),
                              properties_->values()};
    }

private:
    const DomainDefinition* source() const {
        return findDomain(domains_, from_->currentData().toString());
    }

    const DomainRelationDefinition* schema() const {
        const DomainDefinition* domain = source();
        return findRelation(domain ? package_.domainType(domain->type) : nullptr,
                            type_->currentData().toString());
    }

    void rebuildType(const QString& desiredType,
                     const QString& desiredTarget,
                     const QJsonObject& values,
                     PropertyInitialization initialization) {
        updating_ = true;
        type_->clear();
        const DomainDefinition* sourceDomain = source();
        const DomainTypeDefinition* sourceType = sourceDomain
            ? package_.domainType(sourceDomain->type) : nullptr;
        if (sourceType) {
            for (const DomainRelationDefinition& relation : sourceType->relations) {
                type_->addItem(relationText(relation), relation.id);
            }
        }
        addStoredComboValue(type_, desiredType);
        selectComboData(type_, desiredType);
        updating_ = false;
        rebuildTarget(desiredTarget, values, initialization);
    }

    void rebuildTarget(const QString& desiredTarget,
                       const QJsonObject& values,
                       PropertyInitialization initialization) {
        updating_ = true;
        to_->clear();
        const DomainRelationDefinition* relation = schema();
        if (relation) {
            for (const DomainDefinition& domain : domains_) {
                if (relation->targetTypes.contains(domain.type)) {
                    to_->addItem(domainText(domain), domain.id);
                }
            }
        }
        addStoredComboValue(to_, desiredTarget);
        selectComboData(to_, desiredTarget);

        DomainPropertyFormOptions options;
        options.initialization = initialization;
        options.validationMode = PropertyValidationMode::Complete;
        options.allowCustomReferences = true;
        properties_->setSchema(
            relation ? relation->properties
                     : QVector<DomainPropertyDefinition>{},
            domains_, values, options);
        updating_ = false;
        refresh();
    }

    QStringList localErrors() const {
        QStringList errors;
        if (!source()) {
            errors.append(QStringLiteral("Choose a Domain present in this draft as the source."));
        }
        const DomainRelationDefinition* relation = schema();
        if (!relation) {
            errors.append(QStringLiteral("Choose a relation declared by the source Domain Type."));
        }
        const DomainDefinition* target = findDomain(
            domains_, to_->currentData().toString());
        if (!target) {
            errors.append(QStringLiteral("Choose a Domain present in this draft as the target."));
        } else if (relation && !relation->targetTypes.contains(target->type)) {
            errors.append(QStringLiteral("The target Domain Type is not allowed by this relation."));
        }
        errors += properties_->localErrors();
        return errors;
    }

    void refresh() {
        if (updating_) {
            return;
        }
        const QStringList errors = localErrors();
        diagnostics_->setText(errors.isEmpty()
                                  ? QStringLiteral("Local fields are valid. Required "
                                                   "relations and cardinality are checked "
                                                   "for the complete draft.")
                                  : dialogErrorsText(errors).join(QLatin1Char('\n')));
        ok_->setEnabled(errors.isEmpty());
    }

    PackageDefinition package_;
    QVector<DomainDefinition> domains_;
    std::optional<DomainRelation> existing_;
    bool updating_ = false;
    QComboBox* from_ = nullptr;
    QComboBox* type_ = nullptr;
    QComboBox* to_ = nullptr;
    DomainPropertyForm* properties_ = nullptr;
    QLabel* diagnostics_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

class PolicyRecordDialog final : public QDialog {
public:
    PolicyRecordDialog(PackageDefinition package,
                       QVector<DomainDefinition> domains,
                       std::optional<DomainCrossingPolicy> existing,
                       QWidget* parent)
        : QDialog(parent),
          package_(std::move(package)),
          domains_(std::move(domains)),
          existing_(std::move(existing)) {
        setObjectName(
            QStringLiteral("finepaper.domainConfiguration.policyDialog"));
        setWindowTitle(existing_ ? QStringLiteral("Edit Crossing Policy")
                                 : QStringLiteral("Add Crossing Policy"));
        setModal(true);
        resize(650, 650);

        auto* root = new QVBoxLayout(this);
        auto* note = new QLabel(
            QStringLiteral("A crossing policy is the single default for one "
                           "canonically oriented Domain boundary (from → to). "
                           "The physical boundary is bidirectional; this stable "
                           "orientation is west→east or north→south for Mesh "
                           "Links and Router→Endpoint for attachments, not a "
                           "one-way traffic direction."), this);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* form = new QFormLayout;
        id_ = new QLineEdit(this);
        id_->setObjectName(objectName() + QStringLiteral(".id"));
        id_->setReadOnly(existing_.has_value());
        domainType_ = new QComboBox(this);
        domainType_->setObjectName(objectName() + QStringLiteral(".domainType"));
        for (const DomainTypeDefinition& type : package_.domainTypes) {
            domainType_->addItem(typeText(type), type.id);
        }
        from_ = new QComboBox(this);
        from_->setObjectName(objectName() + QStringLiteral(".from"));
        to_ = new QComboBox(this);
        to_->setObjectName(objectName() + QStringLiteral(".to"));
        form->addRow(QStringLiteral("Policy ID"), id_);
        form->addRow(QStringLiteral("Domain Type"), domainType_);
        form->addRow(QStringLiteral("From Domain"), from_);
        form->addRow(QStringLiteral("To Domain"), to_);
        root->addLayout(form);

        auto* propertyGroup = new QGroupBox(
            QStringLiteral("Default crossing properties"), this);
        auto* propertyLayout = new QVBoxLayout(propertyGroup);
        properties_ = new DomainPropertyForm(propertyGroup);
        properties_->setObjectName(objectName() + QStringLiteral(".properties"));
        auto* scroll = new QScrollArea(propertyGroup);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(properties_);
        propertyLayout->addWidget(scroll);
        root->addWidget(propertyGroup, 1);

        diagnostics_ = new QLabel(this);
        diagnostics_->setObjectName(objectName() + QStringLiteral(".diagnostics"));
        diagnostics_->setWordWrap(true);
        diagnostics_->setTextFormat(Qt::PlainText);
        root->addWidget(diagnostics_);

        buttons_ = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        ok_ = buttons_->button(QDialogButtonBox::Ok);
        ok_->setObjectName(objectName() + QStringLiteral(".save"));
        ok_->setText(existing_ ? QStringLiteral("Save Policy")
                               : QStringLiteral("Add Policy"));
        root->addWidget(buttons_);

        if (existing_) {
            id_->setText(existing_->id);
            addStoredComboValue(domainType_, existing_->domainType);
            selectComboData(domainType_, existing_->domainType);
        }
        rebuildDomains(existing_ ? existing_->from : QString(),
                       existing_ ? existing_->to : QString(),
                       existing_ ? existing_->properties : QJsonObject{},
                       existing_ ? PropertyInitialization::ExactValues
                                 : PropertyInitialization::CreateWithDefaults);

        connect(id_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(domainType_, &QComboBox::currentIndexChanged, this, [this] {
            if (!updating_) {
                rebuildDomains(from_->currentData().toString(),
                               to_->currentData().toString(),
                               properties_->values(),
                               PropertyInitialization::CreateWithDefaults);
            }
        });
        connect(from_, &QComboBox::currentIndexChanged,
                this, [this] { refresh(); });
        connect(to_, &QComboBox::currentIndexChanged,
                this, [this] { refresh(); });
        properties_->valuesChanged = [this] { refresh(); };
        connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
            refresh();
            if (ok_->isEnabled()) {
                QDialog::accept();
            }
        });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refresh();
    }

    DomainCrossingPolicy value() const {
        return DomainCrossingPolicy{
            id_->text().trimmed(),
            domainType_->currentData().toString().trimmed(),
            from_->currentData().toString().trimmed(),
            to_->currentData().toString().trimmed(),
            properties_->values()};
    }

private:
    const DomainTypeDefinition* selectedType() const {
        return package_.domainType(domainType_->currentData().toString());
    }

    void rebuildDomains(const QString& desiredFrom,
                        const QString& desiredTo,
                        const QJsonObject& values,
                        PropertyInitialization initialization) {
        updating_ = true;
        from_->clear();
        to_->clear();
        const QString typeId = domainType_->currentData().toString();
        for (const DomainDefinition& domain : domains_) {
            if (domain.type != typeId) {
                continue;
            }
            from_->addItem(domainText(domain), domain.id);
            to_->addItem(domainText(domain), domain.id);
        }
        addStoredComboValue(from_, desiredFrom);
        addStoredComboValue(to_, desiredTo);
        selectComboData(from_, desiredFrom);
        selectComboData(to_, desiredTo);

        DomainPropertyFormOptions options;
        options.initialization = initialization;
        options.validationMode = PropertyValidationMode::Complete;
        options.allowCustomReferences = true;
        const DomainTypeDefinition* type = selectedType();
        properties_->setSchema(type ? type->crossingProperties
                                    : QVector<DomainPropertyDefinition>{},
                               domains_, values, options);
        updating_ = false;
        refresh();
    }

    QStringList localErrors() const {
        QStringList errors;
        if (id_->text().trimmed().isEmpty()) {
            errors.append(QStringLiteral("Policy ID is required."));
        }
        const DomainTypeDefinition* type = selectedType();
        if (!type) {
            errors.append(QStringLiteral("Choose a Domain Type declared by the Package."));
        }
        const DomainDefinition* from = findDomain(
            domains_, from_->currentData().toString());
        const DomainDefinition* to = findDomain(
            domains_, to_->currentData().toString());
        if (!from || (type && from->type != type->id)) {
            errors.append(QStringLiteral("Choose a source Domain of the selected Type."));
        }
        if (!to || (type && to->type != type->id)) {
            errors.append(QStringLiteral("Choose a target Domain of the selected Type."));
        }
        if (existing_ && id_->text().trimmed() != existing_->id) {
            errors.append(QStringLiteral("Existing policy ID is immutable."));
        }
        errors += properties_->localErrors();
        return errors;
    }

    void refresh() {
        if (updating_) {
            return;
        }
        const QStringList errors = localErrors();
        diagnostics_->setText(errors.isEmpty()
                                  ? QStringLiteral("Local fields are valid. Policy ID and "
                                                   "canonical boundary-orientation uniqueness are checked "
                                                   "for the complete draft.")
                                  : dialogErrorsText(errors).join(QLatin1Char('\n')));
        ok_->setEnabled(errors.isEmpty());
    }

    PackageDefinition package_;
    QVector<DomainDefinition> domains_;
    std::optional<DomainCrossingPolicy> existing_;
    bool updating_ = false;
    QLineEdit* id_ = nullptr;
    QComboBox* domainType_ = nullptr;
    QComboBox* from_ = nullptr;
    QComboBox* to_ = nullptr;
    DomainPropertyForm* properties_ = nullptr;
    QLabel* diagnostics_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

class OverrideRecordDialog final : public QDialog {
public:
    OverrideRecordDialog(NocDesign baseDesign,
                         PackageDefinition package,
                         DomainConfiguration configuration,
                         std::optional<DomainEdgeOverride> existing,
                         QWidget* parent)
        : QDialog(parent),
          baseDesign_(std::move(baseDesign)),
          package_(std::move(package)),
          configuration_(std::move(configuration)),
          existing_(std::move(existing)) {
        setObjectName(
            QStringLiteral("finepaper.domainConfiguration.overrideDialog"));
        setWindowTitle(existing_ ? QStringLiteral("Edit Edge Override")
                                 : QStringLiteral("Add Edge Override"));
        setModal(true);
        resize(720, 620);

        auto* root = new QVBoxLayout(this);
        auto* note = new QLabel(
            QStringLiteral("Overrides are exceptions on actual oriented, "
                           "bidirectional physical boundaries. Mesh Links use a "
                           "stable west→east / north→south orientation and endpoint "
                           "attachments use Router→Endpoint; neither denotes "
                           "one-way traffic. Set-valued crossings remain visible "
                           "but cannot be overridden."), this);
        note->setWordWrap(true);
        root->addWidget(note);

        auto* form = new QFormLayout;
        edge_ = new QComboBox(this);
        edge_->setObjectName(objectName() + QStringLiteral(".edge"));
        policy_ = new QLineEdit(this);
        policy_->setObjectName(objectName() + QStringLiteral(".policy"));
        policy_->setReadOnly(true);
        form->addRow(QStringLiteral("Directed crossing"), edge_);
        form->addRow(QStringLiteral("Exact default policy"), policy_);
        root->addLayout(form);

        auto* propertyGroup = new QGroupBox(
            QStringLiteral("Override properties (partial)"), this);
        auto* propertyLayout = new QVBoxLayout(propertyGroup);
        properties_ = new DomainPropertyForm(propertyGroup);
        properties_->setObjectName(objectName() + QStringLiteral(".properties"));
        auto* scroll = new QScrollArea(propertyGroup);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(properties_);
        propertyLayout->addWidget(scroll);
        root->addWidget(propertyGroup, 1);

        diagnostics_ = new QLabel(this);
        diagnostics_->setObjectName(objectName() + QStringLiteral(".diagnostics"));
        diagnostics_->setWordWrap(true);
        diagnostics_->setTextFormat(Qt::PlainText);
        root->addWidget(diagnostics_);

        buttons_ = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        ok_ = buttons_->button(QDialogButtonBox::Ok);
        ok_->setObjectName(objectName() + QStringLiteral(".save"));
        ok_->setText(existing_ ? QStringLiteral("Save Override")
                               : QStringLiteral("Add Override"));
        root->addWidget(buttons_);

        populateCrossings();
        const QString desired = existing_
            ? choiceKey(existing_->edge, existing_->domainType)
            : QString();
        if (existing_ && edge_->findData(desired) < 0) {
            edge_->insertItem(
                0,
                QStringLiteral("[stored / invalid] %1 · %2")
                    .arg(elementText(existing_->edge), existing_->domainType),
                desired);
        }
        selectComboData(edge_, desired);
        rebuildProperties(existing_ ? existing_->properties : QJsonObject{},
                          PropertyInitialization::ExactValues);

        connect(edge_, &QComboBox::currentIndexChanged, this, [this] {
            if (!updating_) {
                rebuildProperties(properties_->values(),
                                  PropertyInitialization::ExactValues);
            }
        });
        properties_->valuesChanged = [this] { refresh(); };
        connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
            refresh();
            if (ok_->isEnabled()) {
                QDialog::accept();
            }
        });
        connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refresh();
    }

    DomainEdgeOverride value() const {
        const Choice* selected = selectedChoice();
        if (selected) {
            return DomainEdgeOverride{selected->crossing.edge,
                                      selected->crossing.domainType,
                                      selected->policy,
                                      properties_->values()};
        }
        DomainEdgeOverride result = existing_.value_or(DomainEdgeOverride{});
        result.properties = properties_->values();
        return result;
    }

private:
    struct Choice {
        DomainCrossingView crossing;
        QString policy;
    };

    static QString choiceKey(const ElementRef& edge, const QString& type) {
        return elementKey(edge) + QChar(0x1e) + type;
    }

    const Choice* selectedChoice() const {
        const auto found = choices_.constFind(edge_->currentData().toString());
        return found == choices_.cend() ? nullptr : &found.value();
    }

    QString crossingLabel(const DomainCrossingView& crossing) const {
        const QString direction = QStringLiteral("%1 → %2")
            .arg(crossing.fromElement.id, crossing.toElement.id);
        const QString kind = crossing.edge.kind == ElementKind::RouterLink
            ? QStringLiteral("Mesh Router Link")
            : QStringLiteral("Endpoint Attachment");
        return QStringLiteral("%1 %2 · %3 · %4: %5 → %6")
            .arg(kind,
                 crossing.edge.id,
                 direction,
                 crossing.domainType,
                 crossing.fromDomains.join(QStringLiteral(" + ")),
                 crossing.toDomains.join(QStringLiteral(" + ")));
    }

    void disableItem(int index) {
        if (auto* model = qobject_cast<QStandardItemModel*>(edge_->model())) {
            if (QStandardItem* item = model->item(index)) {
                item->setEnabled(false);
            }
        }
    }

    void populateCrossings() {
        const NocDesign candidateDesign = domain_configuration::replace(
            baseDesign_, configuration_);
        const QVector<DomainCrossingView> crossings =
            projectDomainCrossings(candidateDesign);
        for (const DomainCrossingView& crossing : crossings) {
            QString label = crossingLabel(crossing);
            const QString key = choiceKey(crossing.edge, crossing.domainType);
            bool eligible = crossing.fromDomains.size() == 1
                && crossing.toDomains.size() == 1;
            QString policy;
            if (eligible) {
                policy = crossing.defaultPolicy.value_or(QString());
                eligible = !policy.isEmpty();
                if (!eligible) {
                    label += QStringLiteral(" [no unique exact default policy]");
                }
            } else {
                label += QStringLiteral(" [set-valued; override unavailable]");
            }
            const int index = edge_->count();
            edge_->addItem(label, key);
            if (eligible) {
                choices_.insert(key, Choice{crossing, policy});
            } else {
                disableItem(index);
            }
        }
    }

    void rebuildProperties(const QJsonObject& values,
                           PropertyInitialization initialization) {
        updating_ = true;
        const Choice* selected = selectedChoice();
        const QString typeId = selected
            ? selected->crossing.domainType
            : existing_ ? existing_->domainType : QString();
        const DomainTypeDefinition* type = package_.domainType(typeId);
        policy_->setText(selected ? selected->policy
                                  : existing_ ? existing_->policy : QString());
        DomainPropertyFormOptions options;
        options.initialization = initialization;
        options.validationMode = PropertyValidationMode::Partial;
        options.allowCustomReferences = true;
        QVector<DomainDefinition> domains = configuration_.domains;
        properties_->setSchema(type ? type->crossingProperties
                                    : QVector<DomainPropertyDefinition>{},
                               domains, values, options);
        updating_ = false;
        refresh();
    }

    QStringList localErrors() const {
        QStringList errors;
        if (!selectedChoice()) {
            errors.append(QStringLiteral(
                "Choose a singleton crossing with exactly one matching canonically oriented default policy."));
        }
        errors += properties_->localErrors();
        return errors;
    }

    void refresh() {
        if (updating_) {
            return;
        }
        const QStringList errors = localErrors();
        diagnostics_->setText(errors.isEmpty()
                                  ? QStringLiteral("Local fields are valid. Override "
                                                   "properties are intentionally partial; "
                                                   "the policy supplies defaults.")
                                  : dialogErrorsText(errors).join(QLatin1Char('\n')));
        ok_->setEnabled(errors.isEmpty());
    }

    NocDesign baseDesign_;
    PackageDefinition package_;
    DomainConfiguration configuration_;
    std::optional<DomainEdgeOverride> existing_;
    QHash<QString, Choice> choices_;
    bool updating_ = false;
    QComboBox* edge_ = nullptr;
    QLineEdit* policy_ = nullptr;
    DomainPropertyForm* properties_ = nullptr;
    QLabel* diagnostics_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace

void DomainConfigurationDialog::wireActions() {
    const auto tokenFor = [](QTableWidget* table)
        -> std::optional<DomainConfigurationDraft::Token> {
        const int row = table ? table->currentRow() : -1;
        QTableWidgetItem* item = row >= 0 ? table->item(row, 0) : nullptr;
        if (!item) {
            return std::nullopt;
        }
        return item->data(domainConfigurationRowTokenRole).toULongLong();
    };
    const auto changed = [this] {
        rebuildAll();
        scheduleValidation();
        notifyDraftStateChanged();
    };

    connect(m_impl->domainActions.add, &QPushButton::clicked, this, [this, changed] {
        DomainRecordDialog dialog(
            m_package, domainValues(m_impl->draft), std::nullopt, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.addDomain(dialog.value());
            changed();
        }
    });
    connect(m_impl->domainActions.edit, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        const auto token = tokenFor(m_domains);
        const auto* row = token
            ? rowByToken(m_impl->draft.domains(), *token) : nullptr;
        if (!row) {
            return;
        }
        DomainRecordDialog dialog(
            m_package, domainValues(m_impl->draft), row->value, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.updateDomain(row->token, dialog.value());
            changed();
        }
    });
    connect(m_impl->domainActions.remove, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        if (const auto token = tokenFor(m_domains)) {
            m_impl->draft.removeDomain(*token);
            changed();
        }
    });

    connect(m_impl->membershipActions.add, &QPushButton::clicked,
            this, [this, changed] {
        MembershipRecordDialog dialog(
            m_baseDesign, m_package, domainValues(m_impl->draft),
            std::nullopt, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.addMembership(dialog.value());
            changed();
        }
    });
    connect(m_impl->membershipActions.edit, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        const auto token = tokenFor(m_memberships);
        const auto* row = token
            ? rowByToken(m_impl->draft.memberships(), *token) : nullptr;
        if (!row) {
            return;
        }
        MembershipRecordDialog dialog(
            m_baseDesign, m_package, domainValues(m_impl->draft),
            row->value, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.updateMembership(row->token, dialog.value());
            changed();
        }
    });
    connect(m_impl->membershipActions.remove, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        if (const auto token = tokenFor(m_memberships)) {
            m_impl->draft.removeMembership(*token);
            changed();
        }
    });

    connect(m_impl->relationActions.add, &QPushButton::clicked,
            this, [this, changed] {
        RelationRecordDialog dialog(
            m_package, domainValues(m_impl->draft), std::nullopt, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.addRelation(dialog.value());
            changed();
        }
    });
    connect(m_impl->relationActions.edit, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        const auto token = tokenFor(m_relations);
        const auto* row = token
            ? rowByToken(m_impl->draft.relations(), *token) : nullptr;
        if (!row) {
            return;
        }
        RelationRecordDialog dialog(
            m_package, domainValues(m_impl->draft), row->value, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.updateRelation(row->token, dialog.value());
            changed();
        }
    });
    connect(m_impl->relationActions.remove, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        if (const auto token = tokenFor(m_relations)) {
            m_impl->draft.removeRelation(*token);
            changed();
        }
    });

    connect(m_impl->policyActions.add, &QPushButton::clicked,
            this, [this, changed] {
        PolicyRecordDialog dialog(
            m_package, domainValues(m_impl->draft), std::nullopt, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.addPolicy(dialog.value());
            changed();
        }
    });
    connect(m_impl->policyActions.edit, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        const auto token = tokenFor(m_policies);
        const auto* row = token
            ? rowByToken(m_impl->draft.policies(), *token) : nullptr;
        if (!row) {
            return;
        }
        PolicyRecordDialog dialog(
            m_package, domainValues(m_impl->draft), row->value, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.updatePolicy(row->token, dialog.value());
            changed();
        }
    });
    connect(m_impl->policyActions.remove, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        if (const auto token = tokenFor(m_policies)) {
            m_impl->draft.removePolicy(*token);
            changed();
        }
    });

    connect(m_impl->overrideActions.add, &QPushButton::clicked,
            this, [this, changed] {
        OverrideRecordDialog dialog(
            m_baseDesign, m_package, m_impl->draft.configuration(),
            std::nullopt, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.addOverride(dialog.value());
            changed();
        }
    });
    connect(m_impl->overrideActions.edit, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        const auto token = tokenFor(m_overrides);
        const auto* row = token
            ? rowByToken(m_impl->draft.overrides(), *token) : nullptr;
        if (!row) {
            return;
        }
        OverrideRecordDialog dialog(
            m_baseDesign, m_package, m_impl->draft.configuration(),
            row->value, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_impl->draft.updateOverride(row->token, dialog.value());
            changed();
        }
    });
    connect(m_impl->overrideActions.remove, &QPushButton::clicked,
            this, [this, tokenFor, changed] {
        if (const auto token = tokenFor(m_overrides)) {
            m_impl->draft.removeOverride(*token);
            changed();
        }
    });
}

DomainConfigurationDialog::~DomainConfigurationDialog() {
    delete m_impl;
}

DomainConfiguration DomainConfigurationDialog::configuration() const {
    return m_impl ? m_impl->draft.configuration() : DomainConfiguration{};
}

bool DomainConfigurationDialog::hasPendingChanges() const {
    return configuration() != m_initialConfiguration
        || m_pendingAuthoritativeConfiguration.has_value();
}

void DomainConfigurationDialog::synchronizeContext(
    NocDesign baseDesign,
    PackageDefinition package,
    DomainConfigurationValidator validator) {
    if (!m_impl) {
        return;
    }
    if (m_validationTimer) {
        m_validationTimer->stop();
    }

    const DomainConfiguration incoming =
        domain_configuration::fromDesign(baseDesign);
    const bool hadPendingChanges = hasPendingChanges();
    const DomainConfiguration currentDraft = configuration();
    m_baseDesign = std::move(baseDesign);
    m_package = std::move(package);
    m_validator = std::move(validator);

    // Normal topology/Endpoint refreshes keep the five-array draft intact.
    // This Workspace's own Apply is recognized when the incoming authoritative
    // value equals its draft. Any other Domain change while a draft exists is
    // an invariant violation: preserve the user's draft, disable Apply, and
    // require an explicit discard instead of silently swallowing either side.
    if (!hadPendingChanges || incoming == currentDraft) {
        m_initialConfiguration = incoming;
        m_pendingAuthoritativeConfiguration.reset();
        m_impl->draft.reset(incoming);
        rebuildAll();
        notifyDraftStateChanged();
    } else if (incoming == m_initialConfiguration) {
        m_pendingAuthoritativeConfiguration.reset();
    } else {
        m_pendingAuthoritativeConfiguration = incoming;
    }
    updateValidation(true);
}

void DomainConfigurationDialog::discardDraft() {
    if (!m_impl || !hasPendingChanges()) {
        return;
    }
    if (m_validationTimer) {
        m_validationTimer->stop();
    }
    if (m_pendingAuthoritativeConfiguration) {
        m_initialConfiguration = *m_pendingAuthoritativeConfiguration;
        m_pendingAuthoritativeConfiguration.reset();
    }
    m_impl->draft.reset(m_initialConfiguration);
    rebuildAll();
    updateValidation(true);
    notifyDraftStateChanged();
}

void DomainConfigurationDialog::setBusy(bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    if (m_tabs) {
        m_tabs->setEnabled(!m_busy);
    }
    if (m_apply) {
        m_apply->setEnabled(!m_busy && m_validatedResult.success);
    }
    if (m_revert) {
        m_revert->setEnabled(!m_busy && hasPendingChanges());
    }
}

void DomainConfigurationDialog::accept() {
    if (m_busy) {
        return;
    }
    if (m_validationTimer) {
        m_validationTimer->stop();
    }
    updateValidation(true);
    if (!m_validatedResult.success) {
        return;
    }
    if (m_presentation == DomainConfigurationPresentation::ModalDialog) {
        QDialog::accept();
        return;
    }

    if (!workspaceApplyRequested) {
        m_diagnostics->setPlainText(QStringLiteral(
            "The Domain Configuration Workspace has no Application Apply "
            "handler. The validated draft remains unapplied."));
        return;
    }
    const DesignResult result = m_validatedResult;
    if (workspaceApplyRequested(result)) {
        m_baseDesign = result.design;
        m_initialConfiguration = domain_configuration::fromDesign(result.design);
        m_pendingAuthoritativeConfiguration.reset();
        m_impl->draft.reset(m_initialConfiguration);
        rebuildAll();
        updateValidation(true);
        notifyDraftStateChanged();
    }
}

void DomainConfigurationDialog::reject() {
    if (m_presentation
        == DomainConfigurationPresentation::EmbeddedWorkspace) {
        return;
    }
    if (!m_discardConfirmed
        && !confirmDiscardDraft(QStringLiteral("Closing the complete editor"))) {
        return;
    }
    const bool wasConfirmed = m_discardConfirmed;
    m_discardConfirmed = true;
    QDialog::reject();
    m_discardConfirmed = wasConfirmed;
}

void DomainConfigurationDialog::closeEvent(QCloseEvent* event) {
    if (m_presentation
        == DomainConfigurationPresentation::EmbeddedWorkspace) {
        event->ignore();
        return;
    }
    if (m_discardConfirmed || configuration() == m_initialConfiguration) {
        QDialog::closeEvent(event);
        return;
    }
    if (!confirmDiscardDraft(QStringLiteral("Closing the complete editor"))) {
        event->ignore();
        return;
    }
    m_discardConfirmed = true;
    QDialog::closeEvent(event);
    m_discardConfirmed = false;
}

void DomainConfigurationDialog::rebuildAll() {
    if (!m_impl) {
        return;
    }
    m_updating = true;

    const auto selectedToken = [](QTableWidget* table)
        -> std::optional<DomainConfigurationDraft::Token> {
        const int row = table ? table->currentRow() : -1;
        QTableWidgetItem* item = row >= 0 ? table->item(row, 0) : nullptr;
        return item
            ? std::optional<DomainConfigurationDraft::Token>(
                  item->data(domainConfigurationRowTokenRole).toULongLong())
            : std::nullopt;
    };
    const auto restoreSelection = [](QTableWidget* table,
                                     std::optional<DomainConfigurationDraft::Token> token) {
        if (!table) {
            return;
        }
        for (int row = 0; row < table->rowCount(); ++row) {
            QTableWidgetItem* item = table->item(row, 0);
            if (item && token
                && item->data(domainConfigurationRowTokenRole).toULongLong()
                    == *token) {
                table->selectRow(row);
                return;
            }
        }
        if (table->rowCount() > 0) {
            table->selectRow(0);
        } else {
            table->clearSelection();
        }
    };

    const auto domainToken = selectedToken(m_domains);
    m_domains->setRowCount(m_impl->draft.domains().size());
    for (qsizetype row = 0; row < m_impl->draft.domains().size(); ++row) {
        const auto& entry = m_impl->draft.domains().at(row);
        m_domains->setVerticalHeaderItem(
            row, new QTableWidgetItem(QString::number(row)));
        m_domains->setItem(row, 0, recordItem(entry.value.type, entry.token));
        m_domains->setItem(row, 1, recordItem(entry.value.name, entry.token));
        m_domains->setItem(row, 2, recordItem(entry.value.id, entry.token));
        m_domains->setItem(
            row, 3, recordItem(compactObject(entry.value.properties), entry.token));
    }
    restoreSelection(m_domains, domainToken);

    const auto membershipToken = selectedToken(m_memberships);
    m_memberships->setRowCount(m_impl->draft.memberships().size());
    for (qsizetype row = 0; row < m_impl->draft.memberships().size(); ++row) {
        const auto& entry = m_impl->draft.memberships().at(row);
        m_memberships->setVerticalHeaderItem(
            row, new QTableWidgetItem(QString::number(row)));
        m_memberships->setItem(
            row, 0, recordItem(elementText(entry.value.element), entry.token));
        m_memberships->setItem(
            row, 1, recordItem(assignmentText(entry.value), entry.token));
    }
    restoreSelection(m_memberships, membershipToken);

    const auto relationToken = selectedToken(m_relations);
    m_relations->setRowCount(m_impl->draft.relations().size());
    for (qsizetype row = 0; row < m_impl->draft.relations().size(); ++row) {
        const auto& entry = m_impl->draft.relations().at(row);
        m_relations->setVerticalHeaderItem(
            row, new QTableWidgetItem(QString::number(row)));
        m_relations->setItem(row, 0, recordItem(entry.value.type, entry.token));
        m_relations->setItem(row, 1, recordItem(entry.value.from, entry.token));
        m_relations->setItem(row, 2, recordItem(entry.value.to, entry.token));
        m_relations->setItem(
            row, 3, recordItem(compactObject(entry.value.properties), entry.token));
    }
    restoreSelection(m_relations, relationToken);

    const auto policyToken = selectedToken(m_policies);
    m_policies->setRowCount(m_impl->draft.policies().size());
    for (qsizetype row = 0; row < m_impl->draft.policies().size(); ++row) {
        const auto& entry = m_impl->draft.policies().at(row);
        m_policies->setVerticalHeaderItem(
            row, new QTableWidgetItem(QString::number(row)));
        m_policies->setItem(row, 0, recordItem(entry.value.id, entry.token));
        m_policies->setItem(
            row, 1, recordItem(entry.value.domainType, entry.token));
        m_policies->setItem(
            row, 2,
            recordItem(QStringLiteral("%1 → %2")
                           .arg(entry.value.from, entry.value.to),
                       entry.token));
        m_policies->setItem(
            row, 3, recordItem(compactObject(entry.value.properties), entry.token));
    }
    restoreSelection(m_policies, policyToken);

    const auto overrideToken = selectedToken(m_overrides);
    m_overrides->setRowCount(m_impl->draft.overrides().size());
    for (qsizetype row = 0; row < m_impl->draft.overrides().size(); ++row) {
        const auto& entry = m_impl->draft.overrides().at(row);
        m_overrides->setVerticalHeaderItem(
            row, new QTableWidgetItem(QString::number(row)));
        m_overrides->setItem(
            row, 0, recordItem(elementText(entry.value.edge), entry.token));
        m_overrides->setItem(
            row, 1, recordItem(entry.value.domainType, entry.token));
        m_overrides->setItem(
            row, 2, recordItem(entry.value.policy, entry.token));
        m_overrides->setItem(
            row, 3, recordItem(compactObject(entry.value.properties), entry.token));
    }
    restoreSelection(m_overrides, overrideToken);

    m_tabs->setTabText(
        0, QStringLiteral("Domains (%1)").arg(m_domains->rowCount()));
    m_tabs->setTabText(
        1, QStringLiteral("Memberships (%1)").arg(m_memberships->rowCount()));
    m_tabs->setTabText(
        2, QStringLiteral("Relations (%1)").arg(m_relations->rowCount()));
    m_tabs->setTabText(
        3, QStringLiteral("Crossing Policies (%1)").arg(m_policies->rowCount()));
    m_tabs->setTabText(
        4, QStringLiteral("Edge Overrides (%1)").arg(m_overrides->rowCount()));
    m_summary->setText(
        QStringLiteral(
            "%1 Domain(s), %2 membership record(s), %3 relation(s), %4 "
            "default policy row(s), and %5 edge override(s). The immutable base "
            "contains a %6×%7 Mesh; Router and Router Link identity is derived.")
            .arg(m_domains->rowCount())
            .arg(m_memberships->rowCount())
            .arg(m_relations->rowCount())
            .arg(m_policies->rowCount())
            .arg(m_overrides->rowCount())
            .arg(m_baseDesign.topology.rows)
            .arg(m_baseDesign.topology.columns));
    m_revert->setEnabled(!m_busy && hasPendingChanges());

    m_domains->resizeColumnsToContents();
    m_memberships->resizeColumnsToContents();
    m_relations->resizeColumnsToContents();
    m_policies->resizeColumnsToContents();
    m_overrides->resizeColumnsToContents();
    m_updating = false;
}

void DomainConfigurationDialog::scheduleValidation() {
    if (m_updating || !m_validationTimer) {
        return;
    }
    m_validatedResult = {};
    m_apply->setEnabled(false);
    m_diagnostics->setPlainText(
        QStringLiteral("Draft changed; validating the complete configuration…"));
    m_validationTimer->start();
}

void DomainConfigurationDialog::updateValidation(bool authoritative) {
    if (m_updating || !m_apply || !m_diagnostics) {
        return;
    }
    if (!authoritative) {
        scheduleValidation();
        return;
    }
    if (m_validator) {
        m_validatedResult = m_validator(configuration());
    } else {
        m_validatedResult = {};
        m_validatedResult.design = domain_configuration::replace(
            m_baseDesign, configuration());
        m_validatedResult.diagnostics.append(Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("domain_configuration.validator_missing"),
            QStringLiteral(
                "The complete Domain editor has no Application validator; "
                "Apply is disabled to avoid accepting an unchecked design."),
            QStringLiteral("/domainConfiguration"),
            QStringLiteral("finepaper")});
    }
    if (m_pendingAuthoritativeConfiguration) {
        m_validatedResult.success = false;
        m_validatedResult.diagnostics.prepend(Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("domain_configuration.external_change_conflict"),
            QStringLiteral(
                "The durable Domain configuration changed while this Workspace "
                "still had an unapplied draft. The draft was preserved. Discard "
                "it explicitly to load the authoritative configuration."),
            QStringLiteral("/domainConfiguration"),
            QStringLiteral("finepaper")});
    }
    const QString text = diagnosticsText(m_validatedResult.diagnostics);
    if (text.isEmpty()) {
        m_diagnostics->setPlainText(
            m_validatedResult.success
                ? QStringLiteral("Complete Domain configuration is valid.")
                : QStringLiteral("Complete Domain configuration is invalid."));
    } else {
        m_diagnostics->setPlainText(text);
    }
    m_diagnostics->setProperty(
        "validationState",
        m_validatedResult.success ? QStringLiteral("valid")
                                  : QStringLiteral("invalid"));
    m_apply->setEnabled(!m_busy && m_validatedResult.success);
    m_revert->setEnabled(!m_busy && hasPendingChanges());
}

void DomainConfigurationDialog::revertDraft() {
    if (!m_impl) {
        return;
    }
    if (hasPendingChanges()
        && !confirmDiscardDraft(QStringLiteral("Reverting the complete draft"))) {
        return;
    }
    if (m_pendingAuthoritativeConfiguration) {
        m_initialConfiguration = *m_pendingAuthoritativeConfiguration;
        m_pendingAuthoritativeConfiguration.reset();
    }
    m_impl->draft.reset(m_initialConfiguration);
    rebuildAll();
    updateValidation(true);
    notifyDraftStateChanged();
}

void DomainConfigurationDialog::notifyDraftStateChanged() {
    if (draftStateChanged) {
        draftStateChanged(hasPendingChanges());
    }
}

bool DomainConfigurationDialog::confirmDiscardDraft(const QString& action) {
    if (!hasPendingChanges()) {
        return true;
    }

    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Discard complete Domain draft?"),
        QStringLiteral(
            "%1 will discard every unapplied change across Domains, "
            "Memberships, Relations, Crossing Policies, and Edge Overrides.")
            .arg(action),
        QMessageBox::NoButton,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.domainConfiguration.discardConfirmation"));
    auto* continueEditing = confirmation.addButton(
        QStringLiteral("Continue editing"), QMessageBox::RejectRole);
    continueEditing->setObjectName(
        QStringLiteral(
            "finepaper.domainConfiguration.discardConfirmation.continue"));
    auto* discard = confirmation.addButton(
        QStringLiteral("Discard draft"), QMessageBox::DestructiveRole);
    discard->setObjectName(
        QStringLiteral(
            "finepaper.domainConfiguration.discardConfirmation.discard"));
    confirmation.setDefaultButton(
        qobject_cast<QPushButton*>(continueEditing));
    confirmation.exec();
    return confirmation.clickedButton() == discard;
}

} // namespace finepaper
