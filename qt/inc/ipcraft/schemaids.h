#pragma once

#include <QString>

namespace ipcraft::schemaids {

inline const QString projectV1 = QStringLiteral("ipcraft.project.v1");
inline const QString packageV1 = QStringLiteral("ipcraft.package.v1");
inline const QString diagnosticsV1 = QStringLiteral("ipcraft.diagnostics.v1");
inline const QString graphConfigV1 = QStringLiteral("ipcraft.graph-config.v1");
inline const QString emittedInputsV1 = QStringLiteral("ipcraft.emitted-inputs.v1");
inline const QString cliResultV1 = QStringLiteral("ipcraft.cli.result.v1");

} // namespace ipcraft::schemaids

namespace IpcraftSchemaIds {
inline const QString& projectV1 = ipcraft::schemaids::projectV1;
inline const QString& packageV1 = ipcraft::schemaids::packageV1;
inline const QString& diagnosticsV1 = ipcraft::schemaids::diagnosticsV1;
inline const QString& graphConfigV1 = ipcraft::schemaids::graphConfigV1;
inline const QString& emittedInputsV1 = ipcraft::schemaids::emittedInputsV1;
inline const QString& cliResultV1 = ipcraft::schemaids::cliResultV1;
}
