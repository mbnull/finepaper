#include "contractartifactloader.h"
#include "contracttesthelpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

namespace {

QJsonValue pointer(const QJsonValue &root, const QString &value) {
    requireContract(value.startsWith(QLatin1Char('/')),
                    value + QStringLiteral(": canonical schema pointer must be RFC 6901"));
    QJsonValue current = root;
    for (QString token : value.mid(1).split(QLatin1Char('/'))) {
        token.replace(QStringLiteral("~1"), QStringLiteral("/"));
        token.replace(QStringLiteral("~0"), QStringLiteral("~"));
        if (current.isObject()) {
            requireContract(current.toObject().contains(token),
                            value + QStringLiteral(": unresolved pointer"));
            current = current.toObject().value(token);
        } else if (current.isArray()) {
            bool ok = false;
            const int index = token.toInt(&ok);
            requireContract(ok && QString::number(index) == token && index >= 0 &&
                                index < current.toArray().size(),
                            value + QStringLiteral(": unresolved array pointer"));
            current = current.toArray().at(index);
        } else {
            requireContract(false, value + QStringLiteral(": pointer descends through scalar"));
        }
    }
    return current;
}

void verifyCanonicalSchemaAgreement() {
    const auto schemaCatalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/schema-catalog.json"));
    QHash<QString, QJsonObject> schemas;
    for (const auto &raw : schemaCatalog.value(QStringLiteral("items")).toArray()) {
        const auto item = raw.toObject();
        schemas.insert(item.value(QStringLiteral("id")).toString(),
                       ContractArtifactLoader::loadObject(
                           QStringLiteral("docs/contracts/") +
                           item.value(QStringLiteral("path")).toString()));
    }
    const auto catalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-canonical-projection-v1.json"));
    const auto rules = catalog.value(QStringLiteral("canonicalCollections")).toArray();
    requireContract(rules.size() == 99,
                    QStringLiteral("canonical contract must contain 99 physical rules"));
    QStringList identities;
    for (const auto &raw : rules) {
        const auto rule = raw.toObject();
        const QString schemaId = rule.value(QStringLiteral("schemaId")).toString();
        const QString schemaPointer = rule.value(QStringLiteral("schemaPointer")).toString();
        identities.append(schemaId + QLatin1Char('#') + schemaPointer);
        requireContract(schemas.contains(schemaId), schemaId + QStringLiteral(": unknown schema"));
        const auto node = pointer(schemas.value(schemaId), schemaPointer).toObject();
        const auto metadata = node.value(QStringLiteral("x-ipcraft-canonical")).toObject();
        requireContract(metadata.value(QStringLiteral("kind")) == rule.value(QStringLiteral("kind")),
                        identities.constLast() + QStringLiteral(": canonical kind mismatch"));
        requireContract(metadata.value(QStringLiteral("sortKey")) ==
                            rule.value(QStringLiteral("sortKey")),
                        identities.constLast() + QStringLiteral(": canonical sort key mismatch"));
    }
    sortedUniqueStrings(identities, QStringLiteral("physical canonical rule identities"));

    const auto core = schemas.value(QStringLiteral("ipcraft.core-canonical-models.v1"));
    const auto definitions = core.value(QStringLiteral("$defs")).toObject();
    requireContract(definitions.size() == 73,
                    QStringLiteral("Core canonical model must expose exactly 73 frozen definitions"));
    for (const QString &required : {
             QStringLiteral("topologyIntent"), QStringLiteral("normalizedTopologyInput"),
             QStringLiteral("derivedState"), QStringLiteral("reconcileApplicability"),
             QStringLiteral("candidateTransaction"), QStringLiteral("impactReport"),
             QStringLiteral("patchBody"), QStringLiteral("patchOperations"),
             QStringLiteral("structureAuthority"), QStringLiteral("pipelinePlan"),
             QStringLiteral("outputManifest"), QStringLiteral("migrationContext")}) {
        requireContract(definitions.contains(required),
                        QStringLiteral("missing frozen Core definition ") + required);
    }

    const auto cases = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/core-set-permutation-v1.json"))
                           .value(QStringLiteral("cases"))
                           .toArray();
    QStringList caseIdentities;
    for (const auto &raw : cases) {
        const auto item = raw.toObject();
        caseIdentities.append(item.value(QStringLiteral("schemaId")).toString() +
                              QLatin1Char('#') +
                              item.value(QStringLiteral("schemaPointer")).toString());
    }
    requireContract(sortedUniqueStrings(caseIdentities, QStringLiteral("collection case identities")) ==
                        sortedUniqueStrings(identities, QStringLiteral("canonical rule identities")),
                    QStringLiteral("schema rules and collection vectors must agree exactly"));
}

} // namespace


int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        verifyCanonicalSchemaAgreement();
        runContractPythonVerifier(
            QStringLiteral("docs/contracts/tools/verify_canonical_rules.py"), {},
            QStringLiteral("99 Core array locations"));
        std::cout << "noc_core_canonical_models_schema_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
