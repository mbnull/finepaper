#include "port.h"

Port::Port(const QString& id,
           Direction direction,
           const QString& type,
           const QString& name,
           const QString& description,
           const QString& role,
           const QString& busType)
    : m_id(id),
      m_direction(direction),
      m_type(type),
      m_name(name),
      m_description(description),
      m_role(role),
      m_busType(busType) {
}
