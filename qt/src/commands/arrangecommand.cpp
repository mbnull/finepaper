#include "commands/arrangecommand.h"
#include "graph.h"
#include "module.h"
#include "modulelabels.h"
#include "portlayout.h"
#include <QPoint>
#include <QRect>
#include <QRegularExpression>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>

namespace {

std::optional<double> parameterAsDouble(const Module* module, const QString& name) {
    if (!module) {
        return std::nullopt;
    }

    const auto it = module->parameters().find(name);
    if (it == module->parameters().end()) {
        return std::nullopt;
    }

    const Parameter::Value& value = it.value().value();
    if (const auto* integerValue = std::get_if<int>(&value)) {
        return static_cast<double>(*integerValue);
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return *doubleValue;
    }
    return std::nullopt;
}

QString sortKeyForModule(const Module* module) {
    return ModuleLabels::externalId(module).isEmpty()
        ? ModuleLabels::displayName(module).toLower()
        : ModuleLabels::externalId(module).toLower();
}

struct OrderedModule {
    Module* module = nullptr;
    bool hasPosition = false;
    double x = 0.0;
    double y = 0.0;
    QString key;
};

std::vector<OrderedModule> orderedModules(const std::vector<std::unique_ptr<Module>>& modules,
                                          const QString& type) {
    std::vector<OrderedModule> ordered;
    ordered.reserve(modules.size());

    for (const auto& module : modules) {
        if (module->type() != type) {
            continue;
        }

        const auto x = parameterAsDouble(module.get(), "x");
        const auto y = parameterAsDouble(module.get(), "y");
        ordered.push_back({
            module.get(),
            x.has_value() && y.has_value(),
            x.value_or(0.0),
            y.value_or(0.0),
            sortKeyForModule(module.get())
        });
    }

    std::sort(ordered.begin(), ordered.end(), [](const OrderedModule& lhs, const OrderedModule& rhs) {
        if (lhs.hasPosition != rhs.hasPosition) {
            return lhs.hasPosition;
        }
        if (lhs.hasPosition && rhs.hasPosition) {
            if (!qFuzzyCompare(lhs.y + 1.0, rhs.y + 1.0)) {
                return lhs.y < rhs.y;
            }
            if (!qFuzzyCompare(lhs.x + 1.0, rhs.x + 1.0)) {
                return lhs.x < rhs.x;
            }
        }
        return lhs.key < rhs.key;
    });

    return ordered;
}

std::optional<QPoint> explicitMeshCoordinate(const Module* module) {
    if (!module || module->type() != "XP") {
        return std::nullopt;
    }

    static const QRegularExpression pattern("^xp_(\\d+)_(\\d+)$", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(ModuleLabels::externalId(module));
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    return QPoint(match.captured(2).toInt(), match.captured(1).toInt());
}

std::optional<QPoint> stepForSide(const QString& side) {
    if (side == "north") return QPoint(0, -1);
    if (side == "east") return QPoint(1, 0);
    if (side == "south") return QPoint(0, 1);
    if (side == "west") return QPoint(-1, 0);
    return std::nullopt;
}

struct MeshRelation {
    QString fromId;
    QString toId;
    QPoint delta;
};

QList<MeshRelation> xpMeshRelations(const Graph* graph) {
    QList<MeshRelation> relations;
    if (!graph) {
        return relations;
    }

    for (const auto& connection : graph->connections()) {
        const Module* sourceModule = graph->getModule(connection->source().moduleId);
        const Module* targetModule = graph->getModule(connection->target().moduleId);
        if (!sourceModule || !targetModule) {
            continue;
        }
        if (sourceModule->type() != "XP" || targetModule->type() != "XP") {
            continue;
        }

        const QString sourceSide = PortLayout::routerSideId(connection->source().portId);
        const QString targetSide = PortLayout::routerSideId(connection->target().portId);
        const auto delta = stepForSide(sourceSide);
        if (!delta.has_value() || PortLayout::oppositeRouterSide(sourceSide) != targetSide) {
            continue;
        }

        relations.append({sourceModule->id(), targetModule->id(), *delta});
    }

    return relations;
}

QHash<QString, QPoint> inferMeshCoordinates(const Graph* graph,
                                            const std::vector<OrderedModule>& orderedXpModules) {
    QHash<QString, QPoint> coordinates;
    if (!graph || orderedXpModules.empty()) {
        return coordinates;
    }

    QHash<QString, QList<QPair<QString, QPoint>>> adjacency;
    for (const MeshRelation& relation : xpMeshRelations(graph)) {
        adjacency[relation.fromId].append({relation.toId, relation.delta});
        adjacency[relation.toId].append({relation.fromId, QPoint(-relation.delta.x(), -relation.delta.y())});
    }

    QList<QString> moduleOrder;
    moduleOrder.reserve(static_cast<qsizetype>(orderedXpModules.size()));
    for (const OrderedModule& orderedModule : orderedXpModules) {
        moduleOrder.append(orderedModule.module->id());
        if (const auto explicitCoord = explicitMeshCoordinate(orderedModule.module); explicitCoord.has_value()) {
            coordinates.insert(orderedModule.module->id(), *explicitCoord);
        }
    }

    for (const QString& startId : moduleOrder) {
        if (coordinates.contains(startId)) {
            continue;
        }

        std::queue<QString> frontier;
        frontier.push(startId);
        coordinates.insert(startId, QPoint(0, 0));

        while (!frontier.empty()) {
            const QString currentId = frontier.front();
            frontier.pop();

            const QPoint current = coordinates.value(currentId);
            for (const auto& edge : adjacency.value(currentId)) {
                const QString& nextId = edge.first;
                const QPoint candidate = current + edge.second;
                if (!coordinates.contains(nextId)) {
                    coordinates.insert(nextId, candidate);
                    frontier.push(nextId);
                }
            }
        }
    }

    for (const OrderedModule& orderedModule : orderedXpModules) {
        if (!coordinates.contains(orderedModule.module->id())) {
            coordinates.insert(orderedModule.module->id(), QPoint(0, 0));
        }
    }

    return coordinates;
}

struct MeshComponent {
    QList<Module*> modules;
    QRect bounds;
};

QList<MeshComponent> buildMeshComponents(const std::vector<OrderedModule>& orderedXpModules,
                                         const QHash<QString, QPoint>& meshCoordinates,
                                         const QList<MeshRelation>& relations) {
    QList<MeshComponent> components;
    QSet<QString> visited;
    QHash<QString, QList<QString>> adjacency;

    for (const MeshRelation& relation : relations) {
        adjacency[relation.fromId].append(relation.toId);
        adjacency[relation.toId].append(relation.fromId);
    }

    for (const OrderedModule& orderedModule : orderedXpModules) {
        Module* startModule = orderedModule.module;
        if (!startModule || visited.contains(startModule->id())) {
            continue;
        }

        QList<Module*> componentModules;
        QList<QString> frontier{startModule->id()};
        visited.insert(startModule->id());

        int minX = 0;
        int maxX = 0;
        int minY = 0;
        int maxY = 0;

        while (!frontier.isEmpty()) {
            const QString currentId = frontier.takeFirst();
            Module* currentModule = nullptr;
            for (const OrderedModule& candidate : orderedXpModules) {
                if (candidate.module && candidate.module->id() == currentId) {
                    currentModule = candidate.module;
                    break;
                }
            }
            if (!currentModule) {
                continue;
            }

            componentModules.append(currentModule);

            const QPoint point = meshCoordinates.value(currentId, QPoint(0, 0));
            minX = std::min(minX, point.x());
            maxX = std::max(maxX, point.x());
            minY = std::min(minY, point.y());
            maxY = std::max(maxY, point.y());

            for (const QString& neighborId : adjacency.value(currentId)) {
                if (!visited.contains(neighborId)) {
                    visited.insert(neighborId);
                    frontier.append(neighborId);
                }
            }
        }

        std::sort(componentModules.begin(), componentModules.end(), [](const Module* lhs, const Module* rhs) {
            return sortKeyForModule(lhs) < sortKeyForModule(rhs);
        });

        components.append({componentModules, QRect(minX, minY, (maxX - minX) + 1, (maxY - minY) + 1)});
    }

    std::sort(components.begin(), components.end(), [&](const MeshComponent& lhs, const MeshComponent& rhs) {
        const QString lhsKey = lhs.modules.isEmpty() ? QString() : sortKeyForModule(lhs.modules.front());
        const QString rhsKey = rhs.modules.isEmpty() ? QString() : sortKeyForModule(rhs.modules.front());
        return lhsKey < rhsKey;
    });

    return components;
}

int endpointSlotForModule(const Graph* graph, const QString& endpointModuleId) {
    if (!graph) {
        return -1;
    }

    for (const auto& connection : graph->connections()) {
        if (connection->target().moduleId != endpointModuleId) {
            continue;
        }
        if (!PortLayout::isEndpointPortId(connection->source().portId)) {
            continue;
        }
        return PortLayout::endpointPortSlot(connection->source().portId);
    }

    return -1;
}

QString xpForEndpoint(const Graph* graph, const QString& endpointModuleId) {
    if (!graph) {
        return {};
    }

    for (const auto& connection : graph->connections()) {
        if (connection->target().moduleId == endpointModuleId &&
            PortLayout::isEndpointPortId(connection->source().portId)) {
            return connection->source().moduleId;
        }
    }

    return {};
}

} // namespace

ArrangeCommand::ArrangeCommand(Graph* graph)
    : m_graph(graph) {
}

void ArrangeCommand::execute() {
    if (!m_graph) {
        return;
    }

    if (!m_initialized) {
        initializeSnapshots();
    }

    applyState(m_after);
    m_executed = true;
}

void ArrangeCommand::undo() {
    if (!m_graph || !m_initialized) {
        return;
    }

    applyState(m_before);
}

void ArrangeCommand::initializeSnapshots() {
    captureCurrentState(m_before);

    const auto placements = buildPlacements();
    m_after = m_before;

    for (auto it = placements.begin(); it != placements.end(); ++it) {
        ModuleSnapshot& snapshot = m_after[it.key()];
        snapshot.x.existed = true;
        snapshot.x.value = it.value().x;
        snapshot.y.existed = true;
        snapshot.y.value = it.value().y;

        if (it.value().setCollapsed) {
            snapshot.collapsed.existed = true;
            snapshot.collapsed.value = it.value().collapsed;
        }
    }

    m_initialized = true;
}

void ArrangeCommand::captureCurrentState(QHash<QString, ModuleSnapshot>& snapshots) const {
    snapshots.clear();

    for (const auto& module : m_graph->modules()) {
        ModuleSnapshot snapshot;
        const auto& params = module->parameters();

        if (const auto xIt = params.find("x"); xIt != params.end()) {
            snapshot.x.existed = true;
            snapshot.x.value = xIt.value().value();
        }
        if (const auto yIt = params.find("y"); yIt != params.end()) {
            snapshot.y.existed = true;
            snapshot.y.value = yIt.value().value();
        }
        if (const auto collapsedIt = params.find("collapsed"); collapsedIt != params.end()) {
            snapshot.collapsed.existed = true;
            snapshot.collapsed.value = collapsedIt.value().value();
        }

        snapshots.insert(module->id(), snapshot);
    }
}

void ArrangeCommand::applyState(const QHash<QString, ModuleSnapshot>& snapshots) const {
    for (auto it = snapshots.begin(); it != snapshots.end(); ++it) {
        Module* module = m_graph->getModule(it.key());
        if (!module) {
            continue;
        }

        const ModuleSnapshot& snapshot = it.value();

        if (snapshot.x.existed) {
            module->setParameter("x", snapshot.x.value);
        } else {
            module->removeParameter("x");
        }

        if (snapshot.y.existed) {
            module->setParameter("y", snapshot.y.value);
        } else {
            module->removeParameter("y");
        }

        if (snapshot.collapsed.existed) {
            module->setParameter("collapsed", snapshot.collapsed.value);
        } else {
            module->removeParameter("collapsed");
        }
    }
}

QHash<QString, ArrangeCommand::ModulePlacement> ArrangeCommand::buildPlacements() const {
    QHash<QString, ModulePlacement> placements;

    const auto orderedXpModules = orderedModules(m_graph->modules(), QStringLiteral("XP"));
    const auto orderedEndpointModules = orderedModules(m_graph->modules(), QStringLiteral("Endpoint"));

    constexpr int kXpSpacingX = 220;
    constexpr int kXpSpacingY = 168;
    constexpr int kEndpointOffsetX = 156;
    constexpr int kEndpointHeight = 54;
    constexpr int kXpTopInset = 16;
    constexpr int kXpBottomInset = 16;
    constexpr int kXpExpandedHeight = 116;
    constexpr int kLooseEndpointSpacingX = 168;
    constexpr int kLooseEndpointSpacingY = 84;
    constexpr int kLooseEndpointMarginY = 116;

    QHash<QString, QPoint> xpPositions;
    const QHash<QString, QPoint> meshCoordinates = inferMeshCoordinates(m_graph, orderedXpModules);
    const QList<MeshRelation> relations = xpMeshRelations(m_graph);
    const QList<MeshComponent> components = buildMeshComponents(orderedXpModules, meshCoordinates, relations);

    int currentOriginX = 0;
    int globalMaxY = std::numeric_limits<int>::min();

    for (const MeshComponent& component : components) {
        const int componentWidth = component.bounds.width();
        const int componentHeight = component.bounds.height();
        const int componentOriginX = currentOriginX - (((componentWidth - 1) * kXpSpacingX) / 2);
        const int componentOriginY = -(((componentHeight - 1) * kXpSpacingY) / 2);

        for (Module* module : component.modules) {
            if (!module) {
                continue;
            }

            const QPoint logical = meshCoordinates.value(module->id(), QPoint(0, 0));
            const int localColumn = logical.x() - component.bounds.left();
            const int localRow = logical.y() - component.bounds.top();
            const int x = componentOriginX + (localColumn * kXpSpacingX);
            const int y = componentOriginY + (localRow * kXpSpacingY);

            xpPositions.insert(module->id(), QPoint(x, y));
            placements.insert(module->id(), ModulePlacement{x, y, true, true});
            globalMaxY = std::max(globalMaxY, y);
        }

        currentOriginX += std::max(1, componentWidth) * kXpSpacingX;
        currentOriginX += kXpSpacingX;
    }

    const double endpointStep = static_cast<double>(kXpExpandedHeight - kXpTopInset - kXpBottomInset) /
                                static_cast<double>(PortLayout::kEndpointPortCount);
    QList<Module*> looseEndpoints;

    for (const OrderedModule& orderedEndpoint : orderedEndpointModules) {
        const QString endpointModuleId = orderedEndpoint.module->id();
        const QString xpModuleId = xpForEndpoint(m_graph, endpointModuleId);
        const int endpointSlot = endpointSlotForModule(m_graph, endpointModuleId);

        if (!xpModuleId.isEmpty() && xpPositions.contains(xpModuleId) && endpointSlot >= 0) {
            const QPoint xpPosition = xpPositions.value(xpModuleId);
            const double centerY = static_cast<double>(xpPosition.y()) + kXpTopInset +
                                   (static_cast<double>(endpointSlot) + 0.5) * endpointStep;
            placements.insert(endpointModuleId, ModulePlacement{
                xpPosition.x() - kEndpointOffsetX,
                static_cast<int>(std::lround(centerY - (kEndpointHeight / 2.0))),
                false,
                false
            });
            continue;
        }

        looseEndpoints.append(orderedEndpoint.module);
    }

    const int looseColumns = !looseEndpoints.isEmpty()
        ? std::max(1, std::min(4, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(looseEndpoints.size()))))))
        : 0;
    const int looseOriginX = looseColumns > 0 ? -((looseColumns - 1) * kLooseEndpointSpacingX) / 2 : 0;
    const int looseOriginY = xpPositions.isEmpty()
        ? 0
        : globalMaxY + kLooseEndpointMarginY;

    for (int index = 0; index < looseEndpoints.size(); ++index) {
        const int row = index / looseColumns;
        const int column = index % looseColumns;
        placements.insert(looseEndpoints.at(index)->id(), ModulePlacement{
            looseOriginX + (column * kLooseEndpointSpacingX),
            looseOriginY + (row * kLooseEndpointSpacingY),
            false,
            false
        });
    }

    if (!placements.isEmpty()) {
        int minX = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        for (auto it = placements.cbegin(); it != placements.cend(); ++it) {
            minX = std::min(minX, it.value().x);
            maxX = std::max(maxX, it.value().x);
        }

        const int centerShiftX = (minX + maxX) / 2;
        for (auto it = placements.begin(); it != placements.end(); ++it) {
            it.value().x -= centerShiftX;
        }
    }

    return placements;
}
