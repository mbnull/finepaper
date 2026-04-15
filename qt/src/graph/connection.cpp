// Connection stores a directed edge between two module port references.
#include "graph/connection.h"

Connection::Connection(const QString& id, const PortRef& source, const PortRef& target)
    : m_id(id), m_source(source), m_target(target) {
}
