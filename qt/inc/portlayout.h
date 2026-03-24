// PortLayout provides utilities for port naming, positioning, and identification
#pragma once

#include "port.h"
#include <QString>

namespace PortLayout {

inline constexpr int kEndpointPortCount = 4;
inline constexpr int kRouterPortCount = 4;

inline QString routerSideId(const QString& portId) {
    if (portId.endsWith("_in")) return portId.left(portId.size() - 3);
    if (portId.endsWith("_out")) return portId.left(portId.size() - 4);
    return portId;
}

inline bool isDirectionalRouterPortId(const QString& portId) {
    const QString side = routerSideId(portId);
    return side == "north" || side == "east" || side == "south" || side == "west";
}

inline bool isEndpointPortId(const QString& portId) {
    return portId.startsWith("ep");
}

inline bool isEndpointPort(const Port& port) {
    return port.type() == "endpoint" || isEndpointPortId(port.id());
}

inline bool isRouterPort(const Port& port) {
    return port.type() == "router" || isDirectionalRouterPortId(port.id());
}

inline int endpointPortSlot(const QString& portId) {
    if (!isEndpointPortId(portId)) return 0;

    bool ok = false;
    int slot = portId.mid(2).toInt(&ok);
    return ok ? slot : 0;
}

inline int routerPortSlot(const QString& portId) {
    const QString side = routerSideId(portId);
    if (side == "north") return 0;
    if (side == "east") return 1;
    if (side == "south") return 2;
    if (side == "west") return 3;
    return 0;
}

inline QString oppositeRouterSide(const QString& side) {
    if (side == "north") return "south";
    if (side == "south") return "north";
    if (side == "east") return "west";
    if (side == "west") return "east";
    return {};
}

inline QString routerInputPortId(const QString& side) {
    return side + "_in";
}

inline QString routerOutputPortId(const QString& side) {
    return side + "_out";
}

} // namespace PortLayout
