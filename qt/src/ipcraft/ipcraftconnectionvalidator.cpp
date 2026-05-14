#include "ipcraft/ipcraftconnectionvalidator.h"

#include <algorithm>
#include <utility>

namespace {

struct ResolvedParticipant {
    IpcraftConnectionParticipant participant;
    const IpcraftPackageManifest* manifest = nullptr;
    const IpcraftModuleDescriptor* module = nullptr;
    const IpcraftInterfaceDescriptor* interfaceDescriptor = nullptr;
};

const IpcraftPackageManifest* manifestForParticipant(
    const QVector<IpcraftPackageManifest>& manifests,
    const IpcraftConnectionParticipant& participant) {
    for (const IpcraftPackageManifest& manifest : manifests) {
        if (manifest.id == participant.packageId) {
            return &manifest;
        }
    }
    return nullptr;
}

const IpcraftInterfaceAcceptRule* acceptRuleForClass(
    const IpcraftInterfaceDescriptor& interfaceDescriptor,
    const QString& connectionClassId) {
    for (const IpcraftInterfaceAcceptRule& rule : interfaceDescriptor.accepts) {
        if (rule.connectionClassId == connectionClassId) {
            return &rule;
        }
    }
    return nullptr;
}

bool sameInterface(const ProjectConnectionInterfaceRef& lhs,
                   const ProjectConnectionInterfaceRef& rhs) {
    return lhs.instanceId == rhs.instanceId &&
           lhs.interfaceId == rhs.interfaceId;
}

bool interfaceAlreadyUsed(const QVector<ProjectConnectionRecord>& currentConnections,
                          const ProjectConnectionInterfaceRef& interfaceRef,
                          const QString& ignoredConnectionId) {
    for (const ProjectConnectionRecord& connection : currentConnections) {
        if (!ignoredConnectionId.isEmpty() && connection.id == ignoredConnectionId) {
            continue;
        }
        if (connection.status == QStringLiteral("invalid")) {
            continue;
        }
        for (const ProjectConnectionInterfaceRef& existing : connection.interfaces) {
            if (sameInterface(existing, interfaceRef)) {
                return true;
            }
        }
    }
    return false;
}

QVector<ProjectConnectionInterfaceRef> normalizedInterfaces(
    QVector<ProjectConnectionInterfaceRef> interfaces,
    bool symmetric) {
    if (!symmetric) {
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

bool symmetricRolesMatch(QStringList classRoles, QStringList participantRoles) {
    if (classRoles.isEmpty()) {
        return participantRoles.isEmpty();
    }

    if (classRoles.size() == 1) {
        return std::all_of(participantRoles.cbegin(), participantRoles.cend(), [&](const QString& role) {
            return role == classRoles.first();
        });
    }

    if (classRoles.size() != participantRoles.size()) {
        return false;
    }

    classRoles.sort();
    participantRoles.sort();
    return classRoles == participantRoles;
}

bool rolesMatch(const IpcraftConnectionClass& connectionClass,
                const QStringList& participantRoles) {
    if (connectionClass.symmetric) {
        return symmetricRolesMatch(connectionClass.roles, participantRoles);
    }

    if (connectionClass.roles.size() != participantRoles.size()) {
        return false;
    }

    for (qsizetype index = 0; index < participantRoles.size(); ++index) {
        if (participantRoles.at(index) != connectionClass.roles.at(index)) {
            return false;
        }
    }
    return true;
}

QString roleList(const QStringList& roles) {
    return roles.join(QStringLiteral(", "));
}

struct ClassEvaluation {
    bool valid = false;
    QString reason;
};

ClassEvaluation evaluateClass(const IpcraftConnectionClass& connectionClass,
                              const QVector<ResolvedParticipant>& participants) {
    QStringList participantRoles;
    for (const ResolvedParticipant& participant : participants) {
        const IpcraftInterfaceAcceptRule* rule =
            acceptRuleForClass(*participant.interfaceDescriptor, connectionClass.id);
        if (rule == nullptr) {
            return {false,
                    QStringLiteral("Interface %1.%2 does not accept connection class '%3'")
                        .arg(participant.participant.interfaceRef.instanceId,
                             participant.participant.interfaceRef.interfaceId,
                             connectionClass.id)};
        }
        participantRoles.append(rule->role);
    }

    if (!rolesMatch(connectionClass, participantRoles)) {
        return {false,
                QStringLiteral("Connection class '%1' requires role order [%2], got [%3]")
                    .arg(connectionClass.id,
                         roleList(connectionClass.roles),
                         roleList(participantRoles))};
    }

    return {true, {}};
}

const IpcraftConnectionClass* findClass(const IpcraftPackageManifest& manifest,
                                        const QString& connectionClassId) {
    for (const IpcraftConnectionClass& connectionClass : manifest.connectionClasses) {
        if (connectionClass.id == connectionClassId) {
            return &connectionClass;
        }
    }
    return nullptr;
}

} // namespace

IpcraftConnectionValidator::IpcraftConnectionValidator(
    QVector<IpcraftPackageManifest> manifests,
    QVector<ProjectConnectionRecord> currentConnections)
    : m_manifests(std::move(manifests)),
      m_currentConnections(std::move(currentConnections)) {}

IpcraftConnectionDecision IpcraftConnectionValidator::validate(
    const QVector<IpcraftConnectionParticipant>& participants,
    const QString& selectedClassId,
    const QString& ignoredConnectionId) const {
    IpcraftConnectionDecision decision;
    decision.status = IpcraftConnectionStatus::Invalid;

    if (participants.size() != 2) {
        decision.message = QStringLiteral("Connection requires exactly two interface participants");
        return decision;
    }

    QVector<ResolvedParticipant> resolved;
    resolved.reserve(participants.size());
    const IpcraftPackageManifest* commonManifest = nullptr;
    for (const IpcraftConnectionParticipant& participant : participants) {
        const IpcraftPackageManifest* manifest = manifestForParticipant(m_manifests, participant);
        if (manifest == nullptr) {
            decision.message = participant.packageId.isEmpty()
                ? QStringLiteral("Connection participant package metadata is missing")
                : QStringLiteral("Connection references missing package '%1'").arg(participant.packageId);
            return decision;
        }
        if (commonManifest != nullptr && commonManifest != manifest) {
            decision.message = QStringLiteral("Connection participants belong to different packages");
            return decision;
        }
        commonManifest = manifest;

        const IpcraftModuleDescriptor* module = manifest->module(participant.moduleId);
        if (module == nullptr) {
            decision.message = QStringLiteral("Connection references missing module '%1'")
                                   .arg(participant.moduleId);
            return decision;
        }

        const IpcraftInterfaceDescriptor* interfaceDescriptor =
            manifest->interfaceDescriptor(participant.moduleId,
                                          participant.interfaceRef.interfaceId);
        if (interfaceDescriptor == nullptr) {
            decision.message = QStringLiteral("Connection references missing interface '%1' on module '%2'")
                                   .arg(participant.interfaceRef.interfaceId, participant.moduleId);
            return decision;
        }

        if (!interfaceDescriptor->multiConnection &&
            interfaceAlreadyUsed(m_currentConnections,
                                 participant.interfaceRef,
                                 ignoredConnectionId)) {
            decision.message = QStringLiteral("Interface %1.%2 is already used by another connection")
                                   .arg(participant.interfaceRef.instanceId,
                                        participant.interfaceRef.interfaceId);
            return decision;
        }

        resolved.push_back(ResolvedParticipant{participant, manifest, module, interfaceDescriptor});
        decision.normalizedInterfaces.push_back(participant.interfaceRef);
    }

    if (commonManifest == nullptr) {
        decision.message = QStringLiteral("Connection package metadata is not available");
        return decision;
    }

    QVector<const IpcraftConnectionClass*> candidateClasses;
    if (!selectedClassId.isEmpty()) {
        const IpcraftConnectionClass* selectedClass = findClass(*commonManifest, selectedClassId);
        if (selectedClass == nullptr) {
            decision.message =
                QStringLiteral("Connection class '%1' is not declared").arg(selectedClassId);
            return decision;
        }
        candidateClasses.push_back(selectedClass);
    } else {
        for (const IpcraftConnectionClass& connectionClass : commonManifest->connectionClasses) {
            candidateClasses.push_back(&connectionClass);
        }
    }

    QVector<const IpcraftConnectionClass*> validClasses;
    QString lastRejection;
    for (const IpcraftConnectionClass* connectionClass : candidateClasses) {
        const ClassEvaluation evaluation = evaluateClass(*connectionClass, resolved);
        if (evaluation.valid) {
            validClasses.push_back(connectionClass);
        } else {
            lastRejection = evaluation.reason;
        }
    }

    if (validClasses.isEmpty()) {
        decision.message = lastRejection.isEmpty()
            ? QStringLiteral("Connection interfaces do not share a valid connection class")
            : lastRejection;
        return decision;
    }

    const IpcraftConnectionClass* selectedClass = validClasses.first();
    decision.selectedClassId = selectedClass->id;
    decision.normalizedInterfaces =
        normalizedInterfaces(std::move(decision.normalizedInterfaces),
                             selectedClass->symmetric);

    if (selectedClassId.isEmpty() && validClasses.size() > 1) {
        decision.status = IpcraftConnectionStatus::Ambiguous;
        for (const IpcraftConnectionClass* connectionClass : validClasses) {
            decision.alternatives.append(connectionClass->id);
        }
        decision.message = QStringLiteral("Connection has multiple valid classes: %1")
                               .arg(decision.alternatives.join(QStringLiteral(", ")));
        return decision;
    }

    decision.status = IpcraftConnectionStatus::Valid;
    return decision;
}
