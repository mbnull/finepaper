// Connection stores graph endpoints plus project-facing interface metadata.
#include "graph/connection.h"

#include <algorithm>
#include <utility>

namespace {

QString normalizedStatus(const QString& status) {
    return status.isEmpty() ? QStringLiteral("valid") : status;
}

} // namespace

Connection::Connection(const QString& id, const PortRef& source, const PortRef& target)
    : m_id(id), m_source(source), m_target(target) {
}

Connection::Connection(const QString& id,
                       const PortRef& source,
                       const PortRef& target,
                       const QString& connectionClassId,
                       QVector<ConnectionInterfaceRef> interfaces,
                       const QString& status,
                       QStringList alternatives,
                       bool symmetricConnectionClass)
    : m_id(id),
      m_source(source),
      m_target(target),
      m_connectionClassId(connectionClassId),
      m_interfaces(normalizedInterfaces(std::move(interfaces), symmetricConnectionClass)),
      m_status(normalizedStatus(status)),
      m_alternatives(std::move(alternatives)) {
}

std::unique_ptr<Connection> Connection::clone() const {
    return std::make_unique<Connection>(*this);
}

void Connection::setConnectionMetadata(QString connectionClassId,
                                       QString status,
                                       QStringList alternatives) {
    m_connectionClassId = std::move(connectionClassId);
    m_status = normalizedStatus(status);
    m_alternatives = std::move(alternatives);
}

QVector<ConnectionInterfaceRef> Connection::normalizedInterfaces(
    QVector<ConnectionInterfaceRef> interfaces,
    bool symmetricConnectionClass) {
    if (!symmetricConnectionClass) {
        return interfaces;
    }

    std::sort(interfaces.begin(), interfaces.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.instanceId != rhs.instanceId) {
            return lhs.instanceId < rhs.instanceId;
        }
        return lhs.interfaceId < rhs.interfaceId;
    });
    return interfaces;
}
