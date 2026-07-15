#include "canonicaljson.h"
#include "contractartifactloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <functional>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const QString &message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

void requireThrowsWithPath(const std::function<void()> &operation,
                           const QString &relativePath) {
    try {
        operation();
    } catch (const std::runtime_error &error) {
        require(QString::fromUtf8(error.what()).contains(relativePath),
                QStringLiteral("error must contain path %1").arg(relativePath));
        return;
    }
    throw std::runtime_error(
        QStringLiteral("operation must reject %1").arg(relativePath).toStdString());
}

void testArtifactLoaderFindsCatalog() {
    QVERIFY2(!ContractArtifactLoader::repositoryRoot().isEmpty(),
             "repository root must resolve");
    require(!ContractArtifactLoader::repositoryRoot().isEmpty(),
            QStringLiteral("repository root must resolve"));
    const auto catalog =
        ContractArtifactLoader::loadObject("docs/contracts/schema-catalog.json");
    QCOMPARE(catalog.value("schema").toString(),
             QString("ipcraft.contract-schema-catalog.v1"));
    require(catalog.value("schema").toString() ==
                QStringLiteral("ipcraft.contract-schema-catalog.v1"),
            QStringLiteral("schema catalog must load"));
}

void testArtifactLoaderRejectsUnsafePathsAndMalformedJson() {
    const QString absolute = QStringLiteral("/etc/passwd");
    requireThrowsWithPath([&] { ContractArtifactLoader::loadBytes(absolute); }, absolute);

    const QString traversal = QStringLiteral("../xmake.lua");
    requireThrowsWithPath([&] { ContractArtifactLoader::loadBytes(traversal); }, traversal);

    const QString missing = QStringLiteral("docs/contracts/does-not-exist.json");
    requireThrowsWithPath([&] { ContractArtifactLoader::loadBytes(missing); }, missing);

    const QString malformed =
        QStringLiteral("qt/test/support/noc_contract/testdata/malformed.json");
    requireThrowsWithPath([&] { ContractArtifactLoader::loadObject(malformed); }, malformed);

    const QString duplicate =
        QStringLiteral("qt/test/support/noc_contract/testdata/duplicate-key.json");
    requireThrowsWithPath([&] { ContractArtifactLoader::loadObject(duplicate); }, duplicate);
}

void testArtifactLoaderEnforcesJsonRootType() {
    const QString arrayPath =
        QStringLiteral("qt/test/support/noc_contract/testdata/array.json");
    QCOMPARE(ContractArtifactLoader::loadArray(arrayPath).size(), 2);
    require(ContractArtifactLoader::loadArray(arrayPath).size() == 2,
            QStringLiteral("array artifact must retain its two items"));
    requireThrowsWithPath([&] { ContractArtifactLoader::loadObject(arrayPath); }, arrayPath);

    const QString objectPath = QStringLiteral("docs/contracts/schema-catalog.json");
    requireThrowsWithPath([&] { ContractArtifactLoader::loadArray(objectPath); }, objectPath);
}

void testRepositoryContainmentUsesNormalizedPathBoundaries() {
    using contract_artifact_detail::isCanonicalPathWithinRepository;
    using contract_artifact_detail::normalizedCanonicalPath;
    const QString nativeSpelling =
        QStringLiteral("root%1child").arg(QDir::separator());
    require(normalizedCanonicalPath(nativeSpelling) == QStringLiteral("root/child"),
            QStringLiteral("native separators must normalize to forward slashes"));
    require(isCanonicalPathWithinRepository(QStringLiteral("C:/repo/root"),
                                            QStringLiteral("C:/repo/root/file.json")),
            QStringLiteral("repository child must be contained"));
    require(isCanonicalPathWithinRepository(QStringLiteral("C:/repo/root"),
                                            QStringLiteral("C:/repo/root")),
            QStringLiteral("exact repository root must satisfy boundary comparison"));
    require(!isCanonicalPathWithinRepository(QStringLiteral("C:/repo/root"),
                                             QStringLiteral("C:/repo/root-sibling/file.json")),
            QStringLiteral("prefix sibling must not be contained"));
    require(!isCanonicalPathWithinRepository(QStringLiteral("C:/repo/root"),
                                             QStringLiteral("C:/repo/rooted/file.json")),
            QStringLiteral("shared text prefix must not bypass path boundary"));

    const QString compiledRoot = ContractArtifactLoader::repositoryRoot();
    require(compiledRoot == QDir::fromNativeSeparators(compiledRoot),
            QStringLiteral("compiled repository root must use forward slashes"));
    require(!compiledRoot.contains(QLatin1Char('\\')),
            QStringLiteral("compiled repository root must contain no backslashes"));
}

void testArtifactLoaderRejectsSymlinkEscape() {
    QTemporaryDir external;
    require(external.isValid(), QStringLiteral("external temporary directory must exist"));
    const QString externalFile = external.filePath(QStringLiteral("outside.json"));
    QFile file(externalFile);
    require(file.open(QIODevice::WriteOnly),
            QStringLiteral("external test artifact must open"));
    file.write("{}\n");
    file.close();

    QDir repository(ContractArtifactLoader::repositoryRoot());
    require(repository.mkpath(QStringLiteral("build")),
            QStringLiteral("repository build directory must exist"));
    QTemporaryDir inside(repository.filePath(
        QStringLiteral("build/noc-contract-symlink-test-XXXXXX")));
    require(inside.isValid(), QStringLiteral("in-repository temporary directory must exist"));
    const QString linkPath = inside.filePath(QStringLiteral("escape.json"));
    require(QFile::link(externalFile, linkPath),
            QStringLiteral("symlink escape fixture must be created"));
    const QString relativeLink = repository.relativeFilePath(linkPath);
    requireThrowsWithPath(
        [&] { ContractArtifactLoader::loadBytes(relativeLink); }, relativeLink);
}

QJsonObject findCollectionCase(const QJsonArray &cases, const QString &kind) {
    for (const auto &value : cases) {
        const auto object = value.toObject();
        if (object.value(QStringLiteral("collectionKind")).toString() == kind) {
            return object;
        }
    }
    throw std::runtime_error(
        QStringLiteral("missing canonical vector kind %1").arg(kind).toStdString());
}

QJsonObject findCollectionCaseById(const QJsonArray &cases, const QString &id) {
    for (const auto &value : cases) {
        const auto object = value.toObject();
        if (object.value(QStringLiteral("id")).toString() == id) {
            return object;
        }
    }
    throw std::runtime_error(
        QStringLiteral("missing canonical vector %1").arg(id).toStdString());
}

CanonicalRuleSet rulesForCase(const QJsonObject &ruleCatalog,
                              const QJsonObject &testCase) {
    return CanonicalRuleSet::fromCatalog(
        ruleCatalog,
        {{QString(),
          testCase.value(QStringLiteral("schemaId")).toString(),
          testCase.value(QStringLiteral("schemaPointer")).toString()}});
}

void testCanonicalHelperMatchesCommittedSetVector() {
    const auto ruleCatalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const auto vectors = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"));
    const auto testCase = findCollectionCase(vectors.value("cases").toArray(), "set");
    const auto rules = rulesForCase(ruleCatalog, testCase);
    const auto variants = testCase.value(QStringLiteral("inputVariants")).toArray();

    for (const auto &variant : variants) {
        const QByteArray canonical = canonicalJson(variant, rules);
        require(canonical ==
                    testCase.value(QStringLiteral("expectedCanonicalJson"))
                        .toString()
                        .toUtf8(),
                QStringLiteral("set variant must match committed canonical JSON"));
        require(sha256Digest(canonical) ==
                    testCase.value(QStringLiteral("expectedDigest")).toString(),
                QStringLiteral("set variant must match committed digest"));
    }
}

void testCanonicalHelperPreservesExplicitOrderedArrays() {
    const auto ruleCatalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const auto vectors = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"));
    const auto testCase = findCollectionCase(vectors.value("cases").toArray(), "ordered");
    const auto rules = rulesForCase(ruleCatalog, testCase);
    const auto variants = testCase.value(QStringLiteral("inputVariants")).toArray();
    require(variants.size() >= 2, QStringLiteral("ordered vector needs two variants"));
    require(canonicalJson(variants.at(0), rules) != canonicalJson(variants.at(1), rules),
            QStringLiteral("ordered variants must remain distinct"));
}

void testCanonicalHelperRejectsMissingRulesAndUnsafeNumbers() {
    requireThrowsWithPath(
        [] { canonicalJson(QJsonArray{1, 2}, CanonicalRuleSet{}); },
        QStringLiteral("/"));
    requireThrowsWithPath(
        [] {
            canonicalJson(QJsonValue(9007199254740992.0), CanonicalRuleSet{});
        },
        QStringLiteral("number"));
}

void requireEqualVector(const QJsonObject &ruleCatalog,
                        const QJsonObject &testCase,
                        QList<CanonicalRuleBinding> extraBindings = {}) {
    QList<CanonicalRuleBinding> bindings{{QString(),
                                          testCase.value("schemaId").toString(),
                                          testCase.value("schemaPointer").toString()}};
    bindings.append(extraBindings);
    const auto rules = CanonicalRuleSet::fromCatalog(ruleCatalog, bindings);
    for (const auto &variant : testCase.value("inputVariants").toArray()) {
        const QByteArray canonical = canonicalJson(variant, rules);
        require(canonical == testCase.value("expectedCanonicalJson").toString().toUtf8(),
                QStringLiteral("%1 canonical JSON mismatch")
                    .arg(testCase.value("id").toString()));
        require(sha256Digest(canonical) == testCase.value("expectedDigest").toString(),
                QStringLiteral("%1 digest mismatch")
                    .arg(testCase.value("id").toString()));
    }
}

void testCanonicalHelperSupportsFrozenSpecialSortKeys() {
    const auto ruleCatalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const auto vectorDocument = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"));
    const auto cases = vectorDocument.value("cases").toArray();

    const QStringList ids{
        QStringLiteral("core-canonical-models-accessSlot-allowedContracts-items-roles"),
        QStringLiteral("interface-contract-capability-values-oneOf-0"),
        QStringLiteral("core-canonical-models-packageRelation-sources"),
        QStringLiteral("core-canonical-models-patchPackageRelationValue-sources")};
    for (const auto &id : ids) {
        requireEqualVector(ruleCatalog, findCollectionCaseById(cases, id));
    }

    requireEqualVector(
        ruleCatalog,
        findCollectionCaseById(cases,
                               QStringLiteral("core-canonical-models-impactReport-impacts")),
        {{QStringLiteral("/*/subjects"),
          QStringLiteral("ipcraft.core-canonical-models.v1"),
          QStringLiteral("/$defs/impact/properties/subjects")}});
}

void testCanonicalHelperValidatesDerivedOrderAndAmbiguousBindings() {
    const auto ruleCatalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const auto vectorDocument = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"));
    const auto testCase = findCollectionCaseById(
        vectorDocument.value("cases").toArray(),
        QStringLiteral("core-canonical-models-candidateTransaction-allocationOrder"));
    const auto rules = rulesForCase(ruleCatalog, testCase);
    const auto variants = testCase.value("inputVariants").toArray();
    require(canonicalJson(variants.first(), rules) ==
                testCase.value("expectedCanonicalJson").toArray().first().toString().toUtf8(),
            QStringLiteral("valid derived order must canonicalize"));
    requireThrowsWithPath([&] { canonicalJson(variants.at(1), rules); }, QStringLiteral("/"));

    const CanonicalRuleBinding binding{QString(),
                                       testCase.value("schemaId").toString(),
                                       testCase.value("schemaPointer").toString()};
    requireThrowsWithPath(
        [&] { CanonicalRuleSet::fromCatalog(ruleCatalog, {binding, binding}); },
        QStringLiteral("ambiguous"));
}

QJsonObject unresolvedPersistedEndpoint(const QString &id, const QString &reason) {
    return QJsonObject{
        {QStringLiteral("state"), QStringLiteral("unresolved")},
        {QStringLiteral("intendedSubject"),
         QJsonObject{{QStringLiteral("kind"), QStringLiteral("component")},
                     {QStringLiteral("id"), id}}},
        {QStringLiteral("reasonCode"), reason}};
}

QJsonObject unresolvedPatchEndpoint(const QString &referenceKind,
                                    const QString &referenceValue,
                                    const QString &reason) {
    return QJsonObject{
        {QStringLiteral("state"), QStringLiteral("unresolved")},
        {QStringLiteral("intendedSubject"),
         QJsonObject{{QStringLiteral("kind"), QStringLiteral("component")},
                     {QStringLiteral("ref"),
                      QJsonObject{{referenceKind, referenceValue}}}}},
        {QStringLiteral("reasonCode"), reason}};
}

void testEndpointSortKeysKeepOpaqueTupleComponentsDistinct() {
    const auto catalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const QChar separator(0x001f);

    const auto persistedRules = CanonicalRuleSet::fromCatalog(
        catalog,
        {{QString(),
          QStringLiteral("ipcraft.core-canonical-models.v1"),
          QStringLiteral("/$defs/packageRelation/properties/sources")}});
    const auto persistedLongReference = unresolvedPersistedEndpoint(
        QStringLiteral("b") + separator + QStringLiteral("c"),
        QStringLiteral("d:tail"));
    const auto persistedLongReason = unresolvedPersistedEndpoint(
        QStringLiteral("b"),
        QStringLiteral("c") + separator + QStringLiteral("d:tail"));
    const QByteArray persistedCanonical = canonicalJson(
        QJsonArray{persistedLongReference, persistedLongReason}, persistedRules);
    const auto persistedSorted =
        QJsonDocument::fromJson(persistedCanonical).array();
    require(persistedSorted.size() == 2,
            QStringLiteral("opaque persisted endpoint tuples must remain distinct"));
    require(persistedSorted.first()
                    .toObject()
                    .value("intendedSubject")
                    .toObject()
                    .value("id")
                    .toString() == QStringLiteral("b"),
            QStringLiteral("persisted endpoint tuples must compare component-by-component"));

    const auto patchRules = CanonicalRuleSet::fromCatalog(
        catalog,
        {{QString(),
          QStringLiteral("ipcraft.core-canonical-models.v1"),
          QStringLiteral("/$defs/patchPackageRelationValue/properties/sources")}});
    const auto patchLongReference = unresolvedPatchEndpoint(
        QStringLiteral("localRef"),
        QStringLiteral("b") + separator + QStringLiteral("c") + QChar(0),
        QStringLiteral("d:tail"));
    const auto patchLongReason = unresolvedPatchEndpoint(
        QStringLiteral("localRef"),
        QStringLiteral("b"),
        QStringLiteral("c") + QChar(0) + separator +
            QStringLiteral("d:tail"));
    const QByteArray patchCanonical =
        canonicalJson(QJsonArray{patchLongReference, patchLongReason}, patchRules);
    const auto patchSorted = QJsonDocument::fromJson(patchCanonical).array();
    require(patchSorted.size() == 2,
            QStringLiteral("opaque patch endpoint tuples must remain distinct"));
    require(patchSorted.first()
                    .toObject()
                    .value("intendedSubject")
                    .toObject()
                    .value("ref")
                    .toObject()
                    .value("localRef")
                    .toString() == QStringLiteral("b"),
            QStringLiteral("patch endpoint tuples must compare component-by-component"));
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        testArtifactLoaderFindsCatalog();
        testArtifactLoaderRejectsUnsafePathsAndMalformedJson();
        testArtifactLoaderEnforcesJsonRootType();
        testRepositoryContainmentUsesNormalizedPathBoundaries();
        testArtifactLoaderRejectsSymlinkEscape();
        testCanonicalHelperMatchesCommittedSetVector();
        testCanonicalHelperPreservesExplicitOrderedArrays();
        testCanonicalHelperRejectsMissingRulesAndUnsafeNumbers();
        testCanonicalHelperSupportsFrozenSpecialSortKeys();
        testCanonicalHelperValidatesDerivedOrderAndAmbiguousBindings();
        testEndpointSortKeysKeepOpaqueTupleComponentsDistinct();
        std::cout << "noc_contract_schema_meta_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
