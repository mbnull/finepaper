// ProjectService owns ProjectReader/ProjectWriter backed durable project state.
#include "project/projectservice.h"

#include "ipcraft/schemaids.h"
#include "project/projectdesignserializer.h"
#include "project/projectwriter.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <optional>
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

std::optional<QJsonObject> graphConfigFromObject(const QJsonObject& object) {
    const QStringList keys{
        QStringLiteral("graph_config"),
        QStringLiteral("graphConfig"),
        QStringLiteral("ipcraft.graph_config")
    };
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isObject()) {
            return value.toObject();
        }
    }
    return std::nullopt;
}

std::optional<QJsonObject> graphConfigForComponent(
    const ipcraft::core::ComponentInstance& component) {
    if (const std::optional<QJsonObject> fromExtension =
            graphConfigFromObject(component.extensionData)) {
        return fromExtension;
    }
    return graphConfigFromObject(component.metadata);
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

QHash<QString, QJsonObject> graphViewNodesById(const ipcraft::core::ProjectDesign& design) {
    QHash<QString, QJsonObject> nodesById;
    for (const ipcraft::core::ViewDocument& view : design.views) {
        if (view.id != QStringLiteral("graph")) {
            continue;
        }

        const QJsonObject nodes = view.layout.value(QStringLiteral("nodes")).toObject();
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
            if (it.value().isObject()) {
                nodesById.insert(it.key(), it.value().toObject());
            }
        }
    }
    return nodesById;
}

void mergeLayoutParameters(QJsonObject& parameters, const QJsonObject& nodeLayout) {
    for (auto it = nodeLayout.constBegin(); it != nodeLayout.constEnd(); ++it) {
        if (isLayoutParameter(it.key()) && isValidLayoutParameterValue(it.key(), it.value())) {
            parameters.insert(it.key(), it.value());
        }
    }
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
    if (designInstance.hasGraphConfig || designInstance.graphConfigIsNull) {
        merged.hasGraphConfig = designInstance.hasGraphConfig;
        merged.graphConfigIsNull = designInstance.graphConfigIsNull;
        merged.graphConfig = designInstance.graphConfig;
    }
    return merged;
}

QStringList stringListFromJsonArray(const QJsonValue& value) {
    QStringList strings;
    if (!value.isArray()) {
        return strings;
    }

    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString() && !item.toString().trimmed().isEmpty()) {
            strings.append(item.toString());
        }
    }
    return strings;
}

QString metadataString(const QJsonObject& metadata,
                       const QString& key,
                       const QString& fallback = {}) {
    const QString value = metadata.value(key).toString(fallback);
    return value.trimmed().isEmpty() ? fallback : value;
}

ProjectConnectionInterfaceRef interfaceRefFromObject(const QJsonObject& object) {
    ProjectConnectionInterfaceRef ref;
    ref.instanceId = object.value(QStringLiteral("instanceId")).toString();
    if (ref.instanceId.trimmed().isEmpty()) {
        ref.instanceId = object.value(QStringLiteral("instance")).toString();
    }
    if (ref.instanceId.trimmed().isEmpty()) {
        ref.instanceId = object.value(QStringLiteral("object")).toString();
    }
    if (ref.instanceId.trimmed().isEmpty()) {
        ref.instanceId = object.value(QStringLiteral("component")).toString();
    }

    ref.interfaceId = object.value(QStringLiteral("interfaceId")).toString();
    if (ref.interfaceId.trimmed().isEmpty()) {
        ref.interfaceId = object.value(QStringLiteral("interface")).toString();
    }
    return ref;
}

QVector<ProjectConnectionInterfaceRef> interfaceRefsFromMetadata(const QJsonObject& metadata) {
    QVector<ProjectConnectionInterfaceRef> refs;
    const QJsonValue interfacesValue = metadata.value(QStringLiteral("interfaces"));
    if (!interfacesValue.isArray()) {
        return refs;
    }

    const QJsonArray interfaces = interfacesValue.toArray();
    refs.reserve(interfaces.size());
    for (const QJsonValue& value : interfaces) {
        if (!value.isObject()) {
            continue;
        }
        const ProjectConnectionInterfaceRef ref = interfaceRefFromObject(value.toObject());
        if (!ref.instanceId.trimmed().isEmpty() && !ref.interfaceId.trimmed().isEmpty()) {
            refs.append(ref);
        }
    }
    return refs;
}

void projectDesignIntoEditorProjectionFields(ProjectDocument& document,
                                             const ipcraft::core::ProjectDesign& design) {
    document.ipcoreState = document.instances;
    const QHash<QString, QJsonObject> graphViewNodes = graphViewNodesById(design);

    document.modules.clear();
    document.modules.reserve(design.components.size());
    for (const ipcraft::core::ComponentInstance& component : design.components) {
        if (graphConfigForComponent(component).has_value()) {
            continue;
        }

        const ProjectIpInstanceRecord* instance = findInstanceById(document.instances, component.id);

        ProjectModuleRecord module;
        module.id = component.id;
        module.ipcoreId = instance ? instance->ipcoreId : component.packageRef.section(QLatin1Char('@'), 0, 0);
        module.instanceId = instance ? instance->instanceId : component.id;
        module.type = component.type;
        module.parameters = component.config;
        mergeLayoutParameters(module.parameters, graphViewNodes.value(component.id));
        document.modules.append(module);
    }

    for (const ipcraft::core::ComponentInstance& component : design.components) {
        const ProjectIpInstanceRecord* instance = findInstanceById(document.instances, component.id);
        if (!instance) {
            continue;
        }
        const std::optional<QJsonObject> graphConfig = graphConfigForComponent(component);
        if (!graphConfig.has_value()) {
            continue;
        }

        const QJsonArray objects = graphConfig->value(QStringLiteral("objects")).toArray();
        for (const QJsonValue& objectValue : objects) {
            if (!objectValue.isObject()) {
                continue;
            }
            const QJsonObject object = objectValue.toObject();
            const QString objectId = object.value(QStringLiteral("id")).toString().trimmed();
            const QString objectType = object.value(QStringLiteral("type")).toString().trimmed();
            if (objectId.isEmpty() || objectType.isEmpty()) {
                continue;
            }

            ProjectModuleRecord module;
            module.id = objectId;
            module.ipcoreId = instance->ipcoreId;
            module.instanceId = instance->instanceId;
            module.type = objectType;
            if (object.value(QStringLiteral("properties")).isObject()) {
                module.parameters = object.value(QStringLiteral("properties")).toObject();
            }
            mergeLayoutParameters(module.parameters, graphViewNodes.value(objectId));
            document.modules.append(module);
        }
    }

    document.connections.clear();
    document.connections.reserve(design.connections.size());
    for (const ipcraft::core::ComponentInstance& component : design.components) {
        const ProjectIpInstanceRecord* instance = findInstanceById(document.instances, component.id);
        if (!instance) {
            continue;
        }
        const std::optional<QJsonObject> graphConfig = graphConfigForComponent(component);
        if (!graphConfig.has_value()) {
            continue;
        }

        const QJsonArray relationships = graphConfig->value(QStringLiteral("relationships")).toArray();
        for (const QJsonValue& relationshipValue : relationships) {
            if (!relationshipValue.isObject()) {
                continue;
            }
            const QJsonObject relationship = relationshipValue.toObject();
            const QString relationshipId =
                relationship.value(QStringLiteral("id")).toString().trimmed();
            const QString relationshipType =
                relationship.value(QStringLiteral("type")).toString().trimmed();
            const QJsonArray endpoints = relationship.value(QStringLiteral("endpoints")).toArray();
            if (relationshipId.isEmpty() || relationshipType.isEmpty() || endpoints.size() < 2) {
                continue;
            }

            ProjectConnectionRecord record;
            record.id = relationshipId;
            record.type = QStringLiteral("graph_config");
            record.sourceKind = QStringLiteral("design");
            record.connectionClassId = relationshipType;
            record.properties = relationship.value(QStringLiteral("properties")).toObject();
            record.status = metadataString(record.properties,
                                           QStringLiteral("status"),
                                           QStringLiteral("valid"));
            record.alternatives = stringListFromJsonArray(
                record.properties.value(QStringLiteral("alternatives")));

            for (const QJsonValue& endpointValue : endpoints) {
                if (!endpointValue.isObject()) {
                    continue;
                }
                const QJsonObject endpoint = endpointValue.toObject();
                ProjectConnectionInterfaceRef ref;
                ref.instanceId = endpoint.value(QStringLiteral("object")).toString();
                ref.interfaceId = endpoint.value(QStringLiteral("role")).toString();
                ref.properties = endpoint.value(QStringLiteral("properties")).toObject();
                if (!ref.instanceId.trimmed().isEmpty() &&
                    !ref.interfaceId.trimmed().isEmpty()) {
                    record.interfaces.append(ref);
                }
            }
            if (record.interfaces.size() >= 2) {
                document.connections.append(record);
            }
        }
    }

    for (const ipcraft::core::Connection& connection : design.connections) {
        ProjectConnectionRecord record;
        record.id = connection.id;
        record.type = connection.kind;
        record.sourceKind = QStringLiteral("design");
        record.source = ProjectConnectionEndpoint{connection.from.component,
                                                  connection.from.interface};
        record.target = ProjectConnectionEndpoint{connection.to.component,
                                                  connection.to.interface};
        record.connectionClassId = metadataString(connection.metadata, QStringLiteral("class"));
        record.status = metadataString(connection.metadata,
                                       QStringLiteral("status"),
                                       QStringLiteral("valid"));
        record.alternatives = stringListFromJsonArray(
            connection.metadata.value(QStringLiteral("alternatives")));
        record.interfaces = interfaceRefsFromMetadata(connection.metadata);
        document.connections.append(record);
    }
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
    projectDesignIntoEditorProjectionFields(document, design);
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

void ProjectService::reloadDesignFromDocument() {
    m_design = ProjectDesignSerializer::fromDocument(m_document);
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
