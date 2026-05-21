#include "ipcraft/artifactmodel.h"

#include "ipcraft/jsonhelpers.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>

namespace {

void insertString(QJsonObject& object, const QString& key, const QString& value) {
    if (!value.isEmpty()) {
        object.insert(key, value);
    }
}

ipcraft::DiagnosticLocation documentLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    return location;
}

ipcraft::DiagnosticLocation artifactLocation(const QString& artifactId) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("artifact");
    location.artifactId = artifactId;
    return location;
}

ipcraft::Diagnostic diagnostic(const QString& ruleId,
                               const QString& message,
                               QVector<ipcraft::DiagnosticLocation> locations) {
    ipcraft::Diagnostic record;
    record.severity = QStringLiteral("error");
    record.source = QStringLiteral("core");
    record.ruleId = ruleId;
    record.category = QStringLiteral("artifact");
    record.message = message;
    record.locations = std::move(locations);
    return record;
}

void addArtifactDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                           const QString& ruleId,
                           const QString& message,
                           const QString& path,
                           const QString& artifactId) {
    QVector<ipcraft::DiagnosticLocation> locations{documentLocation(path)};
    if (!artifactId.isEmpty()) {
        locations.append(artifactLocation(artifactId));
    }
    diagnostics.records.append(diagnostic(ruleId, message, std::move(locations)));
}

QString childPath(const QString& base, const QString& key) {
    return base + QLatin1Char('.') + key;
}

QString indexPath(qsizetype index) {
    return QStringLiteral("$.artifacts[%1]").arg(index);
}

QString slashPath(QString path) {
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

QString portablePath(QString path) {
    path = slashPath(path);
    return QDir::cleanPath(path);
}

bool isWindowsAbsolutePath(const QString& path) {
    return path.size() >= 3 &&
           path.at(0).isLetter() &&
           path.at(1) == QLatin1Char(':') &&
           path.at(2) == QLatin1Char('/');
}

bool hasTraversalSegment(const QString& relativePath) {
    return slashPath(relativePath)
        .split(QLatin1Char('/'), Qt::SkipEmptyParts)
        .contains(QStringLiteral(".."));
}

QString canonicalOrAbsoluteRoot(const QString& rootPath) {
    const QFileInfo rootInfo(rootPath);
    const QString canonical = rootInfo.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? rootInfo.absoluteFilePath() : canonical);
}

bool pathInsideRoot(const QString& rootPath, const QString& path) {
    const QString root = canonicalOrAbsoluteRoot(rootPath);
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    const QString clean = QDir::cleanPath(canonical);
    return clean == root || clean.startsWith(root + QLatin1Char('/'));
}

bool validRelativeGlob(const QString& glob) {
    const QString raw = slashPath(glob);
    const QString normalized = portablePath(glob);
    return !normalized.isEmpty() &&
           !QDir::isAbsolutePath(normalized) &&
           !isWindowsAbsolutePath(normalized) &&
           !hasTraversalSegment(raw);
}

QString stringValue(const QJsonObject& object,
                    std::initializer_list<QString> keys) {
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
    }
    return {};
}

bool boolValue(const QJsonObject& object, const QString& key, bool defaultValue = false) {
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : defaultValue;
}

bool artifactLess(const ipcraft::ArtifactRecord& left,
                  const ipcraft::ArtifactRecord& right) {
    if (left.type != right.type) {
        return left.type < right.type;
    }
    if (left.id != right.id) {
        return left.id < right.id;
    }
    return left.path < right.path;
}

} // namespace

namespace ipcraft {

QJsonObject ArtifactRecord::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("path"), path);
    if (size >= 0) {
        object.insert(QStringLiteral("size"), size);
    }
    insertString(object, QStringLiteral("modified_time"), modifiedTime);
    insertString(object, QStringLiteral("source_instance"), sourceInstanceId);
    insertString(object, QStringLiteral("flow_run_id"), flowRunId);
    if (!metadata.isEmpty()) {
        object.insert(QStringLiteral("metadata"), metadata);
    }
    return sortedJsonObject(object);
}

QJsonObject ArtifactIndex::toJson() const {
    QVector<ArtifactRecord> sortedRecords = records;
    std::sort(sortedRecords.begin(), sortedRecords.end(), artifactLess);

    QJsonArray array;
    for (const ArtifactRecord& record : sortedRecords) {
        array.append(record.toJson());
    }

    QJsonObject object;
    insertString(object, QStringLiteral("flow_run_id"), flowRunId);
    object.insert(QStringLiteral("records"), array);
    return sortedJsonObject(object);
}

ArtifactCollectResult ArtifactCollector::collect(const ArtifactCollectRequest& request) {
    ArtifactCollectResult result;
    result.index.flowRunId = request.flowRunId;

    const QString rootPath =
        request.outputRoot.trimmed().isEmpty() ? request.runRoot : request.outputRoot;
    if (rootPath.trimmed().isEmpty() || !QFileInfo(rootPath).isDir()) {
        addArtifactDiagnostic(result.diagnostics,
                              QStringLiteral("artifact.glob_escape"),
                              QStringLiteral("Artifact collection root is not available."),
                              QStringLiteral("$.run_root"),
                              {});
        result.ok = false;
        return result;
    }

    const QString canonicalRoot = canonicalOrAbsoluteRoot(rootPath);
    for (qsizetype index = 0; index < request.package.artifacts.size(); ++index) {
        const QString artifactPath = indexPath(index);
        const QJsonValue artifactValue = request.package.artifacts.at(index);
        if (!artifactValue.isObject()) {
            addArtifactDiagnostic(result.diagnostics,
                                  QStringLiteral("artifact.glob_escape"),
                                  QStringLiteral("Artifact declaration must be an object."),
                                  artifactPath,
                                  {});
            continue;
        }

        const QJsonObject artifact = artifactValue.toObject();
        const QString id = stringValue(artifact, {QStringLiteral("id")});
        const QString type = stringValue(artifact, {QStringLiteral("type")});
        const QString glob = stringValue(artifact, {QStringLiteral("glob")});
        const QString normalizedGlob = portablePath(glob);
        const bool required = boolValue(artifact, QStringLiteral("required"), false) ||
                              boolValue(artifact, QStringLiteral("primary"), false);

        if (!validRelativeGlob(glob)) {
            addArtifactDiagnostic(result.diagnostics,
                                  QStringLiteral("artifact.glob_escape"),
                                  QStringLiteral("Artifact glob must stay inside the collection root."),
                                  childPath(artifactPath, QStringLiteral("glob")),
                                  id);
            continue;
        }

        int matches = 0;
        QDirIterator iterator(canonicalRoot,
                              QDir::Files | QDir::System | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString absolutePath = iterator.next();
            const QString relativePath =
                QDir(canonicalRoot).relativeFilePath(absolutePath);
            const QString normalizedRelative = portablePath(relativePath);
            if (!QDir::match(normalizedGlob, normalizedRelative)) {
                continue;
            }

            if (!pathInsideRoot(canonicalRoot, absolutePath)) {
                addArtifactDiagnostic(result.diagnostics,
                                      QStringLiteral("artifact.glob_escape"),
                                      QStringLiteral("Artifact glob matched a path outside the collection root."),
                                      childPath(artifactPath, QStringLiteral("glob")),
                                      id);
                continue;
            }

            QFileInfo info(absolutePath);
            ArtifactRecord record;
            record.id = id;
            record.type = type.isEmpty() ? QStringLiteral("other") : type;
            record.path = normalizedRelative;
            record.size = info.size();
            record.modifiedTime = info.lastModified().toUTC().toString(Qt::ISODateWithMs);
            record.sourceInstanceId = request.sourceInstanceId;
            record.flowRunId = request.flowRunId;
            result.index.records.append(record);
            ++matches;
        }

        if (required && matches == 0) {
            addArtifactDiagnostic(result.diagnostics,
                                  QStringLiteral("artifact.required_missing"),
                                  QStringLiteral("Required artifact was not produced."),
                                  artifactPath,
                                  id);
        }
    }

    std::sort(result.index.records.begin(), result.index.records.end(), artifactLess);
    result.ok = result.diagnostics.records.isEmpty();
    return result;
}

} // namespace ipcraft
