#pragma once

#include "app/servicekey.h"

#include <QHash>
#include <type_traits>
#include <typeinfo>

class ServiceRegistry {
public:
    bool registerService(const ServiceKey& key, void* service);
    void* service(const ServiceKey& key) const;
    bool contains(const ServiceKey& key) const;

    template <typename T>
    bool registerService(const ServiceKey& key, T* service) {
        static_assert(!std::is_void_v<T>, "Service type must not be void.");
        return registerService(key, static_cast<void*>(service), &typeid(T));
    }

    template <typename T>
    T* service(const ServiceKey& key) const {
        static_assert(!std::is_void_v<T>, "Service type must not be void.");
        const ServiceEntry* entry = entryFor(key);
        if (!entry || !entry->type || *entry->type != typeid(T)) {
            return nullptr;
        }
        return static_cast<T*>(entry->service);
    }

private:
    struct ServiceEntry {
        void* service = nullptr;
        const std::type_info* type = nullptr;
    };

    bool registerService(const ServiceKey& key, void* service, const std::type_info* type);
    const ServiceEntry* entryFor(const ServiceKey& key) const;

    QHash<QString, ServiceEntry> m_services;
};
