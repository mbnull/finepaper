#pragma once

#include <QString>

class Connection {
public:
    Connection(const QString& id, const QString& sourcePortId, const QString& targetPortId);

    QString id() const { return m_id; }
    QString sourcePortId() const { return m_sourcePortId; }
    QString targetPortId() const { return m_targetPortId; }

private:
    QString m_id;
    QString m_sourcePortId;
    QString m_targetPortId;
};
