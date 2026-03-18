#pragma once

#include "port.h"
#include "parameter.h"
#include <QString>
#include <QObject>
#include <memory>
#include <vector>
#include <unordered_map>

class Module : public QObject {
    Q_OBJECT

public:
    Module(const QString& id, const QString& type, QObject* parent = nullptr);

    QString id() const { return m_id; }
    QString type() const { return m_type; }

    const std::vector<Port>& ports() const { return m_ports; }
    void addPort(const Port& port);

    const std::unordered_map<QString, Parameter>& parameters() const { return m_parameters; }
    void setParameter(const QString& name, Parameter::Value value);

    std::unique_ptr<Module> clone() const;

signals:
    void parameterChanged(const QString& name);

private:
    QString m_id;
    QString m_type;
    std::vector<Port> m_ports;
    std::unordered_map<QString, Parameter> m_parameters;
};
