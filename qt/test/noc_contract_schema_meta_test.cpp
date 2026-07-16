#include "canonicaljson.h"
#include "contractartifactloader.h"
#include "contracttesthelpers.h"

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
#include <limits>
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

void testCanonicalHelperRejectsMissingRulesAndNonFiniteNumbers() {
    requireThrowsWithPath(
        [] { canonicalJson(QJsonArray{1, 2}, CanonicalRuleSet{}); },
        QStringLiteral("/"));
    requireThrowsWithPath(
        [] {
            canonicalJson(QJsonValue(std::numeric_limits<double>::infinity()),
                          CanonicalRuleSet{});
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

void testCanonicalRuleWildcardsTrackArrayDescentOnly() {
    const auto catalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const QString setSchema = QStringLiteral("ipcraft.core-canonical-models.v1");
    const QString setPointer =
        QStringLiteral("/$defs/accessSlot/properties/allowedContracts/items/properties/roles");
    const CanonicalRuleBinding wildcardSet{QStringLiteral("/*"),
                                           setSchema,
                                           setPointer};

    const QJsonObject numericProperty{
        {QStringLiteral("0"), QJsonArray{QStringLiteral("b"), QStringLiteral("a")}}};
    const auto wildcardOnly = CanonicalRuleSet::fromCatalog(catalog, {wildcardSet});
    requireThrowsWithPath(
        [&] { canonicalJson(numericProperty, wildcardOnly); }, QStringLiteral("/0"));

    const auto explicitNumericProperty = CanonicalRuleSet::fromCatalog(
        catalog,
        {{QStringLiteral("/0"), setSchema, setPointer}});
    require(canonicalJson(numericProperty, explicitNumericProperty) ==
                QByteArrayLiteral("{\"0\":[\"a\",\"b\"]}"),
            QStringLiteral("explicit numeric object property rule must apply literally"));

    const CanonicalRuleBinding rootOrdered{
        QString(),
        QStringLiteral("ipcraft.command-result.v1"),
        QStringLiteral("/properties/diagnostics")};
    const QJsonArray nestedNumericProperty{
        QJsonObject{{QStringLiteral("0"),
                     QJsonArray{QStringLiteral("b"), QStringLiteral("a")}}}};
    const auto inferredNestedWildcard = CanonicalRuleSet::fromCatalog(
        catalog,
        {rootOrdered,
         {QStringLiteral("/*/*"), setSchema, setPointer}});
    requireThrowsWithPath(
        [&] { canonicalJson(nestedNumericProperty, inferredNestedWildcard); },
        QStringLiteral("/0/0"));

    const auto explicitNestedNumeric = CanonicalRuleSet::fromCatalog(
        catalog,
        {rootOrdered,
         {QStringLiteral("/*/0"), setSchema, setPointer}});
    require(canonicalJson(nestedNumericProperty, explicitNestedNumeric) ==
                QByteArrayLiteral("[{\"0\":[\"a\",\"b\"]}]"),
            QStringLiteral("array wildcard plus literal numeric property must resolve"));

    const QJsonArray ordinaryArrayItems{
        QJsonObject{{QStringLiteral("roles"),
                     QJsonArray{QStringLiteral("b"), QStringLiteral("a")}}}};
    const auto ordinaryWildcard = CanonicalRuleSet::fromCatalog(
        catalog,
        {rootOrdered,
         {QStringLiteral("/*/roles"), setSchema, setPointer}});
    require(canonicalJson(ordinaryArrayItems, ordinaryWildcard) ==
                QByteArrayLiteral("[{\"roles\":[\"a\",\"b\"]}]"),
            QStringLiteral("ordinary array-item wildcard must remain supported"));
}

QJsonValue resolveJsonPointer(const QJsonValue &document,
                              const QString &pointer,
                              const QString &location) {
    require(pointer.isEmpty() || pointer.startsWith(QLatin1Char('/')),
            location + QStringLiteral(": fragment must be an RFC 6901 pointer"));
    QJsonValue current = document;
    if (pointer.isEmpty()) {
        return current;
    }
    for (const auto &rawToken : pointer.mid(1).split(QLatin1Char('/'))) {
        for (qsizetype index = 0; index < rawToken.size(); ++index) {
            if (rawToken.at(index) == QLatin1Char('~')) {
                require(index + 1 < rawToken.size() &&
                            (rawToken.at(index + 1) == QLatin1Char('0') ||
                             rawToken.at(index + 1) == QLatin1Char('1')),
                        location + QStringLiteral(": invalid RFC 6901 escape"));
                ++index;
            }
        }
        QString token = rawToken;
        token.replace(QStringLiteral("~1"), QStringLiteral("/"));
        token.replace(QStringLiteral("~0"), QStringLiteral("~"));
        if (current.isObject()) {
            const auto object = current.toObject();
            require(object.contains(token),
                    location + QStringLiteral(": unresolved object token ") + token);
            current = object.value(token);
        } else if (current.isArray()) {
            bool ok = false;
            const int arrayIndex = token.toInt(&ok);
            require(ok && QString::number(arrayIndex) == token && arrayIndex >= 0 &&
                        arrayIndex < current.toArray().size(),
                    location + QStringLiteral(": unresolved array token ") + token);
            current = current.toArray().at(arrayIndex);
        } else {
            require(false, location + QStringLiteral(": pointer descends through scalar"));
        }
    }
    return current;
}

void collectAndResolveReferences(const QJsonValue &value,
                                 const QString &currentSchemaPath,
                                 const QHash<QString, QString> &schemaPaths,
                                 const QHash<QString, QJsonObject> &schemas,
                                 const QString &location) {
    if (value.isArray()) {
        const auto array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            collectAndResolveReferences(array.at(index), currentSchemaPath, schemaPaths,
                                        schemas,
                                        location + QStringLiteral("/") + QString::number(index));
        }
        return;
    }
    if (!value.isObject()) {
        return;
    }
    const auto object = value.toObject();
    if (object.contains(QStringLiteral("$ref"))) {
        const QString reference = object.value(QStringLiteral("$ref")).toString();
        require(!reference.isEmpty(), location + QStringLiteral(": $ref must be a string"));
        const qsizetype hash = reference.indexOf(QLatin1Char('#'));
        const QString targetName = hash < 0 ? reference : reference.left(hash);
        const QString fragment = hash < 0 ? QString() : reference.mid(hash + 1);
        QString targetPath = currentSchemaPath;
        if (!targetName.isEmpty()) {
            require(!targetName.contains(QLatin1Char('/')) &&
                        targetName.endsWith(QStringLiteral(".schema.json")),
                    location + QStringLiteral(": non-local schema reference"));
            targetPath = QStringLiteral("schemas/") + targetName;
        }
        require(schemaPaths.values().contains(targetPath),
                location + QStringLiteral(": referenced schema is not catalogued: ") + targetPath);
        const QString targetId = schemaPaths.key(targetPath);
        resolveJsonPointer(schemas.value(targetId), fragment,
                           location + QStringLiteral(": ") + reference);
    }
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        collectAndResolveReferences(iterator.value(), currentSchemaPath, schemaPaths, schemas,
                                    location + QLatin1Char('/') + iterator.key());
    }
}

void requireClosedRootReference(const QString &reference,
                                const QString &currentSchemaPath,
                                const QHash<QString, QString> &schemaPaths,
                                const QHash<QString, QJsonObject> &schemas,
                                const QString &location) {
    const qsizetype hash = reference.indexOf(QLatin1Char('#'));
    const QString targetName = hash < 0 ? reference : reference.left(hash);
    const QString fragment = hash < 0 ? QString() : reference.mid(hash + 1);
    const QString targetPath = targetName.isEmpty()
                                   ? currentSchemaPath
                                   : QStringLiteral("schemas/") + targetName;
    const QString targetId = schemaPaths.key(targetPath);
    require(!targetId.isEmpty(), location + QStringLiteral(": root reference is not catalogued"));
    const auto target = resolveJsonPointer(schemas.value(targetId), fragment, location).toObject();
    require(target.value(QStringLiteral("type")).toString() == QStringLiteral("object") &&
                target.value(QStringLiteral("additionalProperties")) == QJsonValue(false),
            location + QStringLiteral(": referenced root target must be a closed object"));
}

void testSchemaCatalogAndReferencesAreClosed() {
    const auto catalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/schema-catalog.json"));
    const QStringList catalogKeys = catalog.keys();
    require(QSet<QString>(catalogKeys.cbegin(), catalogKeys.cend()) ==
                QSet<QString>{QStringLiteral("schema"), QStringLiteral("items")},
            QStringLiteral("schema catalog root must be closed"));
    require(catalog.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("ipcraft.contract-schema-catalog.v1"),
            QStringLiteral("schema catalog identity mismatch"));
    const auto items = catalog.value(QStringLiteral("items")).toArray();
    require(items.size() == 19, QStringLiteral("Gate 0 must catalog exactly 19 schemas"));

    QStringList ids;
    QStringList paths;
    QHash<QString, QString> schemaPaths;
    QHash<QString, QJsonObject> schemas;
    for (const auto &raw : items) {
        const auto item = raw.toObject();
        const QStringList itemKeys = item.keys();
        require(QSet<QString>(itemKeys.cbegin(), itemKeys.cend()) ==
                    QSet<QString>{QStringLiteral("id"), QStringLiteral("path"),
                                  QStringLiteral("freezeGate")},
                QStringLiteral("schema catalog item must be closed"));
        const QString id = item.value(QStringLiteral("id")).toString();
        const QString path = item.value(QStringLiteral("path")).toString();
        require(item.value(QStringLiteral("freezeGate")).toString() == QStringLiteral("core"),
                id + QStringLiteral(": freeze gate must be core"));
        ids.append(id);
        paths.append(path);
        const auto schema = ContractArtifactLoader::loadObject(
            QStringLiteral("docs/contracts/") + path);
        require(schema.value(QStringLiteral("$schema")).toString() ==
                    QStringLiteral("https://json-schema.org/draft/2020-12/schema"),
                id + QStringLiteral(": Draft 2020-12 identity mismatch"));
        require(schema.value(QStringLiteral("$id")).toString() == id,
                id + QStringLiteral(": $id mismatch"));
        const bool directlyClosed =
            schema.value(QStringLiteral("type")).toString() == QStringLiteral("object") &&
            schema.value(QStringLiteral("additionalProperties")) == QJsonValue(false);
        const bool closedByReference = schema.value(QStringLiteral("$ref")).isString();
        const bool closedUnion = id == QStringLiteral("ipcraft.core-canonical-models.v1") &&
                                 !schema.value(QStringLiteral("oneOf")).toArray().isEmpty();
        require(directlyClosed || closedByReference || closedUnion,
                id + QStringLiteral(": root must be closed directly or by catalogued reference"));
        schemaPaths.insert(id, path);
        schemas.insert(id, schema);
    }
    require(ids == sortedUniqueStrings(ids, QStringLiteral("schema IDs")),
            QStringLiteral("schema catalog must be sorted by ID"));
    sortedUniqueStrings(paths, QStringLiteral("schema paths"));
    for (auto iterator = schemas.begin(); iterator != schemas.end(); ++iterator) {
        collectAndResolveReferences(iterator.value(), schemaPaths.value(iterator.key()),
                                    schemaPaths, schemas, iterator.key());
        const auto schema = iterator.value();
        if (schema.value(QStringLiteral("$ref")).isString()) {
            requireClosedRootReference(schema.value(QStringLiteral("$ref")).toString(),
                                       schemaPaths.value(iterator.key()), schemaPaths, schemas,
                                       iterator.key());
        }
        for (const auto &raw : schema.value(QStringLiteral("oneOf")).toArray()) {
            const QString reference = raw.toObject().value(QStringLiteral("$ref")).toString();
            require(!reference.isEmpty(),
                    iterator.key() + QStringLiteral(": root oneOf branches must be references"));
            requireClosedRootReference(reference, schemaPaths.value(iterator.key()), schemaPaths,
                                       schemas, iterator.key());
        }
    }
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
        testCanonicalHelperRejectsMissingRulesAndNonFiniteNumbers();
        testCanonicalHelperSupportsFrozenSpecialSortKeys();
        testCanonicalHelperValidatesDerivedOrderAndAmbiguousBindings();
        testEndpointSortKeysKeepOpaqueTupleComponentsDistinct();
        testCanonicalRuleWildcardsTrackArrayDescentOnly();
        testSchemaCatalogAndReferencesAreClosed();
        std::cout << "noc_contract_schema_meta_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
