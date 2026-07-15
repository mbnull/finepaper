#include "contractartifactloader.h"
#include "contracttesthelpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>

#include <iostream>

namespace {

QStringList physicalFixturePaths() {
    const QString root = ContractArtifactLoader::repositoryRoot();
    const QDir repository(root);
    QStringList result;
    for (const QString &kind : {QStringLiteral("valid"), QStringLiteral("invalid")}) {
        const QString directory = root + QStringLiteral("/docs/contracts/fixtures/") + kind;
        QDirIterator iterator(directory, {QStringLiteral("*.json")}, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            result.append(repository.relativeFilePath(iterator.next()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void verifyCatalogTotalityInCpp() {
    const auto catalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/fixture-catalog.json"));
    requireContract(catalog.value(QStringLiteral("schema")).toString() ==
                        QStringLiteral("ipcraft.fixture-catalog.v1"),
                    QStringLiteral("fixture catalog identity mismatch"));
    const auto items = catalog.value(QStringLiteral("items")).toArray();
    requireContract(items.size() == 360,
                    QStringLiteral("fixture catalog must contain exactly 360 fixtures"));

    QStringList catalogPaths;
    int accept = 0;
    int reject = 0;
    for (const auto &raw : items) {
        const auto item = raw.toObject();
        const QString path = item.value(QStringLiteral("path")).toString();
        catalogPaths.append(QStringLiteral("docs/contracts/") + path);
        const QString expected = item.value(QStringLiteral("expected")).toString();
        accept += expected == QStringLiteral("accept");
        reject += expected == QStringLiteral("reject");
        ContractArtifactLoader::loadObject(QStringLiteral("docs/contracts/") + path);
    }
    requireContract(accept == 98 && reject == 262,
                    QStringLiteral("fixture accept/reject totals must be 98/262"));
    requireContract(catalogPaths == sortedUniqueStrings(catalogPaths,
                                                        QStringLiteral("fixture catalog paths")),
                    QStringLiteral("fixture catalog paths must be sorted"));
    requireContract(catalogPaths == physicalFixturePaths(),
                    QStringLiteral("fixture catalog and physical fixture tree differ"));
}

void verifyFixtureSemanticsWithPinnedTools() {
    runContractPythonVerifier(
        QStringLiteral("docs/contracts/tools/verify_fixture_catalog.py"), {},
        QStringLiteral("fixture catalog verification passed: 360 fixtures, 19 schemas, 75 error codes"));
    runContractPythonVerifier(
        QStringLiteral("docs/contracts/tools/verify_contract_fixtures.py"), {},
        QStringLiteral("contract fixture verification passed: 98 valid, 262 invalid; "
                       "41 schema-phase, 221 core-semantic; 18 standalone schema roots"));
    QDirIterator cacheIterator(ContractArtifactLoader::repositoryRoot(),
                               QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                               QDirIterator::Subdirectories);
    while (cacheIterator.hasNext()) {
        const QString path = cacheIterator.next();
        requireContract(!path.contains(QStringLiteral("/__pycache__")) &&
                            !path.endsWith(QStringLiteral(".pyc")),
                        QStringLiteral("delegated verifier created Python bytecode: ") + path);
    }
}

} // namespace


int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        verifyCatalogTotalityInCpp();
        verifyFixtureSemanticsWithPinnedTools();
        std::cout << "noc_contract_fixture_catalog_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
