// Module represents a hardware component in the SoC/NoC design with ports and parameters
#pragma once

#include "graph/port.h"
#include "graph/parameter.h"
#include <QString>
#include <QObject>
#include <QHash>
#include <memory>
#include <vector>

class Module : public QObject {
    Q_OBJECT

public:
    Module(const QString& id, const QString& type, QObject* parent = nullptr);

    QString id() const { return m_id; }
    QString type() const { return m_type; }
    QString ipcoreId() const { return m_ipcoreId; }
    QString instanceId() const { return m_instanceId; }
    void setIpcoreId(const QString& ipcoreId);
    void setInstanceId(const QString& instanceId);

    const std::vector<Port>& ports() const { return m_ports; }
    void addPort(const Port& port);

    const QHash<QString, Parameter>& parameters() const { return m_parameters; }
    void setParameter(const QString& name, Parameter::Value value);
    void removeParameter(const QString& name);

    std::unique_ptr<Module> clone() const;

signals:
    void parameterChanged(const QString& name);

private:
    QString m_id;
    QString m_type;
    QString m_ipcoreId;
    QString m_instanceId;
    std::vector<Port> m_ports;
    QHash<QString, Parameter> m_parameters;
};
