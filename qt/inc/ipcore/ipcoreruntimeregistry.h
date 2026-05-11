#pragma once

#include "ipcore/ipcoreruntimedescriptor.h"

#include <QList>
#include <QString>
#include <QStringList>

class IpCoreRuntimeRegistry {
public:
    static IpCoreRuntimeRegistry& instance();

    static QList<IpCoreRuntimeDescriptor> discover(const QStringList& roots);
    static QStringList defaultRuntimeRoots();

    const QList<IpCoreRuntimeDescriptor>& runtimes() const;
    const IpCoreRuntimeDescriptor* runtime(const QString& ipcoreId) const;

private:
    IpCoreRuntimeRegistry();

    QList<IpCoreRuntimeDescriptor> m_runtimes;
};
