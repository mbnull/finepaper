#include "application/domain_configuration.h"

#include "storage/json.h"

#include <QJsonValue>
#include <QStringList>

#include <utility>

namespace finepaper::domain_configuration {
namespace {

const QStringList& fieldNames() {
    static const QStringList names{
        QStringLiteral("domains"),
        QStringLiteral("domainMemberships"),
        QStringLiteral("domainRelations"),
        QStringLiteral("crossingPolicies"),
        QStringLiteral("edgeOverrides")
    };
    return names;
}

void appendExpectedArray(QVector<Diagnostic>& diagnostics,
                         const QString& field,
                         const QString& basePath) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        QStringLiteral("create.expected_array"),
        QStringLiteral("domainConfiguration.%1 must be present as an array")
            .arg(field),
        basePath + QLatin1Char('/') + field,
        QStringLiteral("finepaper")
    });
}

bool isDomainPath(const QString& path) {
    QString relativePath = path;
    const QString configurationRoot = QStringLiteral("/domainConfiguration");
    if (relativePath == configurationRoot) {
        return true;
    }
    if (relativePath.startsWith(configurationRoot + QLatin1Char('/'))) {
        relativePath.remove(0, configurationRoot.size());
    }
    for (const QString& field : fieldNames()) {
        const QString prefix = QLatin1Char('/') + field;
        if (relativePath == prefix
            || relativePath.startsWith(prefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

} // namespace

DomainConfiguration fromDesign(const NocDesign& design) {
    return DomainConfiguration{
        design.domains,
        design.domainMemberships,
        design.domainRelations,
        design.crossingPolicies,
        design.edgeOverrides
    };
}

QJsonObject toJson(const DomainConfiguration& configuration) {
    NocDesign design;
    design.formatVersion = 2;
    design = replace(design, configuration);
    const QJsonObject designObject = designToJson(design);
    QJsonObject object;
    for (const QString& field : fieldNames()) {
        object.insert(field, designObject.value(field));
    }
    return object;
}

NocDesign replace(const NocDesign& design, DomainConfiguration configuration) {
    NocDesign replaced = design;
    replaced.domains = std::move(configuration.domains);
    replaced.domainMemberships = std::move(configuration.domainMemberships);
    replaced.domainRelations = std::move(configuration.domainRelations);
    replaced.crossingPolicies = std::move(configuration.crossingPolicies);
    replaced.edgeOverrides = std::move(configuration.edgeOverrides);
    return replaced;
}

ParseResult parse(const QJsonObject& object,
                  const NocDesign& baseDesign,
                  const QString& basePath) {
    ParseResult result;
    if (baseDesign.formatVersion != 2) {
        result.diagnostics.append(Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("create.domain_configuration_requires_v2"),
            QStringLiteral("domainConfiguration requires Design formatVersion 2"),
            basePath,
            QStringLiteral("finepaper")
        });
        return result;
    }
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!fieldNames().contains(it.key())) {
            result.diagnostics.append(Diagnostic{
                QStringLiteral("error"),
                QStringLiteral("create.unknown_field"),
                QStringLiteral("domainConfiguration.%1 is not supported")
                    .arg(it.key()),
                basePath + QLatin1Char('/') + it.key(),
                QStringLiteral("finepaper")
            });
        }
    }
    for (const QString& field : fieldNames()) {
        if (!object.contains(field) || !object.value(field).isArray()) {
            appendExpectedArray(result.diagnostics, field, basePath);
        }
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    QJsonObject designObject = designToJson(baseDesign);
    for (const QString& field : fieldNames()) {
        designObject.insert(field, object.value(field));
    }
    const DesignLoadResult parsed = designFromJson(designObject);
    result.configuration = fromDesign(parsed.design);
    for (Diagnostic diagnostic : parsed.diagnostics) {
        if (!isDomainPath(diagnostic.path)) {
            continue;
        }
        diagnostic.path = basePath + diagnostic.path;
        result.diagnostics.append(std::move(diagnostic));
    }
    result.success = !hasErrors(result.diagnostics);
    return result;
}

bool ownsDiagnostic(const Diagnostic& diagnostic) {
    return isDomainPath(diagnostic.path);
}

} // namespace finepaper::domain_configuration
