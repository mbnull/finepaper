#pragma once

#include <QStringList>

#include <optional>

namespace finepaper {

struct DomainAssignmentPatch {
    QStringList ensurePresent;
    QStringList ensureAbsent;
    std::optional<QStringList> replacement;

    bool operator==(const DomainAssignmentPatch&) const = default;
};

} // namespace finepaper
