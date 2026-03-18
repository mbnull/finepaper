#include "module.h"

Module::Module(const QString& id, const QString& type, QObject* parent)
    : QObject(parent), m_id(id), m_type(type) {
}

void Module::addPort(const Port& port) {
    m_ports.push_back(port);
}

void Module::setParameter(const QString& name, Parameter::Value value) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        it->second.setValue(value);
    } else {
        m_parameters.emplace(name, Parameter(name, value));
    }
    emit parameterChanged(name);
}
