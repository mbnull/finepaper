// Optional IP-XACT connection-only strict checks for package manifests.
#pragma once

#include "ipcraft/ipcraftmanifest.h"

#include <QList>
#include <QString>
#include <QVector>

struct IpxactConnectionParticipant {
    QString instanceId;
    QString componentRef;
    QString moduleId;
    QString interfaceId;
};

struct IpxactConnection {
    QString id;
    QVector<IpxactConnectionParticipant> participants;
};

struct IpxactConnectionDiagnostic {
    QString connectionId;
    QString message;
};

struct IpxactConnectionCheckResult {
    QList<IpxactConnectionDiagnostic> diagnostics;
};

class IpxactConnectionChecker {
public:
    IpxactConnectionCheckResult check(const IpcraftPackageManifest& manifest,
                                      const QVector<IpxactConnection>& connections) const;
};
