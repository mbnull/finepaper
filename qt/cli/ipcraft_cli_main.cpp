#include "cli/cliresult.h"

#include "ipcraft/artifactmodel.h"
#include "ipcraft/compositionmodel.h"
#include "ipcraft/configschema.h"
#include "ipcraft/diagnosticids.h"
#include "ipcraft/emitter.h"
#include "ipcraft/flowrunner.h"
#include "ipcraft/jsonhelpers.h"
#include "ipcraft/migration.h"
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"
#include "project/projectreader.h"
#include "project/projectwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace {

using ipcraft::cli::CliResult;

void appendDiagnostics(ipcraft::DiagnosticStore& target,
                       const ipcraft::DiagnosticStore& source) {
    for (const ipcraft::Diagnostic& diagnostic : source.records) {
        target.records.append(diagnostic);
    }
}

CliResult failure(const QString& ruleId,
                  const QString& message,
                  const QString& path = {}) {
    CliResult result;
    result.ok = false;
    result.diagnostics.records.append(
        ipcraft::cli::cliDiagnostic(ruleId, message, QStringLiteral("document_path"), path));
    return result;
}

QString optionValue(const QStringList& args, const QString& option) {
    const int index = args.indexOf(option);
    if (index < 0 || index + 1 >= args.size()) {
        return {};
    }
    return args.at(index + 1);
}

bool hasOption(const QStringList& args, const QString& option) {
    return args.contains(option);
}

bool isSafeRunPathSegment(const QString& segment) {
    const QString trimmed = segment.trimmed();
    if (trimmed.isEmpty() ||
        trimmed == QStringLiteral(".") ||
        trimmed == QStringLiteral("..")) {
        return false;
    }
    return !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

ProjectReadResult readProject(const QString& path) {
    return ProjectReader::readFile(path);
}

CliResult projectReadFailure(const ProjectReadResult& readResult) {
    CliResult result;
    result.ok = false;
    appendDiagnostics(result.diagnostics, readResult.diagnostics);
    if (result.diagnostics.records.isEmpty()) {
        result.diagnostics.records.append(ipcraft::cli::cliDiagnostic(
            QStringLiteral("project.read_failed"),
            readResult.error,
            QStringLiteral("file")));
    }
    return result;
}

CliResult projectWriteFailure(const QString& message) {
    return failure(QStringLiteral("project.write_failed"), message, QStringLiteral("$"));
}

ipcraft::PackageSpecCollectionResult readPackages(const QString& packageRoot) {
    return ipcraft::PackageSpecReader().discoverPackageRoots({packageRoot});
}

CliResult packageFailure(const ipcraft::DiagnosticStore& diagnostics) {
    CliResult result;
    result.ok = false;
    appendDiagnostics(result.diagnostics, diagnostics);
    return result;
}

CliResult diagnosticFailure(const ipcraft::Diagnostic& diagnostic) {
    CliResult result;
    result.ok = false;
    result.diagnostics.records.append(diagnostic);
    return result;
}

ipcraft::Diagnostic coreDiagnostic(const QString& ruleId,
                                   const QString& category,
                                   const QString& message,
                                   const QString& path,
                                   const QJsonObject& details = {}) {
    ipcraft::Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("core");
    diagnostic.category = category;
    diagnostic.ruleId = ruleId;
    diagnostic.message = message;
    diagnostic.details = details;
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    diagnostic.locations.append(location);
    return diagnostic;
}

const ProjectIpInstanceRecord* findInstance(const ProjectDocument& project,
                                            const QString& instanceId) {
    for (const ProjectIpInstanceRecord& instance : project.instances) {
        if (instance.id == instanceId) {
            return &instance;
        }
    }
    return nullptr;
}

ipcraft::CompositionEndpointRef endpointRef(const ProjectEndpointRef& endpoint) {
    ipcraft::CompositionEndpointRef ref;
    ref.instanceId = endpoint.instanceId;
    ref.interfaceId = endpoint.interfaceId;
    ref.portId = endpoint.portId;
    ref.role = endpoint.role;
    ref.properties = endpoint.properties;
    return ref;
}

ipcraft::CompositionModel compositionFromProject(const ProjectComposition& composition) {
    ipcraft::CompositionModel model;
    for (const ProjectConnectionRecord& connection : composition.connections) {
        ipcraft::SystemConnection systemConnection;
        systemConnection.id = connection.id;
        systemConnection.type = connection.type;
        systemConnection.source = connection.sourceKind;
        systemConnection.properties = connection.properties;
        systemConnection.native = connection.native;
        for (const ProjectEndpointRef& endpoint : connection.endpoints) {
            systemConnection.endpoints.append(endpointRef(endpoint));
        }
        model.connections.append(systemConnection);
    }
    for (const ProjectExternalPortRecord& port : composition.externalPorts) {
        ipcraft::ExternalPort externalPort;
        externalPort.id = port.id;
        externalPort.name = port.name;
        externalPort.hasInterface = port.hasInterface;
        externalPort.interfaceRef = endpointRef(port.interfaceRef);
        externalPort.properties = port.properties;
        externalPort.native = port.native;
        model.externalPorts.append(externalPort);
    }
    model.groups = composition.groups;
    model.properties = composition.properties;
    model.native = composition.native;
    return model;
}

ipcraft::PackageSpecResolveResult resolveInstancePackage(
    const QVector<ipcraft::PackageSpec>& packages,
    const ProjectIpInstanceRecord& instance) {
    return ipcraft::resolvePackageSpec(packages,
                                       instance.package.id,
                                       instance.package.version);
}

CliResult readProjectAndPackages(const QString& projectPath,
                                 const QString& packageRoot,
                                 ProjectDocument* project,
                                 QVector<ipcraft::PackageSpec>* packages) {
    const ProjectReadResult projectResult = readProject(projectPath);
    if (!projectResult.success) {
        return projectReadFailure(projectResult);
    }
    const ipcraft::PackageSpecCollectionResult packageResult = readPackages(packageRoot);
    if (!packageResult.diagnostics.records.isEmpty()) {
        return packageFailure(packageResult.diagnostics);
    }
    *project = projectResult.document;
    *packages = packageResult.packages;
    CliResult ok;
    ok.ok = true;
    return ok;
}

CliResult validateStaticProject(const ProjectDocument& project,
                                const QString& projectRootPath,
                                const QVector<ipcraft::PackageSpec>& packages) {
    CliResult result;
    result.ok = true;
    QVector<ipcraft::CompositionInstance> compositionInstances;
    for (const ProjectIpInstanceRecord& instance : project.instances) {
        const ipcraft::PackageSpecResolveResult resolved =
            resolveInstancePackage(packages, instance);
        if (!resolved.ok) {
            result.ok = false;
            appendDiagnostics(result.diagnostics, resolved.diagnostics);
            continue;
        }

        ipcraft::ConfigSchemaReadResult schemaResult =
            ipcraft::ConfigSchema::fromJson(resolved.spec.configSchema);
        if (!schemaResult.ok && !schemaResult.diagnostics.records.isEmpty()) {
            result.ok = false;
            appendDiagnostics(result.diagnostics, schemaResult.diagnostics);
        }
        const ipcraft::ConfigValidationOptions configOptions{.projectRootPath = projectRootPath};
        const ipcraft::ConfigValidationResult configResult =
            ipcraft::validateConfigBundle(schemaResult.schema,
                                          ipcraft::ConfigBundle::fromJson(instance.config),
                                          configOptions);
        if (!configResult.ok) {
            result.ok = false;
            appendDiagnostics(result.diagnostics, configResult.diagnostics);
            result.diagnostics.records.append(coreDiagnostic(
                QStringLiteral("project.config_invalid"),
                QStringLiteral("project"),
                QStringLiteral("Project instance config is invalid."),
                QStringLiteral("$.instances[].config"),
                QJsonObject{{QStringLiteral("instance"), instance.id}}));
        }

        ipcraft::CompositionInstance compositionInstance;
        compositionInstance.instanceId = instance.id;
        compositionInstance.package = resolved.spec;
        compositionInstances.append(compositionInstance);
    }

    const ipcraft::CompositionValidationResult compositionResult =
        ipcraft::validateCompositionModel(compositionFromProject(project.composition),
                                          compositionInstances);
    if (!compositionResult.ok) {
        result.ok = false;
        appendDiagnostics(result.diagnostics, compositionResult.diagnostics);
    }

    if (result.ok) {
        QJsonObject payload;
        payload.insert(QStringLiteral("project"),
                       QJsonObject{{QStringLiteral("id"), project.projectId},
                                   {QStringLiteral("name"), project.projectName}});
        payload.insert(QStringLiteral("instances"), project.instances.size());
        result.result = payload;
    }
    return result;
}

CliResult commandInspectProject(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("inspect-project requires a project path."));
    }
    const ProjectReadResult projectResult = readProject(args.at(0));
    if (!projectResult.success) {
        return projectReadFailure(projectResult);
    }
    CliResult result;
    result.ok = true;
    QJsonObject payload;
    payload.insert(QStringLiteral("project"),
                   QJsonObject{{QStringLiteral("id"), projectResult.document.projectId},
                               {QStringLiteral("name"), projectResult.document.projectName}});
    payload.insert(QStringLiteral("instances"), projectResult.document.instances.size());
    result.result = payload;
    return result;
}

CliResult commandValidateProject(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("validate-project requires a project path."));
    }
    const QString packageRoot = optionValue(args, QStringLiteral("--packages"));
    if (packageRoot.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("validate-project requires --packages."));
    }
    ProjectDocument project;
    QVector<ipcraft::PackageSpec> packages;
    CliResult readResult = readProjectAndPackages(args.at(0), packageRoot, &project, &packages);
    if (!readResult.ok) {
        return readResult;
    }
    return validateStaticProject(project, QFileInfo(args.at(0)).absolutePath(), packages);
}

CliResult commandEmitInputs(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("emit-inputs requires a project path."));
    }
    const QString instanceId = optionValue(args, QStringLiteral("--instance"));
    const QString outDir = optionValue(args, QStringLiteral("--out"));
    const QString packageRoot = optionValue(args, QStringLiteral("--packages"));
    if (instanceId.isEmpty() || outDir.isEmpty() || packageRoot.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("emit-inputs requires --instance, --out, and --packages."));
    }
    ProjectDocument project;
    QVector<ipcraft::PackageSpec> packages;
    CliResult readResult = readProjectAndPackages(args.at(0), packageRoot, &project, &packages);
    if (!readResult.ok) {
        return readResult;
    }
    const ProjectIpInstanceRecord* instance = findInstance(project, instanceId);
    if (!instance) {
        return failure(QStringLiteral("cli.instance_not_found"),
                       QStringLiteral("Requested instance was not found."));
    }
    const ipcraft::PackageSpecResolveResult resolved = resolveInstancePackage(packages, *instance);
    if (!resolved.ok) {
        return packageFailure(resolved.diagnostics);
    }
    ipcraft::PackageInputBuildRequest request;
    request.projectId = project.projectId;
    request.instanceId = instance->id;
    request.outputRoot = outDir;
    request.package = resolved.spec;
    request.packageRoot = resolved.spec.packageRootPath;
    request.config = ipcraft::ConfigBundle::fromJson(instance->config);
    request.composition = compositionFromProject(project.composition);
    if (instance->hasGraphConfig && !instance->graphConfigIsNull) {
        const ipcraft::GraphConfigReadResult graphConfig =
            ipcraft::GraphConfig::fromJson(instance->graphConfig);
        if (!graphConfig.ok) {
            return packageFailure(graphConfig.diagnostics);
        }
        request.graphConfig = graphConfig.config;
    }
    const ipcraft::PackageInputBuildResult buildResult =
        ipcraft::PackageInputBuilder::emitInputs(request);
    CliResult result;
    result.ok = buildResult.ok;
    appendDiagnostics(result.diagnostics, buildResult.manifest.diagnostics);
    QJsonObject payload;
    payload.insert(QStringLiteral("manifest"), buildResult.manifest.toJson());
    result.result = payload;
    return result;
}

QJsonObject flowById(const ipcraft::PackageSpec& package, const QString& flowId) {
    for (const QJsonValue& flowValue : package.flows) {
        if (flowValue.isObject() &&
            flowValue.toObject().value(QStringLiteral("id")).toString() == flowId) {
            return flowValue.toObject();
        }
    }
    return {};
}

bool isProjectScopedFlow(const QJsonObject& flow) {
    return flow.value(QStringLiteral("scope")).toString() == QStringLiteral("project");
}

CliResult runFlowForInstance(const ProjectDocument& project,
                             const ProjectIpInstanceRecord& instance,
                             const ipcraft::PackageSpec& package,
                             const QString& flowId,
                             const QString& outDir,
                             QJsonObject* payload) {
    if (!isSafeRunPathSegment(instance.id)) {
        return failure(QStringLiteral("cli.path_escape"),
                       QStringLiteral("Instance id cannot be used as a run output path segment."),
                       QStringLiteral("$.instances[].id"));
    }

    ipcraft::FlowRunRequest request;
    request.projectId = project.projectId;
    request.instanceId = instance.id;
    request.flowId = flowId;
    request.runId = instance.id;
    request.runRoot = QDir(outDir).filePath(instance.id);
    request.outputRoot = request.runRoot;
    request.packageRoot = package.packageRootPath;
    request.package = package;
    request.config = ipcraft::ConfigBundle::fromJson(instance.config);
    request.composition = compositionFromProject(project.composition);
    if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
        const ipcraft::GraphConfigReadResult graphConfig =
            ipcraft::GraphConfig::fromJson(instance.graphConfig);
        if (!graphConfig.ok) {
            return packageFailure(graphConfig.diagnostics);
        }
        request.graphConfig = graphConfig.config;
    }
    const ipcraft::FlowRunResult flowResult = ipcraft::FlowRunner::runFlow(request);
    CliResult result;
    result.ok = flowResult.ok;
    appendDiagnostics(result.diagnostics, flowResult.diagnostics);
    QJsonObject run;
    run.insert(QStringLiteral("instance"), instance.id);
    run.insert(QStringLiteral("flow"), flowId);
    run.insert(QStringLiteral("state"), flowResult.state);
    run.insert(QStringLiteral("artifacts"), flowResult.artifacts.toJson());
    payload->insert(instance.id, run);
    return result;
}

CliResult commandRunFlow(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("run-flow requires a project path."));
    }
    const QString flowId = optionValue(args, QStringLiteral("--flow"));
    const QString instanceId = optionValue(args, QStringLiteral("--instance"));
    const QString outDir = optionValue(args, QStringLiteral("--out"));
    const QString packageRoot = optionValue(args, QStringLiteral("--packages"));
    const bool allInstances = hasOption(args, QStringLiteral("--all-instances"));
    if (flowId.isEmpty() || outDir.isEmpty() || packageRoot.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("run-flow requires --flow, --out, and --packages."));
    }
    if (!instanceId.isEmpty() && allInstances) {
        return failure(QStringLiteral("cli.argument_conflict"),
                       QStringLiteral("run-flow accepts only one of --instance or --all-instances."));
    }

    ProjectDocument project;
    QVector<ipcraft::PackageSpec> packages;
    CliResult readResult = readProjectAndPackages(args.at(0), packageRoot, &project, &packages);
    if (!readResult.ok) {
        return readResult;
    }

    if (instanceId.isEmpty() && !allInstances) {
        for (const ipcraft::PackageSpec& package : packages) {
            const QJsonObject flow = flowById(package, flowId);
            if (flow.isEmpty() || !isProjectScopedFlow(flow)) {
                continue;
            }
            ipcraft::FlowRunRequest request;
            request.projectId = project.projectId;
            request.flowId = flowId;
            request.runId = QStringLiteral("project");
            request.runRoot = QDir(outDir).filePath(QStringLiteral("project"));
            request.outputRoot = request.runRoot;
            request.packageRoot = package.packageRootPath;
            request.package = package;
            request.composition = compositionFromProject(project.composition);
            const ipcraft::FlowRunResult flowResult = ipcraft::FlowRunner::runFlow(request);
            CliResult result;
            result.ok = flowResult.ok;
            appendDiagnostics(result.diagnostics, flowResult.diagnostics);
            QJsonObject payload;
            payload.insert(QStringLiteral("state"), flowResult.state);
            payload.insert(QStringLiteral("artifacts"), flowResult.artifacts.toJson());
            result.result = payload;
            return result;
        }
        return failure(QStringLiteral("cli.instance_scope_required"),
                       QStringLiteral("instance-scoped run-flow requires --instance or --all-instances."));
    }

    QVector<ProjectIpInstanceRecord> targets;
    if (allInstances) {
        targets = project.instances;
        std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
            return left.id < right.id;
        });
    } else {
        const ProjectIpInstanceRecord* instance = findInstance(project, instanceId);
        if (!instance) {
            return failure(QStringLiteral("cli.instance_not_found"),
                           QStringLiteral("Requested instance was not found."));
        }
        targets.append(*instance);
    }

    CliResult aggregate;
    aggregate.ok = true;
    QJsonObject runs;
    for (const ProjectIpInstanceRecord& instance : targets) {
        const ipcraft::PackageSpecResolveResult resolved =
            resolveInstancePackage(packages, instance);
        if (!resolved.ok) {
            aggregate.ok = false;
            appendDiagnostics(aggregate.diagnostics, resolved.diagnostics);
            continue;
        }
        const QJsonObject flow = flowById(resolved.spec, flowId);
        if (flow.isEmpty()) {
            aggregate.ok = false;
            aggregate.diagnostics.records.append(ipcraft::cli::cliDiagnostic(
                ipcraft::diagnosticids::flowUnknownFlow(),
                QStringLiteral("Requested flow is not declared by the package."),
                QStringLiteral("document_path"),
                QStringLiteral("$.flows")));
            continue;
        }
        if (isProjectScopedFlow(flow)) {
            return failure(QStringLiteral("cli.argument_conflict"),
                           QStringLiteral("project-scoped flows do not accept instance targeting."));
        }
        QJsonObject payload;
        CliResult runResult =
            runFlowForInstance(project, instance, resolved.spec, flowId, outDir, &payload);
        runs.insert(instance.id, payload.value(instance.id).toObject());
        if (!runResult.ok) {
            aggregate.ok = false;
        }
        appendDiagnostics(aggregate.diagnostics, runResult.diagnostics);
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("runs"), runs);
    aggregate.result = payload;
    return aggregate;
}

CliResult commandMigrateProject(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("migrate-project requires a project path."));
    }
    const QString target = optionValue(args, QStringLiteral("--to"));
    if (target.isEmpty()) {
        return diagnosticFailure(ipcraft::migrationDiagnostic(
            QStringLiteral("migration.target_required"),
            QStringLiteral("migrate-project requires --to."),
            QStringLiteral("$.to")));
    }
    if (target != ipcraft::schemaids::projectV1) {
        return diagnosticFailure(ipcraft::migrationDiagnostic(
            QStringLiteral("migration.unsupported_target"),
            QStringLiteral("Unsupported migration target."),
            QStringLiteral("$.to")));
    }
    const ProjectReadResult projectResult = readProject(args.at(0));
    if (projectResult.success) {
        const ProjectJsonResult jsonResult = ProjectWriter::toJsonObjectResult(projectResult.document);
        if (!jsonResult.success) {
            return projectWriteFailure(jsonResult.error);
        }
        CliResult result;
        result.ok = true;
        QJsonObject payload;
        payload.insert(QStringLiteral("project"), jsonResult.object);
        result.result = payload;
        return result;
    }

    const ipcraft::ProjectMigrationResult migrationResult =
        ipcraft::ProjectMigrator::migrateFile(args.at(0), target);
    if (!migrationResult.ok) {
        return packageFailure(migrationResult.diagnostics);
    }

    CliResult result;
    result.ok = true;
    QJsonObject payload;
    const ProjectJsonResult jsonResult = ProjectWriter::toJsonObjectResult(migrationResult.document);
    if (!jsonResult.success) {
        return projectWriteFailure(jsonResult.error);
    }
    payload.insert(QStringLiteral("project"), jsonResult.object);
    result.result = payload;
    return result;
}

CliResult commandCollectArtifacts(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("collect-artifacts requires a run directory."));
    }
    const QString specPath = optionValue(args, QStringLiteral("--spec"));
    if (specPath.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("collect-artifacts requires --spec."));
    }
    const ipcraft::PackageSpecReadResult specResult =
        ipcraft::PackageSpecReader().readSpecFile(specPath);
    if (!specResult.ok) {
        return packageFailure(specResult.diagnostics);
    }
    ipcraft::ArtifactCollectRequest request;
    request.runRoot = args.at(0);
    request.flowRunId = QStringLiteral("cli");
    request.package = specResult.spec;
    const ipcraft::ArtifactCollectResult collectResult =
        ipcraft::ArtifactCollector::collect(request);
    CliResult result;
    result.ok = collectResult.ok;
    appendDiagnostics(result.diagnostics, collectResult.diagnostics);
    QJsonObject payload;
    payload.insert(QStringLiteral("artifacts"), collectResult.index.toJson());
    result.result = payload;
    return result;
}

CliResult dispatch(const QStringList& args) {
    if (args.isEmpty()) {
        return failure(QStringLiteral("cli.missing_argument"),
                       QStringLiteral("ipcraft-cli requires a command."));
    }
    const QString command = args.first();
    const QStringList commandArgs = args.mid(1);
    if (command == QStringLiteral("inspect-project")) {
        return commandInspectProject(commandArgs);
    }
    if (command == QStringLiteral("validate-project")) {
        return commandValidateProject(commandArgs);
    }
    if (command == QStringLiteral("emit-inputs")) {
        return commandEmitInputs(commandArgs);
    }
    if (command == QStringLiteral("run-flow")) {
        return commandRunFlow(commandArgs);
    }
    if (command == QStringLiteral("migrate-project")) {
        return commandMigrateProject(commandArgs);
    }
    if (command == QStringLiteral("collect-artifacts")) {
        return commandCollectArtifacts(commandArgs);
    }
    return failure(QStringLiteral("cli.unknown_command"),
                   QStringLiteral("Unknown ipcraft-cli command."));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return ipcraft::cli::writeCliResult(dispatch(app.arguments().mid(1)));
}
