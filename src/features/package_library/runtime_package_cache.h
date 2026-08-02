#pragma once

#include "package/package.h"

#include <QSet>
#include <QStringList>

#include <functional>
#include <optional>

namespace finepaper {

enum class RuntimePackageRefreshPolicy {
    IfCatalogChanged,
    Force,
};

struct RuntimePackageRefreshResult final {
    bool probed = false;
    qsizetype availableCount = 0;
    QVector<Diagnostic> diagnostics;
};

struct RuntimePackageProbeResult final {
    bool available = false;
    QVector<Diagnostic> diagnostics;
};

[[nodiscard]] RuntimePackageProbeResult probeRuntimePackage(
    const PackageDefinition& package);

// Runtime availability belongs to an immutable catalog revision. Routine
// design projections can consult this key-only cache without reparsing Package
// descriptors or retaining pointers across catalog replacement.
class RuntimePackageCache final {
public:
    using Probe = std::function<RuntimePackageProbeResult(
        const PackageDefinition&)>;

    explicit RuntimePackageCache(Probe probe = {});

    RuntimePackageRefreshResult synchronize(
        const QVector<PackageDefinition>& packages,
        quint64 catalogRevision,
        RuntimePackageRefreshPolicy policy =
            RuntimePackageRefreshPolicy::IfCatalogChanged);
    RuntimePackageProbeResult synchronizeOne(
        const QVector<PackageDefinition>& packages,
        quint64 catalogRevision,
        const QString& key);

    [[nodiscard]] bool contains(const QString& key) const;
    [[nodiscard]] qsizetype size() const;
    [[nodiscard]] const QStringList& orderedKeys() const;
    [[nodiscard]] quint64 probeGeneration() const;

private:
    RuntimePackageRefreshResult rebuildSnapshot(
        const QVector<PackageDefinition>& packages,
        quint64 catalogRevision,
        const QString& requestedKey = {},
        RuntimePackageProbeResult* requestedResult = nullptr);
    void rebuildOrder(const QVector<PackageDefinition>& packages);

    Probe m_probe;
    std::optional<quint64> m_catalogRevision = std::nullopt;
    QSet<QString> m_availableKeys;
    QStringList m_orderedKeys;
    quint64 m_probeGeneration = 0;
};

} // namespace finepaper
