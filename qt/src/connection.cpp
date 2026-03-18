#include "connection.h"

Connection::Connection(const QString& id, const QString& sourcePortId, const QString& targetPortId)
    : m_id(id), m_sourcePortId(sourcePortId), m_targetPortId(targetPortId) {
}
