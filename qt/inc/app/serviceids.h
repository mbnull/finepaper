#pragma once

#include "app/servicekey.h"

namespace app::serviceids {

inline ServiceKey project() {
    return ServiceKey::fromLiteral("finepaper.project");
}

inline ServiceKey designEditing() {
    return ServiceKey::fromLiteral("finepaper.design-editing");
}

inline ServiceKey package() {
    return ServiceKey::fromLiteral("finepaper.package");
}

inline ServiceKey toolPipeline() {
    return ServiceKey::fromLiteral("finepaper.tool-pipeline");
}

inline ServiceKey workbench() {
    return ServiceKey::fromLiteral("finepaper.workbench");
}

} // namespace app::serviceids
