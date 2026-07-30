#pragma once

#include "application/domain_configuration.h"

#include <QtGlobal>

namespace finepaper {

class DomainConfigurationDraft {
public:
    using Token = quint64;

    template<typename T>
    struct Row {
        Token token = 0;
        T value;

        bool operator==(const Row&) const = default;
    };

    using DomainRow = Row<DomainDefinition>;
    using MembershipRow = Row<DomainMembership>;
    using RelationRow = Row<DomainRelation>;
    using PolicyRow = Row<DomainCrossingPolicy>;
    using OverrideRow = Row<DomainEdgeOverride>;

    DomainConfigurationDraft() = default;
    explicit DomainConfigurationDraft(const DomainConfiguration& configuration);

    void reset(const DomainConfiguration& configuration);
    DomainConfiguration configuration() const;

    const QVector<DomainRow>& domains() const noexcept;
    const QVector<MembershipRow>& memberships() const noexcept;
    const QVector<RelationRow>& relations() const noexcept;
    const QVector<PolicyRow>& policies() const noexcept;
    const QVector<OverrideRow>& overrides() const noexcept;

    Token addDomain(DomainDefinition value);
    bool updateDomain(Token token, DomainDefinition value);
    bool removeDomain(Token token);

    Token addMembership(DomainMembership value);
    bool updateMembership(Token token, DomainMembership value);
    bool removeMembership(Token token);

    Token addRelation(DomainRelation value);
    bool updateRelation(Token token, DomainRelation value);
    bool removeRelation(Token token);

    Token addPolicy(DomainCrossingPolicy value);
    bool updatePolicy(Token token, DomainCrossingPolicy value);
    bool removePolicy(Token token);

    Token addOverride(DomainEdgeOverride value);
    bool updateOverride(Token token, DomainEdgeOverride value);
    bool removeOverride(Token token);

private:
    Token allocateToken();

    QVector<DomainRow> domains_;
    QVector<MembershipRow> memberships_;
    QVector<RelationRow> relations_;
    QVector<PolicyRow> policies_;
    QVector<OverrideRow> overrides_;
    Token nextToken_ = 1;
};

} // namespace finepaper
