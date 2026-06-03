// Architecture foundation scan gate for ipcraft hard-cutover docs.
#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const QString& message) {
    if (!condition) {
        throw std::runtime_error(message.toStdString());
    }
}

QString readText(const QString& path) {
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + path);
    if (!file.exists()) {
        QFile sourceTreeFile(path);
        if (!sourceTreeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw std::runtime_error(("missing file: " + path).toStdString());
        }
        return QString::fromUtf8(sourceTreeFile.readAll());
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read file: " + path).toStdString());
    }
    return QString::fromUtf8(file.readAll());
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" must contain ") + needle);
}

void testDeletionMapCoversHardCutoverTargets() {
    const QString text =
        readText(QStringLiteral("docs/architecture/ipcraft-architecture-deletion-map.md"));
    const QStringList requiredTerms = {
        QStringLiteral("Graph / Module / Connection"),
        QStringLiteral("NodeEditorWidget"),
        QStringLiteral("ModuleRegistry"),
        QStringLiteral("ipcraft.noc.project.v1"),
        QStringLiteral("ProjectPatchCommand"),
        QStringLiteral("ToolInputBuilder"),
        QStringLiteral("packages/vendor-meshnoc"),
        QStringLiteral("delete"),
        QStringLiteral("replace"),
        QStringLiteral("adapter only"),
    };
    for (const QString& term : requiredTerms) {
        requireContains(text, term, QStringLiteral("deletion map"));
    }
}

void testSchemaMatrixListsAllPublicContracts() {
    const QString text =
        readText(QStringLiteral("docs/audit/ipcraft-public-schema-matrix.md"));
    const QStringList requiredTerms = {
        QStringLiteral("ipcraft.project.v1"),
        QStringLiteral("ipcraft.package.v1"),
        QStringLiteral("ipcraft.component.v1"),
        QStringLiteral("ipcraft.interface.v1"),
        QStringLiteral("ipcraft.connection_rules.v1"),
        QStringLiteral("ipcraft.topology.graph.v1"),
        QStringLiteral("ipcraft.topology.parametric.v1"),
        QStringLiteral("ipcraft.view.v1"),
        QStringLiteral("ipcraft.view.descriptor.v1"),
        QStringLiteral("ipcraft.tool.input.v1"),
        QStringLiteral("ipcraft.tool.result.v1"),
        QStringLiteral("ipcraft.diagnostic.v1"),
        QStringLiteral("ipcraft.artifact.v1"),
        QStringLiteral("ipcraft.patch.v1"),
        QStringLiteral("ipcraft.capability.noc.v1"),
        QStringLiteral("ipcraft.capability.noc.extension.v1"),
        QStringLiteral("parser"),
        QStringLiteral("writer"),
        QStringLiteral("roundtrip"),
        QStringLiteral("negative"),
        QStringLiteral("golden"),
    };
    for (const QString& term : requiredTerms) {
        requireContains(text, term, QStringLiteral("schema matrix"));
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testDeletionMapCoversHardCutoverTargets();
    testSchemaMatrixListsAllPublicContracts();
    std::cout << "ipcraft_architecture_foundation_scan_test passed\n";
    return 0;
}
