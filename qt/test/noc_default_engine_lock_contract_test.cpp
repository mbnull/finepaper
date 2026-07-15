#include "contractartifactloader.h"
#include "contracttesthelpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

namespace {

bool containsString(const QJsonArray &array, const QString &value) {
    return std::any_of(array.begin(), array.end(), [&](const QJsonValue &item) {
        return item.toString() == value;
    });
}

void verifyExactResolutionRule(const QJsonObject &testCase) {
    const auto input = testCase.value(QStringLiteral("input")).toObject();
    const auto expected = testCase.value(QStringLiteral("expected")).toObject();
    const auto lock = input.value(QStringLiteral("projectLock")).toObject();
    const QString outcome = expected.value(QStringLiteral("outcome")).toString();
    if (outcome != QStringLiteral("exact")) {
        requireContract(outcome == QStringLiteral("degraded-inspect") &&
                            expected.value(QStringLiteral("selectedBundleManifestDigest")).isNull(),
                        testCase.value(QStringLiteral("id")).toString() +
                            QStringLiteral(": every non-exact result must be degraded with no fallback"));
        return;
    }

    const QString selected =
        expected.value(QStringLiteral("selectedBundleManifestDigest")).toString();
    requireContract(selected == lock.value(QStringLiteral("bundleManifestDigest")).toString(),
                    QStringLiteral("writable resolution must select exact locked digest"));
    bool exactCompatible = false;
    for (const auto &raw : input.value(QStringLiteral("installedBundles")).toArray()) {
        const auto bundle = raw.toObject();
        const auto manifest = bundle.value(QStringLiteral("manifest")).toObject();
        if (bundle.value(QStringLiteral("bundleManifestDigest")).toString() != selected) {
            continue;
        }
        exactCompatible = bundle.value(QStringLiteral("installed")).toBool() &&
                          !bundle.value(QStringLiteral("revoked")).toBool() &&
                          bundle.value(QStringLiteral("contentVerified")).toBool() &&
                          bundle.value(QStringLiteral("verifiedBundleManifestDigest")).toString() == selected &&
                          manifest.value(QStringLiteral("id")) == lock.value(QStringLiteral("id")) &&
                          manifest.value(QStringLiteral("version")) == lock.value(QStringLiteral("version")) &&
                          manifest.value(QStringLiteral("engineCompatibilityVersion")) ==
                              lock.value(QStringLiteral("engineCompatibilityVersion")) &&
                          manifest.value(QStringLiteral("engineHostContractVersion")) ==
                              lock.value(QStringLiteral("engineHostContractVersion")) &&
                          manifest.value(QStringLiteral("hostSideEffectContractVersion")) ==
                              lock.value(QStringLiteral("hostSideEffectContractVersion")) &&
                          containsString(manifest.value(QStringLiteral("supportedPlatformAbis")).toArray(),
                                         input.value(QStringLiteral("currentPlatformAbi")).toString()) &&
                          containsString(input.value(QStringLiteral("supportedEngineHostContracts")).toArray(),
                                         lock.value(QStringLiteral("engineHostContractVersion")).toString()) &&
                          containsString(input.value(QStringLiteral("supportedHostSideEffectContracts")).toArray(),
                                         lock.value(QStringLiteral("hostSideEffectContractVersion")).toString());
    }
    requireContract(exactCompatible,
                    testCase.value(QStringLiteral("id")).toString() +
                        QStringLiteral(": exact outcome lacks exact compatible verified bundle"));
}

void verifyCatalog() {
    const auto document = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/default-engine-lock-v1.json"));
    const auto resolution = document.value(QStringLiteral("resolutionCases")).toArray();
    const auto migration = document.value(QStringLiteral("migrationCases")).toArray();
    const auto freshness = document.value(QStringLiteral("freshnessCases")).toArray();
    requireContract(resolution.size() == 18 && migration.size() == 6 && freshness.size() == 8,
                    QStringLiteral("Engine vector counts must be exactly 18/6/8"));
    QStringList allIds;
    int exact = 0;
    for (const auto &raw : resolution) {
        const auto item = raw.toObject();
        allIds.append(item.value(QStringLiteral("id")).toString());
        exact += item.value(QStringLiteral("expected")).toObject()
                     .value(QStringLiteral("outcome")).toString() == QStringLiteral("exact");
        verifyExactResolutionRule(item);
    }
    for (const auto &raw : migration) {
        allIds.append(raw.toObject().value(QStringLiteral("id")).toString());
    }
    for (const auto &raw : freshness) {
        allIds.append(raw.toObject().value(QStringLiteral("id")).toString());
    }
    sortedUniqueStrings(allIds, QStringLiteral("Engine behavior case IDs"));
    requireContract(exact == 2,
                    QStringLiteral("only exact available/current-plus-upgrade-discovery stay writable"));
}

} // namespace


int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        verifyCatalog();
        runContractPythonVerifier(
            QStringLiteral("docs/contracts/tools/verify_engine_side_effect_vectors.py"), {},
            QStringLiteral("18 resolution, 6 migration, 8 freshness, 14 causal side-effect cases; "
                           "169 mutations rejected"));
        std::cout << "noc_default_engine_lock_contract_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
