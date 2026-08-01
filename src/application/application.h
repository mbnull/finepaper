#pragma once

#include "application/domain_assignment.h"
#include "application/domain_configuration.h"
#include "application/element_configuration.h"
#include "application/endpoint_configuration.h"
#include "application/mesh_resize_plan.h"
#include "application/package_catalog/catalog.h"
#include "execution/package_protocol.h"
#include "noc/model.h"
#include "package/package.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

struct DesignResult {
    bool success = false;
    NocDesign design;
    QVector<Diagnostic> diagnostics;
};

struct ValidationResult {
    bool success = false;
    QVector<Diagnostic> diagnostics;
};

struct ExecutionTool {
    QString kind;
    QString name;
    QString version;
};

struct GenerationOptions {
    QString outputRoot;
};

struct GenerationResult {
    bool success = false;
    PackageReference package;
    std::optional<ExecutionTool> tool;
    QVector<Diagnostic> diagnostics;
    QVector<Artifact> artifacts;
    QString operationId;
    QString runDirectory;
    QString outputDirectory;
    QString stdoutLog;
    QString stderrLog;
    int exitCode = -1;
};

class FinepaperApplication {
public:
    PackageCatalogReloadResult reloadPackages(const QStringList& roots);
    const QVector<PackageDefinition>& packages() const;

    DesignResult createDesign(const QJsonObject& request) const;
    DesignResult loadDesignFile(const QString& path) const;
    bool saveDesignFile(const QString& path,
                        const NocDesign& design,
                        QVector<Diagnostic>* diagnostics = nullptr) const;

    DesignResult resizeMesh(
        const NocDesign& design,
        int rows,
        int columns,
        const QVector<DomainMembership>& newRouterMemberships = {},
        const MeshResizeImpactConfirmation& confirmation = {}) const;
    DesignResult addEndpoint(
        const NocDesign& design,
        EndpointInstance endpoint,
        const QHash<QString, QStringList>& domainAssignments = {},
        const QVector<DomainEdgeOverride>& attachmentOverrides = {},
        const QVector<ElementConfiguration>& attachmentConfigurations = {}) const;
    DesignResult moveEndpoint(const NocDesign& design,
                              const QString& endpointId,
                              RouterPosition router,
                              std::optional<QString> slot = std::nullopt) const;
    DesignResult removeEndpoint(const NocDesign& design, const QString& endpointId) const;
    DesignResult updateEndpointParameters(
        const NocDesign& design,
        const QString& endpointId,
        const QJsonObject& parameters) const;
    EndpointTypeChangePlan planEndpointTypeChange(
        const NocDesign& design,
        const QString& endpointId,
        const QString& targetType,
        EndpointParameterMigration migration,
        const QJsonObject& parameterPatch = {}) const;
    DesignResult changeEndpointType(
        const NocDesign& design,
        const QString& endpointId,
        const QString& targetType,
        EndpointParameterMigration migration,
        const QJsonObject& parameterPatch = {},
        const EndpointTypeChangeImpactConfirmation& confirmation = {}) const;
    DesignResult updateParameters(const NocDesign& design,
                                  const QJsonObject& parameters) const;
    DesignResult setDesignExtension(const NocDesign& design,
                                    const QString& extensionId,
                                    const QJsonValue& value) const;
    DesignResult removeDesignExtension(const NocDesign& design,
                                       const QString& extensionId) const;
    DesignResult setElementConfiguration(
        const NocDesign& design,
        ElementRef element,
        const QString& propertySet,
        const QJsonObject& properties) const;
    DesignResult clearElementConfiguration(
        const NocDesign& design,
        ElementRef element,
        const QString& propertySet) const;
    DesignResult replaceDomainConfiguration(
        const NocDesign& design,
        DomainConfiguration configuration) const;
    DesignResult addDomain(const NocDesign& design, DomainDefinition domain) const;
    DesignResult updateDomain(const NocDesign& design,
                              const QString& domainId,
                              DomainDefinition domain) const;
    DesignResult removeDomain(const NocDesign& design, const QString& domainId) const;
    DesignResult assignDomainsToElements(
        const NocDesign& design,
        const QVector<ElementRef>& elements,
        const QString& domainType,
        const QStringList& domainIds) const;
    DesignResult patchDomainAssignments(
        const NocDesign& design,
        const QVector<ElementRef>& elements,
        const QString& domainType,
        DomainAssignmentPatch patch) const;
    DesignResult clearDomainAssignment(
        const NocDesign& design,
        const QVector<ElementRef>& elements,
        const QString& domainType) const;

    ValidationResult validate(const NocDesign& design,
                              bool includePackageValidation = true) const;
    GenerationResult generate(const NocDesign& design,
                              const GenerationOptions& options) const;

private:
    DesignResult validateEditedDesign(const NocDesign& design) const;
    QVector<Diagnostic> validateAgainstPackage(
        const NocDesign& design,
        const PackageDefinition& package) const;
    QVector<Diagnostic> runPackageValidation(
        const NocDesign& design,
        const PackageDefinition& package) const;

    PackageCatalog m_catalog;
};

QJsonObject generationResultToJson(const GenerationResult& result);
QJsonObject validationResultToJson(const ValidationResult& result);

} // namespace finepaper
