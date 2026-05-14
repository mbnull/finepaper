#pragma once

#include "ipcraft/ipcraftmanifest.h"
#include "project/projectdocument.h"

#include <QString>
#include <QStringList>
#include <QVector>

enum class IpcraftConnectionStatus {
    Valid,
    Ambiguous,
    Invalid
};

struct IpcraftConnectionDecision {
    IpcraftConnectionStatus status = IpcraftConnectionStatus::Invalid;
    QString selectedClassId;
    QStringList alternatives;
    QString message;
    QVector<ProjectConnectionInterfaceRef> normalizedInterfaces;
};

struct IpcraftConnectionParticipant {
    QString packageId;
    QString moduleId;
    ProjectConnectionInterfaceRef interfaceRef;
};

class IpcraftConnectionValidator {
public:
    IpcraftConnectionValidator(QVector<IpcraftPackageManifest> manifests,
                               QVector<ProjectConnectionRecord> currentConnections = {});

    IpcraftConnectionDecision validate(
        const QVector<IpcraftConnectionParticipant>& participants,
        const QString& selectedClassId = QString(),
        const QString& ignoredConnectionId = QString()) const;

private:
    QVector<IpcraftPackageManifest> m_manifests;
    QVector<ProjectConnectionRecord> m_currentConnections;
};
