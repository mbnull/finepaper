#include "port.h"

Port::Port(const QString& id, Direction direction, const QString& type, const QString& name)
    : m_id(id), m_direction(direction), m_type(type), m_name(name) {
}
