// Graph — central data model for the SoC/NoC topology.
// Owns all Modules and Connections, enforces uniqueness and validity constraints,
// and emits Qt signals when the topology changes so the UI stays in sync.
// Supports JSON serialisation (loadFromJson / saveToJson).
#pragma once

#include "module.h"
#include "connection.h"
#include <QObject>
#include <QMap>
#include <QHash>
#include <QJsonDocument>
#include <vector>
#include <memory>

enum class GraphJsonFlavor {
    Editor,
    Framework
};

class Graph : public QObject {
    Q_OBJECT

public:
    explicit Graph(QObject* parent = nullptr);

    bool addModule(std::unique_ptr<Module> module);
    void removeModule(const QString& moduleId);
    Module* getModule(const QString& moduleId) const;
    std::unique_ptr<Module> takeModule(const QString& moduleId);
    bool insertModule(std::unique_ptr<Module> module);

    void addConnection(std::unique_ptr<Connection> connection);
    void removeConnection(const QString& connectionId);
    std::unique_ptr<Connection> takeConnection(const QString& connectionId);
    void insertConnection(std::unique_ptr<Connection> connection);
    bool isValidConnection(const PortRef& source, const PortRef& target) const;

    const std::vector<std::unique_ptr<Module>>& modules() const { return m_modules; }
    const std::vector<std::unique_ptr<Connection>>& connections() const { return m_connections; }

    bool loadFromJson(const QString& jsonPath);
    bool saveToJson(const QString& jsonPath) const;
    bool saveToXml(const QString& xmlPath) const;
    QJsonDocument toJsonDocument(const QString& designName,
                                 GraphJsonFlavor flavor = GraphJsonFlavor::Framework,
                                 QHash<QString, QString>* externalToInternalIds = nullptr) const;

signals:
    void moduleAdded(Module* module);
    void moduleRemoved(const QString& moduleId);
    void connectionAdded(Connection* connection);
    void connectionRemoved(const QString& connectionId);
    void parameterChanged(const QString& moduleId, const QString& paramName);

private slots:
    void onModuleParameterChanged(const QString& moduleId, const QString& paramName);

private:
    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<std::unique_ptr<Connection>> m_connections;
    QMap<QString, QMetaObject::Connection> m_moduleConnections;
};
