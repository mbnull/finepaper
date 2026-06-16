// ProjectService owns ProjectReader/ProjectWriter backed durable project state.
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "graph/connection.h"
#include "graph/module.h"
#include "project/projectdesignserializer.h"
#include "project/projectwriter.h"

#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <utility>
#include <variant>

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

QJsonValue parameterValueToJson(const Parameter::Value& value) {
    if (const auto* stringValue = std::get_if<QString>(&value)) {
        return *stringValue;
    }
    if (const auto* intValue = std::get_if<int>(&value)) {
        return *intValue;
    }
    if (const auto* doubleValue = std::get_if<double>(&value)) {
        return *doubleValue;
    }
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }
    return {};
}

ProjectModuleRecord moduleRecordFromEditorModule(const Module& module) {
    ProjectModuleRecord record;
    record.id = module.id();
    record.ipcoreId = module.ipcoreId();
    record.instanceId = module.instanceId();
    record.type = module.type();
    for (auto it = module.parameters().constBegin(); it != module.parameters().constEnd(); ++it) {
        record.parameters.insert(it.key(), parameterValueToJson(it.value().value()));
    }
    return record;
}

ProjectConnectionRecord connectionRecordFromEditorConnection(const Connection& connection) {
    ProjectConnectionRecord record;
    record.id = connection.id();
    record.source = ProjectConnectionEndpoint{connection.source().moduleId,
                                              connection.source().portId};
    record.target = ProjectConnectionEndpoint{connection.target().moduleId,
                                              connection.target().portId};
    record.connectionClassId = connection.connectionClassId();
    record.status = connection.status().isEmpty() ? QStringLiteral("valid") : connection.status();
    record.alternatives = connection.alternatives();
    for (const ConnectionInterfaceRef& interfaceRef : connection.interfaces()) {
        record.interfaces.append(ProjectConnectionInterfaceRef{interfaceRef.instanceId,
                                                               interfaceRef.interfaceId});
    }
    return record;
}

bool connectionRecordReferencesModule(const ProjectConnectionRecord& connection,
                                      const QString& moduleId) {
    if (connection.source.moduleId == moduleId || connection.target.moduleId == moduleId) {
        return true;
    }
    return std::any_of(connection.interfaces.constBegin(),
                       connection.interfaces.constEnd(),
                       [&moduleId](const ProjectConnectionInterfaceRef& interfaceRef) {
                           return interfaceRef.instanceId == moduleId;
                       });
}

template <typename Record>
qsizetype indexOfRecordById(const QVector<Record>& records, const QString& id) {
    for (qsizetype index = 0; index < records.size(); ++index) {
        if (records.at(index).id == id) {
            return index;
        }
    }
    return -1;
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

bool ProjectService::upsertEditorModuleRecord(const Module& module) {
    if (!m_hasDocument || module.id().trimmed().isEmpty()) {
        return false;
    }

    const ProjectModuleRecord record = moduleRecordFromEditorModule(module);
    const qsizetype index = indexOfRecordById(m_document.modules, record.id);
    if (index >= 0) {
        m_document.modules[index] = record;
    } else {
        m_document.modules.append(record);
    }
    emit currentDocumentChanged();
    return true;
}

bool ProjectService::removeEditorModuleRecord(const QString& moduleId) {
    if (!m_hasDocument || moduleId.trimmed().isEmpty()) {
        return false;
    }

    for (qsizetype index = m_document.connections.size(); index > 0; --index) {
        const qsizetype recordIndex = index - 1;
        if (connectionRecordReferencesModule(m_document.connections.at(recordIndex), moduleId)) {
            m_document.connections.removeAt(recordIndex);
        }
    }

    const qsizetype index = indexOfRecordById(m_document.modules, moduleId);
    if (index >= 0) {
        m_document.modules.removeAt(index);
    }
    emit currentDocumentChanged();
    return true;
}

bool ProjectService::upsertEditorConnectionRecord(const Connection& connection) {
    if (!m_hasDocument || connection.id().trimmed().isEmpty()) {
        return false;
    }

    const ProjectConnectionRecord record = connectionRecordFromEditorConnection(connection);
    const qsizetype index = indexOfRecordById(m_document.connections, record.id);
    if (index >= 0) {
        m_document.connections[index] = record;
    } else {
        m_document.connections.append(record);
    }
    emit currentDocumentChanged();
    return true;
}

bool ProjectService::removeEditorConnectionRecord(const QString& connectionId) {
    if (!m_hasDocument || connectionId.trimmed().isEmpty()) {
        return false;
    }

    const qsizetype index = indexOfRecordById(m_document.connections, connectionId);
    if (index >= 0) {
        m_document.connections.removeAt(index);
    }
    emit currentDocumentChanged();
    return true;
}
