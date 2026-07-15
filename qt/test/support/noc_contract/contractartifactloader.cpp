#include "contractartifactloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

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

class DuplicateKeyScanner final {
public:
    explicit DuplicateKeyScanner(QByteArrayView bytes) : m_bytes(bytes) {}

    bool hasDuplicateKey() {
        skipWhitespace();
        scanValue();
        skipWhitespace();
        return m_duplicate;
    }

private:
    void skipWhitespace() {
        while (m_position < m_bytes.size()) {
            const char character = m_bytes[m_position];
            if (character != ' ' && character != '\t' && character != '\r' &&
                character != '\n') {
                return;
            }
            ++m_position;
        }
    }

    void scanValue() {
        skipWhitespace();
        if (m_position >= m_bytes.size()) {
            return;
        }
        switch (m_bytes[m_position]) {
        case '{':
            scanObject();
            return;
        case '[':
            scanArray();
            return;
        case '"':
            scanString();
            return;
        default:
            while (m_position < m_bytes.size() &&
                   QByteArrayView(",]} \t\r\n").indexOf(m_bytes[m_position]) < 0) {
                ++m_position;
            }
        }
    }

    QString scanString() {
        const qsizetype begin = m_position++;
        bool escaped = false;
        while (m_position < m_bytes.size()) {
            const char character = m_bytes[m_position++];
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                const QByteArray token(m_bytes.sliced(begin, m_position - begin));
                const auto parsed = QJsonDocument::fromJson("[" + token + "]");
                return parsed.array().first().toString();
            }
        }
        return {};
    }

    void scanObject() {
        ++m_position;
        QSet<QString> keys;
        skipWhitespace();
        while (m_position < m_bytes.size() && m_bytes[m_position] != '}') {
            const QString key = scanString();
            if (keys.contains(key)) {
                m_duplicate = true;
            }
            keys.insert(key);
            skipWhitespace();
            if (m_position < m_bytes.size() && m_bytes[m_position] == ':') {
                ++m_position;
            }
            scanValue();
            skipWhitespace();
            if (m_position < m_bytes.size() && m_bytes[m_position] == ',') {
                ++m_position;
                skipWhitespace();
            }
        }
        if (m_position < m_bytes.size()) {
            ++m_position;
        }
    }

    void scanArray() {
        ++m_position;
        skipWhitespace();
        while (m_position < m_bytes.size() && m_bytes[m_position] != ']') {
            scanValue();
            skipWhitespace();
            if (m_position < m_bytes.size() && m_bytes[m_position] == ',') {
                ++m_position;
                skipWhitespace();
            }
        }
        if (m_position < m_bytes.size()) {
            ++m_position;
        }
    }

    QByteArrayView m_bytes;
    qsizetype m_position = 0;
    bool m_duplicate = false;
};

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
    if (DuplicateKeyScanner(bytes).hasDuplicateKey()) {
        fail(relativePath, QStringLiteral("duplicate JSON object key"));
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
