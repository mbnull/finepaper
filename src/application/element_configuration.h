#pragma once

#include "noc/model.h"
#include "package/package.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace finepaper {

// Package defaults and the persisted sparse delta are kept separate so UI
// code can explain both the effective value and whether a value is actually
// overridden in the Design.
struct ResolvedElementConfiguration {
    ElementRef element;
    QString propertySet;
    QJsonObject defaultProperties;
    QJsonObject overrideProperties;
    QJsonObject properties;
    QVector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const;
};

ResolvedElementConfiguration resolveElementConfiguration(
    const NocDesign& design,
    const PackageDefinition& package,
    const ElementRef& element,
    const QString& propertySet);

// Validates persisted sparse records against the Package schema. Structural
// identity/reference checks remain owned by validateDesignStructure().
QVector<Diagnostic> validateElementConfigurations(
    const NocDesign& design,
    const PackageDefinition& package,
    const ValidationCancellationCheck& cancellationRequested = {});

namespace element_configuration {

struct MutationResult {
    NocDesign design;
    QVector<Diagnostic> diagnostics;
};

// `properties` describes the desired effective values. Omitted keys resolve
// to Package defaults. Only values different from their defaults persist.
MutationResult set(const NocDesign& design,
                   const PackageDefinition& package,
                   const ElementRef& element,
                   const QString& propertySet,
                   const QJsonObject& properties);

MutationResult clear(const NocDesign& design,
                     const PackageDefinition& package,
                     const ElementRef& element,
                     const QString& propertySet);

} // namespace element_configuration
} // namespace finepaper
