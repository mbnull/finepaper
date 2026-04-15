// PortLayout provides utilities for port naming, positioning, and identification
#pragma once

#include "graph/port.h"
#include <QString>
#include <QRegularExpression>
#include <QStringList>

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

inline QString normalizedType(const Port& port) {
    const QString typedValue = !port.busType().isEmpty() ? port.busType() : port.type();
    return typedValue.trimmed().toLower();
}

inline bool supportsInput(const Port& port) {
    return port.direction() == Port::Direction::Input || port.direction() == Port::Direction::InOut;
}

inline bool supportsOutput(const Port& port) {
    return port.direction() == Port::Direction::Output || port.direction() == Port::Direction::InOut;
}

inline QString directionLabel(const Port& port) {
    if (port.direction() == Port::Direction::Input) return QStringLiteral("input");
    if (port.direction() == Port::Direction::Output) return QStringLiteral("output");
    return QStringLiteral("inout");
}

inline bool sameBusFamily(const Port& lhs, const Port& rhs) {
    return normalizedType(lhs) == normalizedType(rhs);
}

inline bool isEndpointPort(const Port& port) {
    return port.type() == QStringLiteral("endpoint") ||
           port.role() == QStringLiteral("attachment") ||
           port.role() == QStringLiteral("endpoint") ||
           isEndpointPortId(port.id());
}

inline bool isRouterPort(const Port& port) {
    return port.role() == QStringLiteral("router") || isDirectionalRouterPortId(port.id());
}

inline QStringList hintTokens(const Port& port) {
    const QString combined = (port.id() + " " + port.name() + " " + port.description()).toLower();
    return combined.split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
}

inline bool containsHintToken(const QStringList& tokens,
                              const QString& longName,
                              const QString& shortName) {
    return tokens.contains(longName) || tokens.contains(shortName);
}

inline QString hintedSide(const Port& port) {
    const QStringList tokens = hintTokens(port);
    if (containsHintToken(tokens, QStringLiteral("north"), QStringLiteral("n")) ||
        tokens.contains(QStringLiteral("top")) || tokens.contains(QStringLiteral("up"))) {
        return QStringLiteral("north");
    }
    if (containsHintToken(tokens, QStringLiteral("east"), QStringLiteral("e")) ||
        tokens.contains(QStringLiteral("right"))) {
        return QStringLiteral("east");
    }
    if (containsHintToken(tokens, QStringLiteral("south"), QStringLiteral("s")) ||
        tokens.contains(QStringLiteral("bottom")) || tokens.contains(QStringLiteral("down"))) {
        return QStringLiteral("south");
    }
    if (containsHintToken(tokens, QStringLiteral("west"), QStringLiteral("w")) ||
        tokens.contains(QStringLiteral("left"))) {
        return QStringLiteral("west");
    }
    return {};
}

inline QString fallbackSide(const Port& port) {
    const QString hinted = hintedSide(port);
    if (!hinted.isEmpty()) {
        return hinted;
    }

    return port.direction() == Port::Direction::Input
        ? QStringLiteral("west")
        : QStringLiteral("east");
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
