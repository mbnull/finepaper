// Graph — central data model for the SoC/NoC topology.
// Owns all Modules and Connections, enforces uniqueness and validity constraints,
// and emits Qt signals when the topology changes so the UI stays in sync.
// Supports JSON serialisation (loadFromJson / saveToJson).
#pragma once

#include "graph/module.h"
#include "graph/connection.h"
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
    ~Graph() override;

    // Adds a new module and emits moduleAdded; returns false on duplicate/invalid IDs.
    bool addModule(std::unique_ptr<Module> module);
    // Removes a module plus incident edges and emits corresponding removal signals.
    void removeModule(const QString& moduleId);
    // Returns the module by ID, or nullptr if not found.
    Module* getModule(const QString& moduleId) const;
    // Removes a module from the graph and transfers ownership to the caller.
    std::unique_ptr<Module> takeModule(const QString& moduleId);
    // Inserts a pre-built module without creating it internally (used by undo/redo).
    bool insertModule(std::unique_ptr<Module> module);

    // Adds a connection and emits connectionAdded.
    void addConnection(std::unique_ptr<Connection> connection);
    // Removes a connection by ID and emits connectionRemoved.
    void removeConnection(const QString& connectionId);
    // Removes a connection from the graph and transfers ownership to the caller.
    std::unique_ptr<Connection> takeConnection(const QString& connectionId);
    // Inserts a pre-built connection (used by undo/redo).
    void insertConnection(std::unique_ptr<Connection> connection);
    // Validates endpoint/module existence, directionality, and occupancy constraints.
    bool isValidConnection(const PortRef& source, const PortRef& target) const;

    const std::vector<std::unique_ptr<Module>>& modules() const { return m_modules; }
    const std::vector<std::unique_ptr<Connection>>& connections() const { return m_connections; }

    // Imports an editor graph JSON file and rebuilds the in-memory graph.
    // Existing modules/connections are cleared before import; returns false on file/JSON errors.
    bool loadFromJson(const QString& jsonPath);
    // Exports current graph using editor-friendly JSON shape.
    bool saveToJson(const QString& jsonPath) const;
    // Exports current graph as XML generated from the editor JSON object model.
    bool saveToXml(const QString& xmlPath) const;
    // Serializes graph in editor/framework flavor, optionally returning external->internal ID map.
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
