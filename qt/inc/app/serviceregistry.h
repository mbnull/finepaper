#pragma once

#include "app/servicekey.h"

#include <QHash>
#include <type_traits>

class ServiceRegistry {
public:
    bool registerService(const ServiceKey& key, void* service);
    void* service(const ServiceKey& key) const;
    bool contains(const ServiceKey& key) const;

    template <typename T>
    T* service(const ServiceKey& key) const {
        static_assert(!std::is_void_v<T>, "Service type must not be void.");
        return static_cast<T*>(service(key));
    }

private:
    QHash<QString, void*> m_services;
};
