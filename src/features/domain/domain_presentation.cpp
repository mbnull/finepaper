#include "features/domain/domain_presentation.h"

#include <QSet>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace finepaper {
namespace {

QStringList normalizedDomainIds(QStringList ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::uint64_t stableDomainHash(const QString& domainType,
                               const QString& domainId) {
    constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offsetBasis;

    const auto mixByte = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= prime;
    };
    const auto mixLength = [&](qsizetype length) {
        const std::uint64_t value = static_cast<std::uint64_t>(length);
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            mixByte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    const auto mixString = [&](const QString& value) {
        mixLength(value.size());
        const ushort* codeUnits = value.utf16();
        for (qsizetype index = 0; index < value.size(); ++index) {
            const std::uint16_t unicode = codeUnits[index];
            mixByte(static_cast<std::uint8_t>(unicode & 0xffU));
            mixByte(static_cast<std::uint8_t>((unicode >> 8) & 0xffU));
        }
    };

    mixString(domainType);
    mixString(domainId);
    return hash;
}

struct DomainPatternDefinition {
    Qt::BrushStyle style;
    const char* label;
};

constexpr std::array<DomainPatternDefinition, 7> domainPatternDefinitions{{
    {Qt::BDiagPattern, "backward diagonal"},
    {Qt::FDiagPattern, "forward diagonal"},
    {Qt::DiagCrossPattern, "diagonal cross"},
    {Qt::HorPattern, "horizontal lines"},
    {Qt::VerPattern, "vertical lines"},
    {Qt::CrossPattern, "crosshatch"},
    {Qt::Dense6Pattern, "dense dots"},
}};

const DomainPatternDefinition& domainPatternDefinition(
    const QString& domainId) {
    const std::uint64_t hash = stableDomainHash(
        QStringLiteral("marker"), domainId);
    return domainPatternDefinitions.at(hash % domainPatternDefinitions.size());
}

DomainAssignmentDisplayState displayState(
    DomainPresentationStatus status,
    const DomainTypeDefinition* type,
    ElementKind kind,
    qsizetype assignmentCount) {
    if (status == DomainPresentationStatus::Inactive) {
        return DomainAssignmentDisplayState::Inactive;
    }
    if (status == DomainPresentationStatus::MissingPackageType || !type) {
        return DomainAssignmentDisplayState::Unavailable;
    }
    if (!type->assignmentRule(kind)) {
        return DomainAssignmentDisplayState::NotApplicable;
    }
    if (assignmentCount == 0) {
        return DomainAssignmentDisplayState::Unassigned;
    }
    if (assignmentCount == 1) {
        return DomainAssignmentDisplayState::Assigned;
    }
    return DomainAssignmentDisplayState::Multiple;
}

} // namespace

QColor DomainElementPresentation::primaryColor() const {
    return colors.size() == 1 ? colors.front() : QColor{};
}

const DomainElementPresentation* DomainPresentationSnapshot::element(
    const ElementRef& reference) const {
    const auto iterator = elements.constFind(reference);
    return iterator == elements.constEnd() ? nullptr : &iterator.value();
}

const DomainCrossingPresentation* DomainPresentationSnapshot::crossing(
    const ElementRef& edge) const {
    const auto iterator = crossings.constFind(edge);
    return iterator == crossings.constEnd() ? nullptr : &iterator.value();
}

QColor domainPresentationColor(const QString& domainType,
                               const QString& domainId) {
    const std::uint64_t hash = stableDomainHash(domainType, domainId);
    const int hue = static_cast<int>(hash % 360U);
    const int saturation = 166 + static_cast<int>((hash >> 12) % 35U);
    const int lightness = 112 + static_cast<int>((hash >> 24) % 27U);
    return QColor::fromHsl(hue, saturation, lightness);
}

Qt::BrushStyle domainPresentationPattern(const QString& domainId) {
    return domainPatternDefinition(domainId).style;
}

QString domainPresentationPatternLabel(const QString& domainId) {
    return QString::fromLatin1(domainPatternDefinition(domainId).label);
}

DomainPresentationSnapshot buildDomainPresentationSnapshot(
    const ResolvedDesign& resolved,
    const PackageDefinition& package,
    const QString& activeDomainType) {
    DomainPresentationSnapshot snapshot;
    snapshot.activeDomainType = activeDomainType;

    const DomainTypeDefinition* type = nullptr;
    if (activeDomainType.isEmpty()) {
        snapshot.status = DomainPresentationStatus::Inactive;
    } else if ((type = package.domainType(activeDomainType))) {
        snapshot.status = DomainPresentationStatus::Ready;
        snapshot.domainTypeLabel = type->label.isEmpty()
            ? activeDomainType : type->label;
    } else {
        snapshot.status = DomainPresentationStatus::MissingPackageType;
        snapshot.domainTypeLabel = activeDomainType;
    }

    QHash<ElementRef, QStringList> assignmentsByElement;
    QHash<QString, QString> namesByDomainId;
    QSet<QString> legendDomainIds;
    QHash<QString, QSet<ElementRef>> membersByDomainId;
    QHash<QString, qsizetype> crossingCountsByDomainId;

    if (!activeDomainType.isEmpty()) {
        for (const DomainDefinition& domain : resolved.design.domains) {
            if (domain.type != activeDomainType) {
                continue;
            }
            legendDomainIds.insert(domain.id);
            if (!namesByDomainId.contains(domain.id)) {
                namesByDomainId.insert(
                    domain.id, domain.name.isEmpty() ? domain.id : domain.name);
            }
        }

        for (const DomainMembership& membership
             : resolved.design.domainMemberships) {
            const auto assignment = membership.assignments.constFind(
                activeDomainType);
            if (assignment == membership.assignments.constEnd()) {
                continue;
            }
            QStringList& combined = assignmentsByElement[membership.element];
            combined.append(assignment.value());
        }

        for (auto iterator = assignmentsByElement.begin();
             iterator != assignmentsByElement.end(); ++iterator) {
            iterator.value() = normalizedDomainIds(std::move(iterator.value()));
            for (const QString& domainId : std::as_const(iterator.value())) {
                legendDomainIds.insert(domainId);
                membersByDomainId[domainId].insert(iterator.key());
            }
        }

        for (const DomainCrossingView& crossing : resolved.domainCrossings) {
            if (crossing.domainType != activeDomainType) {
                continue;
            }
            const QStringList fromIds = normalizedDomainIds(crossing.fromDomains);
            const QStringList toIds = normalizedDomainIds(crossing.toDomains);
            QSet<QString> fromSet;
            QSet<QString> toSet;
            for (const QString& domainId : fromIds) {
                fromSet.insert(domainId);
                legendDomainIds.insert(domainId);
            }
            for (const QString& domainId : toIds) {
                toSet.insert(domainId);
                legendDomainIds.insert(domainId);
            }
            QSet<QString> changedDomains = fromSet;
            changedDomains.unite(toSet);
            QStringList accentIds;
            for (const QString& domainId : std::as_const(changedDomains)) {
                if (fromSet.contains(domainId) != toSet.contains(domainId)) {
                    accentIds.append(domainId);
                    ++crossingCountsByDomainId[domainId];
                }
            }
            accentIds = normalizedDomainIds(std::move(accentIds));

            DomainCrossingPresentation presentation;
            presentation.fromDomainIds = fromIds;
            presentation.toDomainIds = toIds;
            presentation.accentDomainIds = accentIds;
            presentation.fromColors.reserve(fromIds.size());
            presentation.toColors.reserve(toIds.size());
            presentation.accentColors.reserve(accentIds.size());
            for (const QString& domainId : fromIds) {
                presentation.fromColors.append(
                    domainPresentationColor(activeDomainType, domainId));
            }
            for (const QString& domainId : toIds) {
                presentation.toColors.append(
                    domainPresentationColor(activeDomainType, domainId));
            }
            for (const QString& domainId : accentIds) {
                presentation.accentColors.append(
                    domainPresentationColor(activeDomainType, domainId));
            }
            if (!presentation.accentColors.isEmpty()) {
                presentation.primaryAccent = presentation.accentColors.front();
            }
            presentation.defaultPolicy = crossing.defaultPolicy;
            presentation.defaultProperties = crossing.defaultProperties;
            presentation.overridePolicy = crossing.overridePolicy;
            presentation.overrideProperties = crossing.overrideProperties;
            snapshot.crossings.insert(crossing.edge, std::move(presentation));
        }
    }

    const auto appendElement = [&](const ElementRef& reference) {
        DomainElementPresentation presentation;
        presentation.domainIds = assignmentsByElement.value(reference);
        presentation.colors.reserve(presentation.domainIds.size());
        for (const QString& domainId : std::as_const(presentation.domainIds)) {
            presentation.colors.append(
                domainPresentationColor(activeDomainType, domainId));
        }
        presentation.state = displayState(
            snapshot.status, type, reference.kind, presentation.domainIds.size());
        snapshot.elements.insert(reference, std::move(presentation));
    };

    snapshot.elements.reserve(
        resolved.topology.routers.size() + resolved.topology.endpoints.size());
    for (const RouterView& router : resolved.topology.routers) {
        appendElement(ElementRef{ElementKind::Router, router.id});
    }
    for (const EndpointView& endpoint : resolved.topology.endpoints) {
        appendElement(ElementRef{ElementKind::Endpoint, endpoint.id});
    }

    QStringList sortedDomainIds = legendDomainIds.values();
    std::sort(sortedDomainIds.begin(), sortedDomainIds.end());
    snapshot.legend.reserve(sortedDomainIds.size());
    for (const QString& domainId : std::as_const(sortedDomainIds)) {
        snapshot.legend.append(DomainLegendEntry{
            domainId,
            namesByDomainId.value(domainId, domainId),
            domainPresentationColor(activeDomainType, domainId),
            membersByDomainId.value(domainId).size(),
            crossingCountsByDomainId.value(domainId)
        });
    }

    return snapshot;
}

} // namespace finepaper
