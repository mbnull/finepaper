#include "features/package_library/runtime_package_cache.h"

#include "package/package_root_identity.h"

#include <QVersionNumber>

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

Diagnostic runtimeDiagnostic(const QString& code,
                             const QString& message,
                             const QString& path) {
    return Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("package-runtime")};
}

RuntimePackageProbeResult missingPackageResult(const QString& key) {
    RuntimePackageProbeResult result;
    result.diagnostics.append(runtimeDiagnostic(
        QStringLiteral("package.runtime_not_in_catalog"),
        QStringLiteral("Package %1 is not present in the active catalog.")
            .arg(key),
        key));
    return result;
}

bool isNumericIdentifier(const QString& value) {
    return !value.isEmpty()
        && std::all_of(
            value.cbegin(), value.cend(), [](const auto& character) {
                const char16_t codeUnit = character.unicode();
                return codeUnit >= u'0' && codeUnit <= u'9';
            });
}

bool isSemanticNumericIdentifier(const QString& value) {
    return isNumericIdentifier(value)
        && (value.size() == 1 || value.front() != QLatin1Char('0'));
}

bool isSemanticIdentifier(const QString& value) {
    return !value.isEmpty()
        && std::all_of(
            value.cbegin(), value.cend(), [](const auto& character) {
                const char16_t codeUnit = character.unicode();
                return (codeUnit >= u'0' && codeUnit <= u'9')
                    || (codeUnit >= u'A' && codeUnit <= u'Z')
                    || (codeUnit >= u'a' && codeUnit <= u'z')
                    || codeUnit == u'-';
            });
}

bool isSemanticPrereleaseIdentifier(const QString& value) {
    return isSemanticIdentifier(value)
        && (!isNumericIdentifier(value)
            || isSemanticNumericIdentifier(value));
}

QString normalizedNumericIdentifier(QString value) {
    qsizetype firstNonZero = 0;
    while (firstNonZero + 1 < value.size()
           && value.at(firstNonZero) == QLatin1Char('0')) {
        ++firstNonZero;
    }
    return value.sliced(firstNonZero);
}

int compareNumericIdentifiers(const QString& lhs, const QString& rhs) {
    const QString normalizedLhs = normalizedNumericIdentifier(lhs);
    const QString normalizedRhs = normalizedNumericIdentifier(rhs);
    if (normalizedLhs.size() != normalizedRhs.size()) {
        return normalizedLhs.size() < normalizedRhs.size() ? -1 : 1;
    }
    return QString::compare(normalizedLhs, normalizedRhs, Qt::CaseSensitive);
}

struct SemanticVersion final {
    QStringList core;
    QStringList prerelease;
    bool hasBuildMetadata = false;
    bool valid = false;
};

SemanticVersion parseSemanticVersion(const QString& value) {
    SemanticVersion parsed;
    QString precedence = value;
    const qsizetype buildSeparator = precedence.indexOf(QLatin1Char('+'));
    if (buildSeparator >= 0) {
        parsed.hasBuildMetadata = true;
        const QStringList build = precedence.sliced(buildSeparator + 1)
                                      .split(QLatin1Char('.'));
        if (build.isEmpty()
            || !std::all_of(
                build.cbegin(), build.cend(), isSemanticIdentifier)) {
            return parsed;
        }
        precedence.truncate(buildSeparator);
    }

    const qsizetype prereleaseSeparator =
        precedence.indexOf(QLatin1Char('-'));
    if (prereleaseSeparator >= 0) {
        parsed.prerelease = precedence.sliced(prereleaseSeparator + 1)
                                .split(QLatin1Char('.'));
        if (parsed.prerelease.isEmpty()
            || !std::all_of(
                parsed.prerelease.cbegin(),
                parsed.prerelease.cend(),
                isSemanticPrereleaseIdentifier)) {
            return parsed;
        }
        precedence.truncate(prereleaseSeparator);
    }

    parsed.core = precedence.split(QLatin1Char('.'));
    if (parsed.core.size() != 3
        || !std::all_of(
            parsed.core.cbegin(),
            parsed.core.cend(),
            isSemanticNumericIdentifier)) {
        return parsed;
    }
    parsed.valid = true;
    return parsed;
}

int compareSemanticVersions(const SemanticVersion& lhs,
                            const SemanticVersion& rhs) {
    for (qsizetype index = 0; index < lhs.core.size(); ++index) {
        const int order = compareNumericIdentifiers(
            lhs.core.at(index), rhs.core.at(index));
        if (order != 0) {
            return order;
        }
    }
    if (lhs.prerelease.isEmpty() != rhs.prerelease.isEmpty()) {
        return lhs.prerelease.isEmpty() ? 1 : -1;
    }
    const qsizetype sharedSize = (std::min)(
        lhs.prerelease.size(), rhs.prerelease.size());
    for (qsizetype index = 0; index < sharedSize; ++index) {
        const QString& lhsIdentifier = lhs.prerelease.at(index);
        const QString& rhsIdentifier = rhs.prerelease.at(index);
        const bool lhsNumeric = isNumericIdentifier(lhsIdentifier);
        const bool rhsNumeric = isNumericIdentifier(rhsIdentifier);
        if (lhsNumeric != rhsNumeric) {
            return lhsNumeric ? -1 : 1;
        }
        const int order = lhsNumeric
            ? compareNumericIdentifiers(lhsIdentifier, rhsIdentifier)
            : QString::compare(
                  lhsIdentifier, rhsIdentifier, Qt::CaseSensitive);
        if (order != 0) {
            return order;
        }
    }
    if (lhs.prerelease.size() != rhs.prerelease.size()) {
        return lhs.prerelease.size() < rhs.prerelease.size() ? -1 : 1;
    }
    return 0;
}

bool packageDisplayLess(const PackageDefinition* lhs,
                        const PackageDefinition* rhs) {
    if (lhs->id != rhs->id) {
        return lhs->id < rhs->id;
    }
    const SemanticVersion lhsSemantic =
        parseSemanticVersion(lhs->version);
    const SemanticVersion rhsSemantic =
        parseSemanticVersion(rhs->version);
    if (lhsSemantic.valid && rhsSemantic.valid) {
        const int semanticOrder =
            compareSemanticVersions(lhsSemantic, rhsSemantic);
        if (semanticOrder != 0) {
            return semanticOrder > 0;
        }
        if (lhsSemantic.hasBuildMetadata
            != rhsSemantic.hasBuildMetadata) {
            return !lhsSemantic.hasBuildMetadata;
        }
    }
    const QVersionNumber lhsVersion =
        QVersionNumber::fromString(lhs->version);
    const QVersionNumber rhsVersion =
        QVersionNumber::fromString(rhs->version);
    const int versionOrder =
        QVersionNumber::compare(lhsVersion, rhsVersion);
    if (versionOrder != 0) {
        return versionOrder > 0;
    }
    if (lhs->version != rhs->version) {
        return lhs->version > rhs->version;
    }
    return lhs->name < rhs->name;
}

} // namespace

RuntimePackageProbeResult probeRuntimePackage(
    const PackageDefinition& package) {
    const PackageLoadResult loaded = loadPackage(package.rootPath);
    RuntimePackageProbeResult result;
    result.diagnostics = loaded.diagnostics;
    if (!loaded.success || !loaded.package) {
        if (result.diagnostics.isEmpty()) {
            result.diagnostics.append(runtimeDiagnostic(
                QStringLiteral("package.runtime_unavailable"),
                QStringLiteral("Package %1 could not be loaded from %2.")
                    .arg(package.key())
                    .arg(package.rootPath),
                package.rootPath));
        }
        return result;
    }
    if (loaded.package->key() != package.key()) {
        result.diagnostics.append(runtimeDiagnostic(
            QStringLiteral("package.runtime_identity_mismatch"),
            QStringLiteral("Expected Package %1, but %2 now declares %3.")
                .arg(package.key())
                .arg(package.rootPath)
                .arg(loaded.package->key()),
            package.rootPath));
        return result;
    }
    if (normalizedPackageRootPath(loaded.package->rootPath)
        != normalizedPackageRootPath(package.rootPath)) {
        result.diagnostics.append(runtimeDiagnostic(
            QStringLiteral("package.runtime_root_mismatch"),
            QStringLiteral("Package %1 resolved to an unexpected root: %2")
                .arg(package.key())
                .arg(loaded.package->rootPath),
            package.rootPath));
        return result;
    }
    result.available = true;
    return result;
}

RuntimePackageCache::RuntimePackageCache(Probe probe)
    : m_probe(probe ? std::move(probe) : Probe{probeRuntimePackage}) {}

RuntimePackageRefreshResult RuntimePackageCache::synchronize(
    const QVector<PackageDefinition>& packages,
    quint64 catalogRevision,
    RuntimePackageRefreshPolicy policy) {
    if (policy == RuntimePackageRefreshPolicy::IfCatalogChanged
        && m_catalogRevision
        && *m_catalogRevision == catalogRevision) {
        return {false, m_availableKeys.size(), {}};
    }

    return rebuildSnapshot(packages, catalogRevision);
}

RuntimePackageRefreshResult RuntimePackageCache::rebuildSnapshot(
    const QVector<PackageDefinition>& packages,
    quint64 catalogRevision,
    const QString& requestedKey,
    RuntimePackageProbeResult* requestedResult) {
    QSet<QString> availableKeys;
    availableKeys.reserve(packages.size());
    QVector<Diagnostic> diagnostics;
    for (const PackageDefinition& package : packages) {
        RuntimePackageProbeResult result = m_probe(package);
        if (!result.available && result.diagnostics.isEmpty()) {
            result.diagnostics.append(runtimeDiagnostic(
                QStringLiteral("package.runtime_unavailable"),
                QStringLiteral("Package %1 is not runtime-ready.")
                    .arg(package.key()),
                package.rootPath));
        }
        if (requestedResult && package.key() == requestedKey) {
            *requestedResult = result;
        }
        if (result.available) {
            availableKeys.insert(package.key());
        } else {
            diagnostics += result.diagnostics;
        }
    }
    m_catalogRevision = catalogRevision;
    m_availableKeys = std::move(availableKeys);
    rebuildOrder(packages);
    ++m_probeGeneration;
    return {true, m_availableKeys.size(), std::move(diagnostics)};
}

RuntimePackageProbeResult RuntimePackageCache::synchronizeOne(
    const QVector<PackageDefinition>& packages,
    quint64 catalogRevision,
    const QString& key) {
    if (!m_catalogRevision || *m_catalogRevision != catalogRevision) {
        RuntimePackageProbeResult requested = missingPackageResult(key);
        rebuildSnapshot(
            packages, catalogRevision, key, &requested);
        return requested;
    }

    const auto package = std::find_if(
        packages.cbegin(), packages.cend(),
        [&](const PackageDefinition& candidate) {
            return candidate.key() == key;
        });
    RuntimePackageProbeResult result = package == packages.cend()
        ? missingPackageResult(key)
        : m_probe(*package);
    if (!result.available && result.diagnostics.isEmpty()) {
        result.diagnostics.append(runtimeDiagnostic(
            QStringLiteral("package.runtime_unavailable"),
            QStringLiteral("Package %1 is not runtime-ready.").arg(key),
            package == packages.cend() ? key : package->rootPath));
    }
    if (!result.available) {
        m_availableKeys.remove(key);
    } else {
        m_availableKeys.insert(key);
    }
    rebuildOrder(packages);
    ++m_probeGeneration;
    return result;
}

bool RuntimePackageCache::contains(const QString& key) const {
    return m_availableKeys.contains(key);
}

qsizetype RuntimePackageCache::size() const {
    return m_availableKeys.size();
}

const QStringList& RuntimePackageCache::orderedKeys() const {
    return m_orderedKeys;
}

quint64 RuntimePackageCache::probeGeneration() const {
    return m_probeGeneration;
}

void RuntimePackageCache::rebuildOrder(
    const QVector<PackageDefinition>& packages) {
    QVector<const PackageDefinition*> available;
    available.reserve(m_availableKeys.size());
    for (const PackageDefinition& package : packages) {
        if (m_availableKeys.contains(package.key())) {
            available.append(&package);
        }
    }
    std::sort(available.begin(), available.end(), packageDisplayLess);
    m_orderedKeys.clear();
    m_orderedKeys.reserve(available.size());
    for (const PackageDefinition* package : available) {
        m_orderedKeys.append(package->key());
    }
}

} // namespace finepaper
