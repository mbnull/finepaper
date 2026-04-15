// ArrangeCommand computes automatic node placement and stores previous positions for undo.
#include "commands/arrangecommand.h"
#include "graph.h"
#include "module.h"
#include "modulelabels.h"
#include "moduletypemetadata.h"
#include "portlayout.h"
#include <QPoint>
#include <QRect>
#include <QRegularExpression>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <functional>
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

bool isMeshRouterModule(const Module* module) {
    return ModuleTypeMetadata::hasEditorLayout(module, u"mesh_router");
}

bool isEndpointModule(const Module* module) {
    return ModuleTypeMetadata::isInGraphGroup(module, u"endpoints");
}

struct OrderedModule {
    Module* module = nullptr;
    bool hasPosition = false;
    double x = 0.0;
    double y = 0.0;
    QString key;
};

std::vector<OrderedModule> orderedModules(const std::vector<std::unique_ptr<Module>>& modules,
                                          const std::function<bool(const Module*)>& predicate) {
    std::vector<OrderedModule> ordered;
    ordered.reserve(modules.size());

    for (const auto& module : modules) {
        if (!predicate(module.get())) {
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
    if (!module || !isMeshRouterModule(module)) {
        return std::nullopt;
    }

    const QString prefix = ModuleTypeMetadata::externalIdPrefix(module);
    if (prefix.isEmpty() || !ModuleTypeMetadata::supportsMeshCoordinates(module)) {
        return std::nullopt;
    }

    const QRegularExpression pattern("^" + QRegularExpression::escape(prefix) + "_(\\d+)_(\\d+)$",
                                     QRegularExpression::CaseInsensitiveOption);
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

QList<MeshRelation> meshRelations(const Graph* graph) {
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
        if (!isMeshRouterModule(sourceModule) || !isMeshRouterModule(targetModule)) {
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
    for (const MeshRelation& relation : meshRelations(graph)) {
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
        // Compute before/after snapshots once per command instance so redo
        // reapplies identical coordinates.
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
    // Snapshot current mutable placement fields, then overlay computed layout.
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

        // Each field tracks existence explicitly so undo can remove parameters
        // that were absent before arrange.
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

    const auto orderedXpModules = orderedModules(m_graph->modules(), [](const Module* module) {
        return isMeshRouterModule(module);
    });
    const auto orderedEndpointModules = orderedModules(m_graph->modules(), [](const Module* module) {
        return isEndpointModule(module);
    });

    Module* spacingSourceXp = orderedXpModules.empty() ? nullptr : orderedXpModules.front().module;
    Module* spacingSourceEndpoint = orderedEndpointModules.empty() ? nullptr : orderedEndpointModules.front().module;
    const int meshSpacingX = ModuleTypeMetadata::meshSpacingX(spacingSourceXp);
    const int meshSpacingY = ModuleTypeMetadata::meshSpacingY(spacingSourceXp);
    const int looseEndpointSpacingX = ModuleTypeMetadata::looseEndpointSpacingX(spacingSourceEndpoint);
    const int looseEndpointSpacingY = ModuleTypeMetadata::looseEndpointSpacingY(spacingSourceEndpoint);
    const int looseEndpointMarginY = ModuleTypeMetadata::looseEndpointMarginY(spacingSourceEndpoint);

    QHash<QString, QPoint> xpPositions;
    // First infer logical mesh coordinates from explicit IDs + router links,
    // then convert logical grid points into pixel positions.
    const QHash<QString, QPoint> meshCoordinates = inferMeshCoordinates(m_graph, orderedXpModules);
    const QList<MeshRelation> relations = meshRelations(m_graph);
    const QList<MeshComponent> components = buildMeshComponents(orderedXpModules, meshCoordinates, relations);

    int currentOriginX = 0;
    int globalMaxY = std::numeric_limits<int>::min();

    // Lay out disconnected mesh components side-by-side to avoid overlap.
    for (const MeshComponent& component : components) {
        const int componentWidth = component.bounds.width();
        const int componentHeight = component.bounds.height();
        const int componentOriginX = currentOriginX - (((componentWidth - 1) * meshSpacingX) / 2);
        const int componentOriginY = -(((componentHeight - 1) * meshSpacingY) / 2);

        for (Module* module : component.modules) {
            if (!module) {
                continue;
            }

            const QPoint logical = meshCoordinates.value(module->id(), QPoint(0, 0));
            const int localColumn = logical.x() - component.bounds.left();
            const int localRow = logical.y() - component.bounds.top();
            const int x = componentOriginX + (localColumn * meshSpacingX);
            const int y = componentOriginY + (localRow * meshSpacingY);

            xpPositions.insert(module->id(), QPoint(x, y));
            placements.insert(module->id(), ModulePlacement{x, y, true, true});
            globalMaxY = std::max(globalMaxY, y);
        }

        currentOriginX += std::max(1, componentWidth) * meshSpacingX;
        currentOriginX += meshSpacingX;
    }
    QList<Module*> looseEndpoints;

    for (const OrderedModule& orderedEndpoint : orderedEndpointModules) {
        const QString endpointModuleId = orderedEndpoint.module->id();
        const QString xpModuleId = xpForEndpoint(m_graph, endpointModuleId);
        const int endpointSlot = endpointSlotForModule(m_graph, endpointModuleId);

        if (!xpModuleId.isEmpty() && xpPositions.contains(xpModuleId) && endpointSlot >= 0) {
            // Attached endpoints follow their host router's endpoint slot geometry.
            const QPoint xpPosition = xpPositions.value(xpModuleId);
            const Module* xpModule = m_graph->getModule(xpModuleId);
            const qreal xpPortInset = ModuleTypeMetadata::expandedPortInset(xpModule);
            const qreal xpHeight = ModuleTypeMetadata::expandedNodeHeight(xpModule);
            const qreal usableHeight = xpHeight - (xpPortInset * 2.0);
            const qreal endpointStep = usableHeight / static_cast<qreal>(PortLayout::kEndpointPortCount);
            const qreal centerY = static_cast<qreal>(xpPosition.y()) + xpPortInset +
                                  (static_cast<qreal>(endpointSlot) + 0.5) * endpointStep;
            const int endpointHeight = ModuleTypeMetadata::expandedNodeHeight(orderedEndpoint.module);
            const int endpointOffsetX = ModuleTypeMetadata::linkedEndpointOffsetX(xpModule);
            placements.insert(endpointModuleId, ModulePlacement{
                xpPosition.x() - endpointOffsetX,
                static_cast<int>(std::lround(centerY - (static_cast<qreal>(endpointHeight) / 2.0))),
                false,
                false
            });
            continue;
        }

        looseEndpoints.append(orderedEndpoint.module);
    }

    // Endpoints without an attachment are packed into a compact grid below meshes.
    const int looseColumns = !looseEndpoints.isEmpty()
        ? std::max(1, std::min(4, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(looseEndpoints.size()))))))
        : 0;
    const int looseOriginX = looseColumns > 0 ? -((looseColumns - 1) * looseEndpointSpacingX) / 2 : 0;
    const int looseOriginY = xpPositions.isEmpty()
        ? 0
        : globalMaxY + looseEndpointMarginY;

    for (int index = 0; index < looseEndpoints.size(); ++index) {
        const int row = index / looseColumns;
        const int column = index % looseColumns;
        placements.insert(looseEndpoints.at(index)->id(), ModulePlacement{
            looseOriginX + (column * looseEndpointSpacingX),
            looseOriginY + (row * looseEndpointSpacingY),
            false,
            false
        });
    }

    if (!placements.isEmpty()) {
        // Center final result around x=0 so large topologies stay in view after zoom-fit.
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
