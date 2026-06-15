#pragma once

#include <QString>

class ServiceKey {
public:
    static ServiceKey fromLiteral(const char* value);

    ServiceKey() = default;
    explicit ServiceKey(QString value);

    QString value() const;
    bool isValid() const;
    bool operator==(const ServiceKey& other) const;

private:
    QString m_value;
};
