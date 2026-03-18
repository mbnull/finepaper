#pragma once

#include <QString>
#include "portref.h"

class Connection {
public:
    Connection(const QString& id, const PortRef& source, const PortRef& target);

    QString id() const { return m_id; }
    PortRef source() const { return m_source; }
    PortRef target() const { return m_target; }

private:
    QString m_id;
    PortRef m_source;
    PortRef m_target;
};
