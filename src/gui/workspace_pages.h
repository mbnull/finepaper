#pragma once

#include <QStringList>

namespace finepaper {

enum class WorkspacePage {
    Start,
    Overview,
    Topology,
    Parameters,
    Validate,
    Generate
};

inline const QStringList& workspacePageLabels() {
    static const QStringList labels{
        QStringLiteral("Start"),
        QStringLiteral("Overview"),
        QStringLiteral("Topology"),
        QStringLiteral("Parameters"),
        QStringLiteral("Validate"),
        QStringLiteral("Generate")
    };
    return labels;
}

constexpr int workspacePageIndex(WorkspacePage page) {
    return static_cast<int>(page);
}

} // namespace finepaper
