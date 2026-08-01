#pragma once

#include "package/package.h"

#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

// Discovery diagnostics describe individual roots and Package candidates.
// They may contain errors for isolated candidates without making the whole
// catalog unusable.
struct PackageCatalogDiscoveryResult {
    bool catalogFatal = false;
    qsizetype candidateCount = 0;
    qsizetype rejectedCount = 0;
    QVector<PackageDefinition> packages;
    QVector<Diagnostic> diagnostics;
};

enum class PackageCatalogReloadDisposition {
    Rejected,
    Committed,
    CatalogFatal,
};

// committed() is the transaction boundary: when false, callers can rely on
// the previous catalog snapshot remaining unchanged. catalogFatal() explains
// that discovery could not establish a trustworthy replacement snapshot.
struct PackageCatalogReloadResult {
    PackageCatalogReloadDisposition disposition =
        PackageCatalogReloadDisposition::Rejected;
    qsizetype acceptedCount = 0;
    qsizetype rejectedCount = 0;
    QVector<Diagnostic> diagnostics;

    [[nodiscard]] bool committed() const noexcept {
        return disposition == PackageCatalogReloadDisposition::Committed;
    }
    [[nodiscard]] bool catalogFatal() const noexcept {
        return disposition == PackageCatalogReloadDisposition::CatalogFatal;
    }
};

PackageCatalogDiscoveryResult discoverPackageCatalog(const QStringList& roots);

class PackageCatalog {
public:
    PackageCatalogReloadResult reload(const QStringList& roots);
    const QVector<PackageDefinition>& packages() const;
    std::optional<PackageDefinition> resolve(
        const PackageReference& reference) const;

private:
    QVector<PackageDefinition> m_packages;
};

} // namespace finepaper
