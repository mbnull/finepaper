#include "application/domain_configuration_draft.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace finepaper {
namespace {

DomainConfigurationDraft::Token takeToken(
    DomainConfigurationDraft::Token& nextToken) {
    if (nextToken == std::numeric_limits<DomainConfigurationDraft::Token>::max()) {
        qFatal("DomainConfigurationDraft exhausted its row-token space");
    }
    return nextToken++;
}

template<typename Row, typename Value>
void appendRows(QVector<Row>& rows,
                const QVector<Value>& values,
                DomainConfigurationDraft::Token& nextToken) {
    rows.reserve(values.size());
    for (const Value& value : values) {
        rows.append(Row{takeToken(nextToken), value});
    }
}

template<typename Row, typename Value>
DomainConfigurationDraft::Token addRow(
    QVector<Row>& rows,
    Value value,
    DomainConfigurationDraft::Token token) {
    rows.append(Row{token, std::move(value)});
    return token;
}

template<typename Row, typename Value>
bool updateRow(QVector<Row>& rows,
               DomainConfigurationDraft::Token token,
               Value value) {
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& candidate) {
        return candidate.token == token;
    });
    if (row == rows.end()) {
        return false;
    }
    row->value = std::move(value);
    return true;
}

template<typename Row>
bool removeRow(QVector<Row>& rows, DomainConfigurationDraft::Token token) {
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const Row& candidate) {
        return candidate.token == token;
    });
    if (row == rows.end()) {
        return false;
    }
    rows.erase(row);
    return true;
}

template<typename Row, typename Value>
QVector<Value> valuesOf(const QVector<Row>& rows) {
    QVector<Value> values;
    values.reserve(rows.size());
    for (const Row& row : rows) {
        values.append(row.value);
    }
    return values;
}

} // namespace

DomainConfigurationDraft::DomainConfigurationDraft(
    const DomainConfiguration& configuration) {
    reset(configuration);
}

void DomainConfigurationDraft::reset(const DomainConfiguration& configuration) {
    domains_.clear();
    memberships_.clear();
    relations_.clear();
    policies_.clear();
    overrides_.clear();

    appendRows(domains_, configuration.domains, nextToken_);
    appendRows(memberships_, configuration.domainMemberships, nextToken_);
    appendRows(relations_, configuration.domainRelations, nextToken_);
    appendRows(policies_, configuration.crossingPolicies, nextToken_);
    appendRows(overrides_, configuration.edgeOverrides, nextToken_);
}

DomainConfiguration DomainConfigurationDraft::configuration() const {
    DomainConfiguration result;
    result.domains = valuesOf<DomainRow, DomainDefinition>(domains_);
    result.domainMemberships = valuesOf<MembershipRow, DomainMembership>(memberships_);
    result.domainRelations = valuesOf<RelationRow, DomainRelation>(relations_);
    result.crossingPolicies = valuesOf<PolicyRow, DomainCrossingPolicy>(policies_);
    result.edgeOverrides = valuesOf<OverrideRow, DomainEdgeOverride>(overrides_);
    return result;
}

const QVector<DomainConfigurationDraft::DomainRow>&
DomainConfigurationDraft::domains() const noexcept {
    return domains_;
}

const QVector<DomainConfigurationDraft::MembershipRow>&
DomainConfigurationDraft::memberships() const noexcept {
    return memberships_;
}

const QVector<DomainConfigurationDraft::RelationRow>&
DomainConfigurationDraft::relations() const noexcept {
    return relations_;
}

const QVector<DomainConfigurationDraft::PolicyRow>&
DomainConfigurationDraft::policies() const noexcept {
    return policies_;
}

const QVector<DomainConfigurationDraft::OverrideRow>&
DomainConfigurationDraft::overrides() const noexcept {
    return overrides_;
}

DomainConfigurationDraft::Token DomainConfigurationDraft::addDomain(
    DomainDefinition value) {
    return addRow(domains_, std::move(value), allocateToken());
}

bool DomainConfigurationDraft::updateDomain(Token token, DomainDefinition value) {
    return updateRow(domains_, token, std::move(value));
}

bool DomainConfigurationDraft::removeDomain(Token token) {
    return removeRow(domains_, token);
}

DomainConfigurationDraft::Token DomainConfigurationDraft::addMembership(
    DomainMembership value) {
    return addRow(memberships_, std::move(value), allocateToken());
}

bool DomainConfigurationDraft::updateMembership(Token token, DomainMembership value) {
    return updateRow(memberships_, token, std::move(value));
}

bool DomainConfigurationDraft::removeMembership(Token token) {
    return removeRow(memberships_, token);
}

DomainConfigurationDraft::Token DomainConfigurationDraft::addRelation(
    DomainRelation value) {
    return addRow(relations_, std::move(value), allocateToken());
}

bool DomainConfigurationDraft::updateRelation(Token token, DomainRelation value) {
    return updateRow(relations_, token, std::move(value));
}

bool DomainConfigurationDraft::removeRelation(Token token) {
    return removeRow(relations_, token);
}

DomainConfigurationDraft::Token DomainConfigurationDraft::addPolicy(
    DomainCrossingPolicy value) {
    return addRow(policies_, std::move(value), allocateToken());
}

bool DomainConfigurationDraft::updatePolicy(Token token, DomainCrossingPolicy value) {
    return updateRow(policies_, token, std::move(value));
}

bool DomainConfigurationDraft::removePolicy(Token token) {
    return removeRow(policies_, token);
}

DomainConfigurationDraft::Token DomainConfigurationDraft::addOverride(
    DomainEdgeOverride value) {
    return addRow(overrides_, std::move(value), allocateToken());
}

bool DomainConfigurationDraft::updateOverride(Token token, DomainEdgeOverride value) {
    return updateRow(overrides_, token, std::move(value));
}

bool DomainConfigurationDraft::removeOverride(Token token) {
    return removeRow(overrides_, token);
}

DomainConfigurationDraft::Token DomainConfigurationDraft::allocateToken() {
    return takeToken(nextToken_);
}

} // namespace finepaper
