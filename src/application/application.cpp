#include "application/application.h"

#include "application/design_extension_references.h"
#include "application/domain_runtime_validation.h"
#include "application/domain_service.h"

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
#include <cmath>
#include <limits>
#include <optional>

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

QString jsonPointerToken(QString value) {
    value.replace(QLatin1Char('~'), QStringLiteral("~0"));
    value.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return value;
}

QString designIdFromName(const QString& name) {
    QString id = name.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    id.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (id.isEmpty()) {
        return QStringLiteral("noc");
    }
    if (!QRegularExpression(QStringLiteral("^[a-z_]")).match(id).hasMatch()) {
        id.prepend(QStringLiteral("noc_"));
    }
    return id;
}

std::optional<int> requestInteger(const QJsonValue& value,
                                  const QString& path,
                                  QVector<Diagnostic>& diagnostics) {
    if (!value.isDouble() ||
        !std::isfinite(value.toDouble()) ||
        std::floor(value.toDouble()) != value.toDouble() ||
        value.toDouble() < static_cast<double>(std::numeric_limits<int>::min()) ||
        value.toDouble() > static_cast<double>(std::numeric_limits<int>::max())) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("create.expected_integer"),
                         QStringLiteral("value must be an integer"),
                         path);
        return std::nullopt;
    }
    return value.toInt();
}

QString requestString(const QJsonObject& object,
                      const QString& key,
                      const QString& path,
                      const QString& fallback,
                      QVector<Diagnostic>& diagnostics) {
    if (!object.contains(key)) {
        return fallback;
    }
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("create.expected_string"),
                         QStringLiteral("%1 must be a string").arg(key),
                         path + QLatin1Char('/') + key);
        return fallback;
    }
    return value.toString();
}

std::optional<RouterPosition> requestRouter(const QJsonObject& endpoint,
                                            const QString& path,
                                            QVector<Diagnostic>& diagnostics) {
    QJsonValue value = endpoint.value(QStringLiteral("router"));
    if (endpoint.contains(QStringLiteral("attachment"))) {
        const QJsonValue attachmentValue = endpoint.value(QStringLiteral("attachment"));
        if (!attachmentValue.isObject()) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.expected_object"),
                             QStringLiteral("attachment must be an object"),
                             path.left(path.lastIndexOf(QLatin1Char('/'))) +
                                 QStringLiteral("/attachment"));
        } else {
            const QJsonObject attachment = attachmentValue.toObject();
            if (attachment.contains(QStringLiteral("router"))) {
                value = attachment.value(QStringLiteral("router"));
            }
        }
    }
    if (value.isArray()) {
        const QJsonArray values = value.toArray();
        if (values.size() == 2) {
            const auto x = requestInteger(values.at(0), path + QStringLiteral("/0"), diagnostics);
            const auto y = requestInteger(values.at(1), path + QStringLiteral("/1"), diagnostics);
            if (x && y) {
                return RouterPosition{*x, *y};
            }
            return std::nullopt;
        }
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const auto x = requestInteger(object.value(QStringLiteral("x")),
                                      path + QStringLiteral("/x"),
                                      diagnostics);
        const auto y = requestInteger(object.value(QStringLiteral("y")),
                                      path + QStringLiteral("/y"),
                                      diagnostics);
        if (x && y) {
            return RouterPosition{*x, *y};
        }
        return std::nullopt;
    }
    appendDiagnostic(diagnostics,
                     QStringLiteral("error"),
                     QStringLiteral("create.invalid_router"),
                     QStringLiteral("endpoint router must be [x, y] or an object with x and y"),
                     path);
    return std::nullopt;
}

template <typename Definition>
QJsonObject defaultsFor(const QVector<Definition>& definitions) {
    QJsonObject values;
    for (const Definition& definition : definitions) {
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

QVector<Diagnostic> validateDesignExtensionValue(
    const QJsonValue& value,
    const DesignExtensionDefinition& definition,
    const DesignDomainReferenceIndex* domains,
    const QString& basePath) {
    QVector<Diagnostic> diagnostics;
    if (definition.schemaStatus != json_schema::CompileStatus::Ready
        || !definition.compiledSchema) {
        const QString reason = definition.schemaIssues.isEmpty()
            ? QStringLiteral("the Package schema is not supported by this Finepaper build")
            : definition.schemaIssues.constFirst().message;
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.extension_schema_unsupported"),
            reason,
            basePath,
            QStringLiteral("package"));
        return diagnostics;
    }

    const json_schema::ValidationResult validation = json_schema::validate(
        *definition.compiledSchema, value);
    for (const json_schema::Issue& issue : validation.issues) {
        QString message = issue.message;
        if (!issue.schemaPointer.isEmpty()) {
            message += QStringLiteral(" (schema #%1)").arg(issue.schemaPointer);
        }
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.extension_schema_violation"),
            message,
            basePath + issue.instancePointer,
            QStringLiteral("package"));
    }
    if (!validation.success && validation.issues.isEmpty()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.extension_schema_violation"),
            QStringLiteral("the extension value does not satisfy its Package schema"),
            basePath,
            QStringLiteral("package"));
    }
    Q_ASSERT(domains || definition.domainReferences.isEmpty());
    if (validation.success && domains) {
        diagnostics += validateDesignExtensionDomainReferences(
            value, definition, *domains, basePath);
    }
    return diagnostics;
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
    const QJsonValue packageValue = request.value(QStringLiteral("package"));
    if (!packageValue.isObject()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("create.expected_object"),
                         QStringLiteral("package must be an object"),
                         QStringLiteral("/package"));
        return result;
    }
    const QJsonObject packageObject = packageValue.toObject();
    PackageReference reference{
        requestString(packageObject,
                      QStringLiteral("id"),
                      QStringLiteral("/package"),
                      QString(),
                      result.diagnostics).trimmed(),
        requestString(packageObject,
                      QStringLiteral("version"),
                      QStringLiteral("/package"),
                      QString(),
                      result.diagnostics).trimmed()
    };
    if (reference.id.isEmpty() || reference.version.isEmpty()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("create.missing_package"),
                         QStringLiteral("package id and version are required"),
                         QStringLiteral("/package"));
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }
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
    // Package and Design format versions evolve together. Copy the exact
    // supported Package version so newer capabilities are not silently
    // downgraded to a legacy Design shape.
    design.formatVersion = package->formatVersion;
    design.package = reference;
    design.name = requestString(request,
                                QStringLiteral("name"),
                                QString(),
                                QString(),
                                result.diagnostics).trimmed();
    design.id = requestString(request,
                              QStringLiteral("id"),
                              QString(),
                              QString(),
                              result.diagnostics).trimmed();
    if (design.id.isEmpty()) {
        design.id = designIdFromName(design.name);
    }

    QJsonObject topology;
    if (request.contains(QStringLiteral("topology"))) {
        if (!request.value(QStringLiteral("topology")).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.expected_object"),
                             QStringLiteral("topology must be an object"),
                             QStringLiteral("/topology"));
        } else {
            topology = request.value(QStringLiteral("topology")).toObject();
        }
    }
    design.topology.type = requestString(topology,
                                         QStringLiteral("type"),
                                         QStringLiteral("/topology"),
                                         QStringLiteral("mesh"),
                                         result.diagnostics);
    design.topology.rows = package->mesh.defaultRows;
    if (topology.contains(QStringLiteral("rows"))) {
        const auto rows = requestInteger(topology.value(QStringLiteral("rows")),
                                         QStringLiteral("/topology/rows"),
                                         result.diagnostics);
        if (rows) {
            design.topology.rows = *rows;
        }
    }
    design.topology.columns = package->mesh.defaultColumns;
    if (topology.contains(QStringLiteral("columns"))) {
        const auto columns = requestInteger(topology.value(QStringLiteral("columns")),
                                            QStringLiteral("/topology/columns"),
                                            result.diagnostics);
        if (columns) {
            design.topology.columns = *columns;
        }
    }

    design.parameters = defaultsFor(package->parameters);
    if (request.contains(QStringLiteral("parameters"))) {
        if (!request.value(QStringLiteral("parameters")).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.expected_object"),
                             QStringLiteral("parameters must be an object"),
                             QStringLiteral("/parameters"));
        } else {
            mergeValues(design.parameters,
                        request.value(QStringLiteral("parameters")).toObject());
        }
    }

    QJsonArray endpoints;
    if (request.contains(QStringLiteral("endpoints"))) {
        if (!request.value(QStringLiteral("endpoints")).isArray()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.expected_array"),
                             QStringLiteral("endpoints must be an array"),
                             QStringLiteral("/endpoints"));
        } else {
            endpoints = request.value(QStringLiteral("endpoints")).toArray();
        }
    }
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
        endpoint.id = requestString(object,
                                    QStringLiteral("id"),
                                    base,
                                    QString(),
                                    result.diagnostics).trimmed();
        endpoint.type = requestString(object,
                                      QStringLiteral("type"),
                                      base,
                                      QString(),
                                      result.diagnostics).trimmed();
        const auto type = package->endpointType(endpoint.type);
        if (type) {
            endpoint.parameters = defaultsFor(type->parameters);
        }
        if (object.contains(QStringLiteral("parameters"))) {
            if (!object.value(QStringLiteral("parameters")).isObject()) {
                appendDiagnostic(result.diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("create.expected_object"),
                                 QStringLiteral("parameters must be an object"),
                                 base + QStringLiteral("/parameters"));
            } else {
                mergeValues(endpoint.parameters,
                            object.value(QStringLiteral("parameters")).toObject());
            }
        }
        const auto router = requestRouter(object,
                                          base + QStringLiteral("/router"),
                                          result.diagnostics);
        if (router) {
            endpoint.attachment.router = *router;
        }
        QJsonValue slot = object.value(QStringLiteral("slot"));
        if (object.value(QStringLiteral("attachment")).isObject()) {
            const QJsonObject attachment = object.value(QStringLiteral("attachment")).toObject();
            if (attachment.contains(QStringLiteral("slot"))) {
                slot = attachment.value(QStringLiteral("slot"));
            }
        }
        if (!slot.isUndefined()) {
            if (!slot.isString()) {
                appendDiagnostic(result.diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("create.expected_string"),
                                 QStringLiteral("slot must be a string"),
                                 base + QStringLiteral("/attachment/slot"));
            } else if (!slot.toString().isEmpty()) {
                endpoint.attachment.slot = slot.toString();
            }
        }
        design.endpoints.append(std::move(endpoint));
    }

    const QString domainConfigurationKey = QStringLiteral("domainConfiguration");
    if (request.contains(domainConfigurationKey)) {
        if (!formatVersionSupportsDomains(package->formatVersion)) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("create.domain_configuration_requires_v2"),
                             QStringLiteral("domainConfiguration requires a Package format with Domain support"),
                QStringLiteral("/domainConfiguration"));
            result.design = std::move(design);
            return result;
        } else if (!request.value(domainConfigurationKey).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.expected_object"),
                             QStringLiteral("domainConfiguration must be an object"),
                             QStringLiteral("/domainConfiguration"));
            result.design = std::move(design);
            return result;
        } else {
            domain_configuration::ParseResult parsed = domain_configuration::parse(
                request.value(domainConfigurationKey).toObject(), design);
            result.diagnostics += std::move(parsed.diagnostics);
            if (!parsed.success) {
                result.design = std::move(design);
                return result;
            }
            design = domain_configuration::replace(
                design, std::move(parsed.configuration));
        }
    } else {
        domain_service::MutationResult domainResult =
            domain_service::materializeRequiredDomains(design, *package);
        design = std::move(domainResult.design);
        result.diagnostics += std::move(domainResult.diagnostics);
    }

    const QString elementConfigurationsKey =
        QStringLiteral("elementConfigurations");
    if (request.contains(elementConfigurationsKey)) {
        if (!formatVersionSupportsElementConfigurations(
                package->formatVersion)) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("error"),
                QStringLiteral("create.element_configurations_require_v3"),
                QStringLiteral(
                    "elementConfigurations requires a Package format with element-configuration support"),
                QStringLiteral("/elementConfigurations"));
        } else {
            ElementConfigurationsParseResult parsed =
                parseElementConfigurations(
                    request.value(elementConfigurationsKey));
            design.elementConfigurations = std::move(parsed.configurations);
            result.diagnostics += std::move(parsed.diagnostics);
        }
    }

    if (request.contains(QStringLiteral("packageData"))) {
        if (!request.value(QStringLiteral("packageData")).isObject()) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("create.expected_object"),
                             QStringLiteral("packageData must be an object"),
                             QStringLiteral("/packageData"));
        } else {
            design.packageData = request.value(QStringLiteral("packageData")).toObject();
        }
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
    const DesignResult validation = validateEditedDesign(design);
    if (!validation.success) {
        if (diagnostics) {
            *diagnostics += validation.diagnostics;
        }
        return false;
    }
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

DesignResult FinepaperApplication::resizeMesh(
    const NocDesign& design,
    int rows,
    int columns,
    const QVector<DomainMembership>& newRouterMemberships,
    const MeshResizeImpactConfirmation& confirmation) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    domain_service::MutationResult domainResult = domain_service::resizeMesh(
        design, *package, rows, columns, newRouterMemberships, confirmation);
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    QVector<Diagnostic> combinedDiagnostics =
        std::move(domainResult.diagnostics);
    combinedDiagnostics += validated.diagnostics;
    validated.diagnostics = std::move(combinedDiagnostics);
    validated.success = !hasErrors(validated.diagnostics);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::addEndpoint(
    const NocDesign& design,
    EndpointInstance endpoint,
    const QHash<QString, QStringList>& domainAssignments,
    const QVector<DomainEdgeOverride>& attachmentOverrides,
    const QVector<ElementConfiguration>& attachmentConfigurations) const {
    endpoint.id = endpoint.id.trimmed();
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    if (std::any_of(design.endpoints.cbegin(), design.endpoints.cend(),
                    [&](const EndpointInstance& existing) {
                        return existing.id == endpoint.id;
                    })) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("endpoint.duplicate_id"),
                         QStringLiteral("Endpoint id is already in use"),
                         QStringLiteral("/endpoints"));
        return result;
    }
    QVector<Diagnostic> overrideDiagnostics;
    const ElementRef expectedAttachment{
        ElementKind::EndpointAttachment, endpoint.id};
    for (qsizetype index = 0; index < attachmentOverrides.size(); ++index) {
        if (attachmentOverrides.at(index).edge == expectedAttachment) {
            continue;
        }
        appendDiagnostic(
            overrideDiagnostics,
            QStringLiteral("error"),
            QStringLiteral("endpoint.attachment_override_mismatch"),
            QStringLiteral(
                "Endpoint attachment override must reference the Endpoint "
                "being added"),
            QStringLiteral("/edgeOverrides/new-endpoint/%1/edge").arg(index));
    }
    for (qsizetype index = 0;
         index < attachmentConfigurations.size(); ++index) {
        if (attachmentConfigurations.at(index).element
            == expectedAttachment) {
            continue;
        }
        appendDiagnostic(
            overrideDiagnostics,
            QStringLiteral("error"),
            QStringLiteral("endpoint.attachment_configuration_mismatch"),
            QStringLiteral(
                "Endpoint attachment configuration must reference the Endpoint being added"),
            QStringLiteral("/elementConfigurations/new-endpoint/%1/element")
                .arg(index));
    }
    if (hasErrors(overrideDiagnostics)) {
        return DesignResult{
            false,
            design,
            std::move(overrideDiagnostics)
        };
    }
    if (const EndpointTypeDefinition* type = package->endpointType(endpoint.type)) {
        const QJsonObject provided = endpoint.parameters;
        endpoint.parameters = defaultsFor(type->parameters);
        mergeValues(endpoint.parameters, provided);
    }
    domain_service::MutationResult domainResult = domain_service::addEndpoint(
        design, *package, std::move(endpoint), domainAssignments);
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{
            false,
            design,
            std::move(domainResult.diagnostics)
        };
    }
    domainResult.design.edgeOverrides += attachmentOverrides;
    domainResult.design.elementConfigurations += attachmentConfigurations;
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::moveEndpoint(const NocDesign& design,
                                                 const QString& endpointId,
                                                 RouterPosition router,
                                                 std::optional<QString> slot) const {
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
    it->attachment.slot = std::move(slot);
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
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
    edited = domain_service::removeEndpointReferences(edited, endpointId);
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::updateEndpointParameters(
    const NocDesign& design,
    const QString& endpointId,
    const QJsonObject& parameters) const {
    NocDesign edited = design;
    const auto endpoint = std::find_if(
        edited.endpoints.begin(),
        edited.endpoints.end(),
        [&](const EndpointInstance& value) { return value.id == endpointId; });
    if (endpoint == edited.endpoints.end()) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("endpoint.not_found"),
                         QStringLiteral("Endpoint does not exist"),
                         QStringLiteral("/endpoints"));
        return result;
    }
    endpoint->parameters = parameters;
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

EndpointTypeChangePlan FinepaperApplication::planEndpointTypeChange(
    const NocDesign& design,
    const QString& endpointId,
    const QString& targetType,
    EndpointParameterMigration migration,
    const QJsonObject& parameterPatch) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        EndpointTypeChangePlan plan;
        plan.endpointId = endpointId;
        plan.targetType = targetType.trimmed();
        plan.parameterMigration = migration;
        appendDiagnostic(plan.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("package.not_found"),
                         QStringLiteral("design Package is not loaded"),
                         QStringLiteral("/package"));
        return plan;
    }

    EndpointTypeChangePlan plan = endpoint_configuration::buildTypeChangePlan(
        design,
        *package,
        endpointId,
        targetType,
        migration,
        parameterPatch);
    if (hasErrors(plan.diagnostics)) {
        return plan;
    }

    // Validate the exact post-impact candidate during preview so the caller
    // never receives an apparently applicable plan that would fail after it
    // echoes the required confirmation.
    EndpointTypeChangeImpactConfirmation previewConfirmation;
    previewConfirmation.removedAttachmentConfigurations =
        plan.removedAttachmentConfigurations;
    endpoint_configuration::MutationResult preview =
        endpoint_configuration::applyTypeChange(
            design, plan, previewConfirmation);
    if (hasErrors(preview.diagnostics)) {
        plan.diagnostics = std::move(preview.diagnostics);
        return plan;
    }
    plan.diagnostics = std::move(preview.diagnostics);
    const DesignResult validated = validateEditedDesign(preview.design);
    plan.diagnostics += validated.diagnostics;
    return plan;
}

DesignResult FinepaperApplication::changeEndpointType(
    const NocDesign& design,
    const QString& endpointId,
    const QString& targetType,
    EndpointParameterMigration migration,
    const QJsonObject& parameterPatch,
    const EndpointTypeChangeImpactConfirmation& confirmation) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }

    const EndpointTypeChangePlan plan =
        endpoint_configuration::buildTypeChangePlan(
            design,
            *package,
            endpointId,
            targetType,
            migration,
            parameterPatch);
    if (hasErrors(plan.diagnostics)) {
        return DesignResult{false, design, plan.diagnostics};
    }
    endpoint_configuration::MutationResult mutation =
        endpoint_configuration::applyTypeChange(
            design, plan, confirmation);
    if (hasErrors(mutation.diagnostics)) {
        return DesignResult{false, design, std::move(mutation.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(mutation.design);
    QVector<Diagnostic> combinedDiagnostics =
        std::move(mutation.diagnostics);
    combinedDiagnostics += validated.diagnostics;
    validated.diagnostics = std::move(combinedDiagnostics);
    validated.success = !hasErrors(validated.diagnostics);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::updateParameters(const NocDesign& design,
                                                     const QJsonObject& parameters) const {
    NocDesign edited = design;
    edited.parameters = parameters;
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::setDesignExtension(
    const NocDesign& design,
    const QString& extensionId,
    const QJsonValue& value) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }

    const QString extensionPath = QStringLiteral("/packageData/")
        + jsonPointerToken(extensionId);
    if (!package->designExtension(extensionId)) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(
            result.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.undeclared_package_data_extension"),
            QStringLiteral("packageData namespace %1 is not declared by the Package")
                .arg(extensionId),
            extensionPath,
            QStringLiteral("package"));
        return result;
    }
    if (value.isUndefined()) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(
            result.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.extension_value_undefined"),
            QStringLiteral(
                "an undefined extension value is ambiguous; use removeDesignExtension to delete it"),
            extensionPath);
        return result;
    }

    NocDesign edited = design;
    edited.packageData.insert(extensionId, value);
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::removeDesignExtension(
    const NocDesign& design,
    const QString& extensionId) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }

    if (!package->designExtension(extensionId)) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(
            result.diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.undeclared_package_data_extension"),
            QStringLiteral("packageData namespace %1 is not declared by the Package")
                .arg(extensionId),
            QStringLiteral("/packageData/") + jsonPointerToken(extensionId),
            QStringLiteral("package"));
        return result;
    }

    NocDesign edited = design;
    edited.packageData.remove(extensionId);
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::setElementConfiguration(
    const NocDesign& design,
    ElementRef element,
    const QString& propertySet,
    const QJsonObject& properties) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    element_configuration::MutationResult mutation =
        element_configuration::set(
            design, *package, element, propertySet, properties);
    if (hasErrors(mutation.diagnostics)) {
        return DesignResult{false, design, std::move(mutation.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(mutation.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::clearElementConfiguration(
    const NocDesign& design,
    ElementRef element,
    const QString& propertySet) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    element_configuration::MutationResult mutation =
        element_configuration::clear(
            design, *package, element, propertySet);
    if (hasErrors(mutation.diagnostics)) {
        return DesignResult{false, design, std::move(mutation.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(mutation.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::replaceDomainConfiguration(
    const NocDesign& design,
    DomainConfiguration configuration) const {
    if (!formatVersionSupportsDomains(design.formatVersion)) {
        DesignResult result;
        result.design = design;
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("error"),
                         QStringLiteral("domain_configuration.requires_v2"),
                         QStringLiteral("DomainConfiguration requires a Design format with Domain support"),
                         QStringLiteral("/formatVersion"));
        return result;
    }
    const NocDesign edited = domain_configuration::replace(
        design, std::move(configuration));
    DesignResult validated = validateEditedDesign(edited);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::addDomain(const NocDesign& design,
                                              DomainDefinition domain) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    domain_service::MutationResult domainResult = domain_service::addDomain(
        design, *package, std::move(domain));
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::updateDomain(const NocDesign& design,
                                                 const QString& domainId,
                                                 DomainDefinition domain) const {
    domain_service::MutationResult domainResult = domain_service::updateDomain(
        design, domainId, std::move(domain));
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::removeDomain(const NocDesign& design,
                                                 const QString& domainId) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    domain_service::MutationResult domainResult = domain_service::removeDomain(
        design, *package, domainId);
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::assignDomainsToElements(
    const NocDesign& design,
    const QVector<ElementRef>& elements,
    const QString& domainType,
    const QStringList& domainIds) const {
    domain_service::MutationResult domainResult =
        domain_service::assignDomainsToElements(
            design, elements, domainType, domainIds);
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::patchDomainAssignments(
    const NocDesign& design,
    const QVector<ElementRef>& elements,
    const QString& domainType,
    DomainAssignmentPatch patch) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    domain_service::MutationResult domainResult =
        domain_service::patchDomainAssignments(
            design, *package, elements, domainType, std::move(patch));
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
}

DesignResult FinepaperApplication::clearDomainAssignment(
    const NocDesign& design,
    const QVector<ElementRef>& elements,
    const QString& domainType) const {
    const auto package = m_catalog.resolve(design.package);
    if (!package) {
        return validateEditedDesign(design);
    }
    domain_service::MutationResult domainResult =
        domain_service::clearDomainAssignment(
            design, *package, elements, domainType);
    if (hasErrors(domainResult.diagnostics)) {
        return DesignResult{false, design, std::move(domainResult.diagnostics)};
    }
    DesignResult validated = validateEditedDesign(domainResult.design);
    if (!validated.success) {
        validated.design = design;
    }
    return validated;
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

    const QVector<AttachmentSlotDefinition> explicitAttachmentSlots =
        effectiveExplicitAttachmentSlots(package.attachment);
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
        } else {
            diagnostics += validateParameterObject(endpoint.parameters,
                                                   type->parameters,
                                                   base + QStringLiteral("/parameters"),
                                                   QStringLiteral("package"));
        }

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

        const bool hasSlot = endpoint.attachment.slot &&
            !endpoint.attachment.slot->isEmpty();
        if (hasSlot) {
            if (slotsByRouter[router].contains(*endpoint.attachment.slot)) {
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

        if (package.attachment.slotMode == AttachmentSlotMode::Automatic && hasSlot) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("endpoint.automatic_slot_persisted"),
                             QStringLiteral("automatic Package slots are derived and must not be persisted"),
                             base + QStringLiteral("/attachment/slot"),
                             QStringLiteral("package"));
        }

        if (package.attachment.slotMode == AttachmentSlotMode::Explicit) {
            if (!hasSlot) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("endpoint.slot_required"),
                                 QStringLiteral("this Package requires an explicit slot"),
                                 base + QStringLiteral("/attachment/slot"),
                                 QStringLiteral("package"));
            } else if (std::none_of(
                           explicitAttachmentSlots.cbegin(),
                           explicitAttachmentSlots.cend(),
                           [&](const AttachmentSlotDefinition& position) {
                               return position.id == *endpoint.attachment.slot;
                           })) {
                appendDiagnostic(diagnostics,
                                 QStringLiteral("error"),
                                 QStringLiteral("endpoint.unknown_slot"),
                                 QStringLiteral("slot is not declared by the Package"),
                                 base + QStringLiteral("/attachment/slot"),
                                 QStringLiteral("package"));
            }
        }
    }

    diagnostics += validateElementConfigurations(design, package);

    diagnostics += domain_service::validateAgainstPackage(design, package);

    const bool strictDesignExtensions = package.formatVersion >= 3
        || package.designExtensionsDeclared;
    if (strictDesignExtensions) {
        std::optional<DesignDomainReferenceIndex> domainReferenceIndex =
            std::nullopt;
        for (auto it = design.packageData.constBegin();
             it != design.packageData.constEnd(); ++it) {
            const DesignExtensionDefinition* definition =
                package.designExtension(it.key());
            if (definition) {
                if (!definition->domainReferences.isEmpty()
                    && !domainReferenceIndex) {
                    domainReferenceIndex =
                        DesignDomainReferenceIndex::fromDomains(
                            design.domains);
                }
                diagnostics += validateDesignExtensionValue(
                    it.value(),
                    *definition,
                    domainReferenceIndex
                        ? &*domainReferenceIndex
                        : nullptr,
                    QStringLiteral("/packageData/")
                        + jsonPointerToken(it.key()));
                continue;
            }
            appendDiagnostic(
                diagnostics,
                QStringLiteral("error"),
                QStringLiteral("design.undeclared_package_data_extension"),
                QStringLiteral("packageData namespace %1 is not declared by the Package")
                    .arg(it.key()),
                QStringLiteral("/packageData/") + jsonPointerToken(it.key()),
                QStringLiteral("package"));
        }
    } else if (!package.engine && !design.packageData.isEmpty()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("error"),
            QStringLiteral("design.unexpected_package_data"),
            QStringLiteral(
                "generator-only Packages must declare packageData namespaces"),
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
    const JsonObjectLoadResult rawResult = loadJsonObject(resultPath);
    diagnostics += rawResult.diagnostics;
    std::optional<PackageOperationResult> operationResult;
    if (rawResult.success) {
        operationResult = parsePackageOperationResult(
            rawResult.object, resultPath, source, ArtifactResultPolicy::Optional);
        diagnostics += operationResult->diagnostics;
    }

    if (process.crashed || process.exitCode != 0) {
        const bool structuredError = operationResult
            && hasErrors(operationResult->diagnostics);
        if (!structuredError) {
            appendDiagnostic(diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("operation.failed"),
                             process.standardError.trimmed().isEmpty()
                                 ? QStringLiteral("Package validation failed")
                                 : process.standardError.trimmed(),
                             executable,
                             source);
        }
        return diagnostics;
    }
    if (!rawResult.success || !operationResult || !operationResult->protocolValid) {
        return diagnostics;
    }
    if (!operationResult->success && !hasErrors(operationResult->diagnostics)) {
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
        result.diagnostics += domain_runtime_validation::validateConsumption(
            design, *package);
        if (!hasErrors(result.diagnostics)) {
            result.diagnostics += runPackageValidation(design, *package);
        }
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
    const JsonObjectLoadResult rawResult = loadJsonObject(resultPath);
    result.diagnostics += rawResult.diagnostics;
    std::optional<PackageOperationResult> operationResult;
    if (rawResult.success) {
        operationResult = parsePackageOperationResult(
            rawResult.object,
            resultPath,
            QStringLiteral("generator"),
            ArtifactResultPolicy::Required);
        result.diagnostics += operationResult->diagnostics;
    }

    if (process.crashed || process.exitCode != 0) {
        const bool structuredError = operationResult
            && hasErrors(operationResult->diagnostics);
        if (!structuredError) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("operation.failed"),
                             process.standardError.trimmed().isEmpty()
                                 ? QStringLiteral("Generator failed")
                                 : process.standardError.trimmed(),
                             executable,
                             QStringLiteral("generator"));
        }
        return result;
    }
    if (!rawResult.success || !operationResult || !operationResult->protocolValid) {
        return result;
    }
    if (!operationResult->success) {
        if (!hasErrors(operationResult->diagnostics)) {
            appendDiagnostic(result.diagnostics,
                             QStringLiteral("error"),
                             QStringLiteral("operation.failed"),
                             QStringLiteral("Generator returned success=false"),
                             resultPath,
                             QStringLiteral("generator"));
        }
        return result;
    }

    for (qsizetype index = 0; index < operationResult->artifacts.size(); ++index) {
        const QString base = QStringLiteral("/artifacts/%1").arg(index);
        Artifact artifact = operationResult->artifacts.at(index);
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
