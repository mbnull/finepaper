#pragma once

#include <QString>

namespace ipcraft::schemaids {

inline const QString projectV1 = QStringLiteral("ipcraft.project.v1");
inline const QString packageV1 = QStringLiteral("ipcraft.package.v1");
inline const QString componentV1 = QStringLiteral("ipcraft.component.v1");
inline const QString interfaceV1 = QStringLiteral("ipcraft.interface.v1");
inline const QString connectionRulesV1 = QStringLiteral("ipcraft.connection_rules.v1");
inline const QString topologyGraphV1 = QStringLiteral("ipcraft.topology.graph.v1");
inline const QString topologyParametricV1 = QStringLiteral("ipcraft.topology.parametric.v1");
inline const QString viewV1 = QStringLiteral("ipcraft.view.v1");
inline const QString viewDescriptorV1 = QStringLiteral("ipcraft.view.descriptor.v1");
inline const QString toolInputV1 = QStringLiteral("ipcraft.tool.input.v1");
inline const QString toolResultV1 = QStringLiteral("ipcraft.tool.result.v1");
inline const QString diagnosticV1 = QStringLiteral("ipcraft.diagnostic.v1");
inline const QString diagnosticsV1 = QStringLiteral("ipcraft.diagnostics.v1");
inline const QString artifactV1 = QStringLiteral("ipcraft.artifact.v1");
inline const QString patchV1 = QStringLiteral("ipcraft.patch.v1");
inline const QString nocCapabilityV1 = QStringLiteral("ipcraft.capability.noc.v1");
inline const QString nocExtensionV1 = QStringLiteral("ipcraft.capability.noc.extension.v1");
inline const QString graphConfigV1 = QStringLiteral("ipcraft.graph-config.v1");
inline const QString emittedInputsV1 = QStringLiteral("ipcraft.emitted-inputs.v1");
inline const QString cliResultV1 = QStringLiteral("ipcraft.cli.result.v1");

} // namespace ipcraft::schemaids

namespace IpcraftSchemaIds {
inline const QString& projectV1 = ipcraft::schemaids::projectV1;
inline const QString& packageV1 = ipcraft::schemaids::packageV1;
inline const QString& componentV1 = ipcraft::schemaids::componentV1;
inline const QString& interfaceV1 = ipcraft::schemaids::interfaceV1;
inline const QString& connectionRulesV1 = ipcraft::schemaids::connectionRulesV1;
inline const QString& topologyGraphV1 = ipcraft::schemaids::topologyGraphV1;
inline const QString& topologyParametricV1 = ipcraft::schemaids::topologyParametricV1;
inline const QString& viewV1 = ipcraft::schemaids::viewV1;
inline const QString& viewDescriptorV1 = ipcraft::schemaids::viewDescriptorV1;
inline const QString& toolInputV1 = ipcraft::schemaids::toolInputV1;
inline const QString& toolResultV1 = ipcraft::schemaids::toolResultV1;
inline const QString& diagnosticV1 = ipcraft::schemaids::diagnosticV1;
inline const QString& diagnosticsV1 = ipcraft::schemaids::diagnosticsV1;
inline const QString& artifactV1 = ipcraft::schemaids::artifactV1;
inline const QString& patchV1 = ipcraft::schemaids::patchV1;
inline const QString& nocCapabilityV1 = ipcraft::schemaids::nocCapabilityV1;
inline const QString& nocExtensionV1 = ipcraft::schemaids::nocExtensionV1;
inline const QString& graphConfigV1 = ipcraft::schemaids::graphConfigV1;
inline const QString& emittedInputsV1 = ipcraft::schemaids::emittedInputsV1;
inline const QString& cliResultV1 = ipcraft::schemaids::cliResultV1;
}
