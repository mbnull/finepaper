#include "app/projectflowsupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

namespace {

void appendUniquePath(QStringList& paths, const QString& path) {
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return;
    }
    const QString absolutePath = QFileInfo(trimmedPath).absoluteFilePath();
    if (!paths.contains(absolutePath)) {
        paths.append(absolutePath);
    }
}

bool isSourceTreeWithFrameworkTool(const QDir& dir) {
    return QFileInfo(dir.filePath(QStringLiteral("qt/xmake.lua"))).isFile()
           && QFileInfo(dir.filePath(QStringLiteral("ipcraft_generator/bin/ipcraft-generate"))).isFile();
}

void appendSourceTreeFrameworkToolPath(QStringList& paths, const QDir& applicationDir) {
    QDir candidate = applicationDir;
    while (true) {
        if (isSourceTreeWithFrameworkTool(candidate)) {
            appendUniquePath(paths, candidate.filePath(QStringLiteral("ipcraft_generator/bin")));
            return;
        }
        if (!candidate.cdUp()) {
            return;
        }
    }
}

QString normalizedInstanceOutputKey(const QString& instanceId) {
    return instanceId.toCaseFolded();
}

QString packageRootForEntry(const IpCatalogEntry& entry) {
    return !entry.packageManifest.packageRootPath.trimmed().isEmpty()
        ? entry.packageManifest.packageRootPath
        : entry.runtimeRootPath;
}

bool packageSpecHasFlow(const ipcraft::PackageSpec& package, const QString& flowId) {
    return std::any_of(package.flows.constBegin(),
                       package.flows.constEnd(),
                       [&](const QJsonValue& flowValue) {
                           return flowValue.isObject() &&
                                  flowValue.toObject().value(QStringLiteral("id")).toString()
                                      == flowId;
                       });
}

} // namespace

namespace ProjectFlowSupport {

QStringList defaultFrameworkToolSearchPaths() {
    QStringList paths;

    const QDir application(QCoreApplication::applicationDirPath());
    appendSourceTreeFrameworkToolPath(paths, application);
    appendUniquePath(paths, application.filePath(QStringLiteral("ipcraft_generator/bin")));
    appendUniquePath(paths, application.filePath(QStringLiteral("../ipcraft_generator/bin")));
    appendUniquePath(paths, application.filePath(QStringLiteral("../../ipcraft_generator/bin")));

    appendUniquePath(paths, QStringLiteral("/usr/local/libexec/finepaper"));
    appendUniquePath(paths, QStringLiteral("/usr/local/bin"));
    return paths;
}

QString designNameForProject(const QString& projectPath, const QString& explicitDesignName) {
    const QString requested = explicitDesignName.trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString fromProjectPath = QFileInfo(projectPath).completeBaseName().trimmed();
    return fromProjectPath.isEmpty() ? QStringLiteral("design") : fromProjectPath;
}

QString withInstanceContext(const ProjectIpInstanceRecord& instance, const QString& message) {
    return QStringLiteral("Instance '%1' (%2): %3")
        .arg(instance.instanceId, instance.ipcoreId, message);
}

const IpCatalogEntry* findCatalogEntry(const QList<IpCatalogEntry>& entries,
                                       const QString& ipcoreId) {
    const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const IpCatalogEntry& entry) {
        return entry.id == ipcoreId;
    });
    return it == entries.cend() ? nullptr : &(*it);
}

PackageFlowContext packageFlowContextForEntry(const IpCatalogEntry& entry,
                                              const QString& flowId) {
    PackageFlowContext context;
    const QString packageRoot = packageRootForEntry(entry);
    context.packageRoot = packageRoot;
    if (packageRoot.trimmed().isEmpty()) {
        context.errorKind = PackageFlowContext::ErrorKind::MissingPackageRoot;
        context.error = QStringLiteral("Package root is not available.");
        return context;
    }

    const ipcraft::PackageSpecReadResult specResult =
        ipcraft::PackageSpecReader().readPackageRoot(packageRoot);
    if (!specResult.ok) {
        QStringList messages;
        for (const ipcraft::Diagnostic& diagnostic : specResult.diagnostics.records) {
            messages.append(diagnostic.message);
        }
        context.errorKind = PackageFlowContext::ErrorKind::SpecReadFailed;
        context.error = messages.isEmpty()
            ? QStringLiteral("Package spec could not be read.")
            : messages.join(QStringLiteral("\n"));
        return context;
    }

    context.package = specResult.spec;
    if (!packageSpecHasFlow(context.package, flowId)) {
        context.errorKind = PackageFlowContext::ErrorKind::MissingFlow;
        context.error = QStringLiteral("Package does not declare a %1 flow.").arg(flowId);
        return context;
    }

    context.ok = true;
    context.errorKind = PackageFlowContext::ErrorKind::None;
    return context;
}

std::optional<ipcraft::GraphConfig> graphConfigForInstance(
    const ProjectIpInstanceRecord& instance) {
    if (!instance.hasGraphConfig || instance.graphConfigIsNull) {
        return std::nullopt;
    }

    const ipcraft::GraphConfigReadResult graphConfig =
        ipcraft::GraphConfig::fromJson(instance.graphConfig);
    if (graphConfig.ok) {
        return graphConfig.config;
    }
    return std::nullopt;
}

QString readTextFileIfPresent(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool isSafeInstanceOutputKey(const QString& instanceId) {
    const QString trimmed = instanceId.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..")) {
        return false;
    }
    return !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

bool isReservedInstanceOutputKey(const QString& instanceId) {
    return normalizedInstanceOutputKey(instanceId)
        == QStringLiteral("project-snapshot.fpproj");
}

} // namespace ProjectFlowSupport
