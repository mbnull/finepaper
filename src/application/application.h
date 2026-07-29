#pragma once

#include "execution/package_protocol.h"
#include "noc/model.h"
#include "package/package.h"

#include <QJsonObject>
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
    QVector<Diagnostic> reloadPackages(const QStringList& roots);
    const QVector<PackageDefinition>& packages() const;

    DesignResult createDesign(const QJsonObject& request) const;
    DesignResult loadDesignFile(const QString& path) const;
    bool saveDesignFile(const QString& path,
                        const NocDesign& design,
                        QVector<Diagnostic>* diagnostics = nullptr) const;

    DesignResult resizeMesh(const NocDesign& design, int rows, int columns) const;
    DesignResult addEndpoint(const NocDesign& design, EndpointInstance endpoint) const;
    DesignResult moveEndpoint(const NocDesign& design,
                              const QString& endpointId,
                              RouterPosition router,
                              std::optional<QString> slot = std::nullopt) const;
    DesignResult removeEndpoint(const NocDesign& design, const QString& endpointId) const;
    DesignResult updateParameters(const NocDesign& design,
                                  const QJsonObject& parameters) const;

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
