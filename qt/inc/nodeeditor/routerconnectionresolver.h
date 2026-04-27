// RouterConnectionResolver derives logical router links from node positions.
#pragma once

#include "common/portlayout.h"
#include "graph/module.h"
#include "graph/connection.h"
#include <QPointF>
#include <QString>
#include <cmath>
#include <optional>

namespace RouterConnectionResolver {

struct ResolvedRouterConnection {
    PortRef source;
    PortRef target;
};

inline const Port* findPort(const Module* module, const QString& portId) {
    if (!module) {
        return nullptr;
    }

    for (const Port& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }

    return nullptr;
}

inline QString portForSide(const Module* module, const QString& side, Port::Direction direction) {
    if (!module || side.isEmpty()) {
        return {};
    }

    const auto supportsDirection = [direction](const Port& port) {
        return direction == Port::Direction::Output ? PortLayout::supportsOutput(port)
                                                    : PortLayout::supportsInput(port);
    };

    if (const Port* port = findPort(module, side);
        port && PortLayout::isRouterPort(*port) && supportsDirection(*port)) {
        return port->id();
    }

    const QString legacyPortId = direction == Port::Direction::Output
        ? PortLayout::routerOutputPortId(side)
        : PortLayout::routerInputPortId(side);
    if (const Port* port = findPort(module, legacyPortId);
        port && PortLayout::isRouterPort(*port) && supportsDirection(*port)) {
        return port->id();
    }

    return {};
}

inline QString sideFromPosition(const QPointF& from, const QPointF& to) {
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();

    if (std::abs(dx) >= std::abs(dy)) {
        return dx >= 0.0 ? QStringLiteral("east") : QStringLiteral("west");
    }

    return dy >= 0.0 ? QStringLiteral("south") : QStringLiteral("north");
}

inline std::optional<ResolvedRouterConnection> resolveByPosition(const Module* startModule,
                                                                 const Module* targetModule,
                                                                 const QString& startPortId,
                                                                 const QPointF& startPosition,
                                                                 const QPointF& targetPosition) {
    if (!startModule || !targetModule || startModule->id() == targetModule->id()) {
        return std::nullopt;
    }

    const Port* startPort = findPort(startModule, startPortId);
    if (!startPort || !PortLayout::isRouterPort(*startPort)) {
        return std::nullopt;
    }

    const QString side = sideFromPosition(startPosition, targetPosition);
    const QString oppositeSide = PortLayout::oppositeRouterSide(side);
    if (side.isEmpty() || oppositeSide.isEmpty()) {
        return std::nullopt;
    }

    const QString startOutput = portForSide(startModule, side, Port::Direction::Output);
    const QString targetInput = portForSide(targetModule, oppositeSide, Port::Direction::Input);
    if (!startOutput.isEmpty() && !targetInput.isEmpty()) {
        return ResolvedRouterConnection{
            PortRef{startModule->id(), startOutput},
            PortRef{targetModule->id(), targetInput}
        };
    }

    const QString targetOutput = portForSide(targetModule, oppositeSide, Port::Direction::Output);
    const QString startInput = portForSide(startModule, side, Port::Direction::Input);
    if (!targetOutput.isEmpty() && !startInput.isEmpty()) {
        return ResolvedRouterConnection{
            PortRef{targetModule->id(), targetOutput},
            PortRef{startModule->id(), startInput}
        };
    }

    return std::nullopt;
}

} // namespace RouterConnectionResolver
