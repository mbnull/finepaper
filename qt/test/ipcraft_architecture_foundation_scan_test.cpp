// Architecture foundation scan gate for ipcraft hard-cutover docs.
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
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

QString resolveRepositoryPath(const QString& path) {
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../") + path,
        path,
    };

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists()) {
            return info.absoluteFilePath();
        }
    }

    throw std::runtime_error(("missing path: " + path).toStdString());
}

QString readFileText(const QString& filePath, const QString& displayPath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("cannot read file: " + displayPath).toStdString());
    }
    return QString::fromUtf8(file.readAll());
}

QString readText(const QString& path) {
    return readFileText(resolveRepositoryPath(path), path);
}

QJsonObject readJsonObject(const QString& path) {
    const QByteArray bytes = readFileText(resolveRepositoryPath(path), path).toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    require(document.isObject(), path + QStringLiteral(" must parse as a JSON object"));
    return document.object();
}

void requireContains(const QString& text, const QString& needle, const QString& context) {
    require(text.contains(needle), context + QStringLiteral(" must contain ") + needle);
}

void requireArrayContains(const QJsonArray& array,
                          const QString& expected,
                          const QString& context) {
    for (const QJsonValue& item : array) {
        if (item.toString() == expected) {
            return;
        }
    }

    require(false, context + QStringLiteral(" must contain ") + expected);
}

void requireObjectContainsKey(const QJsonObject& object,
                              const QString& key,
                              const QString& context) {
    require(object.contains(key), context + QStringLiteral(" must contain ") + key);
}

bool isGeneratedOrBuildPath(const QString& path) {
    const QString normalized = QStringLiteral("/") + QDir::cleanPath(path) + QStringLiteral("/");
    return normalized.contains(QStringLiteral("/.build/")) ||
           normalized.contains(QStringLiteral("/.xmake/")) ||
           normalized.contains(QStringLiteral("/build/")) ||
           normalized.contains(QStringLiteral("/CMakeFiles/")) ||
           normalized.contains(QStringLiteral("/generated/"));
}

QString displayPath(const QString& absolutePath) {
    return QDir(QDir::currentPath()).relativeFilePath(absolutePath);
}

int lineNumberForIndex(const QString& text, int index) {
    return text.left(index).count(QLatin1Char('\n')) + 1;
}

void failIfViolations(const QString& message, const QStringList& violations) {
    if (!violations.isEmpty()) {
        throw std::runtime_error((message + QStringLiteral("\n") + violations.join(QLatin1Char('\n')))
                                     .toStdString());
    }
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

void testProjectSchemaMatchesFoundationProjectDesignContract() {
    const QJsonObject schema =
        readJsonObject(QStringLiteral("schemas/ipcraft.project.v1.schema.json"));
    const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
    requireArrayContains(required,
                         QStringLiteral("schema"),
                         QStringLiteral("project schema required fields"));
    requireArrayContains(required,
                         QStringLiteral("id"),
                         QStringLiteral("project schema required fields"));
    requireArrayContains(required,
                         QStringLiteral("name"),
                         QStringLiteral("project schema required fields"));
    requireArrayContains(required,
                         QStringLiteral("packages"),
                         QStringLiteral("project schema required fields"));
    requireArrayContains(required,
                         QStringLiteral("components"),
                         QStringLiteral("project schema required fields"));

    const QJsonObject properties =
        schema.value(QStringLiteral("properties")).toObject();
    for (const QString& key : {QStringLiteral("id"),
                               QStringLiteral("name"),
                               QStringLiteral("packages"),
                               QStringLiteral("components"),
                               QStringLiteral("interfaces"),
                               QStringLiteral("connections"),
                               QStringLiteral("topologies"),
                               QStringLiteral("views"),
                               QStringLiteral("diagnostics"),
                               QStringLiteral("artifacts"),
                               QStringLiteral("extensions"),
                               QStringLiteral("metadata")}) {
        requireObjectContainsKey(properties,
                                 key,
                                 QStringLiteral("project schema top-level properties"));
    }

    require(!properties.contains(QStringLiteral("project")),
            QStringLiteral("project schema must not use the old project wrapper"));
    require(!properties.contains(QStringLiteral("instances")),
            QStringLiteral("project schema must not use the old instances root"));
    require(!properties.contains(QStringLiteral("composition")),
            QStringLiteral("project schema must not use the old composition root"));
    require(!properties.contains(QStringLiteral("layout")),
            QStringLiteral("project schema must keep layout under views"));
}

void testCoreSourceExcludesUiGraphAndLegacySymbols() {
    const QStringList roots = {
        QStringLiteral("qt/inc/ipcraft/core"),
        QStringLiteral("qt/src/ipcraft/core"),
    };
    const QStringList sourceFilePatterns = {
        QStringLiteral("*.c"),
        QStringLiteral("*.cc"),
        QStringLiteral("*.cpp"),
        QStringLiteral("*.cxx"),
        QStringLiteral("*.h"),
        QStringLiteral("*.hh"),
        QStringLiteral("*.hpp"),
        QStringLiteral("*.hxx"),
        QStringLiteral("*.ipp"),
    };
    const QStringList forbiddenTerms = {
        QStringLiteral("QWidget"),
        QStringLiteral("QGraphics"),
        QStringLiteral("NodeEditorWidget"),
        QStringLiteral("ModuleRegistry"),
        QStringLiteral("mesh_router"),
        QStringLiteral("ipcraft.noc.project.v1"),
        QStringLiteral("vendor.meshnoc"),
    };

    QStringList violations;
    for (const QString& root : roots) {
        const QFileInfo rootInfo(resolveRepositoryPath(root));
        require(rootInfo.isDir(), root + QStringLiteral(" must be a directory"));

        QDirIterator iterator(rootInfo.absoluteFilePath(),
                              sourceFilePatterns,
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString filePath = iterator.next();
            if (isGeneratedOrBuildPath(filePath)) {
                continue;
            }

            const QString text = readFileText(filePath, displayPath(filePath));
            for (const QString& term : forbiddenTerms) {
                const int index = text.indexOf(term);
                if (index >= 0) {
                    violations.append(QStringLiteral("%1:%2 contains forbidden term %3")
                                          .arg(displayPath(filePath))
                                          .arg(lineNumberForIndex(text, index))
                                          .arg(term));
                }
            }
        }
    }

    failIfViolations(QStringLiteral("core source must not reference UI, graph, or legacy symbols"),
                     violations);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testDeletionMapCoversHardCutoverTargets();
    testSchemaMatrixListsAllPublicContracts();
    testProjectSchemaMatchesFoundationProjectDesignContract();
    testCoreSourceExcludesUiGraphAndLegacySymbols();
    std::cout << "ipcraft_architecture_foundation_scan_test passed\n";
    return 0;
}
