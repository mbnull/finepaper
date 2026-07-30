#pragma once

#include "application/endpoint_domain_assignment.h"
#include "package/package.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace finepaper {

struct EndpointDomainChoice {
    QString id;
    QString label;
    bool available = true;

    bool operator==(const EndpointDomainChoice&) const = default;
};

struct EndpointDomainAssignmentGroup {
    QString domainType;
    QString domainTypeLabel;
    DomainCardinality cardinality = DomainCardinality::Invalid;
    bool required = false;
    QVector<EndpointDomainChoice> choices;
    QStringList selectedDomainIds;

    bool operator==(const EndpointDomainAssignmentGroup&) const = default;
};

// Build the Package-driven assignment rows independently from QWidget code so
// Endpoint creation, detached Endpoint recovery, and tests all share the same
// filtering and default-selection rules.
[[nodiscard]] QVector<EndpointDomainAssignmentGroup>
buildEndpointDomainAssignmentGroups(
    const NocDesign& design,
    const PackageDefinition& package,
    const EndpointDomainAssignments& initialAssignments = {});

class EndpointDomainAssignmentDialog final : public QDialog {
public:
    EndpointDomainAssignmentDialog(
        const NocDesign& design,
        const PackageDefinition& package,
        EndpointDomainAssignments initialAssignments = {},
        QWidget* parent = nullptr);

    [[nodiscard]] EndpointDomainAssignments assignments() const;
    [[nodiscard]] QStringList localErrors() const;
    [[nodiscard]] const QVector<EndpointDomainAssignmentGroup>& groups() const {
        return m_groups;
    }

protected:
    void accept() override;

private:
    struct GroupEditor {
        EndpointDomainAssignmentGroup group;
        QComboBox* single = nullptr;
        QListWidget* multiple = nullptr;
    };

    void updateValidation();

    QVector<EndpointDomainAssignmentGroup> m_groups;
    QVector<GroupEditor> m_editors;
    QLabel* m_diagnostics = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton* m_acceptButton = nullptr;
};

} // namespace finepaper
