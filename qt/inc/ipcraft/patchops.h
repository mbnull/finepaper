#pragma once

#include <QString>

namespace ipcraft::patchops {

inline const QString componentAdd = QStringLiteral("component.add");
inline const QString componentRemove = QStringLiteral("component.remove");
inline const QString componentConfigSet = QStringLiteral("component.config.set");
inline const QString componentConfigUnset = QStringLiteral("component.config.unset");
inline const QString componentGraphObjectAdd = QStringLiteral("component.graph_object.add");
inline const QString componentGraphRelationshipAdd = QStringLiteral("component.graph_relationship.add");
inline const QString componentGraphRelationshipRemove = QStringLiteral("component.graph_relationship.remove");
inline const QString connectionAdd = QStringLiteral("connection.add");
inline const QString connectionRemove = QStringLiteral("connection.remove");
inline const QString connectionConfigSet = QStringLiteral("connection.config.set");
inline const QString connectionMetadataSet = QStringLiteral("connection.metadata.set");
inline const QString connectionClassSet = QStringLiteral("connection.class.set");
inline const QString viewLayoutSet = QStringLiteral("view.layout.set");
inline const QString viewNodePositionSet = QStringLiteral("view.node_position.set");
inline const QString topologyAddOrUpdate = QStringLiteral("topology.add_or_update");
inline const QString topologyRemove = QStringLiteral("topology.remove");

} // namespace ipcraft::patchops
