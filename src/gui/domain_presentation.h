#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

enum class DomainPresentationStatus {
    Inactive,
    Ready,
    MissingPackageType
};

enum class DomainAssignmentDisplayState {
    Inactive,
    Unavailable,
    NotApplicable,
    Unassigned,
    Assigned,
    Multiple
};

struct DomainElementPresentation {
    DomainAssignmentDisplayState state = DomainAssignmentDisplayState::Inactive;
    QStringList domainIds;
    QVector<QColor> colors;

    [[nodiscard]] QColor primaryColor() const;

    bool operator==(const DomainElementPresentation&) const = default;
};

struct DomainLegendEntry {
    QString id;
    QString name;
    QColor color;
    qsizetype memberCount = 0;
    qsizetype crossingCount = 0;

    bool operator==(const DomainLegendEntry&) const = default;
};

struct DomainCrossingPresentation {
    QStringList fromDomainIds;
    QStringList toDomainIds;
    QVector<QColor> fromColors;
    QVector<QColor> toColors;
    QStringList accentDomainIds;
    QVector<QColor> accentColors;
    QColor primaryAccent;
    std::optional<QString> defaultPolicy;
    QJsonObject defaultProperties;
    std::optional<QString> overridePolicy;
    QJsonObject overrideProperties;

    bool operator==(const DomainCrossingPresentation&) const = default;
};

// This is deliberately detached from QGraphicsItem and QWidget. The editor can
// retain its existing graph, look up each semantic ElementRef, update item data,
// and repaint only items whose presentation value changed.
struct DomainPresentationSnapshot {
    QString activeDomainType;
    QString domainTypeLabel;
    DomainPresentationStatus status = DomainPresentationStatus::Inactive;
    QHash<ElementRef, DomainElementPresentation> elements;
    QHash<ElementRef, DomainCrossingPresentation> crossings;
    QVector<DomainLegendEntry> legend;

    [[nodiscard]] const DomainElementPresentation* element(
        const ElementRef& reference) const;
    [[nodiscard]] const DomainCrossingPresentation* crossing(
        const ElementRef& edge) const;

    bool operator==(const DomainPresentationSnapshot&) const = default;
};

// Uses a local stable hash instead of qHash(), whose seed is process-specific.
// The same (type, id) pair therefore receives the same color across sessions.
[[nodiscard]] QColor domainPresentationColor(
    const QString& domainType,
    const QString& domainId);

[[nodiscard]] DomainPresentationSnapshot buildDomainPresentationSnapshot(
    const ResolvedDesign& resolved,
    const PackageDefinition& package,
    const QString& activeDomainType);

} // namespace finepaper
