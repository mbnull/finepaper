#pragma once

#include <QString>

namespace app::pluginids {

inline QString project() {
    return QStringLiteral("finepaper.project");
}

inline QString package() {
    return QStringLiteral("finepaper.package");
}

inline QString toolPipeline() {
    return QStringLiteral("finepaper.tool-pipeline");
}

inline QString nocPlugin() {
    return QStringLiteral("finepaper.noc-plugin");
}

} // namespace app::pluginids
