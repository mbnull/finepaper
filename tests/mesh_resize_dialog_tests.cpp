#include "features/topology/mesh_resize_dialog.h"
#include "features/topology/topology_text.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>

#include <algorithm>
#include <optional>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
}

DomainTypeDefinition domainType(
    const QString& id,
    const QString& label,
    DomainCardinality cardinality,
    bool required,
    QVector<ElementKind> appliesTo = {ElementKind::Router}) {
    DomainTypeDefinition type;
    type.id = id;
    type.label = label;
    type.cardinality = cardinality;
    type.required = required;
    type.appliesTo = std::move(appliesTo);
    return type;
}

DomainTypeDefinition constrainedDomainType(
    const QString& id,
    const QString& label,
    qsizetype minimumAssignments,
    std::optional<qsizetype> maximumAssignments) {
    DomainTypeDefinition type;
    type.id = id;
    type.label = label;
    type.assignmentRules = {DomainAssignmentRule{
        ElementKind::Router,
        minimumAssignments,
        maximumAssignments}};
    return type;
}

PackageDefinition packageFixture() {
    PackageDefinition package;
    package.formatVersion = 2;
    package.id = QStringLiteral("test.mesh-resize-dialog");
    package.version = QStringLiteral("1.0.0");
    package.mesh.minimumRows = 1;
    package.mesh.maximumRows = 3;
    package.mesh.defaultRows = 1;
    package.mesh.minimumColumns = 1;
    package.mesh.maximumColumns = 4;
    package.mesh.defaultColumns = 1;
    package.domainTypes = {
        domainType(QStringLiteral("fabric-zone"),
                   QStringLiteral("Fabric zone"),
                   DomainCardinality::Single,
                   true),
        constrainedDomainType(QStringLiteral("security-label"),
                              QStringLiteral("Security labels"),
                              2,
                              3),
        domainType(QStringLiteral("retention-class"),
                   QStringLiteral("Retention class"),
                   DomainCardinality::Single,
                   true),
        domainType(QStringLiteral("traffic-mark"),
                   QStringLiteral("Traffic marks"),
                   DomainCardinality::Multiple,
                   false),
        domainType(QStringLiteral("endpoint-scope"),
                   QStringLiteral("Endpoint scope"),
                   DomainCardinality::Single,
                   true,
                   {ElementKind::Endpoint})};
    return package;
}

DomainDefinition domain(const QString& id,
                        const QString& type,
                        const QString& name) {
    return DomainDefinition{id, type, name, {}};
}

NocDesign growthDesign() {
    NocDesign design;
    design.formatVersion = 2;
    design.id = QStringLiteral("resize-dialog");
    design.name = QStringLiteral("Resize dialog");
    design.package = PackageReference{
        QStringLiteral("test.mesh-resize-dialog"),
        QStringLiteral("1.0.0")};
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 1};
    design.domains = {
        domain(QStringLiteral("zone-a"),
               QStringLiteral("fabric-zone"),
               QStringLiteral("Zone A")),
        domain(QStringLiteral("zone-b"),
               QStringLiteral("fabric-zone"),
               QStringLiteral("Zone B")),
        domain(QStringLiteral("label-red"),
               QStringLiteral("security-label"),
               QStringLiteral("Red")),
        domain(QStringLiteral("label-blue"),
               QStringLiteral("security-label"),
               QStringLiteral("Blue")),
        domain(QStringLiteral("label-green"),
               QStringLiteral("security-label"),
               QStringLiteral("Green")),
        domain(QStringLiteral("label-yellow"),
               QStringLiteral("security-label"),
               QStringLiteral("Yellow")),
        domain(QStringLiteral("retention-only"),
               QStringLiteral("retention-class"),
               QStringLiteral("Always retained")),
        domain(QStringLiteral("mark-latency"),
               QStringLiteral("traffic-mark"),
               QStringLiteral("Latency sensitive")),
        domain(QStringLiteral("mark-bulk"),
               QStringLiteral("traffic-mark"),
               QStringLiteral("Bulk traffic")),
        domain(QStringLiteral("endpoint-private"),
               QStringLiteral("endpoint-scope"),
               QStringLiteral("Private Endpoint"))};
    return design;
}

template <typename Widget>
Widget* assignmentEditor(MeshResizeDialog& dialog,
                         const QString& routerId,
                         const QString& domainTypeId) {
    for (Widget* widget : dialog.findChildren<Widget*>()) {
        if (widget->property("finepaper.routerId").toString() == routerId
            && widget->property("finepaper.domainType").toString()
                == domainTypeId) {
            return widget;
        }
    }
    return nullptr;
}

bool chooseSingle(QComboBox* combo, const QString& domainId) {
    if (!combo) {
        return false;
    }
    const int index = combo->findData(domainId);
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    return true;
}

bool setMultiple(QListWidget* list,
                 const QStringList& checkedDomainIds) {
    if (!list) {
        return false;
    }
    bool foundAll = true;
    for (const QString& domainId : checkedDomainIds) {
        bool found = false;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toString() == domainId) {
                found = true;
                break;
            }
        }
        foundAll = foundAll && found;
    }
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        item->setCheckState(
            checkedDomainIds.contains(item->data(Qt::UserRole).toString())
                ? Qt::Checked
                : Qt::Unchecked);
    }
    QApplication::processEvents();
    return foundAll;
}

const DomainMembership* membershipFor(
    const QVector<DomainMembership>& memberships,
    const QString& routerId) {
    const auto found = std::find_if(
        memberships.cbegin(), memberships.cend(),
        [&](const DomainMembership& membership) {
            return membership.element
                == ElementRef{ElementKind::Router, routerId};
        });
    return found == memberships.cend() ? nullptr : &*found;
}

void configurableTopologyTextPreservesPercentPlaceholders() {
    const QString routerId = QStringLiteral("router %2 / %3 / %4");
    check(
        topology_text::meshResizeRouterListText(routerId, 7, 9, false)
            == QStringLiteral(
                "router %2 / %3 / %4  ·  (7, 9)  ·  needs assignment"),
        QStringLiteral(
            "Mesh resize Router rows preserve placeholder-like identifier text"));
    check(
        topology_text::meshResizeRouterHeadingText(routerId, 7, 9)
            == QStringLiteral(
                "router %2 / %3 / %4 at Mesh coordinate (7, 9)"),
        QStringLiteral(
            "Mesh resize headings preserve placeholder-like Router identifiers"));

    const QString domainLabel = QStringLiteral("Power %2 / Clock %3");
    check(
        topology_text::compactDomainLegendEntryText(domainLabel, 4)
            == QStringLiteral("4 total · Power %2 / Clock %3"),
        QStringLiteral(
            "compact Domain legends preserve placeholder-like Package labels"));
}

void arbitraryTypesAndPerRouterAssignmentsAreComplete() {
    MeshResizeDialog dialog(growthDesign(), packageFixture());
    dialog.show();
    QApplication::processEvents();

    auto* rows = dialog.findChild<QSpinBox*>(
        QStringLiteral("finepaper.meshResize.rows"));
    auto* columns = dialog.findChild<QSpinBox*>(
        QStringLiteral("finepaper.meshResize.columns"));
    auto* routers = dialog.findChild<QListWidget*>(
        QStringLiteral("finepaper.meshResize.routerList"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.apply"));
    auto* copyAll = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.copyToAll"));
    auto* nextIncomplete = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.nextIncomplete"));
    check(rows && columns && routers && apply && copyAll && nextIncomplete,
          QStringLiteral("dialog exposes stable topology, navigation, and Apply controls"));
    if (!rows || !columns || !routers || !apply || !copyAll
        || !nextIncomplete) {
        return;
    }
    check(rows->minimum() == 1 && rows->maximum() == 3
              && columns->minimum() == 1 && columns->maximum() == 4,
          QStringLiteral("dimension controls use the Package Mesh range"));
    check(!apply->isEnabled(),
          QStringLiteral("the initial no-op topology is not presented as a resize"));

    columns->setValue(3);
    QApplication::processEvents();
    check(dialog.requestedRows() == 1 && dialog.requestedColumns() == 3
              && dialog.plan().newRouters.size() == 2
              && routers->count() == 2,
          QStringLiteral("changing dimensions previews every Mesh-derived new Router"));

    const QString firstRouter = routers->item(0)->data(Qt::UserRole).toString();
    const QString secondRouter = routers->item(1)->data(Qt::UserRole).toString();
    check(firstRouter == QStringLiteral("r-1-0")
              && secondRouter == QStringLiteral("r-2-0"),
          QStringLiteral("Router navigation follows stable projection ids"));

    auto* firstZone = assignmentEditor<QComboBox>(
        dialog, firstRouter, QStringLiteral("fabric-zone"));
    auto* firstLabels = assignmentEditor<QListWidget>(
        dialog, firstRouter, QStringLiteral("security-label"));
    auto* firstRetention = assignmentEditor<QComboBox>(
        dialog, firstRouter, QStringLiteral("retention-class"));
    auto* firstMarks = assignmentEditor<QListWidget>(
        dialog, firstRouter, QStringLiteral("traffic-mark"));
    auto* boundedConstraint = assignmentEditor<QLabel>(
        dialog, firstRouter, QStringLiteral("security-label"));
    auto* unboundedConstraint = assignmentEditor<QLabel>(
        dialog, firstRouter, QStringLiteral("traffic-mark"));
    check(firstZone && firstLabels && firstRetention && firstMarks,
          QStringLiteral("all arbitrary Router-applicable Package types get an editor"));
    check(!assignmentEditor<QComboBox>(
              dialog, firstRouter, QStringLiteral("endpoint-scope")),
          QStringLiteral("Endpoint-only Domain Types do not leak into Router assignment UI"));
    check(firstRetention && !firstRetention->isEnabled()
              && firstRetention->currentData().toString()
                  == QStringLiteral("retention-only"),
          QStringLiteral("a unique required instance is visibly selected automatically"));
    check(boundedConstraint
              && boundedConstraint->text().contains(
                  QStringLiteral("minimum 2"))
              && boundedConstraint->text().contains(
                  QStringLiteral("maximum 3")),
          QStringLiteral(
              "finite canonical Router constraints are stated in visible text"));
    check(unboundedConstraint
              && unboundedConstraint->text().contains(
                  QStringLiteral("minimum 0"))
              && unboundedConstraint->text().contains(
                  QStringLiteral("maximum unbounded")),
          QStringLiteral(
              "unbounded canonical Router constraints are stated in visible text"));

    check(chooseSingle(firstZone, QStringLiteral("zone-a"))
              && setMultiple(firstLabels,
                             {QStringLiteral("label-red")}),
          QStringLiteral(
              "the bounded canonical editor accepts an assignment below its minimum"));
    check(!routers->currentItem()->data(Qt::UserRole + 1).toBool(),
          QStringLiteral(
              "a Router remains incomplete below the canonical minimum"));
    check(setMultiple(firstLabels,
                      {QStringLiteral("label-blue"),
                       QStringLiteral("label-green"),
                       QStringLiteral("label-red"),
                       QStringLiteral("label-yellow")}),
          QStringLiteral(
              "the bounded editor exposes all available Package instances"));
    check(!routers->currentItem()->data(Qt::UserRole + 1).toBool()
              && dialog.localErrors().join(QLatin1Char('\n')).contains(
                  QStringLiteral("at most 3")),
          QStringLiteral(
              "a Router remains incomplete above the finite canonical maximum"));
    check(setMultiple(firstLabels,
                      {QStringLiteral("label-blue"),
                       QStringLiteral("label-red")})
              && setMultiple(firstMarks,
                             {QStringLiteral("mark-latency"),
                              QStringLiteral("mark-bulk")}),
          QStringLiteral(
              "the Router accepts a min-two bounded choice and multiple unbounded choices"));
    check(routers->currentItem()->data(Qt::UserRole + 1).toBool(),
          QStringLiteral("Router navigator marks a fully assigned Router complete"));

    nextIncomplete->click();
    QApplication::processEvents();
    check(routers->currentItem()->data(Qt::UserRole).toString() == secondRouter,
          QStringLiteral("Next incomplete jumps to the next Router requiring work"));
    check(chooseSingle(
              assignmentEditor<QComboBox>(
                  dialog, secondRouter, QStringLiteral("fabric-zone")),
              QStringLiteral("zone-b"))
              && setMultiple(
                  assignmentEditor<QListWidget>(
                      dialog, secondRouter, QStringLiteral("security-label")),
                  {QStringLiteral("label-blue"),
                   QStringLiteral("label-green")}),
          QStringLiteral("a second Router can receive a different explicit assignment"));

    routers->setCurrentRow(0);
    QApplication::processEvents();
    copyAll->click();
    QApplication::processEvents();
    check(apply->isEnabled(),
          QStringLiteral("copy current assignments to all completes every compatible Router draft"));
    routers->setCurrentRow(1);
    QApplication::processEvents();
    check(chooseSingle(
              assignmentEditor<QComboBox>(
                  dialog, secondRouter, QStringLiteral("fabric-zone")),
              QStringLiteral("zone-b"))
              && setMultiple(
                  assignmentEditor<QListWidget>(
                      dialog, secondRouter, QStringLiteral("security-label")),
                  {QStringLiteral("label-blue"),
                   QStringLiteral("label-green")}),
          QStringLiteral("copied assignments remain independently editable per Router"));

    const QVector<DomainMembership> memberships =
        dialog.newRouterMemberships();
    const DomainMembership* first = membershipFor(memberships, firstRouter);
    const DomainMembership* second = membershipFor(memberships, secondRouter);
    check(first && second && memberships.size() == 2,
          QStringLiteral("complete draft resolves atomically for every new Router"));
    check(first
              && first->assignments.value(QStringLiteral("fabric-zone"))
                  == QStringList{QStringLiteral("zone-a")}
              && first->assignments.value(QStringLiteral("security-label"))
                  == QStringList{QStringLiteral("label-blue"),
                                 QStringLiteral("label-red")}
              && first->assignments.value(QStringLiteral("retention-class"))
                  == QStringList{QStringLiteral("retention-only")}
              && first->assignments.value(QStringLiteral("traffic-mark"))
                  == QStringList{QStringLiteral("mark-bulk"),
                                 QStringLiteral("mark-latency")},
          QStringLiteral("first Router preserves its explicit and automatic assignments"));
    check(second
              && second->assignments.value(QStringLiteral("fabric-zone"))
                  == QStringList{QStringLiteral("zone-b")}
              && second->assignments.value(QStringLiteral("security-label"))
                  == QStringList{QStringLiteral("label-blue"),
                                 QStringLiteral("label-green")}
              && second->assignments.value(QStringLiteral("retention-class"))
                  == QStringList{QStringLiteral("retention-only")},
          QStringLiteral("second Router resolves a genuinely different Package-driven assignment"));

    apply->click();
    check(dialog.result() == QDialog::Accepted,
          QStringLiteral("Apply accepts only the complete atomic resize draft"));
}

void missingRequiredInstancesRemainBlocked() {
    NocDesign design = growthDesign();
    design.domains.erase(
        std::remove_if(
            design.domains.begin(), design.domains.end(),
            [](const DomainDefinition& value) {
                return value.type == QStringLiteral("fabric-zone");
            }),
        design.domains.end());
    MeshResizeDialog dialog(design, packageFixture());
    auto* columns = dialog.findChild<QSpinBox*>(
        QStringLiteral("finepaper.meshResize.columns"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.apply"));
    check(columns && apply,
          QStringLiteral("missing-instance fixture exposes resize controls"));
    if (!columns || !apply) {
        return;
    }
    columns->setValue(2);
    QApplication::processEvents();
    const QString errors = dialog.localErrors().join(QLatin1Char('\n'));
    check(!apply->isEnabled()
              && errors.contains(QStringLiteral("0 instances"),
                                 Qt::CaseInsensitive),
          QStringLiteral("a required type with zero instances cannot be guessed or applied"));
    bool foundUnavailable = false;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        foundUnavailable = foundUnavailable
            || label->text().contains(
                QStringLiteral("Create one in Domain Manager"));
    }
    check(foundUnavailable,
          QStringLiteral("zero-instance blocker explains how to repair the design"));
}

NocDesign shrinkDesign(bool withEndpoint) {
    NocDesign design = growthDesign();
    design.formatVersion = 3;
    design.topology = TopologySpec{QStringLiteral("mesh"), 1, 2};
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("fabric-zone"),
                 {QStringLiteral("zone-b")}},
                {QStringLiteral("retention-class"),
                 {QStringLiteral("retention-only")}}}}};
    design.edgeOverrides = {
        DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))},
            QStringLiteral("fabric-zone"),
            QStringLiteral("isolated-crossing"),
            QJsonObject{{QStringLiteral("mode"), QStringLiteral("safe")}}}};
    const QString removedLink =
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
    design.elementConfigurations = {
        ElementConfiguration{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QStringLiteral("vendor.router-implementation"),
            QJsonObject{{QStringLiteral("pipeline"), 3}}},
        ElementConfiguration{
            ElementRef{ElementKind::RouterLink, removedLink},
            QStringLiteral("vendor.mesh-link"),
            QJsonObject{{QStringLiteral("width"), 128}}}
    };
    if (withEndpoint) {
        design.endpoints = {
            EndpointInstance{
                QStringLiteral("ep-on-removed-router"),
                QStringLiteral("client"),
                EndpointAttachment{RouterPosition{1, 0}, std::nullopt},
                {}}};
    }
    return design;
}

void exactImpactConfirmationIsExplicitAndEfficient() {
    const NocDesign design = shrinkDesign(false);
    PackageDefinition package = packageFixture();
    package.formatVersion = 3;
    MeshResizeDialog dialog(design, package);
    auto* columns = dialog.findChild<QSpinBox*>(
        QStringLiteral("finepaper.meshResize.columns"));
    auto* memberships = dialog.findChild<QListWidget*>(
        QStringLiteral("finepaper.meshResize.removedMemberships"));
    auto* overrides = dialog.findChild<QListWidget*>(
        QStringLiteral("finepaper.meshResize.removedEdgeOverrides"));
    auto* configurations = dialog.findChild<QListWidget*>(
        QStringLiteral("finepaper.meshResize.removedElementConfigurations"));
    auto* confirmAll = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.confirmAllImpacts"));
    auto* clear = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.clearImpactConfirmations"));
    auto* diagnostics = dialog.findChild<QLabel*>(
        QStringLiteral("finepaper.meshResize.diagnostics"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.apply"));
    check(columns && memberships && overrides && configurations
              && confirmAll && clear
              && diagnostics && apply,
          QStringLiteral("impact preview exposes stable exact-confirmation controls"));
    if (!columns || !memberships || !overrides || !configurations
        || !confirmAll || !clear
        || !diagnostics || !apply) {
        return;
    }

    columns->setValue(1);
    QApplication::processEvents();
    check(dialog.plan().removedMemberships == design.domainMemberships
              && dialog.plan().removedEdgeOverrides == design.edgeOverrides
              && dialog.plan().removedElementConfigurations
                  == design.elementConfigurations
              && memberships->count() == 1 && overrides->count() == 1
              && configurations->count() == 2,
          QStringLiteral("shrink preview lists every exact Domain and element configuration record"));
    check(memberships->item(0)->text().contains(QStringLiteral("zone-b"))
              && overrides->item(0)->text().contains(
                  QStringLiteral("isolated-crossing"))
              && overrides->item(0)->text().contains(
                  QStringLiteral("safe"))
              && configurations->item(0)->text().contains(
                  QStringLiteral("vendor.router-implementation"))
              && configurations->item(0)->text().contains(
                  QStringLiteral("pipeline"))
              && configurations->item(1)->text().contains(
                  QStringLiteral("vendor.mesh-link"))
              && configurations->item(1)->text().contains(
                  QStringLiteral("128")),
          QStringLiteral("impact rows expose assignment, policy, property-set, and sparse-value details"));
    check(!apply->isEnabled() && confirmAll->isEnabled()
              && !clear->isEnabled(),
          QStringLiteral("unconfirmed impacts gate Apply while bulk exact confirmation is available"));

    memberships->item(0)->setCheckState(Qt::Checked);
    QApplication::processEvents();
    check(!apply->isEnabled() && clear->isEnabled(),
          QStringLiteral("confirming one impact category cannot authorize another"));
    confirmAll->click();
    QApplication::processEvents();
    check(apply->isEnabled()
              && dialog.impactConfirmation()
                  == MeshResizeImpactConfirmation{
                      design.domainMemberships,
                      design.edgeOverrides,
                      design.elementConfigurations}
              && diagnostics->text().contains(
                  QStringLiteral("delete 4 confirmed state record")),
          QStringLiteral("Confirm all checks exactly the current preview and reports deletion count"));

    clear->click();
    QApplication::processEvents();
    check(!apply->isEnabled()
              && dialog.impactConfirmation()
                  == MeshResizeImpactConfirmation{},
          QStringLiteral("Clear removes all exact confirmations"));

    confirmAll->click();
    columns->setValue(2);
    columns->setValue(1);
    QApplication::processEvents();
    check(memberships->item(0)->checkState() == Qt::Unchecked
              && overrides->item(0)->checkState() == Qt::Unchecked
              && configurations->item(0)->checkState() == Qt::Unchecked
              && configurations->item(1)->checkState() == Qt::Unchecked
              && !apply->isEnabled(),
          QStringLiteral("changing the topology invalidates confirmation of an older preview"));
    confirmAll->click();
    QApplication::processEvents();
    apply->click();
    check(dialog.result() == QDialog::Accepted,
          QStringLiteral("exactly confirmed shrink accepts as one transaction"));
}

void endpointDetachIsAlwaysAHardBlocker() {
    PackageDefinition package = packageFixture();
    package.formatVersion = 3;
    MeshResizeDialog dialog(shrinkDesign(true), package);
    auto* columns = dialog.findChild<QSpinBox*>(
        QStringLiteral("finepaper.meshResize.columns"));
    auto* confirmAll = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.confirmAllImpacts"));
    auto* blockers = dialog.findChild<QLabel*>(
        QStringLiteral("finepaper.meshResize.blockers"));
    auto* apply = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.meshResize.apply"));
    check(columns && confirmAll && blockers && apply,
          QStringLiteral("hard-blocker fixture exposes all relevant controls"));
    if (!columns || !confirmAll || !blockers || !apply) {
        return;
    }
    columns->setValue(1);
    confirmAll->click();
    QApplication::processEvents();
    check(dialog.plan().detachedEndpoints
              == QVector<ElementRef>{ElementRef{
                  ElementKind::Endpoint,
                  QStringLiteral("ep-on-removed-router")}}
              && !blockers->isHidden()
              && blockers->text().contains(
                  QStringLiteral("ep-on-removed-router"))
              && !apply->isEnabled(),
          QStringLiteral("Endpoint detach is named and cannot be bypassed by exact impact confirmation"));
    check(dialog.localErrors().join(QLatin1Char('\n')).contains(
              QStringLiteral("mesh.resize_would_detach_endpoint")),
          QStringLiteral("hard blocker remains present in authoritative local diagnostics"));
    apply->click();
    check(dialog.result() != QDialog::Accepted,
          QStringLiteral("disabled Apply cannot commit a topology that detaches an Endpoint"));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    configurableTopologyTextPreservesPercentPlaceholders();
    arbitraryTypesAndPerRouterAssignmentsAreComplete();
    missingRequiredInstancesRemainBlocked();
    exactImpactConfirmationIsExplicitAndEfficient();
    endpointDetachIsAlwaysAHardBlocker();
    if (failures == 0) {
        QTextStream(stdout) << "Mesh resize dialog tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
