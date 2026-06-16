// ProjectValidationRunner implementation.
#include "validation/projectvalidationrunner.h"

#include "ipcraft/core/project_design.h"
#include "ipcraft/ipcraftbuiltinvalidator.h"

namespace {

bool isBlank(const QString& value) {
    return value.trimmed().isEmpty();
}

bool hasIndex(const QString& path, const QString& prefix, qsizetype& index) {
    if (!path.startsWith(prefix)) {
        return false;
    }

    const qsizetype start = prefix.size();
    qsizetype end = path.indexOf(QLatin1Char('/'), start);
    if (end < 0) {
        end = path.size();
    }

    bool ok = false;
    const int parsed = path.mid(start, end - start).toInt(&ok);
    if (!ok || parsed < 0) {
        return false;
    }

    index = parsed;
    return true;
}

bool isValidIndex(qsizetype index, qsizetype size) {
    return index >= 0 && index < size;
}

bool designContainsComponent(const ipcraft::core::ProjectDesign& project,
                             const QString& componentId) {
    if (isBlank(componentId)) {
        return false;
    }

    for (const ipcraft::core::ComponentInstance& component : project.components) {
        if (component.id == componentId) {
            return true;
        }
    }
    return false;
}

bool appendComponentBlocker(const ipcraft::core::ProjectDesign& project,
                            const QString& componentId,
                            QSet<QString>& blockingInstanceIds) {
    if (!designContainsComponent(project, componentId)) {
        return false;
    }

    blockingInstanceIds.insert(componentId);
    return true;
}

bool appendComponentIndexBlocker(const ipcraft::core::ProjectDesign& project,
                                 const QString& path,
                                 QSet<QString>& blockingInstanceIds) {
    qsizetype index = -1;
    if (!hasIndex(path, QStringLiteral("/components/"), index) ||
        !isValidIndex(index, project.components.size())) {
        return false;
    }

    return appendComponentBlocker(project, project.components.at(index).id, blockingInstanceIds);
}

bool appendInterfaceBlocker(const ipcraft::core::ProjectDesign& project,
                            const QString& path,
                            QSet<QString>& blockingInstanceIds) {
    qsizetype index = -1;
    if (!hasIndex(path, QStringLiteral("/interfaces/"), index) ||
        !isValidIndex(index, project.interfaces.size())) {
        return false;
    }

    return appendComponentBlocker(project,
                                  project.interfaces.at(index).ownerComponentId,
                                  blockingInstanceIds);
}

bool appendConnectionBlockers(const ipcraft::core::ProjectDesign& project,
                              const QString& path,
                              QSet<QString>& blockingInstanceIds) {
    qsizetype index = -1;
    if (!hasIndex(path, QStringLiteral("/connections/"), index) ||
        !isValidIndex(index, project.connections.size())) {
        return false;
    }

    const ipcraft::core::Connection& connection = project.connections.at(index);
    bool appended = appendComponentBlocker(project,
                                           connection.from.component,
                                           blockingInstanceIds);
    appended = appendComponentBlocker(project,
                                      connection.to.component,
                                      blockingInstanceIds) || appended;
    return appended;
}

QString packageRefKey(const ipcraft::core::PackageRef& package) {
    if (isBlank(package.id) || isBlank(package.version)) {
        return {};
    }
    return package.id + QLatin1Char('@') + package.version;
}

bool appendPackageBlockers(const ipcraft::core::ProjectDesign& project,
                           const QString& path,
                           QSet<QString>& blockingInstanceIds) {
    qsizetype index = -1;
    if (!hasIndex(path, QStringLiteral("/packages/"), index) ||
        !isValidIndex(index, project.packages.size())) {
        return false;
    }

    const QString key = packageRefKey(project.packages.at(index));
    if (key.isEmpty()) {
        return false;
    }

    bool appended = false;
    for (const ipcraft::core::ComponentInstance& component : project.components) {
        if (component.packageRef == key) {
            appended = appendComponentBlocker(project, component.id, blockingInstanceIds) || appended;
        }
    }
    return appended;
}

bool appendTopologyBlockers(const ipcraft::core::ProjectDesign& project,
                            const QString& path,
                            QSet<QString>& blockingInstanceIds) {
    qsizetype topologyIndex = -1;
    if (!hasIndex(path, QStringLiteral("/topologies/"), topologyIndex) ||
        !isValidIndex(topologyIndex, project.topologies.size())) {
        return false;
    }

    const ipcraft::core::TopologyGraph& topology = project.topologies.at(topologyIndex);
    bool appended = appendComponentBlocker(project,
                                           topology.ownerComponentId,
                                           blockingInstanceIds);

    qsizetype attachmentIndex = -1;
    const QString attachmentPrefix =
        QStringLiteral("/topologies/%1/attachments/").arg(topologyIndex);
    if (hasIndex(path, attachmentPrefix, attachmentIndex) &&
        isValidIndex(attachmentIndex, topology.attachments.size())) {
        appended = appendComponentBlocker(project,
                                          topology.attachments.at(attachmentIndex).componentRef,
                                          blockingInstanceIds) || appended;
    }

    return appended;
}

bool appendIssueBlockers(const ipcraft::core::ProjectDesign& project,
                         const ipcraft::core::ValidationIssue& issue,
                         QSet<QString>& blockingInstanceIds) {
    return appendComponentIndexBlocker(project, issue.path, blockingInstanceIds) ||
           appendInterfaceBlocker(project, issue.path, blockingInstanceIds) ||
           appendConnectionBlockers(project, issue.path, blockingInstanceIds) ||
           appendPackageBlockers(project, issue.path, blockingInstanceIds) ||
           appendTopologyBlockers(project, issue.path, blockingInstanceIds);
}

QString elementIdForIssue(const ipcraft::core::ProjectDesign& project,
                          const ipcraft::core::ValidationIssue& issue) {
    qsizetype index = -1;
    if (hasIndex(issue.path, QStringLiteral("/components/"), index) &&
        isValidIndex(index, project.components.size()) &&
        !isBlank(project.components.at(index).id)) {
        return project.components.at(index).id;
    }

    if (hasIndex(issue.path, QStringLiteral("/connections/"), index) &&
        isValidIndex(index, project.connections.size()) &&
        !isBlank(project.connections.at(index).id)) {
        return project.connections.at(index).id;
    }

    if (hasIndex(issue.path, QStringLiteral("/interfaces/"), index) &&
        isValidIndex(index, project.interfaces.size())) {
        const ipcraft::core::InterfaceInstance& interface = project.interfaces.at(index);
        if (!isBlank(interface.ownerComponentId)) {
            return interface.ownerComponentId;
        }
        if (!isBlank(interface.id)) {
            return interface.id;
        }
    }

    if (hasIndex(issue.path, QStringLiteral("/topologies/"), index) &&
        isValidIndex(index, project.topologies.size()) &&
        !isBlank(project.topologies.at(index).id)) {
        return project.topologies.at(index).id;
    }

    if (hasIndex(issue.path, QStringLiteral("/packages/"), index) &&
        isValidIndex(index, project.packages.size()) &&
        !isBlank(project.packages.at(index).id)) {
        return project.packages.at(index).id;
    }

    return issue.path;
}

ValidationResult resultForIssue(const ipcraft::core::ProjectDesign& project,
                                const ipcraft::core::ValidationIssue& issue) {
    QString message = issue.message;
    if (!issue.path.isEmpty()) {
        message += QStringLiteral(" (%1)").arg(issue.path);
    }
    return ValidationResult(ValidationSeverity::Error,
                            message,
                            elementIdForIssue(project, issue),
                            issue.code);
}

void appendProjectDesignIssues(const ipcraft::core::ProjectDesign& project,
                               ProjectValidationReport& report) {
    const QVector<ipcraft::core::ValidationIssue> issues =
        ipcraft::core::validateProjectDesign(project);
    for (const ipcraft::core::ValidationIssue& issue : issues) {
        report.diagnostics.append(resultForIssue(project, issue));
        if (!appendIssueBlockers(project, issue, report.blockingInstanceIds)) {
            report.blockAllExternalValidation = true;
        }
    }
}

} // namespace

bool ProjectValidationReport::hasErrors() const {
    for (const ValidationResult& diagnostic : diagnostics) {
        if (diagnostic.severity() == ValidationSeverity::Error) {
            return true;
        }
    }
    return false;
}

QList<ValidationResult> ProjectValidationRunner::validate(
    const ipcraft::core::ProjectDesign* projectDesign,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    return validateDetailed(projectDesign, entries, instances).diagnostics;
}

ProjectValidationReport ProjectValidationRunner::validateDetailed(
    const ipcraft::core::ProjectDesign* projectDesign,
    const QList<IpCatalogEntry>& entries,
    const QVector<ProjectIpInstanceRecord>& instances) const {
    IpcraftBuiltInValidator builtInValidator;
    const IpcraftBuiltInValidator::Result builtInResult =
        builtInValidator.validate(nullptr,
                                  entries,
                                  instances,
                                  IpcraftBuiltInValidator::CommandPurpose::Validate);
    ProjectValidationReport report;
    report.diagnostics = builtInResult.diagnostics;
    report.blockingInstanceIds = builtInResult.blockingInstanceIds;

    if (!projectDesign) {
        report.diagnostics.append(ValidationResult(
            ValidationSeverity::Error,
            QStringLiteral("Project design is not available for static validation."),
            QString(),
            QStringLiteral("project_design.missing")));
        report.blockAllExternalValidation = true;
        return report;
    }

    appendProjectDesignIssues(*projectDesign, report);

    if (report.hasErrors() && report.blockingInstanceIds.isEmpty()) {
        report.blockAllExternalValidation = true;
    }

    return report;
}
