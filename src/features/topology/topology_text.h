#pragma once

#include <QString>

namespace finepaper::topology_text {

// Build text containing Package/design identifiers by concatenation so any
// "%N" text in those identifiers remains literal instead of becoming a
// placeholder for a later QString::arg() call.
[[nodiscard]] inline QString meshResizeRouterListText(
    const QString& routerId,
    int x,
    int y,
    bool complete) {
    return routerId
        + QStringLiteral("  ·  (") + QString::number(x)
        + QStringLiteral(", ") + QString::number(y)
        + QStringLiteral(")  ·  ")
        + (complete ? QStringLiteral("complete")
                    : QStringLiteral("needs assignment"));
}

[[nodiscard]] inline QString meshResizeRouterHeadingText(
    const QString& routerId,
    int x,
    int y) {
    return routerId
        + QStringLiteral(" at Mesh coordinate (") + QString::number(x)
        + QStringLiteral(", ") + QString::number(y)
        + QLatin1Char(')');
}

[[nodiscard]] inline QString compactDomainLegendEntryText(
    const QString& label,
    qsizetype totalEntries) {
    return QString::number(totalEntries) + QStringLiteral(" total · ")
        + label;
}

} // namespace finepaper::topology_text
