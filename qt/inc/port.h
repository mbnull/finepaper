// Port represents an input or output connection point on a module
#pragma once

#include <QString>

class Port {
public:
    enum class Direction { Input, Output, InOut };

    Port(const QString& id,
         Direction direction,
         const QString& type,
         const QString& name,
         const QString& description = {},
         const QString& role = {},
         const QString& busType = {});

    QString id() const { return m_id; }
    Direction direction() const { return m_direction; }
    QString type() const { return m_type; }
    QString name() const { return m_name; }
    QString description() const { return m_description; }
    QString role() const { return m_role; }
    QString busType() const { return m_busType; }

private:
    QString m_id;
    Direction m_direction;
    QString m_type;
    QString m_name;
    QString m_description;
    QString m_role;
    QString m_busType;
};
