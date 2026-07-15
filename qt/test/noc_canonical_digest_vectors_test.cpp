#include "canonicaljson.h"
#include "contractartifactloader.h"
#include "contracttesthelpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>
#include <cmath>

namespace {

void requireSafeIntegerJsonDomain(const QJsonValue &value, const QString &location) {
    if (value.isDouble()) {
        const double number = value.toDouble();
        requireContract(std::isfinite(number) && std::trunc(number) == number &&
                            std::abs(number) <= 9007199254740991.0,
                        location + QStringLiteral(": vector number is outside Qt safe-integer domain"));
    } else if (value.isArray()) {
        const auto array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            requireSafeIntegerJsonDomain(array.at(index),
                                         location + QLatin1Char('/') + QString::number(index));
        }
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
            requireSafeIntegerJsonDomain(iterator.value(),
                                         location + QLatin1Char('/') + iterator.key());
        }
    }
}

void verifyNoVectorIsSilentlyOutsideQtNumericDomain() {
    for (const QString &path : {
             QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"),
             QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"),
             QStringLiteral("docs/contracts/vectors/candidate-local-ref-v1.json"),
             QStringLiteral("docs/contracts/vectors/default-engine-lock-v1.json"),
             QStringLiteral("docs/contracts/vectors/host-side-effects-v1.json")}) {
        requireSafeIntegerJsonDomain(ContractArtifactLoader::loadObject(path), path);
    }
}

void verifyCollectionCatalogInCpp() {
    const auto rules = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const auto casesDocument = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"));
    const auto cases = casesDocument.value(QStringLiteral("cases")).toArray();
    requireContract(rules.value(QStringLiteral("canonicalCollections")).toArray().size() == 99,
                    QStringLiteral("exactly 99 physical canonical rules are frozen"));
    requireContract(cases.size() == 99,
                    QStringLiteral("every physical rule needs one collection case"));

    QStringList ids;
    int equal = 0;
    int different = 0;
    int invalid = 0;
    for (const auto &raw : cases) {
        const auto item = raw.toObject();
        ids.append(item.value(QStringLiteral("id")).toString());
        const QString relation = item.value(QStringLiteral("expectedRelation")).toString();
        if (relation == QStringLiteral("equal")) {
            ++equal;
        } else if (relation == QStringLiteral("different")) {
            ++different;
        } else if (relation == QStringLiteral("invalid")) {
            ++invalid;
        } else {
            requireContract(false, item.value(QStringLiteral("id")).toString() +
                                       QStringLiteral(": unknown expectedRelation"));
        }
        requireContract(!item.value(QStringLiteral("inputVariants")).toArray().isEmpty(),
                        ids.constLast() + QStringLiteral(": inputVariants must be non-empty"));
    }
    sortedUniqueStrings(ids, QStringLiteral("collection vector IDs"));
    requireContract(equal > 0 && different > 0 && invalid > 0,
                    QStringLiteral("set, ordered, and derived-order outcomes must all be covered"));

    // Exercise the frozen Qt safe-integer domain directly. The complete catalog,
    // including nested projections and any Decimal cases outside Qt's lossless
    // domain, is checked by the pinned independent Python verifier below.
    const auto first = cases.first().toObject();
    const auto ruleSet = CanonicalRuleSet::fromCatalog(
        rules, {{QString(), first.value(QStringLiteral("schemaId")).toString(),
                 first.value(QStringLiteral("schemaPointer")).toString()}});
    for (const auto &variant : first.value(QStringLiteral("inputVariants")).toArray()) {
        const QByteArray bytes = canonicalJson(variant, ruleSet);
        requireContract(bytes == first.value(QStringLiteral("expectedCanonicalJson")).toString().toUtf8(),
                        QStringLiteral("Qt canonical bytes mismatch for safe-integer witness"));
        requireContract(sha256Digest(bytes) == first.value(QStringLiteral("expectedDigest")).toString(),
                        QStringLiteral("Qt canonical digest mismatch for safe-integer witness"));
    }
}

void verifyFullCatalogWithAuthoritativeVerifier() {
    const QString output = runContractPythonVerifier(
        QStringLiteral("docs/contracts/tools/verify_canonical_vectors.py"), {},
        QStringLiteral("canonical vector verification passed: 134 digests, 1014 collection items, "
                       "25 valid candidate/model inputs, 4 schema negatives, 4 semantic negatives"));
    requireContract(output.count(QStringLiteral("canonical vector verification passed:")) == 1,
                    QStringLiteral("canonical verifier must emit exactly one result summary"));
    runContractPythonVerifier(
        QStringLiteral("docs/contracts/tools/verify_canonical_rules.py"), {},
        QStringLiteral("canonical rule verification passed: 99 Core array locations, "
                       "10 deferred extension display paths, 99 collection cases, 21 candidate cases"));
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        verifyNoVectorIsSilentlyOutsideQtNumericDomain();
        verifyCollectionCatalogInCpp();
        verifyFullCatalogWithAuthoritativeVerifier();
        std::cout << "noc_canonical_digest_vectors_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
