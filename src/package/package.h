#pragma once

#include "noc/model.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

struct ParameterDefinition {
    QString id;
    QString type;
    QString label;
    bool hasDefault = false;
    QJsonValue defaultValue;
    std::optional<double> minimum;
    std::optional<double> maximum;
    QStringList values;
};

struct EndpointTypeDefinition {
    QString id;
    QString label;
    QString icon;
    QVector<ParameterDefinition> parameters;
};

struct MeshDefinition {
    int minimumRows = 1;
    int maximumRows = 1;
    int defaultRows = 1;
    int minimumColumns = 1;
    int maximumColumns = 1;
    int defaultColumns = 1;
};

struct AttachmentDefinition {
    int maxPerRouter = 1;
    QString slotMode = QStringLiteral("automatic");
};

struct GeneratorDefinition {
    QString name;
    QString version;
    QString executable;
    bool supportsValidate = false;
    int timeoutSeconds = 300;
};

struct EngineDefinition {
    QString executable;
    bool providesValidation = false;
    int timeoutSeconds = 1800;
};

struct PackageDefinition {
    QString format;
    int formatVersion = 0;
    QString id;
    QString name;
    QString version;
    QString rootPath;
    MeshDefinition mesh;
    QVector<ParameterDefinition> parameters;
    QVector<EndpointTypeDefinition> endpointTypes;
    AttachmentDefinition attachment;
    GeneratorDefinition generator;
    std::optional<EngineDefinition> engine;

    QString key() const;
    const ParameterDefinition* parameter(const QString& id) const;
    const EndpointTypeDefinition* endpointType(const QString& id) const;
};

struct PackageLoadResult {
    bool success = false;
    std::optional<PackageDefinition> package;
    QVector<Diagnostic> diagnostics;
};

PackageLoadResult loadPackage(const QString& packageRoot);
QVector<Diagnostic> validateParameterObject(
    const QJsonObject& values,
    const QVector<ParameterDefinition>& definitions,
    const QString& basePath,
    const QString& source);

class PackageCatalog {
public:
    QVector<Diagnostic> reload(const QStringList& roots);
    const QVector<PackageDefinition>& packages() const;
    std::optional<PackageDefinition> resolve(const PackageReference& reference) const;

private:
    QVector<PackageDefinition> m_packages;
};

} // namespace finepaper
