#include "ipcraft/emitter.h"

#include "ipcraft/jsonhelpers.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
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

ipcraft::DiagnosticLocation fileLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("file");
    location.file = path;
    return location;
}

ipcraft::Diagnostic diagnostic(const QString& ruleId,
                               const QString& message,
                               QVector<ipcraft::DiagnosticLocation> locations) {
    ipcraft::Diagnostic record;
    record.severity = QStringLiteral("error");
    record.source = QStringLiteral("core");
    record.ruleId = ruleId;
    record.category = QStringLiteral("emitter");
    record.message = message;
    record.locations = std::move(locations);
    return record;
}

void addDocumentDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                           const QString& ruleId,
                           const QString& message,
                           const QString& path) {
    diagnostics.records.append(diagnostic(ruleId, message, {documentLocation(path)}));
}

void addFileDiagnostic(ipcraft::DiagnosticStore& diagnostics,
                       const QString& ruleId,
                       const QString& message,
                       const QString& documentPath,
                       const QString& filePath) {
    diagnostics.records.append(diagnostic(ruleId,
                                          message,
                                          {documentLocation(documentPath),
                                           fileLocation(filePath)}));
}

QString childPath(const QString& base, const QString& key) {
    return base + QLatin1Char('.') + key;
}

QString indexPath(const QString& base, qsizetype index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

QString stringValue(const QJsonObject& object, std::initializer_list<QString> keys) {
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
    }
    return {};
}

QString emitterKind(const QJsonObject& emitter) {
    return stringValue(emitter, {QStringLiteral("kind"), QStringLiteral("type")});
}

QString emitterId(const QJsonObject& emitter, qsizetype index, const QString& fallback) {
    const QString id = stringValue(emitter, {QStringLiteral("id")});
    if (!id.isEmpty()) {
        return id;
    }
    if (!fallback.isEmpty()) {
        return fallback;
    }
    return QStringLiteral("emitter_%1").arg(index);
}

bool hasTraversalSegment(const QString& relativePath) {
    const QStringList segments = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return segments.contains(QStringLiteral(".."));
}

QString portablePath(QString path) {
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(path);
}

QString slashPath(QString path) {
    path = path.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

bool isWindowsAbsolutePath(const QString& path) {
    return path.size() >= 3 &&
           path.at(0).isLetter() &&
           path.at(1) == QLatin1Char(':') &&
           path.at(2) == QLatin1Char('/');
}

QString canonicalOrAbsoluteRoot(const QString& rootPath) {
    const QFileInfo rootInfo(rootPath);
    const QString canonical = rootInfo.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? rootInfo.absoluteFilePath() : canonical);
}

bool existingAncestorsStayInsideRoot(const QString& rootPath, const QString& relativePath) {
    const QString root = canonicalOrAbsoluteRoot(rootPath);
    QString currentPath = root;
    const QStringList segments =
        QDir::fromNativeSeparators(relativePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& segment : segments) {
        const QFileInfo nextInfo(QDir(currentPath).filePath(segment));
        if (!nextInfo.exists()) {
            const QString projected = QDir::cleanPath(QDir(currentPath).filePath(segment));
            return projected == root || projected.startsWith(root + QLatin1Char('/'));
        }
        const QString canonical = nextInfo.canonicalFilePath();
        if (canonical.isEmpty()) {
            return false;
        }
        currentPath = QDir::cleanPath(canonical);
        if (currentPath != root && !currentPath.startsWith(root + QLatin1Char('/'))) {
            return false;
        }
    }
    return currentPath == root || currentPath.startsWith(root + QLatin1Char('/'));
}

bool validateRelativePath(const QString& outputRoot,
                          const QString& relativePath,
                          const QString& pathLocation,
                          ipcraft::DiagnosticStore& diagnostics) {
    const QString raw = slashPath(relativePath);
    const QString normalized = portablePath(relativePath);
    if (normalized.isEmpty()) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.write_failed"),
                              QStringLiteral("Emitter output path is required."),
                              pathLocation);
        return false;
    }
    if (QDir::isAbsolutePath(normalized) || isWindowsAbsolutePath(normalized)) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.path_absolute"),
                              QStringLiteral("Emitter output path must be relative."),
                              pathLocation);
        return false;
    }
    if (hasTraversalSegment(raw) ||
        !existingAncestorsStayInsideRoot(outputRoot, normalized)) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.path_escape"),
                              QStringLiteral("Emitter output path must stay inside the output root."),
                              pathLocation);
        return false;
    }
    return true;
}

bool validatePackageLocalSourcePath(const QString& packageRoot,
                                    const QString& sourcePath,
                                    const QString& pathLocation,
                                    ipcraft::DiagnosticStore& diagnostics,
                                    QString* absoluteSourcePath) {
    const QString raw = slashPath(sourcePath);
    const QString normalized = portablePath(sourcePath);
    if (normalized.isEmpty()) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.source_missing"),
                              QStringLiteral("Emitter source path is required."),
                              pathLocation);
        return false;
    }
    if (QDir::isAbsolutePath(normalized) || isWindowsAbsolutePath(normalized)) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.path_absolute"),
                              QStringLiteral("Emitter source path must be package-relative."),
                              pathLocation);
        return false;
    }
    if (packageRoot.trimmed().isEmpty()) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.source_missing"),
                              QStringLiteral("Emitter package root is required for package-local source paths."),
                              pathLocation);
        return false;
    }
    const QString root = packageRoot;
    if (hasTraversalSegment(raw) || !existingAncestorsStayInsideRoot(root, normalized)) {
        addDocumentDiagnostic(diagnostics,
                              QStringLiteral("emitter.path_escape"),
                              QStringLiteral("Emitter source path must stay inside the package root."),
                              pathLocation);
        return false;
    }
    *absoluteSourcePath = QDir(root).filePath(normalized);
    return true;
}

QByteArray jsonBytes(const QJsonObject& object) {
    return ipcraft::toDeterministicJson(object, QJsonDocument::Indented);
}

QByteArray jsonValueBytes(const QJsonValue& value) {
    if (value.isObject()) {
        return jsonBytes(value.toObject());
    }
    if (value.isArray()) {
        QByteArray bytes = QJsonDocument(ipcraft::sortedJsonValue(value).toArray())
                               .toJson(QJsonDocument::Indented);
        while (!bytes.isEmpty() && (bytes.endsWith('\n') || bytes.endsWith('\r'))) {
            bytes.chop(1);
        }
        bytes.append('\n');
        return bytes;
    }
    return QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
}

bool writeBytes(const QString& outputRoot,
                const QString& relativePath,
                const QByteArray& bytes,
                const QString& pathLocation,
                ipcraft::DiagnosticStore& diagnostics) {
    const QString absolutePath = QDir(outputRoot).filePath(QDir::fromNativeSeparators(relativePath));
    const QFileInfo fileInfo(absolutePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        addFileDiagnostic(diagnostics,
                          QStringLiteral("emitter.write_failed"),
                          QStringLiteral("Could not create emitter output directory."),
                          pathLocation,
                          relativePath);
        return false;
    }

    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addFileDiagnostic(diagnostics,
                          QStringLiteral("emitter.write_failed"),
                          QStringLiteral("Could not open emitter output file."),
                          pathLocation,
                          relativePath);
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        addFileDiagnostic(diagnostics,
                          QStringLiteral("emitter.write_failed"),
                          QStringLiteral("Could not write emitter output file."),
                          pathLocation,
                          relativePath);
        return false;
    }
    return true;
}

QString sha256Hex(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

void appendFileRecord(ipcraft::EmittedInputsManifest& manifest,
                      const QString& id,
                      const QString& kind,
                      const QString& path,
                      const QJsonObject& source,
                      const QByteArray& bytes) {
    ipcraft::EmittedInputFile file;
    file.id = id;
    file.kind = kind;
    file.path = QDir::fromNativeSeparators(path);
    file.path = portablePath(file.path);
    file.source = source;
    file.size = bytes.size();
    file.sha256 = sha256Hex(bytes);
    manifest.files.append(file);
}

bool outputPathAlreadyUsed(const ipcraft::EmittedInputsManifest& manifest,
                           const QString& path) {
    const QString normalizedPath = portablePath(path);
    for (const ipcraft::EmittedInputFile& file : manifest.files) {
        if (file.path == normalizedPath) {
            return true;
        }
    }
    return false;
}

bool rejectDuplicateOutputPath(const ipcraft::EmittedInputsManifest& manifest,
                               const QString& path,
                               const QString& pathLocation,
                               ipcraft::DiagnosticStore& diagnostics) {
    if (!outputPathAlreadyUsed(manifest, path)) {
        return false;
    }
    addDocumentDiagnostic(diagnostics,
                          QStringLiteral("emitter.duplicate_output_path"),
                          QStringLiteral("Emitter output path is duplicated."),
                          pathLocation);
    return true;
}

QByteArray documentContentBytes(const QJsonObject& documentState) {
    const QJsonValue content = documentState.value(QStringLiteral("content"));
    if (content.isString()) {
        QByteArray bytes = content.toString().toUtf8();
        if (!bytes.endsWith('\n')) {
            bytes.append('\n');
        }
        return bytes;
    }
    if (!content.isUndefined()) {
        return jsonValueBytes(content);
    }
    return jsonBytes(documentState);
}

bool copyFile(const QString& outputRoot,
              const QString& sourcePath,
              const QString& destinationPath,
              const QString& pathLocation,
              ipcraft::DiagnosticStore& diagnostics,
              QByteArray* copiedBytes) {
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        addFileDiagnostic(diagnostics,
                          QStringLiteral("emitter.write_failed"),
                          QStringLiteral("Could not open emitter source file."),
                          pathLocation,
                          sourcePath);
        return false;
    }
    const QByteArray bytes = source.readAll();
    if (!writeBytes(outputRoot, destinationPath, bytes, pathLocation, diagnostics)) {
        return false;
    }
    *copiedBytes = bytes;
    return true;
}

} // namespace

namespace ipcraft {

QJsonObject EmittedInputFile::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("kind"), kind);
    object.insert(QStringLiteral("path"), path);
    object.insert(QStringLiteral("source"), sortedJsonObject(source));
    insertString(object, QStringLiteral("sha256"), sha256);
    if (size >= 0) {
        object.insert(QStringLiteral("size"), size);
    }
    return sortedJsonObject(object);
}

QJsonObject EmittedInputsManifest::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schemaids::emittedInputsV1);
    object.insert(QStringLiteral("project"), projectId);
    object.insert(QStringLiteral("instance"), instanceId);
    QJsonObject package;
    package.insert(QStringLiteral("id"), packageId);
    package.insert(QStringLiteral("version"), packageVersion);
    object.insert(QStringLiteral("package"), sortedJsonObject(package));
    insertString(object, QStringLiteral("run_id"), runId);

    QVector<EmittedInputFile> sortedFiles = files;
    std::sort(sortedFiles.begin(), sortedFiles.end(), [](const EmittedInputFile& left,
                                                         const EmittedInputFile& right) {
        if (left.kind != right.kind) {
            return left.kind < right.kind;
        }
        if (left.id != right.id) {
            return left.id < right.id;
        }
        return left.path < right.path;
    });
    QJsonArray fileArray;
    for (const EmittedInputFile& file : sortedFiles) {
        fileArray.append(file.toJson());
    }
    object.insert(QStringLiteral("files"), fileArray);
    object.insert(QStringLiteral("diagnostics"), diagnostics.toJson());
    return sortedJsonObject(object);
}

PackageInputBuildResult PackageInputBuilder::emitInputs(const PackageInputBuildRequest& request) {
    PackageInputBuildResult result;
    result.manifest.projectId = request.projectId;
    result.manifest.instanceId = request.instanceId;
    result.manifest.packageId = request.package.id;
    result.manifest.packageVersion = request.package.version;
    result.manifest.runId = request.runId;

    const QString outputRoot = request.outputRoot.isEmpty()
        ? QDir::currentPath()
        : request.outputRoot;
    if (!QDir().mkpath(outputRoot)) {
        addFileDiagnostic(result.manifest.diagnostics,
                          QStringLiteral("emitter.write_failed"),
                          QStringLiteral("Could not create emitter output root."),
                          QStringLiteral("$.output_root"),
                          outputRoot);
        result.ok = false;
        return result;
    }

    for (qsizetype index = 0; index < request.package.emitters.size(); ++index) {
        const QJsonValue emitterValue = request.package.emitters.at(index);
        const QString emitterPath = indexPath(QStringLiteral("$.emitters"), index);
        if (!emitterValue.isObject()) {
            addDocumentDiagnostic(result.manifest.diagnostics,
                                  QStringLiteral("emitter.write_failed"),
                                  QStringLiteral("Emitter declaration must be an object."),
                                  emitterPath);
            continue;
        }

        const QJsonObject emitter = emitterValue.toObject();
        const QString kind = emitterKind(emitter);
        const QString path = stringValue(emitter,
                                         {QStringLiteral("path"),
                                          QStringLiteral("output_path")});
        const QString normalizedPath = portablePath(path);
        const QString pathLocation = childPath(emitterPath, QStringLiteral("path"));
        const QString id = emitterId(emitter, index, {});

        if (kind == QStringLiteral("plugin_hook")) {
            addDocumentDiagnostic(result.manifest.diagnostics,
                                  QStringLiteral("emitter.plugin_unavailable"),
                                  QStringLiteral("Emitter plugin hooks are not available in this runtime."),
                                  emitterPath);
            continue;
        }

        if (!validateRelativePath(outputRoot, path, pathLocation, result.manifest.diagnostics)) {
            continue;
        }

        QByteArray bytes;
        QString fileKind;
        QJsonObject source;
        QString fileId = id;

        if (kind == QStringLiteral("emit_parameters")) {
            bytes = jsonBytes(request.config.parameters);
            fileKind = QStringLiteral("parameters");
            source.insert(QStringLiteral("parameters"), true);
        } else if (kind == QStringLiteral("emit_table")) {
            const QString tableId = stringValue(emitter, {QStringLiteral("table"),
                                                          QStringLiteral("table_id")});
            if (tableId.isEmpty() || !request.config.tables.contains(tableId)) {
                addDocumentDiagnostic(result.manifest.diagnostics,
                                      QStringLiteral("emitter.source_missing"),
                                      QStringLiteral("Emitter table source is missing."),
                                      emitterPath);
                continue;
            }
            fileId = emitterId(emitter, index, tableId);
            bytes = jsonValueBytes(request.config.tables.value(tableId));
            fileKind = QStringLiteral("table");
            source.insert(QStringLiteral("table"), tableId);
        } else if (kind == QStringLiteral("emit_config_document")) {
            const QString documentId = stringValue(emitter, {QStringLiteral("document"),
                                                             QStringLiteral("document_id")});
            if (documentId.isEmpty() || !request.config.documents.contains(documentId)) {
                addDocumentDiagnostic(result.manifest.diagnostics,
                                      QStringLiteral("emitter.source_missing"),
                                      QStringLiteral("Emitter config document source is missing."),
                                      emitterPath);
                continue;
            }
            fileId = emitterId(emitter, index, documentId);
            bytes = documentContentBytes(request.config.documents.value(documentId).toObject());
            fileKind = QStringLiteral("config_document");
            source.insert(QStringLiteral("document"), documentId);
            source.insert(QStringLiteral("path"), QStringLiteral("$"));
        } else if (kind == QStringLiteral("emit_composition")) {
            bytes = jsonBytes(request.composition.toJson());
            fileKind = QStringLiteral("composition");
            source.insert(QStringLiteral("composition"), true);
        } else if (kind == QStringLiteral("emit_graph_config")) {
            if (!request.graphConfig.has_value()) {
                addDocumentDiagnostic(result.manifest.diagnostics,
                                      QStringLiteral("emitter.source_missing"),
                                      QStringLiteral("Graph config was requested but is not available."),
                                      emitterPath);
                continue;
            }
            bytes = jsonBytes(request.graphConfig->toJson());
            fileKind = QStringLiteral("graph_config");
            source.insert(QStringLiteral("graph_config"), true);
        } else if (kind == QStringLiteral("template")) {
            if (emitter.value(QStringLiteral("content")).isString()) {
                bytes = emitter.value(QStringLiteral("content")).toString().toUtf8();
                if (!bytes.endsWith('\n')) {
                    bytes.append('\n');
                }
            } else {
                const QString templatePath = stringValue(emitter, {QStringLiteral("template"),
                                                                   QStringLiteral("template_path"),
                                                                   QStringLiteral("file")});
                QString absoluteTemplatePath;
                if (!validatePackageLocalSourcePath(request.packageRoot.isEmpty()
                                                        ? request.package.packageRootPath
                                                        : request.packageRoot,
                                                    templatePath,
                                                    childPath(emitterPath, QStringLiteral("template")),
                                                    result.manifest.diagnostics,
                                                    &absoluteTemplatePath)) {
                    continue;
                }
                QFile templateFile(absoluteTemplatePath);
                if (!templateFile.open(QIODevice::ReadOnly)) {
                    addFileDiagnostic(result.manifest.diagnostics,
                                      QStringLiteral("emitter.write_failed"),
                                      QStringLiteral("Could not open emitter template file."),
                                      childPath(emitterPath, QStringLiteral("template")),
                                      templatePath);
                    continue;
                }
                bytes = templateFile.readAll();
            }
            fileKind = QStringLiteral("template");
            source.insert(QStringLiteral("template"), true);
        } else if (kind == QStringLiteral("copy_file")) {
            const QString sourcePath = stringValue(emitter, {QStringLiteral("source_path"),
                                                             QStringLiteral("source"),
                                                             QStringLiteral("file")});
            QByteArray copied;
            QString absoluteSourcePath;
            if (!validatePackageLocalSourcePath(request.packageRoot.isEmpty()
                                                    ? request.package.packageRootPath
                                                    : request.packageRoot,
                                                sourcePath,
                                                childPath(emitterPath, QStringLiteral("source")),
                                                result.manifest.diagnostics,
                                                &absoluteSourcePath)) {
                continue;
            }
            if (rejectDuplicateOutputPath(result.manifest,
                                          normalizedPath,
                                          pathLocation,
                                          result.manifest.diagnostics)) {
                continue;
            }
            if (!copyFile(outputRoot, absoluteSourcePath, normalizedPath, pathLocation, result.manifest.diagnostics, &copied)) {
                continue;
            }
            bytes = copied;
            fileKind = QStringLiteral("copied_file");
            source.insert(QStringLiteral("file"), sourcePath);
            appendFileRecord(result.manifest, fileId, fileKind, normalizedPath, source, bytes);
            continue;
        } else {
            addDocumentDiagnostic(result.manifest.diagnostics,
                                  QStringLiteral("emitter.write_failed"),
                                  QStringLiteral("Emitter kind is not supported."),
                                  childPath(emitterPath, QStringLiteral("kind")));
            continue;
        }

        if (rejectDuplicateOutputPath(result.manifest,
                                      normalizedPath,
                                      pathLocation,
                                      result.manifest.diagnostics)) {
            continue;
        }

        if (!writeBytes(outputRoot, normalizedPath, bytes, pathLocation, result.manifest.diagnostics)) {
            continue;
        }
        appendFileRecord(result.manifest, fileId, fileKind, normalizedPath, source, bytes);
    }

    result.ok = result.manifest.diagnostics.records.isEmpty();
    return result;
}

} // namespace ipcraft
