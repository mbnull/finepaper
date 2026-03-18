#pragma once

#include "module.h"
#include "connection.h"
#include <QObject>
#include <vector>
#include <memory>

class Graph : public QObject {
    Q_OBJECT

public:
    explicit Graph(QObject* parent = nullptr);

    void addModule(std::unique_ptr<Module> module);
    void removeModule(const QString& moduleId);
    Module* getModule(const QString& moduleId) const;
    std::unique_ptr<Module> takeModule(const QString& moduleId);
    void insertModule(std::unique_ptr<Module> module);

    void addConnection(std::unique_ptr<Connection> connection);
    void removeConnection(const QString& connectionId);
    std::unique_ptr<Connection> takeConnection(const QString& connectionId);
    void insertConnection(std::unique_ptr<Connection> connection);
    bool isValidConnection(const PortRef& source, const PortRef& target) const;

    const std::vector<std::unique_ptr<Module>>& modules() const { return m_modules; }
    const std::vector<std::unique_ptr<Connection>>& connections() const { return m_connections; }

    bool loadFromJson(const QString& jsonPath);

signals:
    void moduleAdded(Module* module);
    void moduleRemoved(const QString& moduleId);
    void connectionAdded(Connection* connection);
    void connectionRemoved(const QString& connectionId);
    void parameterChanged(const QString& moduleId, const QString& paramName);

private:
    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<std::unique_ptr<Connection>> m_connections;
};
