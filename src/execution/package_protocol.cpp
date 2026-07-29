#include "execution/package_protocol.h"

#include <QJsonArray>
#include <QSet>

namespace finepaper {
namespace {

void appendProtocolError(PackageOperationResult& result,
                         const QString& resultPath,
                         const QString& path,
                         const QString& message) {
    result.diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        QStringLiteral("operation.invalid_result"),
        message,
        resultPath + path,
        QStringLiteral("execution")});
}

bool requiredString(const QJsonObject& object,
                    const QString& key,
                    QString* value) {
    const QJsonValue raw = object.value(key);
    if (!raw.isString() || raw.toString().trimmed().isEmpty()) {
        return false;
    }
    *value = raw.toString();
    return true;
}

} // namespace

PackageOperationResult parsePackageOperationResult(
    const QJsonObject& object,
    const QString& resultPath,
    const QString& defaultSource,
    ArtifactResultPolicy artifactPolicy) {
    PackageOperationResult result;
    bool schemaError = false;

    const QJsonValue success = object.value(QStringLiteral("success"));
    if (!success.isBool()) {
        appendProtocolError(result,
                            resultPath,
                            QStringLiteral("/success"),
                            QStringLiteral("success must be a boolean"));
        schemaError = true;
    } else {
        result.success = success.toBool();
    }

    if (object.contains(QStringLiteral("diagnostics"))) {
        const QJsonValue diagnostics = object.value(QStringLiteral("diagnostics"));
        if (!diagnostics.isArray()) {
            appendProtocolError(result,
                                resultPath,
                                QStringLiteral("/diagnostics"),
                                QStringLiteral("diagnostics must be an array"));
            schemaError = true;
        } else {
            const QJsonArray values = diagnostics.toArray();
            for (qsizetype index = 0; index < values.size(); ++index) {
                const QString base = QStringLiteral("/diagnostics/%1").arg(index);
                if (!values.at(index).isObject()) {
                    appendProtocolError(result,
                                        resultPath,
                                        base,
                                        QStringLiteral("diagnostic must be an object"));
                    schemaError = true;
                    continue;
                }
                const QJsonObject diagnostic = values.at(index).toObject();
                Diagnostic parsed;
                if (!requiredString(diagnostic, QStringLiteral("severity"), &parsed.severity)
                    || !requiredString(diagnostic, QStringLiteral("code"), &parsed.code)
                    || !requiredString(diagnostic, QStringLiteral("message"), &parsed.message)) {
                    appendProtocolError(
                        result,
                        resultPath,
                        base,
                        QStringLiteral("diagnostic severity, code, and message must be non-empty strings"));
                    schemaError = true;
                    continue;
                }
                if (diagnostic.contains(QStringLiteral("path"))) {
                    if (!diagnostic.value(QStringLiteral("path")).isString()) {
                        appendProtocolError(result,
                                            resultPath,
                                            base + QStringLiteral("/path"),
                                            QStringLiteral("diagnostic path must be a string"));
                        schemaError = true;
                        continue;
                    }
                    parsed.path = diagnostic.value(QStringLiteral("path")).toString();
                }
                if (diagnostic.contains(QStringLiteral("source"))) {
                    if (!diagnostic.value(QStringLiteral("source")).isString()) {
                        appendProtocolError(result,
                                            resultPath,
                                            base + QStringLiteral("/source"),
                                            QStringLiteral("diagnostic source must be a string"));
                        schemaError = true;
                        continue;
                    }
                    parsed.source = diagnostic.value(QStringLiteral("source")).toString();
                } else {
                    parsed.source = defaultSource;
                }
                result.diagnostics.append(std::move(parsed));
            }
        }
    }

    const bool artifactsPresent = object.contains(QStringLiteral("artifacts"));
    if (artifactPolicy == ArtifactResultPolicy::Required && !artifactsPresent) {
        appendProtocolError(result,
                            resultPath,
                            QStringLiteral("/artifacts"),
                            QStringLiteral("artifacts must be present for generation"));
        schemaError = true;
    } else if (artifactsPresent) {
        const QJsonValue artifacts = object.value(QStringLiteral("artifacts"));
        if (!artifacts.isArray()) {
            appendProtocolError(result,
                                resultPath,
                                QStringLiteral("/artifacts"),
                                QStringLiteral("artifacts must be an array"));
            schemaError = true;
        } else {
            const QJsonArray values = artifacts.toArray();
            QSet<QString> artifactIds;
            for (qsizetype index = 0; index < values.size(); ++index) {
                const QString base = QStringLiteral("/artifacts/%1").arg(index);
                if (!values.at(index).isObject()) {
                    appendProtocolError(result,
                                        resultPath,
                                        base,
                                        QStringLiteral("artifact must be an object"));
                    schemaError = true;
                    continue;
                }
                const QJsonObject object = values.at(index).toObject();
                Artifact artifact;
                if (!requiredString(object, QStringLiteral("id"), &artifact.id)
                    || !requiredString(object, QStringLiteral("type"), &artifact.type)
                    || !requiredString(object, QStringLiteral("path"), &artifact.path)) {
                    appendProtocolError(
                        result,
                        resultPath,
                        base,
                        QStringLiteral("artifact id, type, and path must be non-empty strings"));
                    schemaError = true;
                    continue;
                }
                if (artifactIds.contains(artifact.id)) {
                    appendProtocolError(result,
                                        resultPath,
                                        base + QStringLiteral("/id"),
                                        QStringLiteral("artifact id must be unique"));
                    schemaError = true;
                    continue;
                }
                if (object.contains(QStringLiteral("primary"))) {
                    if (!object.value(QStringLiteral("primary")).isBool()) {
                        appendProtocolError(result,
                                            resultPath,
                                            base + QStringLiteral("/primary"),
                                            QStringLiteral("artifact primary must be a boolean"));
                        schemaError = true;
                        continue;
                    }
                    artifact.primary = object.value(QStringLiteral("primary")).toBool();
                }
                artifactIds.insert(artifact.id);
                result.artifacts.append(std::move(artifact));
            }
        }
    }

    result.protocolValid = !schemaError;
    return result;
}

} // namespace finepaper
