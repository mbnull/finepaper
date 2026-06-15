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
    return registerService(key, service, nullptr);
}

bool ServiceRegistry::registerService(const ServiceKey& key,
                                      void* service,
                                      const std::type_info* type) {
    if (!key.isValid() || !service || m_services.contains(key.value())) {
        return false;
    }
    m_services.insert(key.value(), ServiceEntry{service, type});
    return true;
}

void* ServiceRegistry::service(const ServiceKey& key) const {
    const ServiceEntry* entry = entryFor(key);
    return entry ? entry->service : nullptr;
}

bool ServiceRegistry::contains(const ServiceKey& key) const {
    return entryFor(key) != nullptr;
}

const ServiceRegistry::ServiceEntry* ServiceRegistry::entryFor(const ServiceKey& key) const {
    if (!key.isValid()) {
        return nullptr;
    }
    const auto it = m_services.constFind(key.value());
    return it == m_services.cend() ? nullptr : &it.value();
}
