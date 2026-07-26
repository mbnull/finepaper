#include "application/application.h"

#include "execution/process.h"
#include "storage/json.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>

namespace finepaper {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& severity,
                      const QString& code,
                      const QString& message,
                      const QString& path,
                      const QString& source = QStringLiteral("finepaper")) {
    diagnostics.append(Diagnostic{severity, code, message, path, source});
}

QString designIdFromName(const QString& name) {
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("_"));
    id.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    return id.isEmpty() ? QStringLiteral("noc") : id;
}

std::optional<RouterPosition> requestRouter(const QJsonObject& endpoint,
                                            const QString& path,
                                            QVector<Diagnostic>& diagnostics) {
    QJsonValue value = endpoint.value(QStringLiteral("router"));
    if (endpoint.value(QStringLiteral("attachment")).isObject()) {
        value = endpoint.value(QStringLiteral("attachment"))
                    .toObject()
                    .value(QStringLiteral("router"));
    }
    if (value.isArray()) {
        const QJsonArray values = value.toArray();
        if (values.size() == 2 && values.at(0).isDouble() && values.at(1).isDouble()) {
            return RouterPosition{values.at(0).toInt(), values.at(1).toInt()};
        }
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("x")).isDouble() &&
            object.value(QStringLiteral("y")).isDouble()) {
            return RouterPosition{
                object.value(QStringLiteral("x")).toInt(),
                object.value(QStringLiteral("y")).toInt()
            };
        }
    }
    appendDiagnostic(diagnostics,
                     QStringLiteral("error"),
                     QStringLiteral("create.invalid_router"),
                     QStringLiteral("endpoint router must be [x, y] or an object with x and y"),
                     path);
    return std::nullopt;
}

QJsonObject defaultsFor(const QVector<ParameterDefinition>& definitions) {
    QJsonObject values;
    for (const ParameterDefinition& definition : definitions) {
        if (definition.hasDefault) {
            values.insert(definition.id, definition.defaultValue);
        }
    }
    return values;
}

void mergeValues(QJsonObject& target, const QJsonObject& source) {
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        target.insert(it.key(), it.value());
    }
}

QString routerKey(RouterPosition position) {
    return QStringLiteral("%1,%2").arg(position.x).arg(position.y);
}

QString packageExecutable(const PackageDefinition& package, const QString& relativePath) {
    return QDir(package.rootPath).absoluteFilePath(relativePath);
}

bool writeTextFile(const QString& path,
                   const QString& text,
                   QVector<Diagnostic>& diagnostics) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("run.log_write_failed"),
                         QStringLiteral("could not write log file"),
                         path,
                         QStringLiteral("execution"));
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("run.log_write_failed"),
                         QStringLiteral("could not write log file"),
                         path,
                         QStringLiteral("execution"));
        return false;
    }
    return true;
}

QVector<Diagnostic> diagnosticsFromResult(const QJsonObject& object,
                                          const QString& defaultSource) {
    QVector<Diagnostic> diagnostics;
    const QJsonArray values = object.value(QStringLiteral("diagnostics")).toArray();
    for (const QJsonValue& value : values) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject diagnostic = value.toObject();
        diagnostics.append(Diagnostic{
            diagnostic.value(QStringLiteral("severity")).toString(QStringLiteral("error")),
            diagnostic.value(QStringLiteral("code")).toString(QStringLiteral("package.error")),
            diagnostic.value(QStringLiteral("message")).toString(QStringLiteral("Package operation failed")),
            diagnostic.value(QStringLiteral("path")).toString(),
            diagnostic.value(QStringLiteral("source")).toString(defaultSource)
        });
    }
    return diagnostics;
}

QJsonObject packageReferenceToJson(const PackageReference& package) {
    return QJsonObject{
        {QStringLiteral("id"), package.id},
        {QStringLiteral("version"), package.version}
    };
}

bool artifactPathIsContained(const QString& rootPath,
                             const QString& relativePath,
                             QString* absolutePath) {
    if (relativePath.trimmed().isEmpty() || QFileInfo(relativePath).isAbsolute()) {
        return false;
    }
    const QFileInfo rootInfo(rootPath);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        return false;
    }
    const QFileInfo candidateInfo(QDir(rootPath).filePath(relativePath));
    const QString canonicalCandidate = candidateInfo.canonicalFilePath();
    if (canonicalCandidate.isEmpty()) {
        return false;
    }
    if (canonicalCandidate != canonicalRoot &&
        !canonicalCandidate.startsWith(canonicalRoot + QDir::separator())) {
        return false;
    }
    if (absolutePath) {
        *absolutePath = canonicalCandidate;
    }
    return true;
}

QJsonObject artifactToJson(const Artifact& artifact) {
    return QJsonObject{
        {QStringLiteral("id"), artifact.id},
        {QStringLiteral("type"), artifact.type},
        {QStringLiteral("path"), artifact.path},
        {QStringLiteral("primary"), artifact.primary}
    };
}

} // namespace

QVector<Diagnostic> FinepaperApplication::reloadPackages(const QStringList& roots) {
    return m_catalog.reload(roots);
}

const QVector<PackageDefinition>& FinepaperApplication::packages() const {
    return m_catalog.packages();
}

DesignResult FinepaperApplication::createDesign(const QJsonObject& request) const {
    DesignResult result;
    const QJsonObject packageObject = request.value(QStringLiteral("package")).toObject();
    PackageReference reference{
        packageObject.value(QStringLiteral("id")).toString(),
        packageObject.value(QStringLiteral("version")).toString()
    };
    const auto package = m_catalog.resolve(reference);
    if (!package) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.not_found"),
                         QStringLiteral("requested Package is not loaded"),
                         QStringLiteral("/package"));
        return result;
    }

    NocDesign design;
    design.package = reference;
    design.name = request.value(QStringLiteral("name")).toString().trimmed();
    design.id = request.value(QStringLiteral("id")).toString().trimmed();
    if (design.id.isEmpty()) {
        design.id = designIdFromName(design.name);
    }

    const QJsonObject topology = request.value(QStringLiteral("topology")).toObject();
    design.topology.type = topology.value(QStringLiteral("type"))
                               .toString(QStringLiteral("mesh"));
    design.topology.rows = topology.value(QStringLiteral("rows"))
                               .toInt(package->mesh.defaultRows);
    design.topology.columns = topology.value(QStringLiteral("columns"))
                                  .toInt(package->mesh.defaultColumns);

    design.parameters = defaultsFor(package->parameters);
    if (request.value(QStringLiteral("parameters")).isObject()) {
        mergeValues(design.parameters, request.value(QStringLiteral("parameters")).toObject());
    }

    const QJsonArray endpoints = request.value(QStringLiteral("endpoints")).toArray();
    for (qsizetype index = 0; index < endpoints.size(); ++index) {
        const QString base = QStringLiteral("/endpoints/%1").arg(index);
        if (!endpoints.at(index).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.invalid_endpoint"),
                             QStringLiteral("endpoint must be an object"),
                             base);
            continue;
        }
        const QJsonObject object = endpoints.at(index).toObject();
        EndpointInstance endpoint;
        endpoint.id = object.value(QStringLiteral("id")).toString();
        endpoint.type = object.value(QStringLiteral("type")).toString();
        const auto type = package->endpointType(endpoint.type);
        if (type) {
            endpoint.parameters = defaultsFor(type->parameters);
        }
        if (object.value(QStringLiteral("parameters")).isObject()) {
            mergeValues(endpoint.parameters,
                        object.value(QStringLiteral("parameters")).toObject());
        }
        const auto router = requestRouter(object,
                                          base + QStringLiteral("/router"),
                                          result.diagnostics);
        if (router) {
            endpoint.attachment.router = *router;
        }
        const QJsonObject attachment = object.value(QStringLiteral("attachment")).toObject();
        const QJsonValue slot = attachment.contains(QStringLiteral("slot"))
            ? attachment.value(QStringLiteral("slot"))
            : object.value(QStringLiteral("slot"));
        if (slot.isString() && !slot.toString().isEmpty()) {
            endpoint.attachment.slot = slot.toString();
        }
        design.endpoints.append(std::move(endpoint));
    }

    if (request.value(QStringLiteral("packageData")).isObject()) {
        design.packageData = request.value(QStringLiteral("packageData")).toObject();
    }

    result.diagnostics += validateDesignStructure(design);
    result.diagnostics += validateAgainstPackage(design, *package);
    result.success = !hasErrors(result.diagnostics);
    result.design = std::move(design);
    return result;
}

DesignResult FinepaperApplication::loadDesignFile(const QString& path) const {
    const DesignLoadResult loaded = finepaper::loadDesign(path);
    DesignResult result;
    result.success = loaded.success;
    result.design = loaded.design;
    result.diagnostics = loaded.diagnostics;
    return result;
}

bool FinepaperApplication::saveDesignFile(const QString& path,
                                          const NocDesign& design,
                                          QVector<Diagnostic>* diagnostics) const {
    return finepaper::saveDesign(path, design, diagnostics);
}

DesignResult FinepaperApplication::validateEditedDesign(const NocDesign& design) const {
    DesignResult result;
    result.design = design;
    result.diagnostics = validateDesignStructure(design);
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.not_found"),
                         QStringLiteral("design Package is not loaded"),
                         QStringLiteral("/package"));
    } else {
        result.diagnostics += validateAgainstPackage(design, *package);
    }
    result.success = !hasErrors(result.diagnostics);
    return result;
}

DesignResult FinepaperApplication::resizeMesh(const NocDesign& design,
                                               int rows,
                                               int columns) const {
    DesignResult result;
    result.design = design;
    if (rows < 1 || columns < 1) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("mesh.invalid_size"),
                         QStringLiteral("rows and columns must be positive"),
                         QStringLiteral("/topology"));
        return result;
    }
    for (qsizetype index = 0; index < design.endpoints.size(); ++index) {
        const RouterPosition router = design.endpoints.at(index).attachment.router;
        if (router.x >= columns || router.y >= rows) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("mesh.resize_would_detach_endpoint"),
                             QStringLiteral("resize would detach an existing Endpoint"),
                             QStringLiteral("/endpoints/%1/attachment/router").arg(index));
        }
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }
    result.design.topology.rows = rows;
    result.design.topology.columns = columns;
    return validateEditedDesign(result.design);
}

DesignResult FinepaperApplication::addEndpoint(const NocDesign& design,
                                                EndpointInstance endpoint) const {
    endpoint.id = endpoint.id.trimmed();
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    if (std::any_of(design.endpoints.cbegin(), design.endpoints.cend(),
                    [&](const EndpointInstance& existing) { return existing.id == endpoint.id; })) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("endpoint.duplicate_id"),
                         QStringLiteral("Endpoint id is already in use"),
                         QStringLiteral("/endpoints"));
        return result;
    }
    if (const EndpointTypeDefinition* type = package->endpointType(endpoint.type)) {
        const QJsonObject provided = endpoint.parameters;
        endpoint.parameters = defaultsFor(type->parameters);
        mergeValues(endpoint.parameters, provided);
    }
    NocDesign edited = design;
    edited.endpoints.append(std::move(endpoint));
    return validateEditedDesign(edited);
}

DesignResult FinepaperApplication::moveEndpoint(const NocDesign& design,
                                                 const QString& endpointId,
                                                 RouterPosition router) const {
    NocDesign edited = design;
    const auto it = std::find_if(edited.endpoints.begin(), edited.endpoints.end(),
                                 [&](const EndpointInstance& endpoint) {
                                     return endpoint.id == endpointId;
                                 });
    if (it == edited.endpoints.end()) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("endpoint.not_found"),
                         QStringLiteral("Endpoint does not exist"),
                         QStringLiteral("/endpoints"));
        return result;
    }
    it->attachment.router = router;
    return validateEditedDesign(edited);
}

DesignResult FinepaperApplication::removeEndpoint(const NocDesign& design,
                                                   const QString& endpointId) const {
    NocDesign edited = design;
    const auto it = std::find_if(edited.endpoints.begin(), edited.endpoints.end(),
                                 [&](const EndpointInstance& endpoint) {
                                     return endpoint.id == endpointId;
                                 });
    if (it == edited.endpoints.end()) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("endpoint.not_found"),
                         QStringLiteral("Endpoint does not exist"),
                         QStringLiteral("/endpoints"));
        return result;
    }
    edited.endpoints.erase(it);
    return validateEditedDesign(edited);
}

DesignResult FinepaperApplication::updateParameters(const NocDesign& design,
                                                     const QJsonObject& parameters) const {
    NocDesign edited = design;
    edited.parameters = parameters;
    return validateEditedDesign(edited);
}

QVector<Diagnostic> FinepaperApplication::validateAgainstPackage(
    const NocDesign& design,
    const PackageDefinition& package) const {
    QVector<Diagnostic> diagnostics;
    if (design.topology.rows < package.mesh.minimumRows ||
        design.topology.rows > package.mesh.maximumRows) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("mesh.rows_out_of_range"),
                         QStringLiteral("rows are outside the Package range"),
                         QStringLiteral("/topology/rows"));
    }
    if (design.topology.columns < package.mesh.minimumColumns ||
        design.topology.columns > package.mesh.maximumColumns) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("mesh.columns_out_of_range"),
                         QStringLiteral("columns are outside the Package range"),
                         QStringLiteral("/topology/columns"));
    }

    diagnostics += validateParameterObject(design.parameters,
                                           package.parameters,
                                           QStringLiteral("/parameters"),
                                           QStringLiteral("package"));

    QHash<QString, int> endpointCounts;
    QHash<QString, QSet<QString>> slotsByRouter;
    for (qsizetype index = 0; index < design.endpoints.size(); ++index) {
        const EndpointInstance& endpoint = design.endpoints.at(index);
        const QString base = QStringLiteral("/endpoints/%1").arg(index);
        const EndpointTypeDefinition* type = package.endpointType(endpoint.type);
        if (!type) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("endpoint.unknown_type"),
                             QStringLiteral("endpoint type is not declared by the Package"),
                             base + QStringLiteral("/type"),
                             QStringLiteral("package"));
            continue;
        }
        diagnostics += validateParameterObject(endpoint.parameters,
                                               type->parameters,
                                               base + QStringLiteral("/parameters"),
                                               QStringLiteral("package"));

        const QString router = routerKey(endpoint.attachment.router);
        endpointCounts[router] += 1;
        if (endpointCounts.value(router) > package.attachment.maxPerRouter) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("endpoint.router_capacity"),
                             QStringLiteral("Router endpoint capacity is exceeded"),
                             base + QStringLiteral("/attachment/router"),
                             QStringLiteral("package"));
        }

        if (package.attachment.slotMode == QStringLiteral("explicit")) {
            if (!endpoint.attachment.slot || endpoint.attachment.slot->isEmpty()) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("endpoint.slot_required"),
                                 QStringLiteral("this Package requires an explicit slot"),
                                 base + QStringLiteral("/attachment/slot"),
                                 QStringLiteral("package"));
            } else if (slotsByRouter[router].contains(*endpoint.attachment.slot)) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("endpoint.duplicate_slot"),
                                 QStringLiteral("slot is duplicated on the Router"),
                                 base + QStringLiteral("/attachment/slot"),
                                 QStringLiteral("package"));
            } else {
                slotsByRouter[router].insert(*endpoint.attachment.slot);
            }
        }
    }

    if (!package.engine && !design.packageData.isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("design.unexpected_package_data"),
                         QStringLiteral("simple Packages cannot use packageData"),
                         QStringLiteral("/packageData"),
                         QStringLiteral("package"));
    }
    return diagnostics;
}

QVector<Diagnostic> FinepaperApplication::runPackageValidation(
    const NocDesign& design,
    const PackageDefinition& package) const {
    QVector<Diagnostic> diagnostics;
    QString executable;
    int timeoutSeconds = package.generator.timeoutSeconds;
    QString source = QStringLiteral("generator");
    if (package.engine && package.engine->providesValidation) {
        executable = packageExecutable(package, package.engine->executable);
        timeoutSeconds = package.engine->timeoutSeconds;
        source = QStringLiteral("engine");
    } else if (package.generator.supportsValidate) {
        executable = packageExecutable(package, package.generator.executable);
    } else {
        return diagnostics;
    }

    QTemporaryDir runDirectory(QStringLiteral("/tmp/finepaper-validate-XXXXXX"));
    if (!runDirectory.isValid()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("run.create_failed"),
                         QStringLiteral("could not create validation run directory"),
                         QString(),
                         QStringLiteral("execution"));
        return diagnostics;
    }
    const QString inputDirectory = QDir(runDirectory.path()).filePath(QStringLiteral("input"));
    QDir().mkpath(inputDirectory);
    const QString inputPath = QDir(inputDirectory).filePath(QStringLiteral("design.json"));
    const QString resultPath = QDir(runDirectory.path()).filePath(QStringLiteral("result.json"));
    if (!finepaper::saveDesign(inputPath, withResolvedAutomaticSlots(design), &diagnostics)) {
        return diagnostics;
    }

    const ProcessResult process = runProcess(
        executable,
        QStringList{
            QStringLiteral("validate"),
            QStringLiteral("--design"),
            inputPath,
            QStringLiteral("--result"),
            resultPath
        },
        runDirectory.path(),
        timeoutSeconds * 1000);

    if (!process.started) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.start_failed"),
                         process.error,
                         executable,
                         source);
        return diagnostics;
    }
    if (process.timedOut) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.timed_out"),
                         QStringLiteral("Package validation timed out"),
                         executable,
                         source);
        return diagnostics;
    }
    if (process.crashed || process.exitCode != 0) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.failed"),
                         process.standardError.trimmed().isEmpty()
                             ? QStringLiteral("Package validation failed")
                             : process.standardError.trimmed(),
                         executable,
                         source);
        return diagnostics;
    }

    const JsonObjectLoadResult result = loadJsonObject(resultPath);
    diagnostics += result.diagnostics;
    if (!result.success) {
        return diagnostics;
    }
    diagnostics += diagnosticsFromResult(result.object, source);
    if (!result.object.value(QStringLiteral("success")).toBool(false) &&
        !hasErrors(diagnostics)) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.failed"),
                         QStringLiteral("Package validation returned success=false"),
                         resultPath,
                         source);
    }
    return diagnostics;
}

ValidationResult FinepaperApplication::validate(const NocDesign& design,
                                                bool includePackageValidation) const {
    ValidationResult result;
    result.diagnostics = validateDesignStructure(design);
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.not_found"),
                         QStringLiteral("design Package is not loaded"),
                         QStringLiteral("/package"));
        return result;
    }
    result.diagnostics += validateAgainstPackage(design, *package);
    if (!hasErrors(result.diagnostics) && includePackageValidation) {
        result.diagnostics += runPackageValidation(design, *package);
    }
    result.success = !hasErrors(result.diagnostics);
    return result;
}

GenerationResult FinepaperApplication::generate(
    const NocDesign& design,
    const GenerationOptions& options) const {
    GenerationResult result;
    result.package = design.package;
    const ValidationResult validation = validate(design, true);
    result.diagnostics = validation.diagnostics;
    if (!validation.success) {
        return result;
    }
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.not_found"),
                         QStringLiteral("design Package is not loaded"),
                         QStringLiteral("/package"));
        return result;
    }

    result.tool = ExecutionTool{
        QStringLiteral("generator"),
        package->generator.name,
        package->generator.version
    };
    result.operationId = QStringLiteral("op-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzz")))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    const QString outputRoot = QFileInfo(options.outputRoot).absoluteFilePath();
    result.runDirectory = QDir(outputRoot).filePath(
        QStringLiteral("runs/%1").arg(result.operationId));
    const QString inputDirectory = QDir(result.runDirectory).filePath(QStringLiteral("input"));
    result.outputDirectory = QDir(result.runDirectory).filePath(QStringLiteral("artifacts"));
    result.stdoutLog = QDir(result.runDirectory).filePath(QStringLiteral("stdout.log"));
    result.stderrLog = QDir(result.runDirectory).filePath(QStringLiteral("stderr.log"));
    const QString resultPath = QDir(result.runDirectory).filePath(QStringLiteral("result.json"));
    if (!QDir().mkpath(inputDirectory) || !QDir().mkpath(result.outputDirectory)) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("run.create_failed"),
                         QStringLiteral("could not create generation run directory"),
                         result.runDirectory,
                         QStringLiteral("execution"));
        return result;
    }
    const QString inputPath = QDir(inputDirectory).filePath(QStringLiteral("design.json"));
    if (!finepaper::saveDesign(inputPath,
                               withResolvedAutomaticSlots(design),
                               &result.diagnostics)) {
        return result;
    }

    const QString executable = packageExecutable(*package, package->generator.executable);
    const ProcessResult process = runProcess(
        executable,
        QStringList{
            QStringLiteral("generate"),
            QStringLiteral("--design"),
            inputPath,
            QStringLiteral("--output"),
            result.outputDirectory,
            QStringLiteral("--result"),
            resultPath
        },
        result.runDirectory,
        package->generator.timeoutSeconds * 1000);
    result.exitCode = process.exitCode;
    writeTextFile(result.stdoutLog, process.standardOutput, result.diagnostics);
    writeTextFile(result.stderrLog, process.standardError, result.diagnostics);

    if (!process.started) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.start_failed"),
                         process.error,
                         executable,
                         QStringLiteral("generator"));
        return result;
    }
    if (process.timedOut) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.timed_out"),
                         QStringLiteral("Generator timed out"),
                         executable,
                         QStringLiteral("generator"));
        return result;
    }
    if (process.crashed || process.exitCode != 0) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("operation.failed"),
                         process.standardError.trimmed().isEmpty()
                             ? QStringLiteral("Generator failed")
                             : process.standardError.trimmed(),
                         executable,
                         QStringLiteral("generator"));
        return result;
    }

    const JsonObjectLoadResult rawResult = loadJsonObject(resultPath);
    result.diagnostics += rawResult.diagnostics;
    if (!rawResult.success) {
        return result;
    }
    result.diagnostics += diagnosticsFromResult(rawResult.object,
                                                QStringLiteral("generator"));
    if (!rawResult.object.value(QStringLiteral("success")).toBool(false)) {
        if (!hasErrors(result.diagnostics)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("operation.failed"),
                             QStringLiteral("Generator returned success=false"),
                             resultPath,
                             QStringLiteral("generator"));
        }
        return result;
    }

    const QJsonArray artifacts = rawResult.object.value(QStringLiteral("artifacts")).toArray();
    QSet<QString> artifactIds;
    for (qsizetype index = 0; index < artifacts.size(); ++index) {
        const QString base = QStringLiteral("/artifacts/%1").arg(index);
        if (!artifacts.at(index).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("artifact.invalid"),
                             QStringLiteral("artifact must be an object"),
                             base,
                             QStringLiteral("generator"));
            continue;
        }
        const QJsonObject object = artifacts.at(index).toObject();
        Artifact artifact{
            object.value(QStringLiteral("id")).toString(),
            object.value(QStringLiteral("type")).toString(),
            object.value(QStringLiteral("path")).toString(),
            object.value(QStringLiteral("primary")).toBool(false)
        };
        if (artifact.id.isEmpty() || artifactIds.contains(artifact.id)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("artifact.invalid_id"),
                             QStringLiteral("artifact id is missing or duplicated"),
                             base + QStringLiteral("/id"),
                             QStringLiteral("generator"));
            continue;
        }
        QString absolutePath;
        if (!artifactPathIsContained(result.outputDirectory,
                                     artifact.path,
                                     &absolutePath)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("artifact.path_escape"),
                             QStringLiteral("artifact is missing or outside the output directory"),
                             base + QStringLiteral("/path"),
                             QStringLiteral("generator"));
            continue;
        }
        artifactIds.insert(artifact.id);
        result.artifacts.append(std::move(artifact));
    }

    result.success = !hasErrors(result.diagnostics);
    if (!result.success) {
        result.artifacts.clear();
    }
    return result;
}

QJsonObject generationResultToJson(const GenerationResult& result) {
    QJsonArray artifacts;
    for (const Artifact& artifact : result.artifacts) {
        artifacts.append(artifactToJson(artifact));
    }
    QJsonObject object{
        {QStringLiteral("success"), result.success},
        {QStringLiteral("operationId"), result.operationId},
        {QStringLiteral("runDirectory"), result.runDirectory},
        {QStringLiteral("outputDirectory"), result.outputDirectory},
        {QStringLiteral("package"), packageReferenceToJson(result.package)},
        {QStringLiteral("exitCode"), result.exitCode},
        {QStringLiteral("stdoutLog"), result.stdoutLog},
        {QStringLiteral("stderrLog"), result.stderrLog},
        {QStringLiteral("artifacts"), artifacts},
        {QStringLiteral("diagnostics"), diagnosticsToJson(result.diagnostics)}
    };
    if (result.tool) {
        object.insert(QStringLiteral("tool"), QJsonObject{
            {QStringLiteral("kind"), result.tool->kind},
            {QStringLiteral("name"), result.tool->name},
            {QStringLiteral("version"), result.tool->version}
        });
    }
    return object;
}

QJsonObject validationResultToJson(const ValidationResult& result) {
    return QJsonObject{
        {QStringLiteral("success"), result.success},
        {QStringLiteral("diagnostics"), diagnosticsToJson(result.diagnostics)}
    };
}

} // namespace finepaper
