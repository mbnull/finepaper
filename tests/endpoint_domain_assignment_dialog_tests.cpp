#include "application/endpoint_domain_assignment.h"
#include "gui/endpoint_domain_assignment_dialog.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTextStream>

#include <algorithm>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& description) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << description << Qt::endl;
}

DomainTypeDefinition type(const QString& id,
                          DomainCardinality cardinality,
                          bool required,
                          QVector<ElementKind> appliesTo = {
                              ElementKind::Endpoint}) {
    DomainTypeDefinition definition;
    definition.id = id;
    definition.label = id.toUpper();
    definition.appliesTo = std::move(appliesTo);
    definition.cardinality = cardinality;
    definition.required = required;
    return definition;
}

PackageDefinition domainPackage() {
    PackageDefinition package;
    package.domainTypes = {
        type(QStringLiteral("power"), DomainCardinality::Single, true),
        type(QStringLiteral("clock"), DomainCardinality::Multiple, true),
        type(QStringLiteral("tag"), DomainCardinality::Multiple, false),
        type(QStringLiteral("router-only"),
             DomainCardinality::Single,
             true,
             {ElementKind::Router})};
    return package;
}

DomainDefinition domain(const QString& id,
                        const QString& typeId,
                        const QString& name = {}) {
    return DomainDefinition{id, typeId, name, {}};
}

NocDesign choiceDesign() {
    NocDesign design;
    design.domains = {
        domain(QStringLiteral("p-b"), QStringLiteral("power"),
               QStringLiteral("Backup power")),
        domain(QStringLiteral("p-a"), QStringLiteral("power"),
               QStringLiteral("Main power")),
        domain(QStringLiteral("c-b"), QStringLiteral("clock")),
        domain(QStringLiteral("c-a"), QStringLiteral("clock")),
        domain(QStringLiteral("tag-a"), QStringLiteral("tag")),
        domain(QStringLiteral("r-a"), QStringLiteral("router-only"))};
    return design;
}

void snapshotMergesAndNormalizesMemberships() {
    NocDesign design;
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")},
            EndpointDomainAssignments{
                {QStringLiteral(" clock "),
                 {QStringLiteral(" c-b "), QStringLiteral("c-a"),
                  QStringLiteral("c-b"), QString()}},
                {QString(), {QStringLiteral("ignored")}}}},
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep0")},
            EndpointDomainAssignments{
                {QStringLiteral("clock"), {QStringLiteral("c-c")}},
                {QStringLiteral("power"), {QStringLiteral(" p-a ")}}}},
        DomainMembership{
            ElementRef{ElementKind::Endpoint, QStringLiteral("ep1")},
            EndpointDomainAssignments{
                {QStringLiteral("clock"), {QStringLiteral("other")}}}},
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("ep0")},
            EndpointDomainAssignments{
                {QStringLiteral("clock"), {QStringLiteral("router")}}}}};

    const EndpointDomainAssignments assignments =
        endpointDomainAssignments(design, QStringLiteral(" ep0 "));
    check(assignments.size() == 2,
          QStringLiteral("snapshot keeps only normalized non-empty Endpoint assignments"));
    check(assignments.value(QStringLiteral("clock"))
              == QStringList{QStringLiteral("c-a"), QStringLiteral("c-b"),
                             QStringLiteral("c-c")},
          QStringLiteral("snapshot merges duplicate membership rows and sorts unique ids"));
    check(assignments.value(QStringLiteral("power"))
              == QStringList{QStringLiteral("p-a")},
          QStringLiteral("snapshot trims Domain ids"));
    check(endpointDomainAssignments(design, QString()).isEmpty(),
          QStringLiteral("blank Endpoint id cannot capture unrelated memberships"));
}

void projectionIsPackageDrivenAndPreservesInitialState() {
    const EndpointDomainAssignments initial{
        {QStringLiteral("power"), {QStringLiteral("p-b")}},
        {QStringLiteral("clock"),
         {QStringLiteral("c-b"), QStringLiteral("c-a")}},
        {QStringLiteral("tag"), {QStringLiteral("missing-tag")}},
        {QStringLiteral("router-only"), {QStringLiteral("r-a")}}};
    const QVector<EndpointDomainAssignmentGroup> groups =
        buildEndpointDomainAssignmentGroups(
            choiceDesign(), domainPackage(), initial);

    check(groups.size() == 3,
          QStringLiteral("projection includes every and only Endpoint-applicable Package type"));
    if (groups.size() != 3) {
        return;
    }
    check(groups.at(0).domainType == QStringLiteral("power")
              && groups.at(0).cardinality == DomainCardinality::Single
              && groups.at(0).required,
          QStringLiteral("projection preserves Package order and Single/required semantics"));
    check(groups.at(0).choices.size() == 2
              && groups.at(0).choices.at(0).id == QStringLiteral("p-a")
              && groups.at(0).choices.at(1).id == QStringLiteral("p-b"),
          QStringLiteral("available instances are deterministically sorted"));
    check(groups.at(1).selectedDomainIds
              == QStringList{QStringLiteral("c-a"), QStringLiteral("c-b")},
          QStringLiteral("multiple initial assignments are normalized"));
    check(groups.at(2).choices.size() == 2
              && groups.at(2).choices.constLast().id
                  == QStringLiteral("missing-tag")
              && !groups.at(2).choices.constLast().available,
          QStringLiteral("stale detached assignments remain visible and repairable"));
}

void ambiguousRequiredChoicesGateAcceptance() {
    EndpointDomainAssignmentDialog dialog(choiceDesign(), domainPackage());
    dialog.show();
    QApplication::processEvents();

    auto* accept = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointDomainAssignment.accept"));
    auto* power = dialog.findChild<QComboBox*>(
        QStringLiteral("finepaper.endpointDomainAssignment.power.single"));
    auto* clock = dialog.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointDomainAssignment.clock.multiple"));
    auto* tag = dialog.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointDomainAssignment.tag.multiple"));
    check(accept && power && clock && tag,
          QStringLiteral("dialog exposes stable Package-driven controls"));
    check(!dialog.findChild<QComboBox*>(
              QStringLiteral(
                  "finepaper.endpointDomainAssignment.router-only.single")),
          QStringLiteral("Router-only Domain types never leak into Endpoint chooser"));
    if (!accept || !power || !clock || !tag) {
        return;
    }

    check(!accept->isEnabled(),
          QStringLiteral("ambiguous required assignments disable acceptance"));
    power->setCurrentIndex(power->findData(QStringLiteral("p-b")));
    QApplication::processEvents();
    check(!accept->isEnabled(),
          QStringLiteral("all required rows must be complete together"));

    clock->item(1)->setCheckState(Qt::Checked);
    QApplication::processEvents();
    check(accept->isEnabled(),
          QStringLiteral("one selection satisfies required Multiple cardinality"));

    clock->item(0)->setCheckState(Qt::Checked);
    tag->item(0)->setCheckState(Qt::Checked);
    QApplication::processEvents();
    const EndpointDomainAssignments assignments = dialog.assignments();
    check(assignments.value(QStringLiteral("power"))
              == QStringList{QStringLiteral("p-b")}
              && assignments.value(QStringLiteral("clock"))
                  == QStringList{QStringLiteral("c-a"),
                                 QStringLiteral("c-b")}
              && assignments.value(QStringLiteral("tag"))
                  == QStringList{QStringLiteral("tag-a")},
          QStringLiteral("dialog returns exact Single, Multiple, and optional choices"));
}

void uniqueRequiredChoicesAreSelectedWithoutHardcodedTypeKnowledge() {
    NocDesign design;
    design.domains = {
        domain(QStringLiteral("only-power"), QStringLiteral("power")),
        domain(QStringLiteral("only-clock"), QStringLiteral("clock")),
        domain(QStringLiteral("tag-a"), QStringLiteral("tag"))};
    EndpointDomainAssignmentDialog dialog(design, domainPackage());
    auto* accept = dialog.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointDomainAssignment.accept"));
    check(accept && accept->isEnabled(),
          QStringLiteral("each unique required instance is selected automatically"));
    check(dialog.assignments()
              == EndpointDomainAssignments{
                  {QStringLiteral("power"), {QStringLiteral("only-power")}},
                  {QStringLiteral("clock"), {QStringLiteral("only-clock")}}},
          QStringLiteral("automatic choices are derived from arbitrary Package type ids"));
}

void missingAndStaleAssignmentsRequireExplicitRepair() {
    NocDesign noClock = choiceDesign();
    noClock.domains.erase(
        std::remove_if(noClock.domains.begin(), noClock.domains.end(),
                       [](const DomainDefinition& value) {
                           return value.type == QStringLiteral("clock");
                       }),
        noClock.domains.end());
    EndpointDomainAssignmentDialog missing(noClock, domainPackage());
    auto* missingAccept = missing.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointDomainAssignment.accept"));
    auto* diagnostics = missing.findChild<QLabel*>(
        QStringLiteral("finepaper.endpointDomainAssignment.diagnostics"));
    check(missingAccept && !missingAccept->isEnabled()
              && diagnostics
              && diagnostics->text().contains(QStringLiteral("no instances"),
                                               Qt::CaseInsensitive),
          QStringLiteral("required type with zero instances is blocked with an actionable message"));

    EndpointDomainAssignmentDialog stale(
        choiceDesign(),
        domainPackage(),
        EndpointDomainAssignments{
            {QStringLiteral("power"), {QStringLiteral("p-a")}},
            {QStringLiteral("clock"), {QStringLiteral("c-a")}},
            {QStringLiteral("tag"), {QStringLiteral("deleted-tag")}}});
    auto* staleAccept = stale.findChild<QPushButton*>(
        QStringLiteral("finepaper.endpointDomainAssignment.accept"));
    auto* tag = stale.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointDomainAssignment.tag.multiple"));
    check(staleAccept && !staleAccept->isEnabled() && tag
              && tag->count() == 2
              && tag->item(1)->data(Qt::UserRole).toString()
                  == QStringLiteral("deleted-tag")
              && tag->item(1)->checkState() == Qt::Checked,
          QStringLiteral("stale reconnect state is shown instead of silently discarded"));
    if (staleAccept && tag && tag->count() == 2) {
        tag->item(1)->setCheckState(Qt::Unchecked);
        QApplication::processEvents();
        check(staleAccept->isEnabled(),
              QStringLiteral("unchecking a stale optional assignment repairs the draft"));
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    snapshotMergesAndNormalizesMemberships();
    projectionIsPackageDrivenAndPreservesInitialState();
    ambiguousRequiredChoicesGateAcceptance();
    uniqueRequiredChoicesAreSelectedWithoutHardcodedTypeKnowledge();
    missingAndStaleAssignmentsRequireExplicitRepair();
    if (failures == 0) {
        QTextStream(stdout)
            << "Endpoint Domain assignment dialog tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
