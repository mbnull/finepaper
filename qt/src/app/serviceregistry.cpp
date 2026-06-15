#include "app/serviceregistry.h"

#include <utility>

ServiceKey ServiceKey::fromLiteral(const char* value) {
    return ServiceKey(QString::fromUtf8(value));
}

ServiceKey::ServiceKey(QString value) : m_value(std::move(value)) {}

QString ServiceKey::value() const {
    return m_value;
}

bool ServiceKey::isValid() const {
    return !m_value.trimmed().isEmpty() && m_value == m_value.trimmed();
}

bool ServiceKey::operator==(const ServiceKey& other) const {
    return m_value == other.m_value;
}

bool ServiceRegistry::registerService(const ServiceKey& key, void* service) {
    if (!key.isValid() || !service || m_services.contains(key.value())) {
        return false;
    }
    m_services.insert(key.value(), service);
    return true;
}

void* ServiceRegistry::service(const ServiceKey& key) const {
    return key.isValid() ? m_services.value(key.value(), nullptr) : nullptr;
}

bool ServiceRegistry::contains(const ServiceKey& key) const {
    return key.isValid() && m_services.contains(key.value());
}
