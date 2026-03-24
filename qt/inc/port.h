// Port represents an input or output connection point on a module
#pragma once

#include <QString>

class Port {
public:
    enum class Direction { Input, Output };

    Port(const QString& id, Direction direction, const QString& type, const QString& name);

    QString id() const { return m_id; }
    Direction direction() const { return m_direction; }
    QString type() const { return m_type; }
    QString name() const { return m_name; }

private:
    QString m_id;
    Direction m_direction;
    QString m_type;
    QString m_name;
};
