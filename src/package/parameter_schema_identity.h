#pragma once

#include "package/package.h"

#include <QString>

namespace finepaper {

// Stable identities for editor-relevant Package schema semantics. Presentation
// metadata and declaration order are intentionally excluded so label-only
// Package updates do not invalidate a user's draft.
[[nodiscard]] QString parameterSchemaIdentity(
    const QVector<ParameterDefinition>& definitions);
[[nodiscard]] QString elementPropertySchemaIdentity(
    const QVector<ElementPropertyDefinition>& definitions);

} // namespace finepaper
