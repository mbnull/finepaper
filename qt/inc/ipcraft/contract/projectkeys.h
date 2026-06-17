#pragma once

#include <QString>

namespace ipcraft::contract::projectkeys {

inline QString schema() { return QStringLiteral("schema"); }
inline QString id() { return QStringLiteral("id"); }
inline QString name() { return QStringLiteral("name"); }
inline QString version() { return QStringLiteral("version"); }
inline QString packages() { return QStringLiteral("packages"); }
inline QString components() { return QStringLiteral("components"); }
inline QString interfaces() { return QStringLiteral("interfaces"); }
inline QString connections() { return QStringLiteral("connections"); }
inline QString topologies() { return QStringLiteral("topologies"); }
inline QString constraints() { return QStringLiteral("constraints"); }
inline QString views() { return QStringLiteral("views"); }
inline QString diagnostics() { return QStringLiteral("diagnostics"); }
inline QString artifacts() { return QStringLiteral("artifacts"); }
inline QString extensions() { return QStringLiteral("extensions"); }
inline QString metadata() { return QStringLiteral("metadata"); }

inline QString type() { return QStringLiteral("type"); }
inline QString packageRef() { return QStringLiteral("packageRef"); }
inline QString identity() { return QStringLiteral("identity"); }
inline QString config() { return QStringLiteral("config"); }
inline QString extensionData() { return QStringLiteral("extensionData"); }

inline QString ownerComponentId() { return QStringLiteral("ownerComponentId"); }
inline QString role() { return QStringLiteral("role"); }
inline QString direction() { return QStringLiteral("direction"); }
inline QString protocol() { return QStringLiteral("protocol"); }
inline QString clockRef() { return QStringLiteral("clockRef"); }
inline QString resetRef() { return QStringLiteral("resetRef"); }

inline QString from() { return QStringLiteral("from"); }
inline QString to() { return QStringLiteral("to"); }
inline QString kind() { return QStringLiteral("kind"); }
inline QString component() { return QStringLiteral("component"); }
inline QString interfaceId() { return QStringLiteral("interface"); }

inline QString family() { return QStringLiteral("family"); }
inline QString providerRef() { return QStringLiteral("providerRef"); }
inline QString parameters() { return QStringLiteral("parameters"); }
inline QString nodes() { return QStringLiteral("nodes"); }
inline QString links() { return QStringLiteral("links"); }
inline QString attachments() { return QStringLiteral("attachments"); }
inline QString routing() { return QStringLiteral("routing"); }
inline QString topologyId() { return QStringLiteral("topologyId"); }
inline QString attachmentPoint() { return QStringLiteral("attachmentPoint"); }
inline QString componentRef() { return QStringLiteral("componentRef"); }
inline QString interfaceRef() { return QStringLiteral("interfaceRef"); }
inline QString adapterRef() { return QStringLiteral("adapterRef"); }

inline QString targetRef() { return QStringLiteral("targetRef"); }
inline QString sourceRef() { return QStringLiteral("sourceRef"); }
inline QString templates() { return QStringLiteral("templates"); }
inline QString portGrouping() { return QStringLiteral("portGrouping"); }
inline QString labels() { return QStringLiteral("labels"); }
inline QString badges() { return QStringLiteral("badges"); }
inline QString propertyGroups() { return QStringLiteral("propertyGroups"); }
inline QString layoutPreference() { return QStringLiteral("layoutPreference"); }
inline QString interactionAffordances() { return QStringLiteral("interactionAffordances"); }
inline QString diagnosticsOverlay() { return QStringLiteral("diagnosticsOverlay"); }
inline QString icons() { return QStringLiteral("icons"); }
inline QString layout() { return QStringLiteral("layout"); }
inline QString presentationState() { return QStringLiteral("presentationState"); }

inline QString ownerPackageId() { return QStringLiteral("ownerPackageId"); }
inline QString schemaId() { return QStringLiteral("schemaId"); }
inline QString data() { return QStringLiteral("data"); }
inline QString validationState() { return QStringLiteral("validationState"); }

} // namespace ipcraft::contract::projectkeys
