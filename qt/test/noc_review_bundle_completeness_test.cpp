#include "contractartifactloader.h"
#include "contracttesthelpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QFile>

#include <iostream>

namespace {

constexpr auto kMainSpec =
    "docs/superpowers/specs/2026-07-12-default-noc-design-engine-workbench-design.md";

QStringList activeAdrs() {
    const QString root = ContractArtifactLoader::repositoryRoot();
    const QDir repository(root);
    QStringList result;
    QDirIterator iterator(root + QStringLiteral("/docs/adr"), {QStringLiteral("*.md")},
                          QDir::Files);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = repository.relativeFilePath(absolute);
        const QString beginning = QString::fromUtf8(
            ContractArtifactLoader::loadBytes(relative).left(2048));
        if (!beginning.contains(QRegularExpression(QStringLiteral("superseded"),
                                                   QRegularExpression::CaseInsensitiveOption))) {
            result.append(relative);
        }
    }
    return sortedUniqueStrings(result, QStringLiteral("active ADR paths"));
}

QStringList exactFreezeSet() {
    QStringList paths{QStringLiteral("CONTEXT.md"), QString::fromUtf8(kMainSpec)};
    for (QChar appendix = QLatin1Char('a'); appendix <= QLatin1Char('f');
         appendix = QChar(appendix.unicode() + 1)) {
        const QStringList matches = QDir(ContractArtifactLoader::repositoryRoot() +
                                         QStringLiteral("/docs/superpowers/specs"))
                                        .entryList({QStringLiteral("appendix-") + appendix +
                                                    QStringLiteral("-*.md")}, QDir::Files);
        requireContract(matches.size() == 1,
                        QStringLiteral("Appendix %1 must resolve exactly once").arg(appendix));
        paths.append(QStringLiteral("docs/superpowers/specs/") + matches.first());
    }
    paths.append(activeAdrs());

    const QString root = ContractArtifactLoader::repositoryRoot();
    const QDir repository(root);
    QDirIterator contracts(root + QStringLiteral("/docs/contracts"), QDir::Files,
                           QDirIterator::Subdirectories);
    while (contracts.hasNext()) {
        const QString relative = repository.relativeFilePath(contracts.next());
        if (relative.contains(QStringLiteral("/__pycache__/")) ||
            relative.endsWith(QStringLiteral(".pyc"))) {
            continue;
        }
        if (relative == QStringLiteral("docs/contracts/freeze-inputs.json") ||
            relative == QStringLiteral("docs/contracts/CORE-FREEZE.md") ||
            relative == QStringLiteral("docs/contracts/GATE-STATUS.md")) {
            continue;
        }
        paths.append(relative);
    }
    return sortedUniqueStrings(paths, QStringLiteral("computed freeze paths"));
}

void verifyRelativeMarkdownLinks(const QStringList &paths) {
    const QString root = ContractArtifactLoader::repositoryRoot();
    const QRegularExpression link(QStringLiteral(R"(\[[^\]]*\]\(([^)]+)\))"));
    for (const QString &path : paths) {
        if (!path.endsWith(QStringLiteral(".md"))) {
            continue;
        }
        const QString text = QString::fromUtf8(ContractArtifactLoader::loadBytes(path));
        auto match = link.globalMatch(text);
        while (match.hasNext()) {
            validateFrozenMarkdownLink(root, path, match.next().captured(1),
                                       QSet<QString>(paths.cbegin(), paths.cend()));
        }
    }
}

void writeTestFile(const QString &path, const QByteArray &contents = {}) {
    QFile file(path);
    requireContract(file.open(QIODevice::WriteOnly), path + QStringLiteral(": test file open failed"));
    file.write(contents);
}

void testFrozenMarkdownLinkBoundaryMutations() {
    QTemporaryDir repository;
    QTemporaryDir external;
    requireContract(repository.isValid() && external.isValid(),
                    QStringLiteral("temporary Markdown repositories must exist"));
    QDir root(repository.path());
    requireContract(root.mkpath(QStringLiteral("docs/spec")),
                    QStringLiteral("temporary Markdown directory creation failed"));
    writeTestFile(root.filePath(QStringLiteral("docs/spec/source.md")));
    writeTestFile(root.filePath(QStringLiteral("docs/spec/frozen.md")));
    writeTestFile(root.filePath(QStringLiteral("omitted.txt")));
    writeTestFile(external.filePath(QStringLiteral("outside.md")));
    requireContract(QFile::link(external.filePath(QStringLiteral("outside.md")),
                                root.filePath(QStringLiteral("docs/spec/escape.md"))),
                    QStringLiteral("temporary symlink escape creation failed"));
    const QSet<QString> frozen{QStringLiteral("docs/spec/source.md"),
                               QStringLiteral("docs/spec/frozen.md")};

    validateFrozenMarkdownLink(repository.path(), QStringLiteral("docs/spec/source.md"),
                               QStringLiteral("#same-document"), frozen);
    validateFrozenMarkdownLink(repository.path(), QStringLiteral("docs/spec/source.md"),
                               QStringLiteral("frozen.md?view=1#section"), frozen);
    for (const QString &invalid : {QStringLiteral("../../../../etc/passwd"),
                                   QStringLiteral("../../omitted.txt"),
                                   QStringLiteral("escape.md"),
                                   QStringLiteral("missing.md")}) {
        bool rejected = false;
        try {
            validateFrozenMarkdownLink(repository.path(), QStringLiteral("docs/spec/source.md"),
                                       invalid, frozen);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        requireContract(rejected, invalid + QStringLiteral(": unsafe Markdown link was accepted"));
    }
}

void verifyFreezeManifest() {
    const auto manifest = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/freeze-inputs.json"));
    requireContract(manifest.value(QStringLiteral("schema")).toString() ==
                        QStringLiteral("ipcraft.core-freeze-inputs.v1") &&
                        manifest.value(QStringLiteral("normativeRevision")).toInt() == 5,
                    QStringLiteral("freeze input identity/revision mismatch"));
    requireContract(manifest.value(QStringLiteral("selfExclusion")).toString() ==
                        QStringLiteral("docs/contracts/freeze-inputs.json is intentionally omitted "
                                       "to avoid recursive self-digest"),
                    QStringLiteral("freeze manifest self-exclusion rule mismatch"));
    const auto items = manifest.value(QStringLiteral("files")).toArray();
    QStringList paths;
    for (const auto &raw : items) {
        const auto item = raw.toObject();
        const QStringList itemKeys = item.keys();
        requireContract(QSet<QString>(itemKeys.cbegin(), itemKeys.cend()) ==
                            QSet<QString>{QStringLiteral("path"), QStringLiteral("sha256"),
                                          QStringLiteral("role")},
                        QStringLiteral("freeze file entry must be closed"));
        const QString path = item.value(QStringLiteral("path")).toString();
        paths.append(path);
        requireContract(!QFileInfo(ContractArtifactLoader::repositoryRoot() +
                                   QLatin1Char('/') + path).isSymLink(),
                        path + QStringLiteral(": freeze input must not be a symlink"));
        const QString actual = QStringLiteral("sha256:") +
            QString::fromLatin1(QCryptographicHash::hash(
                ContractArtifactLoader::loadBytes(path), QCryptographicHash::Sha256).toHex());
        requireContract(item.value(QStringLiteral("sha256")).toString() == actual,
                        path + QStringLiteral(": raw-byte digest mismatch"));
        const QString expectedRole = path.contains(QStringLiteral("/tools/"))
                                         ? QStringLiteral("authoring-tool")
                                     : path.contains(QStringLiteral("/fixtures/"))
                                         ? QStringLiteral("generated-fixture")
                                         : QStringLiteral("normative-artifact");
        requireContract(item.value(QStringLiteral("role")).toString() == expectedRole,
                        path + QStringLiteral(": freeze role mismatch"));
    }
    requireContract(paths == sortedUniqueStrings(paths, QStringLiteral("freeze input paths")),
                    QStringLiteral("freeze input paths must be sorted"));
    requireContract(paths == exactFreezeSet(),
                    QStringLiteral("freeze input set differs from exact review bundle set"));
    verifyRelativeMarkdownLinks(paths);
}

void verifyStableErrorCatalog() {
    const auto catalog = ContractArtifactLoader::loadObject(
        QStringLiteral("docs/contracts/error-codes-v1.json"));
    const QStringList rootKeys = catalog.keys();
    requireContract(QSet<QString>(rootKeys.cbegin(), rootKeys.cend()) ==
                        QSet<QString>{QStringLiteral("schema"), QStringLiteral("version"),
                                      QStringLiteral("codes")},
                    QStringLiteral("error catalog root must be closed"));
    const auto entries = catalog.value(QStringLiteral("codes")).toArray();
    requireContract(entries.size() == 75,
                    QStringLiteral("error catalog must contain exactly 75 stable codes"));
    QStringList codes;
    QHash<QString, QString> owners;
    const QSet<QString> directOwnerPrefixes{
        QStringLiteral("command"), QStringLiteral("contract"), QStringLiteral("dependency"),
        QStringLiteral("diagnostic"), QStringLiteral("engine"), QStringLiteral("host"),
        QStringLiteral("output"), QStringLiteral("package"), QStringLiteral("patch"),
        QStringLiteral("pipeline"), QStringLiteral("project"), QStringLiteral("provider"),
        QStringLiteral("recovery"), QStringLiteral("tool")};
    const QSet<QString> sideEffectPrefixes{
        QStringLiteral("attachment"), QStringLiteral("domain"),
        QStringLiteral("engine_migration"), QStringLiteral("package_relation")};
    for (const auto &raw : entries) {
        const auto entry = raw.toObject();
        const QStringList keys = entry.keys();
        requireContract(QSet<QString>(keys.cbegin(), keys.cend()) ==
                            QSet<QString>{QStringLiteral("code"), QStringLiteral("owner"),
                                          QStringLiteral("blocking"),
                                          QStringLiteral("messageTemplate")},
                        QStringLiteral("error catalog entry must have exact V1 fields"));
        codes.append(entry.value(QStringLiteral("code")).toString());
        const QString prefix = codes.constLast().section(QLatin1Char('.'), 0, 0);
        const QString expectedOwner = sideEffectPrefixes.contains(prefix)
                                          ? QStringLiteral("host-side-effects")
                                      : directOwnerPrefixes.contains(prefix)
                                          ? prefix
                                          : QString();
        requireContract(!expectedOwner.isEmpty() &&
                            entry.value(QStringLiteral("owner")).toString() == expectedOwner,
                        codes.constLast() + QStringLiteral(": owner violates the closed prefix policy"));
        owners.insert(codes.constLast(), expectedOwner);
        requireContract(!entry.value(QStringLiteral("owner")).toString().isEmpty() &&
                            entry.value(QStringLiteral("blocking")).isBool() &&
                            !entry.value(QStringLiteral("messageTemplate")).toString().isEmpty(),
                        codes.constLast() + QStringLiteral(": error metadata is incomplete"));
    }
    requireContract(codes == sortedUniqueStrings(codes, QStringLiteral("stable error codes")),
                    QStringLiteral("stable error codes must be sorted"));
    for (auto expected : {
             std::pair{QStringLiteral("command.pending_candidate_required"), QStringLiteral("command")},
             std::pair{QStringLiteral("patch.schema_invalid"), QStringLiteral("patch")},
             std::pair{QStringLiteral("project.locked"), QStringLiteral("project")},
             std::pair{QStringLiteral("dependency.bundle_mismatch"), QStringLiteral("dependency")},
             std::pair{QStringLiteral("tool.timed_out"), QStringLiteral("tool")},
             std::pair{QStringLiteral("domain.disconnected"), QStringLiteral("host-side-effects")},
             std::pair{QStringLiteral("provider.timeout"), QStringLiteral("provider")}}) {
        requireContract(owners.value(expected.first) == expected.second,
                        expected.first + QStringLiteral(": representative owner mismatch"));
    }

    QString normativeText;
    for (const QString &path : {
             QStringLiteral("docs/superpowers/specs/appendix-b-patch-command-reconciliation-contract.md"),
             QStringLiteral("docs/superpowers/specs/appendix-c-package-contract-provider-tool-contract.md"),
             QStringLiteral("docs/superpowers/specs/appendix-f-core-canonical-models.md"),
             QStringLiteral("docs/contracts/fixture-error-policy-v1.json")}) {
        normativeText += QString::fromUtf8(ContractArtifactLoader::loadBytes(path));
    }
    const QRegularExpression literal(
        QStringLiteral(R"((?:attachment|command|contract|dependency|diagnostic|domain|engine|engine_migration|host|output|package|package_relation|patch|pipeline|project|provider|recovery|tool)\.[a-z0-9_]+)"));
    QSet<QString> mentioned;
    auto matches = literal.globalMatch(normativeText);
    while (matches.hasNext()) {
        mentioned.insert(matches.next().captured(0));
    }
    const QSet<QString> nonErrorLiterals{
        QStringLiteral("contract.v1"), QStringLiteral("host.v1"),
        QStringLiteral("output.json"), QStringLiteral("package.v1"),
        QStringLiteral("patch.v1"), QStringLiteral("pipeline.json"),
        QStringLiteral("provider.json"), QStringLiteral("recovery.v1"),
        QStringLiteral("tool.drc"), QStringLiteral("tool.generator"),
        QStringLiteral("engine.so")};
    mentioned.subtract(nonErrorLiterals);
    requireContract(mentioned == QSet<QString>(codes.cbegin(), codes.cend()),
                    QStringLiteral("error catalog differs from Appendix B/C/F and machine policy literals"));
}

} // namespace


int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        testFrozenMarkdownLinkBoundaryMutations();
        verifyStableErrorCatalog();
        verifyFreezeManifest();
        std::cout << "noc_review_bundle_completeness_test passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
