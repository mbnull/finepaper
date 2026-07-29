#include "noc/model.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace finepaper {
namespace {

QString routerKey(RouterPosition position) {
    return QStringLiteral("%1,%2").arg(position.x).arg(position.y);
}

void appendError(QVector<Diagnostic>& diagnostics,
                 const QString& code,
                 const QString& message,
                 const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("finepaper")
    });
}

} // namespace

bool hasErrors(const QVector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == QStringLiteral("error");
    });
}

QString routerId(RouterPosition position) {
    return QStringLiteral("r-%1-%2").arg(position.x).arg(position.y);
}

QString linkId(const QString& fromRouter, const QString& toRouter) {
    return QStringLiteral("link-%1--%2").arg(fromRouter, toRouter);
}

NocDesign withResolvedAutomaticSlots(const NocDesign& design) {
    NocDesign resolved = design;
    QHash<QString, QVector<qsizetype>> byRouter;
    for (qsizetype index = 0; index < resolved.endpoints.size(); ++index) {
        byRouter[routerKey(resolved.endpoints.at(index).attachment.router)].append(index);
    }

    for (auto it = byRouter.begin(); it != byRouter.end(); ++it) {
        QVector<qsizetype>& indices = it.value();
        std::sort(indices.begin(), indices.end(), [&](qsizetype lhs, qsizetype rhs) {
            return resolved.endpoints.at(lhs).id < resolved.endpoints.at(rhs).id;
        });

        QSet<QString> usedSlots;
        for (qsizetype index : std::as_const(indices)) {
            const auto& slot = resolved.endpoints.at(index).attachment.slot;
            if (slot && !slot->isEmpty()) {
                usedSlots.insert(*slot);
            }
        }

        int nextSlot = 0;
        for (qsizetype index : std::as_const(indices)) {
            EndpointInstance& endpoint = resolved.endpoints[index];
            if (endpoint.attachment.slot && !endpoint.attachment.slot->isEmpty()) {
                continue;
            }
            while (usedSlots.contains(QString::number(nextSlot))) {
                ++nextSlot;
            }
            endpoint.attachment.slot = QString::number(nextSlot);
            usedSlots.insert(QString::number(nextSlot));
            ++nextSlot;
        }
    }
    return resolved;
}

TopologyProjection projectTopology(const NocDesign& design) {
    TopologyProjection projection;
    if (design.topology.type != QStringLiteral("mesh") ||
        design.topology.rows <= 0 ||
        design.topology.columns <= 0 ||
        design.topology.rows > kMaximumMeshDimension ||
        design.topology.columns > kMaximumMeshDimension) {
        return projection;
    }

    const qint64 routerCount = static_cast<qint64>(design.topology.rows)
        * static_cast<qint64>(design.topology.columns);
    if (routerCount > kMaximumProjectedRouterCount) {
        return projection;
    }

    projection.routers.reserve(static_cast<qsizetype>(routerCount));
    for (int y = 0; y < design.topology.rows; ++y) {
        for (int x = 0; x < design.topology.columns; ++x) {
            const RouterPosition current{x, y};
            const QString currentId = routerId(current);
            projection.routers.append(RouterView{currentId, current});

            if (x + 1 < design.topology.columns) {
                const QString eastId = routerId(RouterPosition{x + 1, y});
                projection.links.append(LinkView{
                    linkId(currentId, eastId),
                    currentId,
                    eastId
                });
            }
            if (y + 1 < design.topology.rows) {
                const QString southId = routerId(RouterPosition{x, y + 1});
                projection.links.append(LinkView{
                    linkId(currentId, southId),
                    currentId,
                    southId
                });
            }
        }
    }

    const NocDesign resolved = withResolvedAutomaticSlots(design);
    projection.endpoints.reserve(resolved.endpoints.size());
    for (const EndpointInstance& endpoint : resolved.endpoints) {
        projection.endpoints.append(EndpointView{
            endpoint.id,
            endpoint.type,
            endpoint.attachment.router,
            routerId(endpoint.attachment.router),
            endpoint.attachment.slot.value_or(QString())
        });
    }
    return projection;
}

QVector<Diagnostic> validateDesignStructure(const NocDesign& design) {
    QVector<Diagnostic> diagnostics;
    if (design.format != QStringLiteral("finepaper.noc-design")) {
        appendError(diagnostics,
                    QStringLiteral("design.unsupported_format"),
                    QStringLiteral("format must be finepaper.noc-design"),
                    QStringLiteral("/format"));
    }
    if (design.formatVersion != 1) {
        appendError(diagnostics,
                    QStringLiteral("design.unsupported_version"),
                    QStringLiteral("formatVersion must be 1"),
                    QStringLiteral("/formatVersion"));
    }
    if (design.id.trimmed().isEmpty()) {
        appendError(diagnostics,
                    QStringLiteral("design.missing_id"),
                    QStringLiteral("design id is required"),
                    QStringLiteral("/id"));
    }
    if (design.name.trimmed().isEmpty()) {
        appendError(diagnostics,
                    QStringLiteral("design.missing_name"),
                    QStringLiteral("design name is required"),
                    QStringLiteral("/name"));
    }
    if (design.package.id.trimmed().isEmpty() || design.package.version.trimmed().isEmpty()) {
        appendError(diagnostics,
                    QStringLiteral("design.missing_package"),
                    QStringLiteral("package id and version are required"),
                    QStringLiteral("/package"));
    }
    if (design.topology.type != QStringLiteral("mesh")) {
        appendError(diagnostics,
                    QStringLiteral("topology.unsupported_type"),
                    QStringLiteral("Mesh V1 only supports topology.type=mesh"),
                    QStringLiteral("/topology/type"));
    }
    if (design.topology.rows <= 0) {
        appendError(diagnostics,
                    QStringLiteral("topology.invalid_rows"),
                    QStringLiteral("rows must be greater than zero"),
                    QStringLiteral("/topology/rows"));
    }
    if (design.topology.columns <= 0) {
        appendError(diagnostics,
                    QStringLiteral("topology.invalid_columns"),
                    QStringLiteral("columns must be greater than zero"),
                    QStringLiteral("/topology/columns"));
    }
    if (design.topology.rows > kMaximumMeshDimension ||
        design.topology.columns > kMaximumMeshDimension ||
        (design.topology.rows > 0 && design.topology.columns > 0 &&
         static_cast<qint64>(design.topology.rows)
                 * static_cast<qint64>(design.topology.columns)
             > kMaximumProjectedRouterCount)) {
        appendError(diagnostics,
                    QStringLiteral("topology.projection_too_large"),
                    QStringLiteral("topology exceeds Finepaper's safe projection limit"),
                    QStringLiteral("/topology"));
    }

    QSet<QString> endpointIds;
    for (qsizetype index = 0; index < design.endpoints.size(); ++index) {
        const EndpointInstance& endpoint = design.endpoints.at(index);
        const QString base = QStringLiteral("/endpoints/%1").arg(index);
        if (endpoint.id.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.missing_id"),
                        QStringLiteral("endpoint id is required"),
                        base + QStringLiteral("/id"));
        } else if (endpointIds.contains(endpoint.id)) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.duplicate_id"),
                        QStringLiteral("endpoint id is duplicated"),
                        base + QStringLiteral("/id"));
        } else {
            endpointIds.insert(endpoint.id);
        }
        if (endpoint.type.trimmed().isEmpty()) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.missing_type"),
                        QStringLiteral("endpoint type is required"),
                        base + QStringLiteral("/type"));
        }
        const RouterPosition position = endpoint.attachment.router;
        if (position.x < 0 || position.x >= design.topology.columns ||
            position.y < 0 || position.y >= design.topology.rows) {
            appendError(diagnostics,
                        QStringLiteral("endpoint.router_out_of_range"),
                        QStringLiteral("endpoint router coordinate is outside the Mesh"),
                        base + QStringLiteral("/attachment/router"));
        }
    }
    return diagnostics;
}

} // namespace finepaper
