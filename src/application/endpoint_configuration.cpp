#include "application/endpoint_configuration.h"

#include <algorithm>

namespace finepaper {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& code,
                      const QString& message,
                      const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("package")});
}

QJsonObject defaultsFor(const EndpointTypeDefinition& type) {
    QJsonObject parameters;
    for (const ParameterDefinition& definition : type.parameters) {
        if (definition.hasDefault) {
            parameters.insert(definition.id, definition.defaultValue);
        }
    }
    return parameters;
}

bool compatibleParameterValue(const QJsonValue& value,
                              const ParameterDefinition& definition) {
    const QVector<Diagnostic> diagnostics = validateParameterObject(
        QJsonObject{{definition.id, value}},
        QVector<ParameterDefinition>{definition},
        QStringLiteral("/parameters"),
        QStringLiteral("package"));
    return !hasErrors(diagnostics);
}

bool sameConfigurationIdentity(const ElementConfiguration& lhs,
                               const ElementConfiguration& rhs) {
    return lhs.element == rhs.element
        && lhs.propertySet == rhs.propertySet;
}

void validateExactImpactConfirmation(
    const QVector<ElementConfiguration>& expected,
    const QVector<ElementConfiguration>& confirmed,
    QVector<Diagnostic>& diagnostics) {
    QVector<bool> expectedAccounted(expected.size(), false);
    QVector<bool> confirmedMatched(confirmed.size(), false);

    // Exact records are matched before identities so an extra duplicate cannot
    // consume the only valid confirmation.
    for (qsizetype confirmedIndex = 0;
         confirmedIndex < confirmed.size(); ++confirmedIndex) {
        for (qsizetype expectedIndex = 0;
             expectedIndex < expected.size(); ++expectedIndex) {
            if (!expectedAccounted.at(expectedIndex)
                && confirmed.at(confirmedIndex) == expected.at(expectedIndex)) {
                expectedAccounted[expectedIndex] = true;
                confirmedMatched[confirmedIndex] = true;
                break;
            }
        }
    }

    const QString base = QStringLiteral(
        "/endpointTypeChange/impactConfirmation/removedAttachmentConfigurations");
    for (qsizetype confirmedIndex = 0;
         confirmedIndex < confirmed.size(); ++confirmedIndex) {
        if (confirmedMatched.at(confirmedIndex)) {
            continue;
        }
        qsizetype staleExpected = -1;
        for (qsizetype expectedIndex = 0;
             expectedIndex < expected.size(); ++expectedIndex) {
            if (!expectedAccounted.at(expectedIndex)
                && sameConfigurationIdentity(confirmed.at(confirmedIndex),
                                             expected.at(expectedIndex))) {
                staleExpected = expectedIndex;
                break;
            }
        }
        if (staleExpected >= 0) {
            expectedAccounted[staleExpected] = true;
            appendDiagnostic(
                diagnostics,
                QStringLiteral(
                    "endpoint.type_change_stale_attachment_configuration_confirmation"),
                QStringLiteral(
                    "confirmed attachment configuration no longer exactly matches the type-change preview"),
                QStringLiteral("%1/%2").arg(base).arg(confirmedIndex));
        } else {
            appendDiagnostic(
                diagnostics,
                QStringLiteral(
                    "endpoint.type_change_extra_attachment_configuration_confirmation"),
                QStringLiteral(
                    "confirmation contains an attachment configuration not present in the type-change preview"),
                QStringLiteral("%1/%2").arg(base).arg(confirmedIndex));
        }
    }

    for (qsizetype expectedIndex = 0;
         expectedIndex < expected.size(); ++expectedIndex) {
        if (!expectedAccounted.at(expectedIndex)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral(
                    "endpoint.type_change_missing_attachment_configuration_confirmation"),
                QStringLiteral(
                    "type change requires exact confirmation before removing an attachment configuration"),
                base);
        }
    }
}

} // namespace

bool EndpointTypeChangePlan::canApply() const {
    return !hasErrors(diagnostics);
}

bool EndpointTypeChangePlan::requiresImpactConfirmation() const {
    return !removedAttachmentConfigurations.isEmpty();
}

namespace endpoint_configuration {

EndpointTypeChangePlan buildTypeChangePlan(
    const NocDesign& design,
    const PackageDefinition& package,
    const QString& endpointId,
    const QString& targetTypeValue,
    EndpointParameterMigration migration,
    const QJsonObject& parameterPatch) {
    EndpointTypeChangePlan plan;
    plan.endpointId = endpointId;
    plan.targetType = targetTypeValue.trimmed();
    plan.parameterMigration = migration;

    if (migration != EndpointParameterMigration::ResetToDefaults
        && migration != EndpointParameterMigration::PreserveCompatible) {
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("endpoint.invalid_parameter_migration"),
            QStringLiteral("Endpoint parameter migration strategy is invalid"),
            QStringLiteral("/endpointTypeChange/parameterMigration"));
        return plan;
    }

    const auto endpoint = std::find_if(
        design.endpoints.cbegin(),
        design.endpoints.cend(),
        [&](const EndpointInstance& value) { return value.id == endpointId; });
    if (endpoint == design.endpoints.cend()) {
        appendDiagnostic(plan.diagnostics,
                         QStringLiteral("endpoint.not_found"),
                         QStringLiteral("Endpoint does not exist"),
                         QStringLiteral("/endpoints"));
        return plan;
    }
    const qsizetype endpointIndex = std::distance(
        design.endpoints.cbegin(), endpoint);
    const QString endpointPath = QStringLiteral("/endpoints/%1")
                                     .arg(endpointIndex);
    plan.currentType = endpoint->type;
    if (plan.targetType == plan.currentType) {
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("endpoint.type_change_same_type"),
            QStringLiteral(
                "target Endpoint type is already active; edit parameters directly instead"),
            endpointPath + QStringLiteral("/type"));
        return plan;
    }

    const EndpointTypeDefinition* target = package.endpointType(plan.targetType);
    if (!target) {
        appendDiagnostic(
            plan.diagnostics,
            QStringLiteral("endpoint.unknown_type"),
            QStringLiteral("target Endpoint type is not declared by the Package"),
            endpointPath + QStringLiteral("/type"));
        return plan;
    }

    plan.parameters = defaultsFor(*target);
    if (migration == EndpointParameterMigration::PreserveCompatible) {
        for (const ParameterDefinition& definition : target->parameters) {
            if (!endpoint->parameters.contains(definition.id)) {
                continue;
            }
            const QJsonValue value = endpoint->parameters.value(definition.id);
            if (compatibleParameterValue(value, definition)) {
                plan.parameters.insert(definition.id, value);
            }
        }
    }
    for (auto patch = parameterPatch.constBegin();
         patch != parameterPatch.constEnd(); ++patch) {
        plan.parameters.insert(patch.key(), patch.value());
    }
    plan.diagnostics += validateParameterObject(
        plan.parameters,
        target->parameters,
        endpointPath + QStringLiteral("/parameters"),
        QStringLiteral("package"));

    const ElementRef attachment{
        ElementKind::EndpointAttachment, endpointId};
    for (const ElementConfiguration& configuration
         : design.elementConfigurations) {
        if (configuration.element != attachment) {
            continue;
        }
        const ElementPropertySetDefinition* propertySet =
            package.elementPropertySet(configuration.propertySet);
        if (propertySet
            && !propertySet->endpointTypes.isEmpty()
            && !propertySet->endpointTypes.contains(plan.targetType)) {
            plan.removedAttachmentConfigurations.append(configuration);
        }
    }

    for (const DomainMembership& membership : design.domainMemberships) {
        if (membership.element
            == ElementRef{ElementKind::Endpoint, endpointId}) {
            plan.retainedDomainMemberships.append(membership);
        }
    }
    return plan;
}

MutationResult applyTypeChange(
    const NocDesign& design,
    const EndpointTypeChangePlan& plan,
    const EndpointTypeChangeImpactConfirmation& confirmation) {
    MutationResult result;
    result.design = design;
    result.diagnostics = plan.diagnostics;
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    const auto endpoint = std::find_if(
        result.design.endpoints.begin(),
        result.design.endpoints.end(),
        [&](const EndpointInstance& value) {
            return value.id == plan.endpointId;
        });
    if (endpoint == result.design.endpoints.end()) {
        appendDiagnostic(result.diagnostics,
                         QStringLiteral("endpoint.not_found"),
                         QStringLiteral("Endpoint does not exist"),
                         QStringLiteral("/endpoints"));
        return result;
    }
    if (endpoint->type != plan.currentType) {
        appendDiagnostic(
            result.diagnostics,
            QStringLiteral("endpoint.type_change_stale_plan"),
            QStringLiteral(
                "Endpoint type changed after the type-change preview was built"),
            QStringLiteral("/endpoints/type"));
        return result;
    }

    validateExactImpactConfirmation(
        plan.removedAttachmentConfigurations,
        confirmation.removedAttachmentConfigurations,
        result.diagnostics);
    if (hasErrors(result.diagnostics)) {
        result.design = design;
        return result;
    }

    endpoint->type = plan.targetType;
    endpoint->parameters = plan.parameters;
    result.design.elementConfigurations.erase(
        std::remove_if(
            result.design.elementConfigurations.begin(),
            result.design.elementConfigurations.end(),
            [&](const ElementConfiguration& configuration) {
                return plan.removedAttachmentConfigurations.contains(
                    configuration);
            }),
        result.design.elementConfigurations.end());
    return result;
}

} // namespace endpoint_configuration
} // namespace finepaper
