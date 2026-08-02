#include "features/package_library/runtime_package_cache.h"

#include <QCoreApplication>
#include <QHash>
#include <QTextStream>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    QTextStream(stderr) << "FAILED: " << message << Qt::endl;
    ++failures;
}

PackageDefinition package(
    const QString& id,
    const QString& version,
    const QString& name = {}) {
    PackageDefinition value;
    value.id = id;
    value.version = version;
    value.name = name.isEmpty() ? id : name;
    value.rootPath = QStringLiteral("/catalog/%1/%2").arg(id, version);
    return value;
}

RuntimePackageProbeResult probeResult(bool available) {
    return RuntimePackageProbeResult{available, {}};
}

void sameRevisionDoesNotProbeAgain() {
    int probes = 0;
    RuntimePackageCache cache([&](const PackageDefinition&) {
        ++probes;
        return probeResult(true);
    });
    const QVector packages{
        package(QStringLiteral("alpha"), QStringLiteral("1.0.0")),
        package(QStringLiteral("beta"), QStringLiteral("1.0.0"))};

    const RuntimePackageRefreshResult first = cache.synchronize(packages, 4);
    const RuntimePackageRefreshResult second = cache.synchronize(packages, 4);
    check(first.probed && !second.probed && probes == 2
              && cache.probeGeneration() == 1 && cache.size() == 2,
          QStringLiteral(
              "routine refreshes reuse availability for one immutable catalog revision"));
}

void catalogRevisionAndForceReplaceTheSnapshot() {
    int probes = 0;
    QHash<QString, bool> available{
        {QStringLiteral("alpha@1.0.0"), true},
        {QStringLiteral("beta@1.0.0"), false}};
    RuntimePackageCache cache([&](const PackageDefinition& candidate) {
        ++probes;
        return probeResult(available.value(candidate.key()));
    });
    const QVector packages{
        package(QStringLiteral("alpha"), QStringLiteral("1.0.0")),
        package(QStringLiteral("beta"), QStringLiteral("1.0.0"))};

    cache.synchronize(packages, 1);
    available[QStringLiteral("alpha@1.0.0")] = false;
    available[QStringLiteral("beta@1.0.0")] = true;
    cache.synchronize(packages, 2);
    check(probes == 4 && !cache.contains(QStringLiteral("alpha@1.0.0"))
              && cache.contains(QStringLiteral("beta@1.0.0")),
          QStringLiteral(
              "a new catalog revision atomically replaces runtime availability"));

    available[QStringLiteral("alpha@1.0.0")] = true;
    cache.synchronize(
        packages, 2, RuntimePackageRefreshPolicy::Force);
    check(probes == 6 && cache.size() == 2
              && cache.probeGeneration() == 3,
          QStringLiteral(
              "forced refresh re-probes a retained catalog at the same revision"));
}

void targetedRefreshIsRevisionSafe() {
    int probes = 0;
    QHash<QString, bool> available{
        {QStringLiteral("alpha@1.0.0"), true},
        {QStringLiteral("beta@1.0.0"), true}};
    RuntimePackageCache cache([&](const PackageDefinition& candidate) {
        ++probes;
        return probeResult(available.value(candidate.key()));
    });
    const QVector packages{
        package(QStringLiteral("alpha"), QStringLiteral("1.0.0")),
        package(QStringLiteral("beta"), QStringLiteral("1.0.0"))};

    check(cache.synchronizeOne(
              packages, 8, QStringLiteral("alpha@1.0.0")).available
              && probes == 2 && cache.size() == 2,
          QStringLiteral(
              "a targeted preflight initializes the complete snapshot when its revision is unknown"));

    available[QStringLiteral("alpha@1.0.0")] = false;
    check(!cache.synchronizeOne(
              packages, 8, QStringLiteral("alpha@1.0.0")).available
              && probes == 3 && cache.size() == 1
              && cache.contains(QStringLiteral("beta@1.0.0")),
          QStringLiteral(
              "a targeted preflight updates only the requested Package in the current snapshot"));
}

void unavailableAndRemovedKeysDoNotLeakAcrossSnapshots() {
    RuntimePackageCache cache([](const PackageDefinition& candidate) {
        return probeResult(
            candidate.id != QStringLiteral("unavailable"));
    });
    const QVector initial{
        package(QStringLiteral("ready"), QStringLiteral("1.0.0")),
        package(QStringLiteral("unavailable"), QStringLiteral("1.0.0"))};
    cache.synchronize(initial, 1);
    cache.synchronize(
        QVector{package(QStringLiteral("replacement"),
                        QStringLiteral("2.0.0"))},
        2);
    check(cache.orderedKeys()
              == QStringList{QStringLiteral("replacement@2.0.0")}
              && !cache.contains(QStringLiteral("ready@1.0.0")),
          QStringLiteral(
              "catalog replacement drops keys from both unavailable and removed Packages"));
}

void orderingUsesIdAndDescendingSemanticVersion() {
    RuntimePackageCache cache([](const PackageDefinition&) {
        return probeResult(true);
    });
    const QVector packages{
        package(QStringLiteral("zeta"), QStringLiteral("1.0.0")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0")),
        package(QStringLiteral("alpha"), QStringLiteral("10.0.0")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0-beta")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0-beta.2")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0-beta.11")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0-alpha")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0+build.2")),
        package(QStringLiteral("alpha"), QStringLiteral("2.0.0+build.1"))};
    cache.synchronize(packages, 1);
    check(cache.orderedKeys()
              == QStringList{
                  QStringLiteral("alpha@10.0.0"),
                  QStringLiteral("alpha@2.0.0"),
                  QStringLiteral("alpha@2.0.0+build.2"),
                  QStringLiteral("alpha@2.0.0+build.1"),
                  QStringLiteral("alpha@2.0.0-beta.11"),
                  QStringLiteral("alpha@2.0.0-beta.2"),
                  QStringLiteral("alpha@2.0.0-beta"),
                  QStringLiteral("alpha@2.0.0-alpha"),
                  QStringLiteral("zeta@1.0.0")},
          QStringLiteral(
              "runtime keys retain deterministic id and SemVer prerelease presentation order"));
}

void targetedDiagnosticsRemainActionable() {
    const Diagnostic missingManifest{
        QStringLiteral("error"),
        QStringLiteral("package.read_failed"),
        QStringLiteral("could not read package.json"),
        QStringLiteral("/catalog/alpha/1.0.0/package.json"),
        QStringLiteral("package")};
    RuntimePackageCache cache([&](const PackageDefinition&) {
        return RuntimePackageProbeResult{false, {missingManifest}};
    });
    const QVector packages{
        package(QStringLiteral("alpha"), QStringLiteral("1.0.0"))};

    const RuntimePackageProbeResult targeted = cache.synchronizeOne(
        packages, 3, QStringLiteral("alpha@1.0.0"));
    check(!targeted.available && targeted.diagnostics.size() == 1
              && targeted.diagnostics.constFirst().code
                     == QStringLiteral("package.read_failed")
              && cache.size() == 0,
          QStringLiteral(
              "targeted preflight preserves the Package loader diagnostic"));

    const RuntimePackageProbeResult absent = cache.synchronizeOne(
        packages, 3, QStringLiteral("missing@9.0.0"));
    check(!absent.available && !absent.diagnostics.isEmpty()
              && absent.diagnostics.constFirst().code
                     == QStringLiteral("package.runtime_not_in_catalog"),
          QStringLiteral(
              "a missing catalog key produces a specific runtime diagnostic"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    sameRevisionDoesNotProbeAgain();
    catalogRevisionAndForceReplaceTheSnapshot();
    targetedRefreshIsRevisionSafe();
    unavailableAndRemovedKeysDoNotLeakAcrossSnapshots();
    orderingUsesIdAndDescendingSemanticVersion();
    targetedDiagnosticsRemainActionable();
    QTextStream(stdout)
        << (failures == 0 ? "package-runtime-cache tests passed"
                          : "package-runtime-cache tests failed")
        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
