#include "package/package.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace finepaper {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& severity,
                      const QString& code,
                      const QString& message,
                      const QString& path,
                      const QString& source = QStringLiteral("package")) {
    diagnostics.append(Diagnostic{severity, code, message, path, source});
}

std::optional<QJsonObject> readObject(const QString& path, QVector<Diagnostic>& diagnostics) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.read_failed"),
                         QStringLiteral("could not read %1").arg(path),
                         path);
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_json"),
                         error.errorString(),
                         path);
        return std::nullopt;
    }
    return document.object();
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.missing_field"),
                         QStringLiteral("%1 must be a non-empty string").arg(key),
                         path + QLatin1Char('/') + key);
        return {};
    }
    return value.toString().trimmed();
}

int requiredInteger(const QJsonObject& object,
                    const QString& key,
                    int fallback,
                    const QString& path,
                    QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_integer"),
                         QStringLiteral("%1 must be an integer").arg(key),
                         path + QLatin1Char('/') + key);
        return fallback;
    }
    return value.toInt();
}

ParameterDefinition parseParameter(const QJsonObject& object,
                                   const QString& path,
                                   QVector<Diagnostic>& diagnostics) {
    ParameterDefinition definition;
    definition.id = requiredString(object, QStringLiteral("id"), path, diagnostics);
    definition.type = requiredString(object, QStringLiteral("type"), path, diagnostics);
    definition.label = object.value(QStringLiteral("label")).toString(definition.id);
    if (object.contains(QStringLiteral("default"))) {
        definition.hasDefault = true;
        definition.defaultValue = object.value(QStringLiteral("default"));
    }
    if (object.value(QStringLiteral("minimum")).isDouble()) {
        definition.minimum = object.value(QStringLiteral("minimum")).toDouble();
    }
    if (object.value(QStringLiteral("maximum")).isDouble()) {
        definition.maximum = object.value(QStringLiteral("maximum")).toDouble();
    }
    const QJsonArray values = object.value(QStringLiteral("values")).toArray();
    for (const QJsonValue& value : values) {
        if (value.isString()) {
            definition.values.append(value.toString());
        }
    }
    return definition;
}

QVector<ParameterDefinition> parseParameters(const QJsonValue& value,
                                             const QString& path,
                                             QVector<Diagnostic>& diagnostics) {
    QVector<ParameterDefinition> definitions;
    if (value.isUndefined()) {
        return definitions;
    }
    if (!value.isArray()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_parameters"),
                         QStringLiteral("parameters must be an array"),
                         path);
        return definitions;
    }
    const QJsonArray array = value.toArray();
    QSet<QString> ids;
    definitions.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_parameter"),
                             QStringLiteral("parameter must be an object"),
                             QStringLiteral("%1/%2").arg(path).arg(index));
            continue;
        }
        ParameterDefinition definition = parseParameter(
            array.at(index).toObject(),
            QStringLiteral("%1/%2").arg(path).arg(index),
            diagnostics);
        if (!definition.id.isEmpty() && ids.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_parameter"),
                             QStringLiteral("parameter id is duplicated"),
                             QStringLiteral("%1/%2/id").arg(path).arg(index));
        }
        ids.insert(definition.id);
        definitions.append(std::move(definition));
    }
    return definitions;
}

bool pathIsInside(const QString& rootPath, const QString& candidatePath) {
    const QString root = QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath());
    const QString candidate = QDir::cleanPath(QFileInfo(candidatePath).absoluteFilePath());
    return candidate == root || candidate.startsWith(root + QDir::separator());
}

void validateExecutablePath(const QString& packageRoot,
                            const QString& relativePath,
                            const QString& jsonPath,
                            QVector<Diagnostic>& diagnostics) {
    if (relativePath.isEmpty()) {
        return;
    }
    const QString absolutePath = QDir(packageRoot).filePath(relativePath);
    if (!pathIsInside(packageRoot, absolutePath)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.executable_escape"),
                         QStringLiteral("executable path escapes package root"),
                         jsonPath);
        return;
    }
    const QFileInfo info(absolutePath);
    if (!info.isFile() || !info.isExecutable()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.executable_missing"),
                         QStringLiteral("executable does not exist or is not executable"),
                         jsonPath);
    }
}

bool valueMatchesType(const QJsonValue& value, const ParameterDefinition& definition) {
    if (definition.type == QStringLiteral("integer")) {
        return value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
    }
    if (definition.type == QStringLiteral("number")) {
        return value.isDouble();
    }
    if (definition.type == QStringLiteral("boolean")) {
        return value.isBool();
    }
    if (definition.type == QStringLiteral("string") ||
        definition.type == QStringLiteral("enum")) {
        return value.isString();
    }
    return false;
}

} // namespace

QString PackageDefinition::key() const {
    return id + QLatin1Char('@') + version;
}

const ParameterDefinition* PackageDefinition::parameter(const QString& id) const {
    const auto it = std::find_if(parameters.cbegin(), parameters.cend(), [&](const auto& value) {
        return value.id == id;
    });
    return it == parameters.cend() ? nullptr : &(*it);
}

const EndpointTypeDefinition* PackageDefinition::endpointType(const QString& id) const {
    const auto it = std::find_if(endpointTypes.cbegin(), endpointTypes.cend(), [&](const auto& value) {
        return value.id == id;
    });
    return it == endpointTypes.cend() ? nullptr : &(*it);
}

PackageLoadResult loadPackage(const QString& packageRoot) {
    PackageLoadResult result;
    const QString absoluteRoot = QDir::cleanPath(QFileInfo(packageRoot).absoluteFilePath());
    const QString manifestPath = QDir(absoluteRoot).filePath(QStringLiteral("package.json"));
    const auto rootObject = readObject(manifestPath, result.diagnostics);
    if (!rootObject) {
        return result;
    }

    PackageDefinition package;
    package.rootPath = absoluteRoot;
    package.format = requiredString(*rootObject,
                                    QStringLiteral("format"),
                                    QString(),
                                    result.diagnostics);
    package.formatVersion = requiredInteger(*rootObject,
                                            QStringLiteral("formatVersion"),
                                            0,
                                            QString(),
                                            result.diagnostics);
    package.id = requiredString(*rootObject, QStringLiteral("id"), QString(), result.diagnostics);
    package.name = requiredString(*rootObject, QStringLiteral("name"), QString(), result.diagnostics);
    package.version = requiredString(*rootObject,
                                     QStringLiteral("version"),
                                     QString(),
                                     result.diagnostics);
    if (package.format != QStringLiteral("finepaper.noc-package")) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.unsupported_format"),
                         QStringLiteral("format must be finepaper.noc-package"),
                         QStringLiteral("/format"));
    }
    if (package.formatVersion != 1) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.unsupported_version"),
                         QStringLiteral("formatVersion must be 1"),
                         QStringLiteral("/formatVersion"));
    }

    const QJsonObject mesh = rootObject->value(QStringLiteral("mesh")).toObject();
    const QJsonObject rows = mesh.value(QStringLiteral("rows")).toObject();
    const QJsonObject columns = mesh.value(QStringLiteral("columns")).toObject();
    package.mesh.minimumRows = requiredInteger(rows,
                                               QStringLiteral("min"),
                                               1,
                                               QStringLiteral("/mesh/rows"),
                                               result.diagnostics);
    package.mesh.maximumRows = requiredInteger(rows,
                                               QStringLiteral("max"),
                                               1,
                                               QStringLiteral("/mesh/rows"),
                                               result.diagnostics);
    package.mesh.defaultRows = requiredInteger(rows,
                                               QStringLiteral("default"),
                                               1,
                                               QStringLiteral("/mesh/rows"),
                                               result.diagnostics);
    package.mesh.minimumColumns = requiredInteger(columns,
                                                  QStringLiteral("min"),
                                                  1,
                                                  QStringLiteral("/mesh/columns"),
                                                  result.diagnostics);
    package.mesh.maximumColumns = requiredInteger(columns,
                                                  QStringLiteral("max"),
                                                  1,
                                                  QStringLiteral("/mesh/columns"),
                                                  result.diagnostics);
    package.mesh.defaultColumns = requiredInteger(columns,
                                                  QStringLiteral("default"),
                                                  1,
                                                  QStringLiteral("/mesh/columns"),
                                                  result.diagnostics);

    package.parameters = parseParameters(rootObject->value(QStringLiteral("parameters")),
                                         QStringLiteral("/parameters"),
                                         result.diagnostics);

    const QJsonArray endpointTypes = rootObject->value(QStringLiteral("endpointTypes")).toArray();
    QSet<QString> endpointIds;
    for (qsizetype index = 0; index < endpointTypes.size(); ++index) {
        const QString base = QStringLiteral("/endpointTypes/%1").arg(index);
        if (!endpointTypes.at(index).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.invalid_endpoint_type"),
                             QStringLiteral("endpoint type must be an object"),
                             base);
            continue;
        }
        const QJsonObject object = endpointTypes.at(index).toObject();
        EndpointTypeDefinition definition;
        definition.id = requiredString(object, QStringLiteral("id"), base, result.diagnostics);
        definition.label = object.value(QStringLiteral("label")).toString(definition.id);
        definition.icon = object.value(QStringLiteral("icon")).toString();
        definition.parameters = parseParameters(object.value(QStringLiteral("parameters")),
                                                base + QStringLiteral("/parameters"),
                                                result.diagnostics);
        if (!definition.id.isEmpty() && endpointIds.contains(definition.id)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.duplicate_endpoint_type"),
                             QStringLiteral("endpoint type id is duplicated"),
                             base + QStringLiteral("/id"));
        }
        endpointIds.insert(definition.id);
        package.endpointTypes.append(std::move(definition));
    }

    const QJsonObject attachment = rootObject->value(QStringLiteral("attachment")).toObject();
    package.attachment.maxPerRouter = requiredInteger(attachment,
                                                      QStringLiteral("maxPerRouter"),
                                                      1,
                                                      QStringLiteral("/attachment"),
                                                      result.diagnostics);
    package.attachment.slotMode = attachment.value(QStringLiteral("slotMode"))
                                      .toString(QStringLiteral("automatic"));
    if (package.attachment.slotMode != QStringLiteral("automatic") &&
        package.attachment.slotMode != QStringLiteral("explicit")) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.invalid_slot_mode"),
                         QStringLiteral("slotMode must be automatic or explicit"),
                         QStringLiteral("/attachment/slotMode"));
    }

    const QJsonObject generator = rootObject->value(QStringLiteral("generator")).toObject();
    package.generator.name = requiredString(generator,
                                            QStringLiteral("name"),
                                            QStringLiteral("/generator"),
                                            result.diagnostics);
    package.generator.version = requiredString(generator,
                                               QStringLiteral("version"),
                                               QStringLiteral("/generator"),
                                               result.diagnostics);
    package.generator.executable = requiredString(generator,
                                                  QStringLiteral("executable"),
                                                  QStringLiteral("/generator"),
                                                  result.diagnostics);
    package.generator.supportsValidate = generator.value(QStringLiteral("supportsValidate"))
                                             .toBool(false);
    package.generator.timeoutSeconds = generator.value(QStringLiteral("timeoutSeconds"))
                                           .toInt(300);
    validateExecutablePath(absoluteRoot,
                           package.generator.executable,
                           QStringLiteral("/generator/executable"),
                           result.diagnostics);

    if (rootObject->value(QStringLiteral("engine")).isObject()) {
        const QJsonObject engineObject = rootObject->value(QStringLiteral("engine")).toObject();
        EngineDefinition engine;
        engine.executable = requiredString(engineObject,
                                           QStringLiteral("executable"),
                                           QStringLiteral("/engine"),
                                           result.diagnostics);
        engine.providesValidation = engineObject.value(QStringLiteral("providesValidation"))
                                        .toBool(false);
        engine.timeoutSeconds = engineObject.value(QStringLiteral("timeoutSeconds")).toInt(1800);
        validateExecutablePath(absoluteRoot,
                               engine.executable,
                               QStringLiteral("/engine/executable"),
                               result.diagnostics);
        package.engine = std::move(engine);
    }

    result.success = !hasErrors(result.diagnostics);
    if (result.success) {
        result.package = std::move(package);
    }
    return result;
}

QVector<Diagnostic> validateParameterObject(
    const QJsonObject& values,
    const QVector<ParameterDefinition>& definitions,
    const QString& basePath,
    const QString& source) {
    QVector<Diagnostic> diagnostics;
    QSet<QString> knownIds;
    for (const ParameterDefinition& definition : definitions) {
        knownIds.insert(definition.id);
        if (!values.contains(definition.id)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.missing"),
                             QStringLiteral("parameter %1 is required").arg(definition.id),
                             basePath + QLatin1Char('/') + definition.id,
                             source);
            continue;
        }
        const QJsonValue value = values.value(definition.id);
        if (!valueMatchesType(value, definition)) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.invalid_type"),
                             QStringLiteral("parameter %1 has the wrong type").arg(definition.id),
                             basePath + QLatin1Char('/') + definition.id,
                             source);
            continue;
        }
        if (value.isDouble()) {
            const double number = value.toDouble();
            if (definition.minimum && number < *definition.minimum) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("parameter.below_minimum"),
                                 QStringLiteral("parameter %1 is below its minimum").arg(definition.id),
                                 basePath + QLatin1Char('/') + definition.id,
                                 source);
            }
            if (definition.maximum && number > *definition.maximum) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("parameter.above_maximum"),
                                 QStringLiteral("parameter %1 is above its maximum").arg(definition.id),
                                 basePath + QLatin1Char('/') + definition.id,
                                 source);
            }
        }
        if (definition.type == QStringLiteral("enum") &&
            !definition.values.contains(value.toString())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.invalid_enum"),
                             QStringLiteral("parameter %1 has an unsupported value").arg(definition.id),
                             basePath + QLatin1Char('/') + definition.id,
                             source);
        }
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!knownIds.contains(it.key())) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("parameter.unknown"),
                             QStringLiteral("parameter %1 is not declared by the Package").arg(it.key()),
                             basePath + QLatin1Char('/') + it.key(),
                             source);
        }
    }
    return diagnostics;
}

QVector<Diagnostic> PackageCatalog::reload(const QStringList& roots) {
    QVector<Diagnostic> diagnostics;
    QVector<PackageDefinition> loaded;
    QSet<QString> keys;

    for (const QString& rootValue : roots) {
        const QString rootPath = QDir::cleanPath(QFileInfo(rootValue).absoluteFilePath());
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("package.root_missing"),
                             QStringLiteral("package root does not exist"),
                             rootPath);
            continue;
        }

        QStringList packagePaths;
        if (QFileInfo(QDir(rootPath).filePath(QStringLiteral("package.json"))).isFile()) {
            packagePaths.append(rootPath);
        } else {
            const QFileInfoList children = QDir(rootPath).entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot,
                QDir::Name);
            for (const QFileInfo& child : children) {
                if (QFileInfo(QDir(child.absoluteFilePath())
                                  .filePath(QStringLiteral("package.json"))).isFile()) {
                    packagePaths.append(child.absoluteFilePath());
                }
            }
        }

        for (const QString& packagePath : packagePaths) {
            PackageLoadResult loadResult = loadPackage(packagePath);
            diagnostics += loadResult.diagnostics;
            if (!loadResult.success || !loadResult.package) {
                continue;
            }
            if (keys.contains(loadResult.package->key())) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("package.duplicate"),
                                 QStringLiteral("duplicate Package %1").arg(loadResult.package->key()),
                                 packagePath);
                continue;
            }
            keys.insert(loadResult.package->key());
            loaded.append(std::move(*loadResult.package));
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.key() < rhs.key();
    });
    m_packages = std::move(loaded);
    return diagnostics;
}

const QVector<PackageDefinition>& PackageCatalog::packages() const {
    return m_packages;
}

std::optional<PackageDefinition> PackageCatalog::resolve(
    const PackageReference& reference) const {
    const auto it = std::find_if(m_packages.cbegin(), m_packages.cend(), [&](const auto& package) {
        return package.id == reference.id && package.version == reference.version;
    });
    if (it == m_packages.cend()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace finepaper
