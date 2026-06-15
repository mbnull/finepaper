// ProjectService owns ProjectReader/ProjectWriter backed durable project state.
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "project/projectwriter.h"

#include <QFileInfo>
#include <QSet>
#include <utility>

namespace {

ProjectServiceResult successResult() {
    return {true, {}, {}};
}

ProjectServiceResult failureResult(const QString& error,
                                   ipcraft::DiagnosticStore diagnostics = {}) {
    ProjectServiceResult result;
    result.success = false;
    result.error = error;
    result.diagnostics = std::move(diagnostics);
    return result;
}

QString cleanProjectName(const QString& projectName) {
    const QString trimmed = projectName.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Untitled") : trimmed;
}

void normalizeDocument(ProjectDocument& document) {
    if (document.schema.isEmpty()) {
        document.schema = ipcraft::schemaids::projectV1;
    }
    if (document.projectName.trimmed().isEmpty()) {
        document.projectName = QStringLiteral("Untitled");
    }
}

QString packageRefKey(const QString& id, const QString& version) {
    return version.isEmpty() ? id : id + QLatin1Char('@') + version;
}

QString packageRefKey(const ipcraft::core::PackageRef& package) {
    return packageRefKey(package.id, package.version);
}

ProjectPackageRef packageRefFromComponentRef(const QString& componentPackageRef,
                                             const QVector<ipcraft::core::PackageRef>& packages) {
    for (const ipcraft::core::PackageRef& package : packages) {
        if (componentPackageRef == packageRefKey(package) ||
            componentPackageRef == package.id) {
            return ProjectPackageRef{package.id, package.version};
        }
    }

    const qsizetype separator = componentPackageRef.lastIndexOf(QLatin1Char('@'));
    if (separator > 0 && separator + 1 < componentPackageRef.size()) {
        return ProjectPackageRef{componentPackageRef.left(separator),
                                 componentPackageRef.mid(separator + 1)};
    }

    return ProjectPackageRef{componentPackageRef, {}};
}

void appendPackageRefIfNew(QVector<ipcraft::core::PackageRef>& packages,
                           QSet<QString>& packageKeys,
                           const QString& id,
                           const QString& version) {
    if (id.trimmed().isEmpty()) {
        return;
    }

    const QString key = packageRefKey(id, version);
    if (packageKeys.contains(key)) {
        return;
    }

    packageKeys.insert(key);
    packages.append(ipcraft::core::PackageRef{id, version});
}

ipcraft::core::ProjectDesign designFromDocument(const ProjectDocument& document) {
    ipcraft::core::ProjectDesign design;
    design.schema = document.schema.isEmpty() ? ipcraft::schemaids::projectV1 : document.schema;
    design.id = document.projectId;
    design.name = document.projectName;

    QSet<QString> packageKeys;
    for (const ProjectIpcoreRecord& package : document.ipcores) {
        appendPackageRefIfNew(design.packages, packageKeys, package.id, package.version);
    }

    for (const ProjectIpInstanceRecord& instance : document.instances) {
        appendPackageRefIfNew(design.packages,
                              packageKeys,
                              instance.package.id,
                              instance.package.version);

        ipcraft::core::ComponentInstance component;
        component.id = instance.id;
        component.type = instance.native.value(QStringLiteral("componentType")).toString();
        if (component.type.isEmpty()) {
            component.type = instance.native.value(QStringLiteral("type")).toString();
        }
        component.packageRef = packageRefKey(instance.package.id, instance.package.version);
        component.config = instance.config;
        if (instance.native.value(QStringLiteral("identity")).isObject()) {
            component.identity = instance.native.value(QStringLiteral("identity")).toObject();
        }
        if (instance.native.value(QStringLiteral("metadata")).isObject()) {
            component.metadata = instance.native.value(QStringLiteral("metadata")).toObject();
        }
        if (instance.native.value(QStringLiteral("extensionData")).isObject()) {
            component.extensionData = instance.native.value(QStringLiteral("extensionData")).toObject();
        }
        design.components.append(component);
    }

    design.metadata = document.projectMetadata;
    return design;
}

ProjectDocument documentFromDesign(const ipcraft::core::ProjectDesign& design) {
    ProjectDocument document;
    document.schema = design.schema.isEmpty() ? ipcraft::schemaids::projectV1 : design.schema;
    document.projectId = design.id;
    document.projectName = design.name;
    document.name = design.name;
    document.projectMetadata = design.metadata;

    for (const ipcraft::core::PackageRef& package : design.packages) {
        document.ipcores.append(ProjectIpcoreRecord{package.id, package.version});
    }

    for (const ipcraft::core::ComponentInstance& component : design.components) {
        ProjectIpInstanceRecord instance;
        instance.id = component.id;
        instance.instanceId = component.id;
        instance.package = packageRefFromComponentRef(component.packageRef, design.packages);
        instance.ipcoreId = instance.package.id;
        instance.config = component.config;
        if (!component.type.isEmpty()) {
            instance.native.insert(QStringLiteral("componentType"), component.type);
        }
        if (!component.identity.isEmpty()) {
            instance.native.insert(QStringLiteral("identity"), component.identity);
        }
        if (!component.metadata.isEmpty()) {
            instance.native.insert(QStringLiteral("metadata"), component.metadata);
        }
        if (!component.extensionData.isEmpty()) {
            instance.native.insert(QStringLiteral("extensionData"), component.extensionData);
        }
        document.instances.append(instance);
    }

    return document;
}

} // namespace

ProjectService::ProjectService(QObject* parent) : QObject(parent) {}

bool ProjectService::hasDocument() const {
    return m_hasDocument;
}

const ProjectDocument& ProjectService::document() const {
    return m_document;
}

const ipcraft::core::ProjectDesign& ProjectService::design() const {
    return m_design;
}

QString ProjectService::currentPath() const {
    return m_currentPath;
}

void ProjectService::clear() {
    m_document = ProjectDocument{};
    m_design = ipcraft::core::ProjectDesign{};
    m_currentPath.clear();
    m_hasDocument = false;
    emit currentDocumentChanged();
}

ProjectServiceResult ProjectService::createNew(const QString& projectName) {
    ProjectDocument document;
    document.schema = ipcraft::schemaids::projectV1;
    document.projectName = cleanProjectName(projectName);
    document.name = document.projectName;
    return replaceDocument(std::move(document));
}

ProjectServiceResult ProjectService::loadFile(const QString& path) {
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const ProjectReadResult readResult = ProjectReader::readFile(absolutePath);
    if (!readResult.success) {
        return failureResult(readResult.error, readResult.diagnostics);
    }

    m_document = readResult.document;
    normalizeDocument(m_document);
    m_design = designFromDocument(m_document);
    m_currentPath = absolutePath;
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::saveFile(const QString& path) {
    if (!m_hasDocument) {
        return failureResult(QStringLiteral("No project document is open."));
    }

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const ProjectWriteResult writeResult = ProjectWriter::writeFile(absolutePath, m_document);
    if (!writeResult.success) {
        return failureResult(writeResult.error);
    }

    m_currentPath = absolutePath;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocument(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_design = designFromDocument(m_document);
    m_currentPath.clear();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromLoadedFile(ProjectDocument document,
                                                                   const QString& path) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_design = designFromDocument(m_document);
    m_currentPath = QFileInfo(path).absoluteFilePath();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromProjection(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_design = designFromDocument(m_document);
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

void ProjectService::replaceDesign(ipcraft::core::ProjectDesign design) {
    m_design = std::move(design);
    m_document = documentFromDesign(m_design);
    normalizeDocument(m_document);
    m_hasDocument = true;
    emit currentDocumentChanged();
}

ipcraft::core::PatchApplyResult ProjectService::applyDesignPatch(
    const ipcraft::core::ProjectDesign& project,
    const ipcraft::core::ProjectPatch& patch) const {
    return ipcraft::core::applyPatch(project, patch);
}
