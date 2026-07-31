#include "features/design_extensions/design_extensions_workspace.h"

#include "features/design_extensions/design_extension_editor_dialog.h"

#include <QAbstractItemView>
#include <QFont>
#include <QJsonArray>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <utility>

namespace finepaper {
namespace {

QString schemaTitle(const DesignExtensionDefinition& definition) {
    const QJsonValue title = definition.schemaDocument.value(
        QStringLiteral("title"));
    return title.isString() && !title.toString().trimmed().isEmpty()
        ? title.toString().trimmed()
        : definition.id;
}

QString schemaDescription(const DesignExtensionDefinition& definition) {
    const QJsonValue description = definition.schemaDocument.value(
        QStringLiteral("description"));
    return description.isString() ? description.toString().trimmed()
                                  : QString();
}

QString firstSchemaIssue(const DesignExtensionDefinition& definition) {
    if (definition.schemaIssues.isEmpty()) {
        return QStringLiteral(
            "The Package schema is not supported by this Finepaper build.");
    }
    const json_schema::Issue& issue = definition.schemaIssues.constFirst();
    QString text = issue.message;
    if (!issue.schemaPointer.isEmpty()) {
        text += QStringLiteral(" Schema: #%1").arg(issue.schemaPointer);
    }
    return text;
}

bool supportedEditor(const DesignExtensionDefinition& definition) {
    return definition.schemaStatus == json_schema::CompileStatus::Ready
        && definition.compiledSchema && definition.editor
        && definition.editor->kind == QStringLiteral("json-schema");
}

QString editorUnavailableReason(
    const DesignExtensionDefinition& definition) {
    if (definition.schemaStatus != json_schema::CompileStatus::Ready
        || !definition.compiledSchema) {
        return firstSchemaIssue(definition);
    }
    if (!definition.editor) {
        return QStringLiteral(
            "The Package did not declare an editor for this extension. "
            "Finepaper will not infer one from its namespace.");
    }
    if (definition.editor->kind != QStringLiteral("json-schema")) {
        return QStringLiteral(
            "This Finepaper build does not support the Package editor kind “%1”.")
            .arg(definition.editor->kind);
    }
    return {};
}

} // namespace

DesignExtensionsWorkspace::DesignExtensionsWorkspace(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.designExtensionsWorkspace"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(10);

    auto* heading = new QLabel(QStringLiteral("Design Extensions"), this);
    QFont headingFont = heading->font();
    headingFont.setPointSize(18);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto* introduction = new QLabel(
        QStringLiteral(
            "Configure design data declared by the active NoC IP Package. "
            "Available editors and validation schemas come from explicit "
            "Package capabilities."),
        this);
    introduction->setObjectName(
        QStringLiteral("finepaper.designExtensions.introduction"));
    introduction->setWordWrap(true);
    introduction->setTextFormat(Qt::PlainText);
    layout->addWidget(introduction);

    m_workspaceState = new QLabel(this);
    m_workspaceState->setObjectName(
        QStringLiteral("finepaper.designExtensions.workspaceState"));
    m_workspaceState->setWordWrap(true);
    m_workspaceState->setTextFormat(Qt::PlainText);
    m_workspaceState->setTextInteractionFlags(Qt::TextSelectableByKeyboard
                                              | Qt::TextSelectableByMouse);
    layout->addWidget(m_workspaceState);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(
        QStringLiteral("finepaper.designExtensions.splitter"));
    m_splitter->setChildrenCollapsible(false);

    auto* listPane = new QWidget(m_splitter);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 8, 0);
    auto* listLabel = new QLabel(QStringLiteral("Package extensions"), listPane);
    QFont listLabelFont = listLabel->font();
    listLabelFont.setBold(true);
    listLabel->setFont(listLabelFont);
    listLayout->addWidget(listLabel);
    m_filter = new QLineEdit(listPane);
    m_filter->setObjectName(
        QStringLiteral("finepaper.designExtensions.filter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter extensions"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setAccessibleName(QStringLiteral("Filter design extensions"));
    listLabel->setBuddy(m_filter);
    listLayout->addWidget(m_filter);
    m_list = new QListWidget(listPane);
    m_list->setObjectName(QStringLiteral("finepaper.designExtensions.list"));
    m_list->setAccessibleName(QStringLiteral("Package design extensions"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);
    m_list->setWordWrap(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listLayout->addWidget(m_list, 1);

    auto* detailPane = new QWidget(m_splitter);
    auto* detailLayout = new QVBoxLayout(detailPane);
    detailLayout->setContentsMargins(8, 0, 0, 0);
    detailLayout->setSpacing(10);

    m_title = new QLabel(QStringLiteral("Select an extension"), detailPane);
    m_title->setObjectName(
        QStringLiteral("finepaper.designExtensions.title"));
    QFont detailTitleFont = m_title->font();
    detailTitleFont.setPointSize(15);
    detailTitleFont.setBold(true);
    m_title->setFont(detailTitleFont);
    m_title->setWordWrap(true);
    m_title->setTextFormat(Qt::PlainText);
    detailLayout->addWidget(m_title);

    m_namespace = new QLabel(detailPane);
    m_namespace->setObjectName(
        QStringLiteral("finepaper.designExtensions.namespace"));
    m_namespace->setWordWrap(true);
    m_namespace->setTextFormat(Qt::PlainText);
    m_namespace->setTextInteractionFlags(Qt::TextSelectableByKeyboard
                                         | Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_namespace);

    m_status = new QLabel(detailPane);
    m_status->setObjectName(
        QStringLiteral("finepaper.designExtensions.status"));
    QFont statusFont = m_status->font();
    statusFont.setBold(true);
    m_status->setFont(statusFont);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    detailLayout->addWidget(m_status);

    m_description = new QLabel(detailPane);
    m_description->setObjectName(
        QStringLiteral("finepaper.designExtensions.description"));
    m_description->setWordWrap(true);
    m_description->setTextFormat(Qt::PlainText);
    m_description->setTextInteractionFlags(Qt::TextSelectableByKeyboard
                                           | Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_description);

    m_statusDetails = new QLabel(detailPane);
    m_statusDetails->setObjectName(
        QStringLiteral("finepaper.designExtensions.statusDetails"));
    m_statusDetails->setWordWrap(true);
    m_statusDetails->setTextFormat(Qt::PlainText);
    m_statusDetails->setTextInteractionFlags(Qt::TextSelectableByKeyboard
                                             | Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_statusDetails);

    auto* buttonRow = new QHBoxLayout;
    m_openButton = new QPushButton(QStringLiteral("&View…"), detailPane);
    m_openButton->setObjectName(
        QStringLiteral("finepaper.designExtensions.open"));
    m_removeButton = new QPushButton(
        QStringLiteral("&Remove Configuration…"), detailPane);
    m_removeButton->setObjectName(
        QStringLiteral("finepaper.designExtensions.remove"));
    buttonRow->addWidget(m_openButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    detailLayout->addLayout(buttonRow);
    detailLayout->addStretch(1);

    m_splitter->addWidget(listPane);
    m_splitter->addWidget(detailPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({280, 620});
    layout->addWidget(m_splitter, 1);

    connect(m_filter, &QLineEdit::textChanged,
            this, &DesignExtensionsWorkspace::rebuildList);
    auto* focusFilter = new QShortcut(QKeySequence::Find, this);
    focusFilter->setObjectName(
        QStringLiteral("finepaper.designExtensions.focusFilter"));
    focusFilter->setContext(Qt::WidgetWithChildrenShortcut);
    connect(focusFilter, &QShortcut::activated, this, [this] {
        m_filter->setFocus(Qt::ShortcutFocusReason);
        m_filter->selectAll();
    });
    connect(m_list, &QListWidget::currentRowChanged,
            this, &DesignExtensionsWorkspace::updateDetails);
    connect(m_list, &QListWidget::itemActivated,
            this, [this](QListWidgetItem*) { openSelected(); });
    connect(m_openButton, &QPushButton::clicked,
            this, &DesignExtensionsWorkspace::openSelected);
    connect(m_removeButton, &QPushButton::clicked,
            this, &DesignExtensionsWorkspace::removeSelected);

    rebuildEntries();
}

void DesignExtensionsWorkspace::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const Qt::Orientation orientation = event->size().width() < 720
        ? Qt::Vertical
        : Qt::Horizontal;
    if (!m_splitter || m_splitter->orientation() == orientation) {
        return;
    }
    m_splitter->setOrientation(orientation);
    if (orientation == Qt::Vertical) {
        m_splitter->setSizes(
            {150, (std::max)(190, m_splitter->height() - 150)});
    } else {
        m_splitter->setSizes(
            {280, (std::max)(420, m_splitter->width() - 280)});
    }
}

void DesignExtensionsWorkspace::setContext(
    const NocDesign* design,
    const PackageDefinition* package) {
    m_hasDesign = design;
    m_hasPackage = package;
    m_packageData = design ? design->packageData : QJsonObject{};
    m_requiredPackage = design
        ? QStringLiteral("%1@%2")
              .arg(design->package.id, design->package.version)
        : QString();
    if (m_validationCachePackage != m_requiredPackage) {
        m_validationCache.clear();
        m_validationCachePackage = m_requiredPackage;
    }
    m_definitions = package ? package->designExtensions
                            : QVector<DesignExtensionDefinition>{};

    rebuildEntries();
    rebuildList();
}

void DesignExtensionsWorkspace::setBusy(bool busy) {
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    updateDetails();
}

void DesignExtensionsWorkspace::rebuildEntries() {
    m_entries.clear();
    QSet<QString> declared;
    QSet<QString> cacheable;
    for (const DesignExtensionDefinition& definition : m_definitions) {
        declared.insert(definition.id);
        Entry entry;
        entry.id = definition.id;
        entry.title = schemaTitle(definition);
        entry.description = schemaDescription(definition);
        entry.configured = m_packageData.contains(definition.id);
        entry.value = entry.configured
            ? m_packageData.value(definition.id)
            : initialValue(definition);
        entry.definition = definition;
        entry.editable = supportedEditor(definition);
        entry.removable = entry.configured;

        const bool schemaReady = definition.schemaStatus
                == json_schema::CompileStatus::Ready
            && definition.compiledSchema;
        bool storedValueValid = true;
        if (entry.configured && schemaReady) {
            cacheable.insert(definition.id);
            storedValueValid = structurallyValid(definition, entry.value);
        }

        if (!schemaReady) {
            entry.statusKind = Entry::StatusKind::Warning;
            entry.status = entry.configured
                ? QStringLiteral("Unsupported schema · Read-only")
                : QStringLiteral("Unavailable · Unsupported schema");
            entry.statusDetails = firstSchemaIssue(definition);
        } else if (entry.configured && !storedValueValid) {
            entry.statusKind = Entry::StatusKind::Warning;
            entry.status = entry.editable
                ? QStringLiteral("Invalid value · Repair available")
                : QStringLiteral("Invalid value · Read-only");
            entry.statusDetails = entry.editable
                ? QStringLiteral(
                      "Open the JSON editor to repair the stored value before validation or generation.")
                : editorUnavailableReason(definition);
        } else if (!entry.editable) {
            entry.statusKind = Entry::StatusKind::ReadOnly;
            entry.status = entry.configured
                ? QStringLiteral("Configured · Read-only")
                : QStringLiteral("Not configured · No supported editor");
            entry.statusDetails = editorUnavailableReason(definition);
        } else if (entry.configured) {
            entry.statusKind = Entry::StatusKind::Configured;
            entry.status = QStringLiteral("Configured · Editable");
            entry.statusDetails = QStringLiteral(
                "The stored JSON satisfies the Package schema. Package semantic checks run during Validate / DRC and generation.");
        } else {
            entry.status = QStringLiteral("Not configured · Editable");
            entry.statusDetails = definition.schemaDocument.contains(
                                      QStringLiteral("default"))
                ? QStringLiteral(
                      "Configure this optional extension from the Package-declared default.")
                : QStringLiteral(
                      "Configure this optional extension. The initial JSON is a conservative schema-root value.");
        }
        m_entries.append(std::move(entry));
    }

    QStringList extraIds;
    for (auto it = m_packageData.constBegin();
         it != m_packageData.constEnd(); ++it) {
        if (!declared.contains(it.key())) {
            extraIds.append(it.key());
        }
    }
    std::sort(extraIds.begin(), extraIds.end());
    for (const QString& id : extraIds) {
        Entry entry;
        entry.id = id;
        entry.title = id;
        entry.value = m_packageData.value(id);
        entry.configured = true;
        entry.statusKind = Entry::StatusKind::Warning;
        entry.status = m_hasPackage
            ? QStringLiteral("Undeclared Package data · Read-only")
            : QStringLiteral("Package unavailable · Read-only");
        entry.statusDetails = m_hasPackage
            ? QStringLiteral(
                  "The current Package does not declare this namespace. Finepaper preserves it but cannot safely edit or remove it.")
            : QStringLiteral(
                  "Install or reload the exact Package to recover declared editor capabilities. Finepaper preserves this JSON unchanged.");
        m_entries.append(std::move(entry));
    }

    for (auto it = m_validationCache.begin();
         it != m_validationCache.end();) {
        if (cacheable.contains(it.key())) {
            ++it;
        } else {
            it = m_validationCache.erase(it);
        }
    }
}

void DesignExtensionsWorkspace::rebuildList() {
    const QString selectedId = selectedExtensionId();
    const QString filter = m_filter->text().trimmed();
    m_list->clear();
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        const Entry& entry = m_entries.at(index);
        const QString searchable = entry.title + QLatin1Char(' ') + entry.id
            + QLatin1Char(' ') + entry.status;
        if (!filter.isEmpty()
            && !searchable.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2")
                .arg(entry.title)
                .arg(entry.status),
            m_list);
        switch (entry.statusKind) {
        case Entry::StatusKind::Configured:
            item->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
            break;
        case Entry::StatusKind::ReadOnly:
            item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
            break;
        case Entry::StatusKind::Warning:
            item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
            break;
        case Entry::StatusKind::Normal:
            item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
            break;
        }
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(
            QStringLiteral("%1\nNamespace: %2")
                .arg(entry.statusDetails)
                .arg(entry.id));
        if (entry.id == selectedId) {
            m_list->setCurrentItem(item);
        }
    }
    if (m_list->currentRow() < 0 && m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    updateDetails();
}

void DesignExtensionsWorkspace::updateDetails() {
    if (!m_hasDesign) {
        m_workspaceState->setText(
            QStringLiteral(
                "No design is open. Create a NoC design or open an existing design to inspect its Package extensions."));
    } else if (!m_hasPackage) {
        m_workspaceState->setText(
            QStringLiteral(
                "The exact Package %1 is not loaded. Stored Package data remains available for read-only inspection; install or reload that Package to restore editing.")
                .arg(m_requiredPackage));
    } else if (m_entries.isEmpty()) {
        m_workspaceState->setText(
            QStringLiteral(
                "This NoC IP Package does not declare Design Extensions, and the design contains no retained Package data."));
    } else {
        const qsizetype configured = std::count_if(
            m_entries.cbegin(), m_entries.cend(),
            [](const Entry& entry) { return entry.configured; });
        const QString namespaceLabel = m_entries.size() == 1
            ? QStringLiteral("extension namespace")
            : QStringLiteral("extension namespaces");
        m_workspaceState->setText(
            QStringLiteral("%1 of %2 %3 configured for %4.")
                .arg(configured)
                .arg(m_entries.size())
                .arg(namespaceLabel, m_requiredPackage));
    }

    const int index = selectedEntryIndex();
    if (index < 0) {
        if (!m_hasDesign) {
            m_title->setText(QStringLiteral("No design open"));
        } else if (!m_hasPackage) {
            m_title->setText(QStringLiteral("Package unavailable"));
        } else if (!m_filter->text().trimmed().isEmpty()
                   && m_list->count() == 0) {
            m_title->setText(QStringLiteral("No matching extensions"));
        } else if (m_entries.isEmpty()) {
            m_title->setText(QStringLiteral("No design extensions"));
        } else {
            m_title->setText(QStringLiteral("Select an extension"));
        }
        m_namespace->clear();
        m_status->clear();
        m_description->clear();
        m_description->setVisible(false);
        m_statusDetails->clear();
        m_openButton->setEnabled(false);
        m_openButton->setVisible(false);
        m_removeButton->setEnabled(false);
        m_removeButton->setVisible(false);
        return;
    }

    const Entry& entry = m_entries.at(index);
    m_title->setText(entry.title);
    m_namespace->setText(
        QStringLiteral("Namespace: %1").arg(entry.id));
    m_status->setText(entry.status);
    m_description->setText(entry.description);
    m_description->setVisible(!entry.description.isEmpty());
    m_statusDetails->setText(entry.statusDetails);

    const bool canOpen = entry.configured || entry.editable;
    m_openButton->setVisible(true);
    m_openButton->setEnabled(canOpen && !m_busy);
    m_openButton->setText(
        entry.editable
            ? (entry.configured ? QStringLiteral("&Edit…")
                                : QStringLiteral("&Configure…"))
            : QStringLiteral("&View…"));
    m_openButton->setToolTip(
        entry.editable
            ? QStringLiteral("Open the schema-aware JSON source editor.")
            : QStringLiteral("Inspect the stored JSON without changing it."));
    m_removeButton->setEnabled(entry.removable && !m_busy);
    m_removeButton->setVisible(entry.removable);
}

void DesignExtensionsWorkspace::openSelected() {
    const int index = selectedEntryIndex();
    if (index < 0 || m_busy) {
        return;
    }
    const Entry entry = m_entries.at(index);
    if (!entry.configured && !entry.editable) {
        return;
    }

    DesignExtensionEditorContext context;
    context.id = entry.id;
    context.title = entry.title;
    context.description = entry.description;
    context.editorMessage = entry.statusDetails;
    context.value = entry.value;
    context.definition = entry.definition;
    context.configured = entry.configured;
    context.editable = entry.editable;

    DesignExtensionEditorDialog dialog(std::move(context), this);
    dialog.applyRequested = applyRequested;
    dialog.exec();
}

void DesignExtensionsWorkspace::removeSelected() {
    const int index = selectedEntryIndex();
    if (index < 0 || m_busy || !removeRequested) {
        return;
    }
    const Entry entry = m_entries.at(index);
    if (!entry.removable) {
        return;
    }

    QMessageBox confirmation(
        QMessageBox::Warning,
        QStringLiteral("Remove Design Extension"),
        QStringLiteral(
            "Remove the stored configuration for “%1”?\n\n"
            "The Package will no longer receive this extension during validation "
            "or generation. This cannot be undone except by configuring it again.")
            .arg(entry.title),
        QMessageBox::Yes | QMessageBox::Cancel,
        this);
    confirmation.setObjectName(
        QStringLiteral("finepaper.designExtensions.removeConfirmation"));
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    if (confirmation.exec() != QMessageBox::Yes) {
        return;
    }

    const DesignResult result = removeRequested(entry.id);
    if (!result.success) {
        QStringList messages;
        messages.reserve(result.diagnostics.size());
        for (const Diagnostic& diagnostic : result.diagnostics) {
            messages.append(QStringLiteral("%1: %2")
                                .arg(diagnostic.path.isEmpty()
                                         ? QStringLiteral("/")
                                         : diagnostic.path,
                                     diagnostic.message));
        }
        m_statusDetails->setText(
            messages.isEmpty()
                ? QStringLiteral(
                      "Removal was rejected; the stored configuration is unchanged.")
                : QStringLiteral("Removal was rejected:\n%1")
                      .arg(messages.join(QLatin1Char('\n'))));
    }
}

QString DesignExtensionsWorkspace::selectedExtensionId() const {
    const QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

int DesignExtensionsWorkspace::selectedEntryIndex() const {
    const QString id = selectedExtensionId();
    const auto found = std::find_if(
        m_entries.cbegin(), m_entries.cend(),
        [&id](const Entry& entry) { return entry.id == id; });
    return found == m_entries.cend()
        ? -1
        : static_cast<int>(std::distance(m_entries.cbegin(), found));
}

bool DesignExtensionsWorkspace::structurallyValid(
    const DesignExtensionDefinition& definition,
    const QJsonValue& value) {
    const auto cached = m_validationCache.constFind(definition.id);
    if (cached != m_validationCache.cend()
        && cached->schema == definition.compiledSchema
        && cached->value == value) {
        return cached->valid;
    }
    const bool valid = definition.schemaStatus
            == json_schema::CompileStatus::Ready
        && definition.compiledSchema
        && json_schema::validate(*definition.compiledSchema, value).success;
    m_validationCache.insert(
        definition.id,
        ValidationCacheEntry{definition.compiledSchema, value, valid});
    return valid;
}

QJsonValue DesignExtensionsWorkspace::initialValue(
    const DesignExtensionDefinition& definition) {
    if (definition.schemaDocument.contains(QStringLiteral("default"))) {
        return definition.schemaDocument.value(QStringLiteral("default"));
    }
    const QJsonValue type = definition.schemaDocument.value(
        QStringLiteral("type"));
    if (type == QStringLiteral("array")) {
        return QJsonArray{};
    }
    if (type == QStringLiteral("object")) {
        return QJsonObject{};
    }
    return QJsonValue(QJsonValue::Null);
}

} // namespace finepaper
