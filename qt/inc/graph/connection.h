// Connection represents a link between two module interfaces in the NoC topology.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include "graph/portref.h"

struct ConnectionInterfaceRef {
    QString instanceId;
    QString interfaceId;
};

class Connection {
public:
    Connection(const QString& id, const PortRef& source, const PortRef& target);
    Connection(const QString& id,
               const PortRef& source,
               const PortRef& target,
               const QString& connectionClassId,
               QVector<ConnectionInterfaceRef> interfaces,
               const QString& status = QStringLiteral("valid"),
               QStringList alternatives = {},
               bool symmetricConnectionClass = false);

    QString id() const { return m_id; }
    PortRef source() const { return m_source; }
    PortRef target() const { return m_target; }
    QString connectionClassId() const { return m_connectionClassId; }
    QVector<ConnectionInterfaceRef> interfaces() const { return m_interfaces; }
    QString status() const { return m_status; }
    QStringList alternatives() const { return m_alternatives; }
    std::unique_ptr<Connection> clone() const;

    static QVector<ConnectionInterfaceRef> normalizedInterfaces(
        QVector<ConnectionInterfaceRef> interfaces,
        bool symmetricConnectionClass);

private:
    QString m_id;
    PortRef m_source;
    PortRef m_target;
    QString m_connectionClassId;
    QVector<ConnectionInterfaceRef> m_interfaces;
    QString m_status = QStringLiteral("valid");
    QStringList m_alternatives;
};
