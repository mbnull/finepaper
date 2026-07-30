#include "application/element_configuration.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <utility>

namespace finepaper {
namespace {

void appendDiagnostic(QVector<Diagnostic>& diagnostics,
                      const QString& code,
                      const QString& message,
                      const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"), code, message, path, QStringLiteral("package")});
}

bool sameIdentity(const ElementConfiguration& configuration,
                  const ElementRef& element,
                  const QString& propertySet) {
    return configuration.element == element
        && configuration.propertySet == propertySet;
}

const EndpointInstance* endpointForAttachment(const NocDesign& design,
                                               const ElementRef& element) {
    if (element.kind != ElementKind::EndpointAttachment) {
        return nullptr;
    }
    const auto endpoint = std::find_if(
        design.endpoints.cbegin(),
        design.endpoints.cend(),
        [&](const EndpointInstance& candidate) {
            return candidate.id == element.id;
        });
    return endpoint == design.endpoints.cend() ? nullptr : &(*endpoint);
}

const ElementPropertySetDefinition* validateTargetAndPropertySet(
    const NocDesign& design,
    const PackageDefinition& package,
    const ElementRef& element,
    const QString& propertySet,
    const QString& basePath,
    QVector<Diagnostic>& diagnostics) {
    if (!formatVersionSupportsElementConfigurations(design.formatVersion)
        || !formatVersionSupportsElementConfigurations(package.formatVersion)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.requires_v3"),
            QStringLiteral(
                "Element configuration requires Design and Package formats with element-configuration support"),
            basePath);
        return nullptr;
    }

    if (!isElementConfigurationTargetKind(element.kind)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.unsupported_element_kind"),
            QStringLiteral(
                "Element configuration kind must be router, router-link, or endpoint-attachment"),
            basePath + QStringLiteral("/element/kind"));
    }
    if (element.id.trimmed().isEmpty()) {
        appendDiagnostic(diagnostics,
                         QStringLiteral("element_configuration.missing_element_id"),
                         QStringLiteral("Element configuration id is required"),
                         basePath + QStringLiteral("/element/id"));
    } else if (isElementConfigurationTargetKind(element.kind)
               && !designReferenceExists(design, element)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.unknown_element"),
            QStringLiteral("Element configuration references an unknown element"),
            basePath + QStringLiteral("/element"));
    }

    if (propertySet.trimmed().isEmpty()) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.missing_property_set"),
            QStringLiteral("Element configuration propertySet is required"),
            basePath + QStringLiteral("/propertySet"));
        return nullptr;
    }
    const ElementPropertySetDefinition* definition =
        package.elementPropertySet(propertySet);
    if (!definition) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.unknown_property_set"),
            QStringLiteral("propertySet is not declared by the Package"),
            basePath + QStringLiteral("/propertySet"));
        return nullptr;
    }
    if (!definition->appliesTo.contains(element.kind)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.not_applicable"),
            QStringLiteral("propertySet does not apply to this element kind"),
            basePath + QStringLiteral("/propertySet"));
        return nullptr;
    }

    if (element.kind == ElementKind::EndpointAttachment
        && !definition->endpointTypes.isEmpty()) {
        const EndpointInstance* endpoint = endpointForAttachment(design, element);
        if (endpoint
            && !definition->endpointTypes.contains(endpoint->type)) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("element_configuration.endpoint_type_not_applicable"),
                QStringLiteral(
                    "propertySet does not apply to this Endpoint attachment type"),
                basePath + QStringLiteral("/propertySet"));
            return nullptr;
        }
    }
    return definition;
}

bool scalarMatchesType(const QJsonValue& value, ParameterType type) {
    switch (type) {
    case ParameterType::Integer:
        return value.isDouble() && std::isfinite(value.toDouble())
            && std::floor(value.toDouble()) == value.toDouble();
    case ParameterType::Number:
        return value.isDouble() && std::isfinite(value.toDouble());
    case ParameterType::Boolean:
        return value.isBool();
    case ParameterType::String:
    case ParameterType::Enumeration:
        return value.isString();
    case ParameterType::Invalid:
        return false;
    }
    return false;
}

void validateScalar(const QJsonValue& value,
                    const ElementPropertyDefinition& definition,
                    const QString& path,
                    QVector<Diagnostic>& diagnostics) {
    if (!scalarMatchesType(value, definition.type)) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.invalid_type"),
            QStringLiteral("property %1 has the wrong type").arg(definition.id),
            path);
        return;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (definition.minimum && number < *definition.minimum) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("element_configuration.below_minimum"),
                QStringLiteral("property %1 is below its minimum")
                    .arg(definition.id),
                path);
        }
        if (definition.maximum && number > *definition.maximum) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("element_configuration.above_maximum"),
                QStringLiteral("property %1 is above its maximum")
                    .arg(definition.id),
                path);
        }
    }
    if (definition.type == ParameterType::Enumeration
        && !definition.values.contains(value.toString())) {
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.invalid_enum"),
            QStringLiteral("property %1 has an unsupported value")
                .arg(definition.id),
            path);
    }
}

void validateProperties(const QJsonObject& values,
                        const ElementPropertySetDefinition& definition,
                        const QString& basePath,
                        QVector<Diagnostic>& diagnostics) {
    QSet<QString> knownProperties;
    for (const ElementPropertyDefinition& property : definition.properties) {
        knownProperties.insert(property.id);
        if (!values.contains(property.id)) {
            continue;
        }
        const QJsonValue value = values.value(property.id);
        const QString path = basePath + QLatin1Char('/') + property.id;
        if (!property.multiple) {
            validateScalar(value, property, path, diagnostics);
            continue;
        }
        if (!value.isArray()) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("element_configuration.invalid_type"),
                QStringLiteral("property %1 must be an array").arg(property.id),
                path);
            continue;
        }
        const QJsonArray items = value.toArray();
        for (qsizetype index = 0; index < items.size(); ++index) {
            validateScalar(items.at(index),
                           property,
                           QStringLiteral("%1/%2").arg(path).arg(index),
                           diagnostics);
        }
    }
    for (auto value = values.constBegin(); value != values.constEnd(); ++value) {
        if (knownProperties.contains(value.key())) {
            continue;
        }
        appendDiagnostic(
            diagnostics,
            QStringLiteral("element_configuration.unknown_property"),
            QStringLiteral("property %1 is not declared by the Package")
                .arg(value.key()),
            basePath + QLatin1Char('/') + value.key());
    }
}

QJsonObject defaultsFor(const ElementPropertySetDefinition& definition,
                        const QString& basePath,
                        QVector<Diagnostic>& diagnostics) {
    QJsonObject defaults;
    for (const ElementPropertyDefinition& property : definition.properties) {
        if (!property.hasDefault) {
            appendDiagnostic(
                diagnostics,
                QStringLiteral("element_configuration.missing_default"),
                QStringLiteral("Package property %1 has no default")
                    .arg(property.id),
                basePath + QStringLiteral("/properties/") + property.id);
            continue;
        }
        defaults.insert(property.id, property.defaultValue);
    }
    return defaults;
}

void mergeValues(QJsonObject& target, const QJsonObject& values) {
    for (auto value = values.constBegin(); value != values.constEnd(); ++value) {
        target.insert(value.key(), value.value());
    }
}

QJsonObject sparseDelta(const QJsonObject& effective,
                        const ElementPropertySetDefinition& definition) {
    QJsonObject delta;
    for (const ElementPropertyDefinition& property : definition.properties) {
        const QJsonValue value = effective.value(property.id);
        if (value != property.defaultValue) {
            delta.insert(property.id, value);
        }
    }
    return delta;
}

} // namespace

bool ResolvedElementConfiguration::success() const {
    return !hasErrors(diagnostics);
}

ResolvedElementConfiguration resolveElementConfiguration(
    const NocDesign& design,
    const PackageDefinition& package,
    const ElementRef& element,
    const QString& propertySet) {
    ResolvedElementConfiguration result;
    result.element = element;
    result.propertySet = propertySet;
    const QString basePath = QStringLiteral("/elementConfiguration");
    const ElementPropertySetDefinition* definition =
        validateTargetAndPropertySet(
            design, package, element, propertySet, basePath, result.diagnostics);
    if (!definition) {
        return result;
    }
    result.defaultProperties = defaultsFor(
        *definition, basePath, result.diagnostics);

    bool found = false;
    for (const ElementConfiguration& configuration
         : design.elementConfigurations) {
        if (!sameIdentity(configuration, element, propertySet)) {
            continue;
        }
        if (found) {
            appendDiagnostic(
                result.diagnostics,
                QStringLiteral("element_configuration.duplicate"),
                QStringLiteral(
                    "Element has more than one configuration for this propertySet"),
                basePath);
            continue;
        }
        found = true;
        result.overrideProperties = configuration.properties;
    }
    validateProperties(result.overrideProperties,
                       *definition,
                       basePath + QStringLiteral("/properties"),
                       result.diagnostics);
    result.properties = result.defaultProperties;
    mergeValues(result.properties, result.overrideProperties);
    return result;
}

QVector<Diagnostic> validateElementConfigurations(
    const NocDesign& design,
    const PackageDefinition& package) {
    QVector<Diagnostic> diagnostics;
    for (qsizetype index = 0;
         index < design.elementConfigurations.size(); ++index) {
        const ElementConfiguration& configuration =
            design.elementConfigurations.at(index);
        const QString basePath =
            QStringLiteral("/elementConfigurations/%1").arg(index);
        const ElementPropertySetDefinition* definition =
            validateTargetAndPropertySet(
                design,
                package,
                configuration.element,
                configuration.propertySet,
                basePath,
                diagnostics);
        if (!definition) {
            continue;
        }
        validateProperties(configuration.properties,
                           *definition,
                           basePath + QStringLiteral("/properties"),
                           diagnostics);
        for (const ElementPropertyDefinition& property
             : definition->properties) {
            if (configuration.properties.contains(property.id)
                && configuration.properties.value(property.id)
                    == property.defaultValue) {
                appendDiagnostic(
                    diagnostics,
                    QStringLiteral("element_configuration.non_sparse_property"),
                    QStringLiteral(
                        "property %1 equals its Package default and must not be persisted")
                        .arg(property.id),
                    basePath + QStringLiteral("/properties/") + property.id);
            }
        }
    }
    return diagnostics;
}

namespace element_configuration {

MutationResult set(const NocDesign& design,
                   const PackageDefinition& package,
                   const ElementRef& element,
                   const QString& propertySet,
                   const QJsonObject& properties) {
    MutationResult result;
    result.design = design;
    const QString basePath = QStringLiteral("/elementConfiguration");
    const ElementPropertySetDefinition* definition =
        validateTargetAndPropertySet(
            design, package, element, propertySet, basePath, result.diagnostics);
    if (!definition) {
        return result;
    }
    validateProperties(properties,
                       *definition,
                       basePath + QStringLiteral("/properties"),
                       result.diagnostics);
    QJsonObject effective = defaultsFor(
        *definition, basePath, result.diagnostics);
    mergeValues(effective, properties);
    if (hasErrors(result.diagnostics)) {
        return result;
    }
    const QJsonObject delta = sparseDelta(effective, *definition);

    QVector<ElementConfiguration> configurations;
    configurations.reserve(result.design.elementConfigurations.size()
                           + (delta.isEmpty() ? 0 : 1));
    bool replacementInserted = false;
    for (const ElementConfiguration& configuration
         : std::as_const(result.design.elementConfigurations)) {
        if (!sameIdentity(configuration, element, propertySet)) {
            configurations.append(configuration);
            continue;
        }
        if (!replacementInserted && !delta.isEmpty()) {
            configurations.append(
                ElementConfiguration{element, propertySet, delta});
        }
        replacementInserted = true;
    }
    if (!replacementInserted && !delta.isEmpty()) {
        configurations.append(ElementConfiguration{element, propertySet, delta});
    }
    result.design.elementConfigurations = std::move(configurations);
    return result;
}

MutationResult clear(const NocDesign& design,
                     const PackageDefinition& package,
                     const ElementRef& element,
                     const QString& propertySet) {
    MutationResult result;
    result.design = design;
    if (!validateTargetAndPropertySet(
            design,
            package,
            element,
            propertySet,
            QStringLiteral("/elementConfiguration"),
            result.diagnostics)) {
        return result;
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }
    result.design.elementConfigurations.erase(
        std::remove_if(
            result.design.elementConfigurations.begin(),
            result.design.elementConfigurations.end(),
            [&](const ElementConfiguration& configuration) {
                return sameIdentity(configuration, element, propertySet);
            }),
        result.design.elementConfigurations.end());
    return result;
}

} // namespace element_configuration
} // namespace finepaper
