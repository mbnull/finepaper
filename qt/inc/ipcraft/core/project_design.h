#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ipcraft::core {

struct ValidationIssue {
    QString code;
    QString message;
    QString path;
};

struct PackageRef {
    QString id;
    QString version;
};

struct EndpointRef {
    QString component;
    QString interface;
};

struct ComponentInstance {
    QString id;
    QString type;
    QString packageRef;
    QJsonObject config;
    QJsonObject identity;
    QJsonObject metadata;
    QJsonObject extensionData;
};

struct InterfaceInstance {
    QString id;
    QString ownerComponentId;
    QString type;
    QString role;
    QString direction;
    QString protocol;
    QString clockRef;
    QString resetRef;
    QJsonObject config;
    QJsonObject metadata;
};

struct Connection {
    QString id;
    EndpointRef from;
    EndpointRef to;
    QString kind = QStringLiteral("interface");
    QJsonObject config;
    QJsonObject constraints;
    QJsonObject metadata;
};

struct TopologyAttachment {
    QString id;
    QString topologyId;
    QJsonObject attachmentPoint;
    QString componentRef;
    QString interfaceRef;
    QString adapterRef;
    QJsonObject config;
};

struct TopologyGraph {
    QString id;
    QString schema;
    QString ownerComponentId;
    QString kind;
    QString family;
    QString providerRef;
    QJsonObject parameters;
    QJsonObject constraints;
    QVector<QJsonObject> nodes;
    QVector<QJsonObject> links;
    QVector<TopologyAttachment> attachments;
    QJsonObject routing;
    QJsonObject metadata;
};

struct ViewDocument {
    QString id;
    QString schema;
    QString kind;
    QString targetRef;
    QString providerRef;
    QString sourceRef;
    QJsonObject layout;
    QJsonObject presentationState;
    QJsonObject metadata;
};

struct ExtensionBlock {
    QString ownerPackageId;
    QString schemaId;
    int version = 0;
    QJsonObject data;
    QJsonObject validationState;
};

struct ProjectDesign {
    QString schema;
    QString id;
    QString name;
    QVector<PackageRef> packages;
    QVector<ComponentInstance> components;
    QVector<InterfaceInstance> interfaces;
    QVector<Connection> connections;
    QVector<TopologyGraph> topologies;
    QJsonObject constraints;
    QVector<ViewDocument> views;
    QVector<QJsonObject> diagnostics;
    QVector<QJsonObject> artifacts;
    QVector<ExtensionBlock> extensions;
    QJsonObject metadata;
};

QVector<ValidationIssue> validateProjectDesign(const ProjectDesign& project);
QJsonObject extensionBlockToJson(const ExtensionBlock& extension);
ExtensionBlock extensionBlockFromJson(const QJsonObject& object);

} // namespace ipcraft::core
