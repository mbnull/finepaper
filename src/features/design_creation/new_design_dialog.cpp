#include "features/design_creation/new_design_dialog.h"

#include "ui/theme/ui_tokens.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

constexpr qsizetype maximumCapabilityPreviewItems = 4;

QString definitionDisplayText(const QString& id, const QString& label) {
    const QString normalizedLabel = label.trimmed();
    if (normalizedLabel.isEmpty() || normalizedLabel == id) {
        return id;
    }
    return QStringLiteral("%1 (%2)").arg(normalizedLabel, id);
}

template <typename Definition>
QStringList definitionDisplayTexts(const QVector<Definition>& definitions) {
    const qsizetype previewCount = (std::min)(
        definitions.size(), maximumCapabilityPreviewItems);
    QStringList result;
    result.reserve(previewCount);
    for (qsizetype index = 0; index < previewCount; ++index) {
        const Definition& definition = definitions.at(index);
        result.append(definitionDisplayText(definition.id, definition.label));
    }
    return result;
}

QString topologyDisplayText(const QString& topologyId) {
    QString result = topologyId.trimmed();
    result.replace(QLatin1Char('-'), QLatin1Char(' '));
    result.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (result.isEmpty()) {
        return QStringLiteral("Not declared");
    }
    result[0] = result.at(0).toUpper();
    return result;
}

QString capabilityListText(const QStringList& values, qsizetype declaredCount) {
    const qsizetype totalCount = (std::max)(declaredCount, values.size());
    if (totalCount == 0) {
        return QStringLiteral("None declared");
    }
    const qsizetype previewCount = (std::min)(
        values.size(), maximumCapabilityPreviewItems);
    const QString preview = values.sliced(0, previewCount).join(
        QStringLiteral(", "));
    if (previewCount == 0) {
        return QStringLiteral("%1 declared").arg(totalCount);
    }
    const qsizetype remaining = totalCount - previewCount;
    return remaining > 0
        ? QStringLiteral("%1 (+%2 more)")
              .arg(preview, QString::number(remaining))
        : preview;
}

QLabel* formLabel(const QString& text, QWidget* buddy, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setBuddy(buddy);
    return label;
}

} // namespace

DesignCreationPackageOption designCreationPackageOption(
    const PackageDefinition& package) {
    DesignCreationPackageOption option;
    option.reference = PackageReference{package.id, package.version};
    option.name = package.name;
    option.defaultTopology.rows = package.mesh.defaultRows;
    option.defaultTopology.columns = package.mesh.defaultColumns;
    option.minimumRows = package.mesh.minimumRows;
    option.maximumRows = package.mesh.maximumRows;
    option.minimumColumns = package.mesh.minimumColumns;
    option.maximumColumns = package.mesh.maximumColumns;
    option.endpointTypes = definitionDisplayTexts(package.endpointTypes);
    option.domainTypes = definitionDisplayTexts(package.domainTypes);
    option.endpointTypeCount = package.endpointTypes.size();
    option.domainTypeCount = package.domainTypes.size();
    option.elementPropertySetCount = package.elementPropertySets.size();
    option.designExtensionCount = package.designExtensions.size();
    return option;
}

QVector<DesignCreationPackageOption> designCreationPackageOptions(
    const QVector<PackageDefinition>& packages) {
    QVector<DesignCreationPackageOption> result;
    result.reserve(packages.size());
    for (const PackageDefinition& package : packages) {
        result.append(designCreationPackageOption(package));
    }
    return result;
}

NewDesignDialog::NewDesignDialog(
    QVector<DesignCreationPackageOption> packages,
    const QString& preferredPackageKey,
    const QString& suggestedName,
    QWidget* parent)
    : QDialog(parent),
      m_packages(std::move(packages)) {
    setObjectName(QStringLiteral("finepaper.newDesignDialog"));
    setWindowTitle(QStringLiteral("New NoC Design"));
    setModal(true);
    setSizeGripEnabled(true);
    setMinimumSize(480, 380);
    resize(680, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        ui::UiMetrics::spacing24,
        ui::UiMetrics::spacing24,
        ui::UiMetrics::spacing24,
        ui::UiMetrics::spacing16);
    root->setSpacing(ui::UiMetrics::spacing16);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("finepaper.newDesignScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QScrollArea::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAccessibleName(QStringLiteral("New design settings"));

    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, ui::UiMetrics::spacing8, 0);
    contentLayout->setSpacing(ui::UiMetrics::spacing16);

    auto* heading = new QLabel(QStringLiteral("Create a NoC design"), content);
    heading->setObjectName(QStringLiteral("finepaper.newDesignHeading"));
    heading->setProperty("finepaperRole", QStringLiteral("title"));
    heading->setFont(ui::fontForRole(ui::UiFontRole::Title, heading->font()));
    contentLayout->addWidget(heading);

    auto* introduction = new QLabel(
        QStringLiteral(
            "Choose the NoC IP Package that defines the available topology, "
            "Endpoint, Domain, and configuration capabilities. The design "
            "remains bound to the selected Package version."),
        content);
    introduction->setObjectName(
        QStringLiteral("finepaper.newDesignIntroduction"));
    introduction->setTextFormat(Qt::PlainText);
    introduction->setWordWrap(true);
    introduction->setProperty("finepaperRole", QStringLiteral("muted"));
    introduction->setTextInteractionFlags(
        Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    contentLayout->addWidget(introduction);

    auto* packageGroup = new QGroupBox(
        QStringLiteral("NoC IP Package"), content);
    packageGroup->setObjectName(QStringLiteral("finepaper.newDesignPackageGroup"));
    auto* packageLayout = new QVBoxLayout(packageGroup);
    packageLayout->setSpacing(ui::UiMetrics::spacing12);
    m_packageSelector = new QComboBox(packageGroup);
    m_packageSelector->setObjectName(
        QStringLiteral("finepaper.newDesignPackageSelector"));
    m_packageSelector->setAccessibleName(QStringLiteral("NoC IP Package"));
    m_packageSelector->setAccessibleDescription(
        QStringLiteral(
            "Selects the exact Package version and the capabilities used by "
            "the new design."));
    for (const DesignCreationPackageOption& package : m_packages) {
        m_packageSelector->addItem(
            QStringLiteral("%1 — %2 (%3)")
                .arg(package.name,
                     package.reference.version,
                     package.reference.id),
            package.key());
    }
    packageLayout->addWidget(m_packageSelector);

    m_packageDetails = new QLabel(packageGroup);
    m_packageDetails->setObjectName(
        QStringLiteral("finepaper.newDesignPackageDetails"));
    m_packageDetails->setTextFormat(Qt::PlainText);
    m_packageDetails->setWordWrap(true);
    m_packageDetails->setProperty("finepaperRole", QStringLiteral("muted"));
    m_packageDetails->setTextInteractionFlags(
        Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_packageDetails->setAccessibleName(
        QStringLiteral("Selected Package capabilities"));
    packageLayout->addWidget(m_packageDetails);
    contentLayout->addWidget(packageGroup);

    auto* designGroup = new QGroupBox(QStringLiteral("Design"), content);
    designGroup->setObjectName(QStringLiteral("finepaper.newDesignDesignGroup"));
    auto* designForm = new QFormLayout(designGroup);
    designForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    designForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    designForm->setHorizontalSpacing(ui::UiMetrics::spacing16);
    designForm->setVerticalSpacing(ui::UiMetrics::spacing12);

    m_designName = new QLineEdit(designGroup);
    m_designName->setObjectName(QStringLiteral("finepaper.newDesignName"));
    m_designName->setAccessibleName(QStringLiteral("Design name"));
    m_designName->setAccessibleDescription(
        QStringLiteral("A readable name for the new NoC design."));
    m_designName->setClearButtonEnabled(false);
    m_designName->setText(
        suggestedName.trimmed().isEmpty()
            ? QStringLiteral("my_noc") : suggestedName);
    designForm->addRow(
        formLabel(QStringLiteral("Design &name"), m_designName, designGroup),
        m_designName);
    contentLayout->addWidget(designGroup);

    auto* topologyGroup = new QGroupBox(
        QStringLiteral("Initial topology"), content);
    topologyGroup->setObjectName(
        QStringLiteral("finepaper.newDesignTopologyGroup"));
    auto* topologyForm = new QFormLayout(topologyGroup);
    topologyForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    topologyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    topologyForm->setHorizontalSpacing(ui::UiMetrics::spacing16);
    topologyForm->setVerticalSpacing(ui::UiMetrics::spacing12);

    m_topologyType = new QLabel(topologyGroup);
    m_topologyType->setObjectName(
        QStringLiteral("finepaper.newDesignTopologyType"));
    m_topologyType->setTextFormat(Qt::PlainText);
    m_topologyType->setTextInteractionFlags(
        Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    m_topologyType->setAccessibleName(QStringLiteral("Topology type"));
    topologyForm->addRow(QStringLiteral("Type"), m_topologyType);

    m_rows = new QSpinBox(topologyGroup);
    m_rows->setObjectName(QStringLiteral("finepaper.newDesignRows"));
    m_rows->setAccessibleName(QStringLiteral("Mesh rows"));
    m_rows->setSuffix(QStringLiteral(" rows"));
    topologyForm->addRow(
        formLabel(QStringLiteral("&Rows"), m_rows, topologyGroup), m_rows);

    m_columns = new QSpinBox(topologyGroup);
    m_columns->setObjectName(QStringLiteral("finepaper.newDesignColumns"));
    m_columns->setAccessibleName(QStringLiteral("Mesh columns"));
    m_columns->setSuffix(QStringLiteral(" columns"));
    topologyForm->addRow(
        formLabel(QStringLiteral("&Columns"), m_columns, topologyGroup),
        m_columns);
    contentLayout->addWidget(topologyGroup);

    m_validation = new QLabel(content);
    m_validation->setObjectName(
        QStringLiteral("finepaper.newDesignValidation"));
    m_validation->setTextFormat(Qt::PlainText);
    m_validation->setWordWrap(true);
    m_validation->setProperty("finepaperRole", QStringLiteral("error"));
    m_validation->setAccessibleName(
        QStringLiteral("Design creation validation"));
    m_validation->setTextInteractionFlags(
        Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    contentLayout->addWidget(m_validation);
    contentLayout->addStretch(1);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("finepaper.newDesignButtons"));
    m_createButton = buttons->button(QDialogButtonBox::Ok);
    m_createButton->setObjectName(QStringLiteral("finepaper.newDesignCreate"));
    m_createButton->setText(QStringLiteral("Create Design"));
    m_createButton->setProperty("finepaperRole", QStringLiteral("primary"));
    m_createButton->setAccessibleName(QStringLiteral("Create Design"));
    m_createButton->setDefault(true);
    root->addWidget(buttons);

    const int preferredIndex =
        m_packageSelector->findData(preferredPackageKey);
    if (preferredIndex >= 0) {
        m_packageSelector->setCurrentIndex(preferredIndex);
    }

    connect(buttons, &QDialogButtonBox::accepted,
            this, &NewDesignDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(m_packageSelector, &QComboBox::currentIndexChanged,
            this, [this] { updatePackageSelection(); });
    connect(m_designName, &QLineEdit::textChanged,
            this, [this] { updateAcceptState(); });
    connect(m_rows, &QSpinBox::valueChanged,
            this, [this] { saveCurrentTopologyDraft(); });
    connect(m_columns, &QSpinBox::valueChanged,
            this, [this] { saveCurrentTopologyDraft(); });

    QWidget::setTabOrder(m_packageSelector, m_designName);
    QWidget::setTabOrder(m_designName, m_rows);
    QWidget::setTabOrder(m_rows, m_columns);
    QWidget::setTabOrder(m_columns, m_createButton);
    if (QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAccessibleName(QStringLiteral("Cancel design creation"));
        QWidget::setTabOrder(m_createButton, cancel);
    }

    updatePackageSelection();
    m_packageSelector->setFocus(Qt::OtherFocusReason);
}

DesignCreationRequest NewDesignDialog::draft() const {
    PackageReference reference;
    TopologySpec topology;
    if (const DesignCreationPackageOption* package = selectedPackage()) {
        reference = package->reference;
        topology = package->defaultTopology;
        topology.rows = m_rows->value();
        topology.columns = m_columns->value();
    }
    return DesignCreationRequest{
        m_designName->text().trimmed(),
        std::move(reference),
        std::move(topology)};
}

QString NewDesignDialog::selectedPackageKey() const {
    const DesignCreationPackageOption* package = selectedPackage();
    return package ? package->key() : QString();
}

void NewDesignDialog::accept() {
    updateAcceptState();
    if (!m_createButton->isEnabled()) {
        if (!selectedPackage()) {
            m_packageSelector->setFocus(Qt::OtherFocusReason);
        } else {
            m_designName->setFocus(Qt::OtherFocusReason);
        }
        return;
    }
    QDialog::accept();
}

const DesignCreationPackageOption* NewDesignDialog::selectedPackage() const {
    const int index = m_packageSelector->currentIndex();
    return index >= 0 && index < m_packages.size()
        ? &m_packages.at(index) : nullptr;
}

void NewDesignDialog::saveCurrentTopologyDraft() {
    if (m_updatingPackage || m_currentPackageKey.isEmpty()) {
        return;
    }
    m_topologyDrafts.insert(
        m_currentPackageKey,
        TopologyDraft{m_rows->value(), m_columns->value()});
}

void NewDesignDialog::updatePackageSelection() {
    saveCurrentTopologyDraft();
    const DesignCreationPackageOption* package = selectedPackage();
    m_updatingPackage = true;
    m_currentPackageKey = package ? package->key() : QString();

    if (!package) {
        m_packageSelector->setEnabled(false);
        m_topologyType->setText(QStringLiteral("Not available"));
        m_rows->setRange(1, 1);
        m_columns->setRange(1, 1);
        m_rows->setValue(1);
        m_columns->setValue(1);
        m_rows->setEnabled(false);
        m_columns->setEnabled(false);
        m_packageDetails->setText(
            QStringLiteral("No valid runtime NoC IP Package is available."));
        m_packageDetails->setAccessibleDescription(m_packageDetails->text());
        m_updatingPackage = false;
        updateAcceptState();
        return;
    }

    m_packageSelector->setEnabled(true);
    const int minimumRows = (std::max)(1, package->minimumRows);
    const int maximumRows = (std::max)(minimumRows, package->maximumRows);
    const int minimumColumns = (std::max)(1, package->minimumColumns);
    const int maximumColumns =
        (std::max)(minimumColumns, package->maximumColumns);
    m_rows->setRange(minimumRows, maximumRows);
    m_columns->setRange(minimumColumns, maximumColumns);
    m_rows->setEnabled(true);
    m_columns->setEnabled(true);

    const TopologyDraft topology = m_topologyDrafts.value(
        package->key(),
        TopologyDraft{package->defaultTopology.rows,
                      package->defaultTopology.columns});
    m_rows->setValue((std::clamp)(topology.rows, minimumRows, maximumRows));
    m_columns->setValue(
        (std::clamp)(topology.columns, minimumColumns, maximumColumns));
    m_topologyType->setText(
        topologyDisplayText(package->defaultTopology.type));

    const QString details = QStringList{
        package->name,
        package->key(),
        QStringLiteral("Topology range: %1–%2 rows × %3–%4 columns")
            .arg(minimumRows)
            .arg(maximumRows)
            .arg(minimumColumns)
            .arg(maximumColumns),
        QStringLiteral("Endpoint types: %1")
            .arg(capabilityListText(
                package->endpointTypes, package->endpointTypeCount)),
        QStringLiteral("Domain types: %1")
            .arg(capabilityListText(
                package->domainTypes, package->domainTypeCount)),
        QStringLiteral("Element property sets: %1 · Design extensions: %2")
            .arg(package->elementPropertySetCount)
            .arg(package->designExtensionCount)
    }.join(QLatin1Char('\n'));
    m_packageDetails->setText(details);
    m_packageDetails->setAccessibleDescription(details);

    m_updatingPackage = false;
    saveCurrentTopologyDraft();
    updateAcceptState();
}

void NewDesignDialog::updateAcceptState() {
    QString error;
    if (!selectedPackage()) {
        error = QStringLiteral(
            "Install or reload a valid runtime NoC IP Package before creating a design.");
    } else if (m_designName->text().trimmed().isEmpty()) {
        error = QStringLiteral("Enter a design name to continue.");
    }

    const bool valid = error.isEmpty();
    m_createButton->setEnabled(valid);
    m_createButton->setToolTip(valid ? QString() : error);
    m_createButton->setAccessibleDescription(
        valid ? QStringLiteral("Creates the design with the selected Package and topology.")
              : error);
    m_validation->setText(error);
    m_validation->setVisible(!valid);
}

} // namespace finepaper
