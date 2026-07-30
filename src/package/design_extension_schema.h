#pragma once

#include "noc/model.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace finepaper::package_detail {

// Loads a schema from an already containment-checked Package path. The loader
// performs bounded I/O, requires an object root, and rejects standard reference
// keywords that would require resolving another document. Package roots are
// expected to remain immutable for the duration of one load operation.
std::optional<QJsonObject> loadDesignExtensionSchema(
    const QString& schemaPath,
    const QString& diagnosticPath,
    QVector<Diagnostic>& diagnostics);

} // namespace finepaper::package_detail
