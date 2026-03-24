#include "module.h"
#include <memory>

Module::Module(const QString& id, const QString& type, QObject* parent)
    : QObject(parent), m_id(id), m_type(type) {
}

void Module::addPort(const Port& port) {
    m_ports.push_back(port);
}

// Update existing parameter or create new one, then notify listeners
void Module::setParameter(const QString& name, Parameter::Value value) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        it.value().setValue(value);
    } else {
        m_parameters.insert(name, Parameter(name, value));
    }
    emit parameterChanged(name);
}

void Module::removeParameter(const QString& name) {
    m_parameters.remove(name);
    emit parameterChanged(name);
}

// Deep copy module with all ports and parameters
std::unique_ptr<Module> Module::clone() const {
    auto cloned = std::make_unique<Module>(m_id, m_type);
    cloned->m_ports = m_ports;
    cloned->m_parameters = m_parameters;
    return cloned;
}
