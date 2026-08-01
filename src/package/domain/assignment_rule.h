#pragma once

#include "noc/model.h"

#include <optional>

namespace finepaper {

// Canonical Package constraint for one Domain Type and one assignable element
// kind. The absence of a rule means the Domain Type is not applicable to that
// kind. An absent maximum means there is no Package-defined upper bound.
struct DomainAssignmentRule {
    ElementKind elementKind = ElementKind::Invalid;
    qsizetype minimumAssignments = 0;
    std::optional<qsizetype> maximumAssignments = std::nullopt;

    [[nodiscard]] bool isValid() const noexcept {
        return isDomainMembershipElementKind(elementKind)
            && minimumAssignments >= 0
            && (!maximumAssignments
                || (*maximumAssignments > 0
                    && *maximumAssignments >= minimumAssignments));
    }

    [[nodiscard]] bool requiresAssignment() const noexcept {
        return isValid() && minimumAssignments > 0;
    }

    [[nodiscard]] bool isSingleAssignment() const noexcept {
        return isValid() && maximumAssignments == 1;
    }

    [[nodiscard]] bool acceptsCount(qsizetype count) const noexcept {
        return isValid()
            && count >= minimumAssignments
            && (!maximumAssignments || count <= *maximumAssignments);
    }

    bool operator==(const DomainAssignmentRule&) const = default;
};

} // namespace finepaper
