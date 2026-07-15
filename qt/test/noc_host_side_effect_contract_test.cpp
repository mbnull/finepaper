#include "contractartifactloader.h"
#include "contracttesthelpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

namespace {

void verifySideEffectCatalogEnvelope() {
    const auto document = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/vectors/host-side-effects-v1.json"));
    requireContract(document.value(QStringLiteral("schema")).toString() ==
                        QStringLiteral("ipcraft.host-side-effect-behavior-vectors.v1") &&
                        document.value(QStringLiteral("contractVersion")).toString() ==
                        QStringLiteral("ipcraft.noc-side-effects.v1"),
                    QStringLiteral("Host side-effect catalog identity mismatch"));
    const auto cases = document.value(QStringLiteral("cases")).toArray();
    requireContract(cases.size() == 14,
                    QStringLiteral("Host side-effect catalog must contain exactly 14 cases"));
    QStringList ids;
    int confirmation = 0;
    int autoCommit = 0;
    int blocked = 0;
    int diagnostics = 0;
    for (const auto &raw : cases) {
        const auto wrapper = raw.toObject();
        ids.append(wrapper.value(QStringLiteral("caseId")).toString());
        const auto sideEffect = wrapper.value(QStringLiteral("document")).toObject();
        requireContract(sideEffect.value(QStringLiteral("schema")).toString() ==
                            QStringLiteral("ipcraft.noc-side-effects.v1"),
                        ids.constLast() + QStringLiteral(": wrong document schema"));
        const auto expected = sideEffect.value(QStringLiteral("expected")).toObject();
        const QString disposition =
            expected.value(QStringLiteral("commitDisposition")).toString();
        autoCommit += disposition == QStringLiteral("auto-commit");
        confirmation += disposition == QStringLiteral("confirmation-required");
        blocked += disposition == QStringLiteral("blocked");
        const QString expectedGroupState =
            disposition == QStringLiteral("confirmation-required")
                ? QStringLiteral("ready-to-commit")
                : disposition;
        requireContract(expectedGroupState ==
                            expected.value(QStringLiteral("groupState")).toString(),
                        ids.constLast() + QStringLiteral(": disposition/group state mismatch"));
        requireContract(expected.value(QStringLiteral("requiresConfirmation")).toBool() ==
                            (disposition == QStringLiteral("confirmation-required")),
                        ids.constLast() + QStringLiteral(": confirmation flag mismatch"));
        diagnostics += expected.value(QStringLiteral("coreDiagnostics")).toArray().size();
        requireContract(expected.value(QStringLiteral("applicationPatch")).toObject()
                                .value(QStringLiteral("source")).toObject()
                                .value(QStringLiteral("kind")).toString() ==
                            QStringLiteral("application-reconcile"),
                        ids.constLast() + QStringLiteral(": side effects must remain Host-authored"));
    }
    sortedUniqueStrings(ids, QStringLiteral("Host side-effect case IDs"));
    requireContract(autoCommit > 0 && confirmation > 0 && blocked > 0 && diagnostics > 0,
                    QStringLiteral("all side-effect dispositions and diagnostics need coverage"));
}

} // namespace


int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        verifySideEffectCatalogEnvelope();
        runContractPythonVerifier(
            QStringLiteral("docs/contracts/tools/verify_engine_side_effect_vectors.py"), {},
            QStringLiteral("14 causal side-effect cases; 169 mutations rejected"));
        runContractPythonVerifier(
            QStringLiteral("docs/contracts/tools/verify_engine_side_effect_contracts.py"), {},
            QStringLiteral("engine/side-effect witnesses passed:"));
        std::cout << "noc_host_side_effect_contract_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
