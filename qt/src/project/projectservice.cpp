// ProjectService owns ProjectReader/ProjectWriter backed durable project state.
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "graph/connection.h"
#include "graph/module.h"
#include "project/projectdesignserializer.h"
#include "project/projectwriter.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
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

QString instanceScopeKey(const QString& ipcoreId, const QString& instanceId) {
    return ipcoreId + QLatin1Char('\n') + instanceId;
}

bool isLayoutParameter(const QString& key) {
    return key == QStringLiteral("x") ||
           key == QStringLiteral("y") ||
           key == QStringLiteral("collapsed");
}

bool isValidLayoutParameterValue(const QString& key, const QJsonValue& value) {
    if (key == QStringLiteral("collapsed")) {
        return value.isBool();
    }
    if (key == QStringLiteral("x") || key == QStringLiteral("y")) {
        return value.isDouble();
    }
    return false;
}

QJsonObject graphObjectProperties(const QJsonObject& parameters) {
    QJsonObject properties;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (!isLayoutParameter(it.key())) {
            properties.insert(it.key(), it.value());
        }
    }
    return properties;
}

QJsonObject layoutNodeProperties(const QJsonObject& parameters) {
    QJsonObject properties;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (isLayoutParameter(it.key()) && isValidLayoutParameterValue(it.key(), it.value())) {
            properties.insert(it.key(), it.value());
        }
    }
    return properties;
}

QJsonArray stringArray(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject normalizedGraphConfigBase(const ProjectIpInstanceRecord& instance) {
    QJsonObject graphConfig;
    if (instance.hasGraphConfig && !instance.graphConfigIsNull) {
        graphConfig = instance.graphConfig;
    }
    graphConfig.insert(QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1);
    if (!graphConfig.value(QStringLiteral("properties")).isObject()) {
        graphConfig.insert(QStringLiteral("properties"), QJsonObject{});
    }
    if (!graphConfig.value(QStringLiteral("native")).isObject()) {
        graphConfig.insert(QStringLiteral("native"), QJsonObject{});
    }
    return graphConfig;
}

bool hasGraphView(const QJsonObject& layout) {
    const QJsonArray views = layout.value(QStringLiteral("views")).toArray();
    return std::any_of(views.constBegin(), views.constEnd(), [](const QJsonValue& viewValue) {
        return viewValue.isObject() &&
               viewValue.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("graph");
    });
}

QJsonObject mergeGraphLayoutNode(const QJsonObject& existingNode,
                                 const QJsonObject& projectedNode) {
    QJsonObject node = existingNode;
    for (auto it = projectedNode.constBegin(); it != projectedNode.constEnd(); ++it) {
        node.insert(it.key(), it.value());
    }
    return node;
}

void mergeGraphLayoutNodes(QJsonObject& layout, const QJsonObject& layoutNodes) {
    QJsonArray views = layout.value(QStringLiteral("views")).toArray();
    bool updated = false;
    for (qsizetype index = 0; index < views.size(); ++index) {
        if (!views.at(index).isObject()) {
            continue;
        }
        QJsonObject view = views.at(index).toObject();
        if (view.value(QStringLiteral("id")).toString() != QStringLiteral("graph")) {
            continue;
        }

        QJsonObject canvas = view.value(QStringLiteral("canvas")).toObject();
        const QJsonObject existingNodes = canvas.value(QStringLiteral("nodes")).toObject();
        QJsonObject mergedNodes;
        for (auto it = layoutNodes.constBegin(); it != layoutNodes.constEnd(); ++it) {
            const QJsonObject existingNode = existingNodes.value(it.key()).toObject();
            mergedNodes.insert(it.key(), mergeGraphLayoutNode(existingNode, it.value().toObject()));
        }
        canvas.insert(QStringLiteral("nodes"), mergedNodes);
        if (!canvas.value(QStringLiteral("connections")).isObject()) {
            canvas.insert(QStringLiteral("connections"), QJsonObject{});
        }
        view.insert(QStringLiteral("canvas"), canvas);
        views[index] = view;
        updated = true;
        break;
    }

    if (!updated && !layoutNodes.isEmpty()) {
        QJsonObject canvas;
        canvas.insert(QStringLiteral("nodes"), layoutNodes);
        canvas.insert(QStringLiteral("connections"), QJsonObject{});
        QJsonObject view;
        view.insert(QStringLiteral("id"), QStringLiteral("graph"));
        view.insert(QStringLiteral("kind"), QStringLiteral("canvas"));
        view.insert(QStringLiteral("canvas"), canvas);
        views.append(view);
        updated = true;
    }

    if (updated) {
        layout.insert(QStringLiteral("views"), views);
    }
}

void attachEditorProjectionRecords(ProjectDocument& document) {
    QHash<QString, qsizetype> instanceIndexes;
    for (qsizetype index = 0; index < document.instances.size(); ++index) {
        const ProjectIpInstanceRecord& instance = document.instances.at(index);
        instanceIndexes.insert(instanceScopeKey(instance.package.id, instance.id), index);
    }

    QHash<QString, qsizetype> moduleInstanceIndexes;
    QHash<qsizetype, QJsonArray> objectsByInstance;
    QJsonObject layoutNodes;
    for (const ProjectModuleRecord& module : document.modules) {
        const QString scope = instanceScopeKey(module.ipcoreId, module.instanceId);
        if (!instanceIndexes.contains(scope)) {
            continue;
        }
        const qsizetype instanceIndex = instanceIndexes.value(scope);
        moduleInstanceIndexes.insert(module.id, instanceIndex);

        QJsonObject object;
        object.insert(QStringLiteral("id"), module.id);
        object.insert(QStringLiteral("type"), module.type);
        const QJsonObject properties = graphObjectProperties(module.parameters);
        if (!properties.isEmpty()) {
            object.insert(QStringLiteral("properties"), properties);
        }
        objectsByInstance[instanceIndex].append(object);

        const QJsonObject layout = layoutNodeProperties(module.parameters);
        if (!layout.isEmpty()) {
            layoutNodes.insert(module.id, layout);
        }
    }

    QHash<qsizetype, QJsonArray> relationshipsByInstance;
    for (const ProjectConnectionRecord& connection : document.connections) {
        QJsonArray endpoints;
        qsizetype ownerIndex = -1;
        bool sameOwner = true;
        const QVector<ProjectConnectionInterfaceRef> interfaces = !connection.interfaces.isEmpty()
            ? connection.interfaces
            : QVector<ProjectConnectionInterfaceRef>{
                ProjectConnectionInterfaceRef{connection.source.moduleId, connection.source.portId},
                ProjectConnectionInterfaceRef{connection.target.moduleId, connection.target.portId}
            };

        for (const ProjectConnectionInterfaceRef& interfaceRef : interfaces) {
            if (!moduleInstanceIndexes.contains(interfaceRef.instanceId)) {
                sameOwner = false;
                break;
            }
            const qsizetype endpointOwner = moduleInstanceIndexes.value(interfaceRef.instanceId);
            if (ownerIndex < 0) {
                ownerIndex = endpointOwner;
            } else if (ownerIndex != endpointOwner) {
                sameOwner = false;
                break;
            }

            QJsonObject endpoint;
            endpoint.insert(QStringLiteral("object"), interfaceRef.instanceId);
            endpoint.insert(QStringLiteral("role"), interfaceRef.interfaceId);
            if (!interfaceRef.properties.isEmpty()) {
                endpoint.insert(QStringLiteral("properties"), interfaceRef.properties);
            }
            endpoints.append(endpoint);
        }

        if (!sameOwner || ownerIndex < 0 || endpoints.size() < 2) {
            continue;
        }

        QJsonObject relationship;
        relationship.insert(QStringLiteral("id"), connection.id);
        relationship.insert(QStringLiteral("type"),
                            connection.connectionClassId.trimmed().isEmpty()
                                ? QStringLiteral("connection")
                                : connection.connectionClassId);
        relationship.insert(QStringLiteral("endpoints"), endpoints);
        QJsonObject properties = connection.properties;
        if (!connection.status.trimmed().isEmpty()) {
            properties.insert(QStringLiteral("status"), connection.status);
        }
        if (!connection.alternatives.isEmpty()) {
            properties.insert(QStringLiteral("alternatives"), stringArray(connection.alternatives));
        }
        if (!properties.isEmpty()) {
            relationship.insert(QStringLiteral("properties"), properties);
        }
        relationshipsByInstance[ownerIndex].append(relationship);
    }

    for (qsizetype index = 0; index < document.instances.size(); ++index) {
        ProjectIpInstanceRecord& instance = document.instances[index];
        const bool hasEditorProjection = objectsByInstance.contains(index) ||
                                         relationshipsByInstance.contains(index);
        const bool hasExistingGraphConfig = instance.hasGraphConfig &&
                                            !instance.graphConfigIsNull;
        if (!hasEditorProjection && !hasExistingGraphConfig) {
            continue;
        }

        QJsonObject graphConfig = normalizedGraphConfigBase(instance);
        graphConfig.insert(QStringLiteral("objects"), objectsByInstance.value(index));
        graphConfig.insert(QStringLiteral("relationships"), relationshipsByInstance.value(index));
        instance.hasGraphConfig = true;
        instance.graphConfigIsNull = false;
        instance.graphConfig = graphConfig;
    }

    if (!layoutNodes.isEmpty() || hasGraphView(document.layout)) {
        mergeGraphLayoutNodes(document.layout, layoutNodes);
    }
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

void ProjectService::reloadDesignFromDocument() {
    m_design = ProjectDesignSerializer::fromDocument(m_document);
}

void ProjectService::reloadDesignFromDocumentWithEditorProjection() {
    attachEditorProjectionRecords(m_document);
    reloadDesignFromDocument();
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
    reloadDesignFromDocument();
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
    reloadDesignFromDocument();
    m_currentPath.clear();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromLoadedFile(ProjectDocument document,
                                                                   const QString& path) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    reloadDesignFromDocument();
    m_currentPath = QFileInfo(path).absoluteFilePath();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentFromProjection(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    reloadDesignFromDocument();
    m_hasDocument = true;
    emit currentDocumentChanged();
    return successResult();
}

ProjectServiceResult ProjectService::replaceDocumentPreservingPath(ProjectDocument document) {
    m_document = std::move(document);
    normalizeDocument(m_document);
    reloadDesignFromDocument();
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
    reloadDesignFromDocument();
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
    reloadDesignFromDocumentWithEditorProjection();
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
    reloadDesignFromDocumentWithEditorProjection();
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
    reloadDesignFromDocumentWithEditorProjection();
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
    reloadDesignFromDocumentWithEditorProjection();
    emit currentDocumentChanged();
    return true;
}
