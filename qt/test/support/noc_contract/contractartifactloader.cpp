#include "contractartifactloader.h"
#include "strictjson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <stdexcept>

namespace {

[[noreturn]] void fail(const QString &relativePath, const QString &message) {
    throw std::runtime_error(
        QStringLiteral("%1: %2").arg(relativePath, message).toStdString());
}

QString artifactPath(const QString &relativePath) {
    if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)) {
        fail(relativePath, QStringLiteral("artifact path must be repository-relative"));
    }

    const QString cleanPath = QDir::cleanPath(relativePath);
    if (cleanPath == QStringLiteral("..") ||
        cleanPath.startsWith(QStringLiteral("../"))) {
        fail(relativePath, QStringLiteral("artifact path escapes repository root"));
    }

    const QString root = ContractArtifactLoader::repositoryRoot();
    if (root.isEmpty()) {
        fail(relativePath, QStringLiteral("repository root does not exist"));
    }

    const QFileInfo info(QDir(root).filePath(cleanPath));
    const QString canonicalPath = contract_artifact_detail::normalizedCanonicalPath(
        info.canonicalFilePath());
    if (canonicalPath.isEmpty() || !info.isFile()) {
        fail(relativePath, QStringLiteral("artifact is missing or not a regular file"));
    }
    if (!contract_artifact_detail::isCanonicalPathWithinRepository(root,
                                                                   canonicalPath)) {
        fail(relativePath, QStringLiteral("artifact resolves outside repository root"));
    }
    return canonicalPath;
}

QJsonDocument loadDocument(const QString &relativePath) {
    const QByteArray bytes = ContractArtifactLoader::loadBytes(relativePath);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        fail(relativePath,
             QStringLiteral("JSON parse error at offset %1: %2")
                 .arg(error.offset)
                 .arg(error.errorString()));
    }
    QString strictError;
    if (!noc_contract::validateStrictJson(bytes, &strictError)) {
        fail(relativePath, strictError);
    }
    return document;
}

} // namespace

namespace contract_artifact_detail {

QString normalizedCanonicalPath(const QString &path) {
    return QDir::fromNativeSeparators(path);
}

bool isCanonicalPathWithinRepository(const QString &repositoryRoot,
                                     const QString &candidatePath) {
    QString normalizedRoot = normalizedCanonicalPath(repositoryRoot);
    const QString normalizedCandidate = normalizedCanonicalPath(candidatePath);
    while (normalizedRoot.size() > 1 && normalizedRoot.endsWith(QLatin1Char('/'))) {
        normalizedRoot.chop(1);
    }
    return normalizedCandidate == normalizedRoot ||
           normalizedCandidate.startsWith(normalizedRoot + QLatin1Char('/'));
}

} // namespace contract_artifact_detail

QString ContractArtifactLoader::repositoryRoot() {
    return contract_artifact_detail::normalizedCanonicalPath(
        QDir(QString::fromUtf8(IPCRAFT_REPOSITORY_ROOT)).canonicalPath());
}

QByteArray ContractArtifactLoader::loadBytes(const QString &relativePath) {
    QFile file(artifactPath(relativePath));
    if (!file.open(QIODevice::ReadOnly)) {
        fail(relativePath, QStringLiteral("cannot open contract artifact"));
    }
    return file.readAll();
}

QJsonObject ContractArtifactLoader::loadObject(const QString &relativePath) {
    const auto document = loadDocument(relativePath);
    if (!document.isObject()) {
        fail(relativePath, QStringLiteral("expected a JSON object"));
    }
    return document.object();
}

QJsonArray ContractArtifactLoader::loadArray(const QString &relativePath) {
    const auto document = loadDocument(relativePath);
    if (!document.isArray()) {
        fail(relativePath, QStringLiteral("expected a JSON array"));
    }
    return document.array();
}
