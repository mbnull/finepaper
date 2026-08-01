#include "features/domain/domain_manager_panel.h"
#include "features/domain/domain_instance_dialog.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

PackageDefinition genericPackage() {
    DomainTypeDefinition zones;
    zones.id = QStringLiteral("security-zone");
    zones.label = QStringLiteral("Security zones");
    zones.appliesTo = {ElementKind::Router, ElementKind::Endpoint};
    zones.cardinality = DomainCardinality::Multiple;
    DomainPropertyDefinition classification;
    classification.id = QStringLiteral("classification");
    classification.label = QStringLiteral("Classification");
    classification.type = ParameterType::String;
    classification.hasDefault = true;
    classification.defaultValue = QStringLiteral("standard");
    zones.properties = {classification};

    DomainTypeDefinition tiers;
    tiers.id = QStringLiteral("fabric-tier");
    tiers.label = QStringLiteral("Fabric tier");
    tiers.appliesTo = {ElementKind::Router};
    tiers.cardinality = DomainCardinality::Single;
    tiers.required = true;

    PackageDefinition package;
    package.format = QStringLiteral("finepaper.noc-package");
    package.formatVersion = 2;
    package.id = QStringLiteral("test.domain-manager-panel");
    package.name = QStringLiteral("Generic Domain Manager Panel");
    package.version = QStringLiteral("1.0.0");
    package.domainTypes = {zones, tiers};
    return package;
}

NocDesign genericDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("domain-manager-panel");
    design.name = QStringLiteral("Domain Manager Panel");
    design.package = PackageReference{
        QStringLiteral("test.domain-manager-panel"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.endpoints = {
        EndpointInstance{
            QStringLiteral("ep-a"),
            QStringLiteral("client"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            {}
        }
    };
    design.domains = {
        DomainDefinition{
            QStringLiteral("zone-c"),
            QStringLiteral("security-zone"),
            QStringLiteral("Restricted"),
            {}
        },
        DomainDefinition{
            QStringLiteral("zone-a"),
            QStringLiteral("security-zone"),
            QStringLiteral("Trusted"),
            {}
        },
        DomainDefinition{
            QStringLiteral("zone-b"),
            QStringLiteral("security-zone"),
            QStringLiteral("Shared"),
            {}
        },
        DomainDefinition{
            QStringLiteral("tier-edge"),
            QStringLiteral("fabric-tier"),
            QStringLiteral("Edge"),
            {}
        },
        DomainDefinition{
            QStringLiteral("tier-core"),
            QStringLiteral("fabric-tier"),
            QStringLiteral("Core"),
            {}
        }
    };
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{QStringLiteral("zone-a"),
                                QStringLiteral("zone-b")}
                },
                {
                    QStringLiteral("fabric-tier"),
                    QStringList{QStringLiteral("tier-core")}
                }
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{QStringLiteral("zone-a"),
                                QStringLiteral("zone-c")}
                },
                {
                    QStringLiteral("fabric-tier"),
                    QStringList{QStringLiteral("tier-edge")}
                }
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep-a")},
            QHash<QString, QStringList>{
                {
                    QStringLiteral("security-zone"),
                    QStringList{QStringLiteral("zone-a")}
                }
            }
        }
    };
    return design;
}

QListWidgetItem* assignmentItem(QListWidget* list, const QString& domainId) {
    if (!list) {
        return nullptr;
    }
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(domainManagerDomainIdRole).toString() == domainId) {
            return item;
        }
    }
    return nullptr;
}

int instanceRow(QTableWidget* table, const QString& domainId) {
    if (!table) {
        return -1;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* item = table->item(row, 2);
        if (item
            && item->data(domainManagerDomainIdRole).toString() == domainId) {
            return row;
        }
    }
    return -1;
}

struct AssignmentCall {
    QVector<ElementRef> elements;
    QString domainType;
    DomainAssignmentPatch patch;
};

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);

    const PackageDefinition package = genericPackage();
    const NocDesign design = genericDesign();
    const ResolvedDesign resolved = resolveDesign(design);
    const ElementRef router0{ElementKind::Router, QStringLiteral("r-0-0")};
    const ElementRef router1{ElementKind::Router, QStringLiteral("r-1-0")};
    const ElementRef endpoint{ElementKind::Endpoint, QStringLiteral("ep-a")};
    const ElementRef link{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))};

    DomainManagerPanel panel;
    panel.resize(640, 720);
    panel.show();
    panel.setContext(&design, &resolved, &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();

    auto* typeSelector = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.domainManager.typeSelector"));
    auto* instances = panel.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainManager.instanceView"));
    auto* tabs = panel.findChild<QTabWidget*>(
        QStringLiteral("finepaper.domainManager.tabs"));
    auto* instancesPage = panel.findChild<QWidget*>(
        QStringLiteral("finepaper.domainManager.instancesPage"));
    auto* assignmentPage = panel.findChild<QWidget*>(
        QStringLiteral("finepaper.domainManager.assignmentPage"));
    auto* assignmentState = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.domainManager.assignmentState"));
    auto* assignmentFeedback = panel.findChild<QLabel*>(
        QStringLiteral("finepaper.domainManager.assignmentFeedback"));
    auto* singleAssignment = panel.findChild<QComboBox*>(
        QStringLiteral("finepaper.domainManager.assignmentEditor"));
    auto* multipleAssignment = panel.findChild<QListWidget*>(
        QStringLiteral("finepaper.domainManager.assignmentEditor.multiple"));
    auto* applyAssignment = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.applyAssignment"));
    auto* clearAssignment = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.clearAssignment"));
    auto* discardAssignment = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.discardAssignment"));
    auto* completeConfiguration = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.completeConfiguration"));
    auto* addDomain = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.addDomain"));
    auto* editDomain = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.editDomain"));
    auto* deleteDomain = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.deleteDomain"));
    auto* selectMembers = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.selectMembers"));
    auto* selectAllEligible = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.selectAllEligible"));
    auto* selectUnassigned = panel.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.selectUnassigned"));

    check(typeSelector && instances && tabs && instancesPage
              && assignmentPage && assignmentState
              && assignmentFeedback && singleAssignment
              && multipleAssignment && applyAssignment && clearAssignment
              && discardAssignment
              && completeConfiguration && addDomain && editDomain && deleteDomain
              && selectMembers && selectAllEligible && selectUnassigned,
          QStringLiteral("Domain Manager exposes its stable test controls"));
    if (!typeSelector || !instances || !tabs || !instancesPage
        || !assignmentPage || !assignmentState
        || !assignmentFeedback || !singleAssignment
        || !multipleAssignment || !applyAssignment || !clearAssignment
        || !discardAssignment
        || !completeConfiguration || !addDomain || !editDomain || !deleteDomain
        || !selectMembers || !selectAllEligible || !selectUnassigned) {
        return 1;
    }

    PackageDefinition v3Package = package;
    v3Package.formatVersion = 3;
    NocDesign v3Design = design;
    v3Design.formatVersion = 3;
    const ResolvedDesign v3Resolved = resolveDesign(v3Design);
    panel.setContext(&v3Design, &v3Resolved, &v3Package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();
    check(completeConfiguration->isEnabled() && addDomain->isEnabled(),
          QStringLiteral("Package and Design V3 retain complete Domain editing capabilities"));
    panel.setContext(&design, &resolved, &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();

    int completeConfigurationRequests = 0;
    panel.completeConfigurationRequested = [&completeConfigurationRequests] {
        ++completeConfigurationRequests;
    };
    check(completeConfiguration->isEnabled(),
          QStringLiteral("Package V2 enables the complete Domain configuration entry"));
    completeConfiguration->click();
    QApplication::processEvents();
    check(completeConfigurationRequests == 1,
          QStringLiteral("the complete Domain configuration entry emits its callback"));

    check(typeSelector->count() == 2
              && typeSelector->itemData(0).toString()
                  == QStringLiteral("security-zone")
              && typeSelector->itemText(0)
                  == QStringLiteral("Security zones (security-zone)")
              && typeSelector->itemData(1).toString()
                  == QStringLiteral("fabric-tier")
              && panel.currentDomainType() == QStringLiteral("security-zone"),
          QStringLiteral("Package V2 Domain types drive the selector without fixed names"));

    check(!panel.canActivateAssignmentPage(),
          QStringLiteral(
              "Domain assignment navigation stays unavailable without a semantic selection"));
    typeSelector->setCurrentIndex(
        typeSelector->findData(QStringLiteral("fabric-tier")));
    panel.setSelection({endpoint});
    QApplication::processEvents();
    check(assignmentState->property("assignmentState").toString()
                  == QStringLiteral("no-eligible")
              && panel.canActivateAssignmentPage(),
          QStringLiteral(
              "an Endpoint remains assignable when another Package Domain type supports it"));
    tabs->setCurrentWidget(instancesPage);
    panel.activateAssignmentPage();
    QApplication::processEvents();
    check(tabs->currentWidget() == assignmentPage
              && panel.currentDomainType() == QStringLiteral("security-zone")
              && multipleAssignment->isVisible(),
          QStringLiteral(
              "assignment navigation selects the first compatible Package Domain type"));

    panel.setSelection({link});
    tabs->setCurrentWidget(instancesPage);
    QApplication::processEvents();
    check(!panel.canActivateAssignmentPage(),
          QStringLiteral(
              "derived Router Links do not expose a dead-end Domain assignment route"));
    panel.activateAssignmentPage();
    check(tabs->currentWidget() == instancesPage,
          QStringLiteral(
              "an ineligible selection cannot force the assignment page open"));
    panel.setSelection({});
    typeSelector->setCurrentIndex(
        typeSelector->findData(QStringLiteral("security-zone")));
    QApplication::processEvents();

    check(instances->rowCount() == 3
              && instances->item(0, 2)->text() == QStringLiteral("zone-a")
              && instances->item(1, 2)->text() == QStringLiteral("zone-b")
              && instances->item(2, 2)->text() == QStringLiteral("zone-c"),
          QStringLiteral("the instance table is a deterministic textual Legend"));

    const int zoneARow = instanceRow(instances, QStringLiteral("zone-a"));
    const int zoneBRow = instanceRow(instances, QStringLiteral("zone-b"));
    const int zoneCRow = instanceRow(instances, QStringLiteral("zone-c"));
    check(zoneARow >= 0 && zoneBRow >= 0 && zoneCRow >= 0,
          QStringLiteral("every generic Domain instance appears in the Legend"));
    if (zoneARow >= 0 && zoneBRow >= 0 && zoneCRow >= 0) {
        check(instances->item(zoneARow, 1)->text() == QStringLiteral("Trusted")
                  && instances->item(zoneARow, 3)
                         ->data(domainManagerMemberCountRole).toLongLong() == 3
                  && instances->item(zoneBRow, 3)
                         ->data(domainManagerMemberCountRole).toLongLong() == 1
                  && instances->item(zoneCRow, 3)
                         ->data(domainManagerMemberCountRole).toLongLong() == 1,
              QStringLiteral("Legend rows show names and resolved member counts"));
        check(instances->item(zoneARow, 0)
                      ->data(domainManagerColorRole).value<QColor>().isValid()
                  && instances->item(zoneARow, 0)->background().style()
                         == domainPresentationPattern(QStringLiteral("zone-a"))
                  && !instances->item(zoneARow, 0)
                          ->data(Qt::AccessibleTextRole).toString().isEmpty()
                  && instances->item(zoneBRow, 4)
                         ->data(domainManagerCrossingCountRole).toLongLong() > 0
                  && instances->item(zoneCRow, 4)
                         ->data(domainManagerCrossingCountRole).toLongLong() > 0,
              QStringLiteral(
                  "Legend rows expose deterministic color/pattern markers and crossing counts"));
    }

    NocDesign markupDesign = design;
    auto markupDomain = std::find_if(
        markupDesign.domains.begin(), markupDesign.domains.end(),
        [](const DomainDefinition& domain) {
            return domain.id == QStringLiteral("zone-a");
        });
    if (markupDomain != markupDesign.domains.end()) {
        markupDomain->name = QStringLiteral("<b>Trusted</b>");
    }
    const ResolvedDesign markupResolved = resolveDesign(markupDesign);
    panel.setContext(
        &markupDesign, &markupResolved, &package,
        QStringLiteral("security-zone"));
    QApplication::processEvents();
    const int markupRow = instanceRow(instances, QStringLiteral("zone-a"));
    const QString markupTooltip = markupRow >= 0
        ? instances->item(markupRow, 0)->toolTip() : QString{};
    check(markupRow >= 0
              && markupTooltip.contains(QStringLiteral("&lt;b&gt;Trusted&lt;/b&gt;"))
              && !markupTooltip.contains(QStringLiteral("<b>Trusted</b>")),
          QStringLiteral(
              "Domain marker tooltips preserve configurable text without interpreting it as markup"));
    panel.setContext(&design, &resolved, &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();

    QVector<QVector<ElementRef>> requestedSelections;
    panel.selectElementsRequested = [&requestedSelections](
        QVector<ElementRef> elements) {
        requestedSelections.append(std::move(elements));
    };
    instances->selectRow(zoneARow);
    selectMembers->click();
    selectAllEligible->click();
    QApplication::processEvents();
    check(requestedSelections.size() == 2
              && requestedSelections.at(0)
                  == QVector<ElementRef>{router0, router1, endpoint}
              && requestedSelections.at(1)
                  == QVector<ElementRef>{router0, router1, endpoint},
          QStringLiteral("Domain Manager selects a Domain's members or all applicable elements"));

    NocDesign designWithUnassignedEndpoint = design;
    designWithUnassignedEndpoint.endpoints.append(EndpointInstance{
        QStringLiteral("ep-unassigned"),
        QStringLiteral("client"),
        EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
        {}});
    const ResolvedDesign resolvedWithUnassignedEndpoint =
        resolveDesign(designWithUnassignedEndpoint);
    panel.setContext(&designWithUnassignedEndpoint,
                     &resolvedWithUnassignedEndpoint,
                     &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();
    check(selectUnassigned->isEnabled()
              && selectUnassigned->text().contains(QStringLiteral("(1)")),
          QStringLiteral("unassigned selection helper previews its exact target count"));
    selectUnassigned->click();
    QApplication::processEvents();
    check(requestedSelections.size() == 3
              && requestedSelections.constLast()
                  == QVector<ElementRef>{ElementRef{
                      ElementKind::Endpoint,
                      QStringLiteral("ep-unassigned")}},
          QStringLiteral("unassigned helper targets only applicable elements without an assignment"));
    panel.setContext(&design, &resolved, &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();

    std::optional<DomainDefinition> addedDomain = std::nullopt;
    panel.addDomainRequested = [&addedDomain](DomainDefinition domain) {
        addedDomain = std::move(domain);
    };
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog
            || dialog->objectName()
                != QStringLiteral("finepaper.domainInstanceDialog")) {
            return;
        }
        if (auto* id = dialog->findChild<QLineEdit*>(
                QStringLiteral("finepaper.domainInstance.id"))) {
            id->setText(QStringLiteral("zone-d"));
        }
        if (auto* name = dialog->findChild<QLineEdit*>(
                QStringLiteral("finepaper.domainInstance.name"))) {
            name->setText(QStringLiteral("Partner"));
        }
        dialog->accept();
    });
    addDomain->click();
    check(addedDomain
              && addedDomain->id == QStringLiteral("zone-d")
              && addedDomain->type == QStringLiteral("security-zone")
              && addedDomain->name == QStringLiteral("Partner")
              && addedDomain->properties.value(
                     QStringLiteral("classification")).toString()
                  == QStringLiteral("standard"),
          QStringLiteral("Add creates the current arbitrary Type with schema defaults"));

    std::optional<std::pair<QString, DomainDefinition>> updatedDomain;
    panel.updateDomainRequested = [&updatedDomain](
        QString originalId,
        DomainDefinition domain) {
        updatedDomain = std::pair<QString, DomainDefinition>{
            std::move(originalId), std::move(domain)};
    };
    instances->selectRow(zoneARow);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog
            || dialog->objectName()
                != QStringLiteral("finepaper.domainInstanceDialog")) {
            return;
        }
        if (auto* name = dialog->findChild<QLineEdit*>(
                QStringLiteral("finepaper.domainInstance.name"))) {
            name->setText(QStringLiteral("Trusted updated"));
        }
        dialog->accept();
    });
    editDomain->click();
    check(updatedDomain
              && updatedDomain->first == QStringLiteral("zone-a")
              && updatedDomain->second.id == QStringLiteral("zone-a")
              && updatedDomain->second.type
                  == QStringLiteral("security-zone")
              && updatedDomain->second.name
                  == QStringLiteral("Trusted updated"),
          QStringLiteral("Edit preserves stable Domain identity and returns schema data"));

    QString removedDomainId;
    panel.removeDomainRequested = [&removedDomainId](QString domainId) {
        removedDomainId = std::move(domainId);
    };
    QTimer::singleShot(0, [] {
        auto* message = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        if (message
            && message->objectName()
                == QStringLiteral(
                    "finepaper.domainManager.deleteConfirmation")) {
            message->done(QMessageBox::Yes);
        }
    });
    deleteDomain->click();
    check(removedDomainId == QStringLiteral("zone-a"),
          QStringLiteral("Delete requests removal of the selected Domain, never a Router"));

    QVector<AssignmentCall> calls;
    panel.assignmentPatchRequested = [&calls](QVector<ElementRef> elements,
                                               QString domainType,
                                               DomainAssignmentPatch patch) {
        calls.append(AssignmentCall{
            std::move(elements), std::move(domainType), std::move(patch)});
    };

    tabs->setCurrentIndex(1);
    panel.setSelection({router0, router1});
    QApplication::processEvents();
    check(assignmentState->property("assignmentState").toString()
                  == QStringLiteral("mixed")
              && multipleAssignment->isVisible()
              && !singleAssignment->isVisible(),
          QStringLiteral("different multiple-cardinality sets appear as Mixed"));

    const qsizetype callsBeforeClear = calls.size();
    clearAssignment->click();
    QApplication::processEvents();
    check(calls.size() == callsBeforeClear
              && applyAssignment->isEnabled()
              && discardAssignment->isEnabled()
              && !clearAssignment->isEnabled()
              && !multipleAssignment->isEnabled()
              && assignmentState->text().contains(QStringLiteral("Pending: clear")),
          QStringLiteral("Clear stages one explicit draft instead of mutating immediately"));
    applyAssignment->click();
    QApplication::processEvents();
    check(calls.size() == callsBeforeClear + 1
              && calls.constLast().elements
                  == QVector<ElementRef>{router0, router1}
              && calls.constLast().domainType
                  == QStringLiteral("security-zone")
              && calls.constLast().patch.replacement
                  == std::optional<QStringList>{QStringList{}},
          QStringLiteral("applying a staged Clear emits one atomic empty replacement"));
    discardAssignment->click();
    QApplication::processEvents();

    QListWidgetItem* zoneA = assignmentItem(
        multipleAssignment, QStringLiteral("zone-a"));
    QListWidgetItem* zoneB = assignmentItem(
        multipleAssignment, QStringLiteral("zone-b"));
    QListWidgetItem* zoneC = assignmentItem(
        multipleAssignment, QStringLiteral("zone-c"));
    check(zoneA && zoneB && zoneC
              && zoneA->checkState() == Qt::Checked
              && zoneB->checkState() == Qt::PartiallyChecked
              && zoneC->checkState() == Qt::PartiallyChecked,
          QStringLiteral("multiple Mixed state uses All and Some check states"));

    if (zoneB) {
        zoneB->setCheckState(Qt::Checked);
        QApplication::processEvents();
        check(applyAssignment->isEnabled(),
              QStringLiteral("resolving a partial value enables Apply"));
        zoneB->setCheckState(Qt::PartiallyChecked);
        QApplication::processEvents();
        check(!applyAssignment->isEnabled(),
              QStringLiteral("returning to the original partial state removes the pending edit"));
        const qsizetype callsBeforeNoOp = calls.size();
        applyAssignment->click();
        QApplication::processEvents();
        check(calls.size() == callsBeforeNoOp,
              QStringLiteral("an untouched partial state cannot emit an empty patch"));

        zoneB->setCheckState(Qt::Checked);
        applyAssignment->click();
        QApplication::processEvents();
        check(calls.size() == callsBeforeNoOp + 1
                  && calls.constLast().elements
                      == QVector<ElementRef>{router0, router1}
                  && calls.constLast().domainType
                      == QStringLiteral("security-zone")
                  && calls.constLast().patch.ensurePresent
                      == QStringList{QStringLiteral("zone-b")}
                  && calls.constLast().patch.ensureAbsent.isEmpty()
                  && !calls.constLast().patch.replacement,
              QStringLiteral("multiple edits emit one atomic ensure-present patch"));
    }
    discardAssignment->click();
    QApplication::processEvents();

    panel.setSelection({router0, router1});
    zoneC = assignmentItem(multipleAssignment, QStringLiteral("zone-c"));
    if (zoneC) {
        zoneC->setCheckState(Qt::Checked);
    }
    QApplication::processEvents();
    check(!completeConfiguration->isEnabled() && !clearAssignment->isEnabled()
              && !selectMembers->isEnabled()
              && !selectAllEligible->isEnabled()
              && !selectUnassigned->isEnabled(),
          QStringLiteral("pending assignment changes freeze competing configuration and selection operations"));
    completeConfiguration->click();
    QApplication::processEvents();
    check(completeConfigurationRequests == 1,
          QStringLiteral("a disabled complete configuration entry cannot emit its callback"));
    panel.setSelection({endpoint});
    QApplication::processEvents();
    check(assignmentState->text().contains(QStringLiteral("original 2 eligible"))
              && discardAssignment->isEnabled(),
          QStringLiteral("a selection change preserves the pending assignment target"));
    discardAssignment->click();
    QApplication::processEvents();
    check(!discardAssignment->isEnabled()
              && assignmentState->property("assignmentState").toString()
                  == QStringLiteral("common")
              && assignmentState->text().contains(QStringLiteral("1 of 1")),
          QStringLiteral("Discard adopts the latest canvas selection and clears pending state"));
    check(completeConfiguration->isEnabled(),
          QStringLiteral("Discard restores the complete Domain configuration entry"));
    completeConfiguration->click();
    QApplication::processEvents();
    check(completeConfigurationRequests == 2,
          QStringLiteral("the restored complete configuration entry emits its callback"));

    PackageDefinition emptyDomainPackage = package;
    emptyDomainPackage.domainTypes.clear();
    NocDesign staleDomainDesign = design;
    panel.setContext(&staleDomainDesign, &resolved, &emptyDomainPackage, {});
    QApplication::processEvents();
    check(completeConfiguration->isEnabled(),
          QStringLiteral("a Package with no Domain types still permits opening the complete editor to remove stale rows"));
    completeConfiguration->click();
    QApplication::processEvents();
    check(completeConfigurationRequests == 3,
          QStringLiteral("the repair entry remains available for stale Domain data"));

    panel.setContext(&design, &resolved, &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();

    panel.setSelection({router0, router1});
    zoneC = assignmentItem(multipleAssignment, QStringLiteral("zone-c"));
    if (zoneC) {
        zoneC->setCheckState(Qt::Checked);
    }
    panel.setSelection({endpoint});
    const qsizetype callsBeforePendingApply = calls.size();
    applyAssignment->click();
    QApplication::processEvents();
    check(calls.size() == callsBeforePendingApply + 1
              && calls.constLast().elements
                  == QVector<ElementRef>{router0, router1}
              && calls.constLast().patch.ensurePresent
                  == QStringList{QStringLiteral("zone-c")},
          QStringLiteral("Apply after a selection change still targets the original selection"));
    discardAssignment->click();
    QApplication::processEvents();

    panel.setSelection({router0, router1});
    const int tierIndex = typeSelector->findData(QStringLiteral("fabric-tier"));
    typeSelector->setCurrentIndex(tierIndex);
    QApplication::processEvents();
    check(panel.currentDomainType() == QStringLiteral("fabric-tier")
              && assignmentState->property("assignmentState").toString()
                  == QStringLiteral("mixed")
              && singleAssignment->isVisible()
              && !multipleAssignment->isVisible()
              && !singleAssignment->currentData().isValid()
              && !applyAssignment->isEnabled(),
          QStringLiteral("single-cardinality Mixed state starts with a non-applicable placeholder"));
    const int coreIndex = singleAssignment->findData(QStringLiteral("tier-core"));
    singleAssignment->setCurrentIndex(coreIndex);
    QApplication::processEvents();
    const qsizetype callsBeforeSingle = calls.size();
    applyAssignment->click();
    QApplication::processEvents();
    check(calls.size() == callsBeforeSingle + 1
              && calls.constLast().domainType == QStringLiteral("fabric-tier")
              && calls.constLast().elements
                  == QVector<ElementRef>{router0, router1}
              && calls.constLast().patch.replacement
                  == std::optional<QStringList>{
                      QStringList{QStringLiteral("tier-core")}}
              && calls.constLast().patch.ensurePresent.isEmpty()
              && calls.constLast().patch.ensureAbsent.isEmpty(),
          QStringLiteral("single Mixed selection emits an exact replacement patch"));
    discardAssignment->click();
    QApplication::processEvents();

    PackageDefinition asymmetricPackage = package;
    auto asymmetricType = std::find_if(
        asymmetricPackage.domainTypes.begin(),
        asymmetricPackage.domainTypes.end(),
        [](const DomainTypeDefinition& type) {
            return type.id == QStringLiteral("security-zone");
        });
    if (asymmetricType != asymmetricPackage.domainTypes.end()) {
        // Canonical Router/Endpoint rules intentionally conflict with the
        // compatibility fields so the panel cannot accidentally use them.
        asymmetricType->cardinality = DomainCardinality::Single;
        asymmetricType->required = true;
        asymmetricType->assignmentRules = {
            DomainAssignmentRule{
                ElementKind::Router, 1, qsizetype{1}},
            DomainAssignmentRule{
                ElementKind::Endpoint, 0, qsizetype{2}}};
    }
    DomainInstanceDialog asymmetricInstanceDialog(
        asymmetricType == asymmetricPackage.domainTypes.end()
            ? QVector<DomainTypeDefinition>{}
            : QVector<DomainTypeDefinition>{*asymmetricType},
        design.domains,
        std::nullopt,
        QStringLiteral("security-zone"),
        {});
    auto* asymmetricDescription = asymmetricInstanceDialog.findChild<QLabel*>(
        QStringLiteral("finepaper.domainInstance.typeDescription"));
    check(asymmetricDescription
              && asymmetricDescription->text().contains(
                  QStringLiteral("Security zones (security-zone)"))
              && asymmetricDescription->text().contains(
                  QStringLiteral("Router assignments: minimum 1, maximum 1"))
              && asymmetricDescription->text().contains(
                  QStringLiteral("Endpoint assignments: minimum 0, maximum 2")),
          QStringLiteral("Domain instance summary uses stable type identity and every canonical per-kind limit"));
    panel.setContext(&design, &resolved, &asymmetricPackage,
                     QStringLiteral("security-zone"));
    typeSelector->setCurrentIndex(
        typeSelector->findData(QStringLiteral("security-zone")));
    panel.setSelection({router0});
    QApplication::processEvents();
    check(singleAssignment->isVisible() && !multipleAssignment->isVisible()
              && !clearAssignment->isEnabled()
              && assignmentState->text().contains(
                  QStringLiteral("Router assignments: minimum 1, maximum 1")),
          QStringLiteral("Router assignment controls follow the canonical Router rule and show its limits"));
    panel.setSelection({endpoint});
    QApplication::processEvents();
    check(!singleAssignment->isVisible() && multipleAssignment->isVisible()
              && clearAssignment->isEnabled()
              && assignmentState->text().contains(
                  QStringLiteral("Endpoint assignments: minimum 0, maximum 2")),
          QStringLiteral("Endpoint assignment controls follow the independent canonical Endpoint rule"));
    panel.setSelection({router0, endpoint});
    QApplication::processEvents();
    check(!singleAssignment->isVisible() && multipleAssignment->isVisible()
              && !clearAssignment->isEnabled()
              && assignmentState->text().contains(
                  QStringLiteral("Router assignments: minimum 1, maximum 1"))
              && assignmentState->text().contains(
                  QStringLiteral("Endpoint assignments: minimum 0, maximum 2")),
          QStringLiteral("mixed-kind selection keeps both per-kind limits and blocks an invalid bulk clear"));
    zoneC = assignmentItem(multipleAssignment, QStringLiteral("zone-c"));
    zoneB = assignmentItem(multipleAssignment, QStringLiteral("zone-b"));
    if (zoneC) {
        zoneC->setCheckState(Qt::Checked);
        QApplication::processEvents();
    }
    check(zoneC && !applyAssignment->isEnabled()
              && assignmentFeedback->isVisible()
              && assignmentFeedback->text().contains(
                  QStringLiteral(
                      "Router r-0-0 would have 3 Domain assignments; maximum is 1"))
              && applyAssignment->accessibleDescription()
                  == assignmentFeedback->text(),
          QStringLiteral("Apply stays disabled when ensure-present would exceed the Router maximum in a mixed-kind selection"));
    if (zoneC) {
        zoneC->setCheckState(Qt::Unchecked);
    }
    if (zoneB) {
        zoneB->setCheckState(Qt::Unchecked);
    }
    QApplication::processEvents();
    check(zoneB && applyAssignment->isEnabled()
              && !assignmentFeedback->isVisible()
              && applyAssignment->accessibleDescription().isEmpty(),
          QStringLiteral("Apply enables when the same delta leaves every mixed-kind target within its own limits"));
    discardAssignment->click();
    QApplication::processEvents();
    panel.setContext(&design, &resolved, &package,
                     QStringLiteral("security-zone"));
    QApplication::processEvents();

    typeSelector->setCurrentIndex(
        typeSelector->findData(QStringLiteral("security-zone")));
    panel.setSelection({link});
    QApplication::processEvents();
    check(assignmentState->property("assignmentState").toString()
                  == QStringLiteral("no-eligible")
              && assignmentState->text().contains(QStringLiteral("No assignable"))
              && !multipleAssignment->isEnabled()
              && !applyAssignment->isEnabled(),
          QStringLiteral("a derived Router Link is visible semantic selection but never assignment-eligible"));

    check(panel.findChild<QPushButton*>(
              QStringLiteral("finepaper.domainManager.addRouter")) == nullptr
              && panel.findChild<QPushButton*>(
                     QStringLiteral("finepaper.domainManager.deleteRouter")) == nullptr,
          QStringLiteral("Domain Manager exposes no Router creation or deletion controls"));
    const QList<QPushButton*> buttons = panel.findChildren<QPushButton*>();
    check(std::none_of(buttons.cbegin(), buttons.cend(), [](const QPushButton* button) {
              return button->text().contains(QStringLiteral("Add Router"),
                                             Qt::CaseInsensitive)
                  || button->text().contains(QStringLiteral("Delete Router"),
                                             Qt::CaseInsensitive);
          }),
          QStringLiteral("the fixed Mesh boundary is not presented as Router CRUD"));

    if (failures == 0) {
        QTextStream(stdout) << "All Domain Manager panel tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
