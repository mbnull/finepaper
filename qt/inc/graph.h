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

    void addConnection(std::unique_ptr<Connection> connection);
    void removeConnection(const QString& connectionId);

    const std::vector<std::unique_ptr<Module>>& modules() const { return m_modules; }
    const std::vector<std::unique_ptr<Connection>>& connections() const { return m_connections; }

signals:
    void moduleAdded(Module* module);
    void moduleRemoved(const QString& moduleId);
    void connectionAdded(Connection* connection);
    void connectionRemoved(const QString& connectionId);

private:
    std::vector<std::unique_ptr<Module>> m_modules;
    std::vector<std::unique_ptr<Connection>> m_connections;
};
