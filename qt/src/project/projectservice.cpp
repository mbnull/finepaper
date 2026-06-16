// ProjectService owns ProjectReader/ProjectWriter backed durable project state.
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "project/projectdesignserializer.h"
#include "project/projectwriter.h"

#include <QFileInfo>
#include <QSet>
#include <QStringList>
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

QJsonObject mergeDesignOwnedNative(const QJsonObject& existingNative,
                                   const QJsonObject& designNative) {
    static const QStringList designOwnedKeys = {
        QStringLiteral("componentType"),
        QStringLiteral("identity"),
        QStringLiteral("metadata"),
        QStringLiteral("extensionData")
    };

    QJsonObject merged = existingNative;
    for (const QString& key : designOwnedKeys) {
        if (designNative.contains(key)) {
            merged.insert(key, designNative.value(key));
        } else {
            merged.remove(key);
        }
    }
    return merged;
}

QJsonObject mergeDesignSupplementNative(const QJsonObject& existingNative,
                                        const QJsonObject& designDocumentNative) {
    static const QString supplementKey =
        QStringLiteral("ipcraft.projectDesignSupplement.v1");

    QJsonObject merged = existingNative;
    if (designDocumentNative.contains(supplementKey)) {
        merged.insert(supplementKey, designDocumentNative.value(supplementKey));
    } else {
        merged.remove(supplementKey);
    }
    return merged;
}

const ProjectIpInstanceRecord* findInstanceById(
    const QVector<ProjectIpInstanceRecord>& instances,
    const QString& id) {
    for (const ProjectIpInstanceRecord& instance : instances) {
        if (instance.id == id) {
            return &instance;
        }
    }
    return nullptr;
}

ProjectIpInstanceRecord mergeDesignOwnedInstance(
    const ProjectIpInstanceRecord* existingInstance,
    const ProjectIpInstanceRecord& designInstance) {
    ProjectIpInstanceRecord merged = existingInstance ? *existingInstance
                                                      : ProjectIpInstanceRecord{};
    merged.id = designInstance.id;
    merged.instanceId = designInstance.instanceId.isEmpty() ? designInstance.id
                                                            : designInstance.instanceId;
    merged.package = designInstance.package;
    merged.ipcoreId = designInstance.ipcoreId.isEmpty() ? designInstance.package.id
                                                        : designInstance.ipcoreId;
    merged.config = designInstance.config;
    merged.native = mergeDesignOwnedNative(merged.native, designInstance.native);
    return merged;
}

void mergeDesignIntoDocument(ProjectDocument& document,
                             const ipcraft::core::ProjectDesign& design) {
    const ProjectDocument designDocument = ProjectDesignSerializer::toDocument(design);
    const QVector<ProjectIpInstanceRecord> existingInstances = document.instances;

    document.schema = designDocument.schema;
    document.projectId = designDocument.projectId;
    document.projectName = designDocument.projectName;
    document.name = designDocument.name;
    document.projectMetadata = designDocument.projectMetadata;
    document.native = mergeDesignSupplementNative(document.native, designDocument.native);
    document.ipcores = designDocument.ipcores;
    document.instances.clear();
    document.instances.reserve(designDocument.instances.size());
    for (const ProjectIpInstanceRecord& designInstance : designDocument.instances) {
        document.instances.append(mergeDesignOwnedInstance(
            findInstanceById(existingInstances, designInstance.id),
            designInstance));
    }
    document.ipcoreState = document.instances;
}

void mergeDesignOnlyComponentsIntoDocument(ProjectDocument& document,
                                           const ipcraft::core::ProjectDesign& design) {
    const ProjectDocument designDocument = ProjectDesignSerializer::toDocument(design);
    QSet<QString> existingInstanceIds;
    for (const ProjectIpInstanceRecord& instance : document.instances) {
        existingInstanceIds.insert(instance.id);
    }

    for (const ProjectIpInstanceRecord& designInstance : designDocument.instances) {
        if (existingInstanceIds.contains(designInstance.id)) {
            continue;
        }
        document.instances.append(mergeDesignOwnedInstance(nullptr, designInstance));
        existingInstanceIds.insert(designInstance.id);
    }
    document.native = mergeDesignSupplementNative(document.native, designDocument.native);
    document.ipcoreState = document.instances;
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
    m_design = ProjectDesignSerializer::fromDocument(m_document);
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
    m_design = ProjectDesignSerializer::fromDocument(m_document);
    m_currentPath.clear();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromLoadedFile(ProjectDocument document,
                                                                   const QString& path) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_design = ProjectDesignSerializer::fromDocument(m_document);
    m_currentPath = QFileInfo(path).absoluteFilePath();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromProjection(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_design = ProjectDesignSerializer::fromDocument(m_document);
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentPreservingPath(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    m_design = ProjectDesignSerializer::fromDocument(m_document);
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

void ProjectService::replaceDesign(ipcraft::core::ProjectDesign design) {
    m_design = std::move(design);
    if (!m_hasDocument) {
        m_document = ProjectDocument{};
    }
    mergeDesignIntoDocument(m_document, m_design);
    normalizeDocument(m_document);
    m_hasDocument = true;
    emit currentDocumentChanged();
}

void ProjectService::mergeDesignOnlyComponents(const ipcraft::core::ProjectDesign& design) {
    if (!m_hasDocument) {
        replaceDesign(design);
        return;
    }

    mergeDesignOnlyComponentsIntoDocument(m_document, design);
    normalizeDocument(m_document);
    m_design = ProjectDesignSerializer::fromDocument(m_document);
    emit currentDocumentChanged();
}

ipcraft::core::PatchApplyResult ProjectService::applyDesignPatch(
    const ipcraft::core::ProjectDesign& project,
    const ipcraft::core::ProjectPatch& patch) const {
    return ipcraft::core::applyPatch(project, patch);
}
