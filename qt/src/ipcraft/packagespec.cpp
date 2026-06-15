#include "ipcraft/packagespec.h"

#include "ipcraft/schemaids.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

namespace {

constexpr auto kPackageFileName = "ipcraft.json";
constexpr qint64 kMaxPackageFileBytes = 16 * 1024 * 1024;

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
                               const ipcraft::DiagnosticLocation& location,
                               const QString& source = QStringLiteral("package.parser")) {
    ipcraft::Diagnostic record;
    record.severity = QStringLiteral("error");
    record.source = source;
    record.ruleId = ruleId;
    record.category = QStringLiteral("package");
    record.message = message;
    record.locations.append(location);
    return record;
}

void addDiagnostic(ipcraft::DiagnosticStore& store,
                   const QString& ruleId,
                   const QString& message,
                   const QString& path,
                   const QString& source = QStringLiteral("package.parser")) {
    store.records.append(diagnostic(ruleId, message, documentLocation(path), source));
}

void addFileDiagnostic(ipcraft::DiagnosticStore& store,
                       const QString& ruleId,
                       const QString& message,
                       const QString& filePath,
                       const QString& source = QStringLiteral("package.parser")) {
    store.records.append(diagnostic(ruleId, message, fileLocation(filePath), source));
}

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
}

QString optionalString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       ipcraft::DiagnosticStore& diagnostics,
                       bool requireNonEmpty = false) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return {};
    }
    if (!value.isString()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.type_mismatch"),
                      QStringLiteral("Field '%1' must be a string.").arg(key),
                      path);
        return {};
    }
    const QString stringValue = value.toString();
    if (requireNonEmpty && stringValue.trimmed().isEmpty()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.missing_required"),
                      QStringLiteral("Field '%1' is required.").arg(key),
                      path);
    }
    return stringValue;
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       ipcraft::DiagnosticStore& diagnostics) {
    if (!object.contains(key)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.missing_required"),
                      QStringLiteral("Field '%1' is required.").arg(key),
                      path);
        return {};
    }
    return optionalString(object, key, path, diagnostics, true);
}

bool optionalObject(const QJsonObject& object,
                    const QString& key,
                    const QString& path,
                    ipcraft::DiagnosticStore& diagnostics,
                    QJsonObject* output) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        *output = {};
        return false;
    }
    if (!value.isObject()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.type_mismatch"),
                      QStringLiteral("Field '%1' must be an object.").arg(key),
                      path);
        *output = {};
        return false;
    }
    *output = value.toObject();
    return true;
}

bool requiredObject(const QJsonObject& object,
                    const QString& key,
                    const QString& path,
                    ipcraft::DiagnosticStore& diagnostics,
                    QJsonObject* output) {
    if (!object.contains(key)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.missing_required"),
                      QStringLiteral("Field '%1' is required.").arg(key),
                      path);
        *output = {};
        return false;
    }
    return optionalObject(object, key, path, diagnostics, output);
}

bool optionalArray(const QJsonObject& object,
                   const QString& key,
                   const QString& path,
                   ipcraft::DiagnosticStore& diagnostics,
                   QJsonArray* output) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        *output = {};
        return false;
    }
    if (!value.isArray()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.type_mismatch"),
                      QStringLiteral("Field '%1' must be an array.").arg(key),
                      path);
        *output = {};
        return false;
    }
    *output = value.toArray();
    return true;
}

bool optionalBool(const QJsonObject& object,
                  const QString& key,
                  const QString& path,
                  ipcraft::DiagnosticStore& diagnostics,
                  bool defaultValue = false) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return defaultValue;
    }
    if (!value.isBool()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.type_mismatch"),
                      QStringLiteral("Field '%1' must be a boolean.").arg(key),
                      path);
        return defaultValue;
    }
    return value.toBool();
}

bool isHex(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c - 'A' + 10;
}

class DuplicateKeyScanner {
public:
    explicit DuplicateKeyScanner(const QByteArray& data)
        : m_data(data) {}

    bool scan(QString* errorMessage) {
        m_pos = 0;
        skipWhitespace();
        if (!parseValue(errorMessage)) {
            return false;
        }
        skipWhitespace();
        if (m_pos != m_data.size()) {
            *errorMessage = QStringLiteral("Invalid JSON after document end");
            return false;
        }
        return true;
    }

private:
    void skipWhitespace() {
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                return;
            }
            ++m_pos;
        }
    }

    bool parseValue(QString* errorMessage) {
        skipWhitespace();
        if (m_pos >= m_data.size()) {
            *errorMessage = QStringLiteral("Invalid JSON value");
            return false;
        }

        const char c = m_data.at(m_pos);
        if (c == '{') {
            return parseObject(errorMessage);
        }
        if (c == '[') {
            return parseArray(errorMessage);
        }
        if (c == '"') {
            QString ignored;
            return parseString(&ignored, errorMessage);
        }
        return skipPrimitive(errorMessage);
    }

    bool parseObject(QString* errorMessage) {
        ++m_pos;
        skipWhitespace();
        if (m_pos < m_data.size() && m_data.at(m_pos) == '}') {
            ++m_pos;
            return true;
        }

        QSet<QString> keys;
        while (m_pos < m_data.size()) {
            skipWhitespace();
            QString key;
            if (!parseString(&key, errorMessage)) {
                return false;
            }
            if (keys.contains(key)) {
                *errorMessage = QStringLiteral("Duplicate JSON key '%1'").arg(key);
                return false;
            }
            keys.insert(key);

            skipWhitespace();
            if (m_pos >= m_data.size() || m_data.at(m_pos) != ':') {
                *errorMessage = QStringLiteral("Invalid JSON object member");
                return false;
            }
            ++m_pos;

            if (!parseValue(errorMessage)) {
                return false;
            }

            skipWhitespace();
            if (m_pos < m_data.size() && m_data.at(m_pos) == ',') {
                ++m_pos;
                continue;
            }
            if (m_pos < m_data.size() && m_data.at(m_pos) == '}') {
                ++m_pos;
                return true;
            }
            *errorMessage = QStringLiteral("Invalid JSON object separator");
            return false;
        }

        *errorMessage = QStringLiteral("Unterminated JSON object");
        return false;
    }

    bool parseArray(QString* errorMessage) {
        ++m_pos;
        skipWhitespace();
        if (m_pos < m_data.size() && m_data.at(m_pos) == ']') {
            ++m_pos;
            return true;
        }

        while (m_pos < m_data.size()) {
            if (!parseValue(errorMessage)) {
                return false;
            }

            skipWhitespace();
            if (m_pos < m_data.size() && m_data.at(m_pos) == ',') {
                ++m_pos;
                continue;
            }
            if (m_pos < m_data.size() && m_data.at(m_pos) == ']') {
                ++m_pos;
                return true;
            }
            *errorMessage = QStringLiteral("Invalid JSON array separator");
            return false;
        }

        *errorMessage = QStringLiteral("Unterminated JSON array");
        return false;
    }

    bool parseString(QString* value, QString* errorMessage) {
        if (m_pos >= m_data.size() || m_data.at(m_pos) != '"') {
            *errorMessage = QStringLiteral("Invalid JSON string");
            return false;
        }

        QString parsed;
        ++m_pos;
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos++);
            if (c == '"') {
                *value = parsed;
                return true;
            }

            if (c == '\\') {
                if (m_pos >= m_data.size()) {
                    *errorMessage = QStringLiteral("Invalid JSON escape");
                    return false;
                }
                const char escaped = m_data.at(m_pos++);
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    parsed.append(QChar(escaped));
                    break;
                case 'b':
                    parsed.append(QChar('\b'));
                    break;
                case 'f':
                    parsed.append(QChar('\f'));
                    break;
                case 'n':
                    parsed.append(QChar('\n'));
                    break;
                case 'r':
                    parsed.append(QChar('\r'));
                    break;
                case 't':
                    parsed.append(QChar('\t'));
                    break;
                case 'u': {
                    if (m_pos + 4 > m_data.size()) {
                        *errorMessage = QStringLiteral("Invalid JSON unicode escape");
                        return false;
                    }
                    int value = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char hex = m_data.at(m_pos + i);
                        if (!isHex(hex)) {
                            *errorMessage = QStringLiteral("Invalid JSON unicode escape");
                            return false;
                        }
                        value = (value << 4) + hexValue(hex);
                    }
                    m_pos += 4;
                    parsed.append(QChar(value));
                    break;
                }
                default:
                    *errorMessage = QStringLiteral("Invalid JSON escape");
                    return false;
                }
            } else if (!appendUtf8(c, &parsed, errorMessage)) {
                return false;
            }
        }

        *errorMessage = QStringLiteral("Unterminated JSON string");
        return false;
    }

    bool appendUtf8(char leadByte, QString* parsed, QString* errorMessage) {
        const auto lead = static_cast<unsigned char>(leadByte);
        if (lead < 0x20) {
            *errorMessage = QStringLiteral("Invalid JSON control character");
            return false;
        }
        if (lead < 0x80) {
            parsed->append(QChar::fromLatin1(static_cast<char>(lead)));
            return true;
        }

        uint codePoint = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            unsigned char b1 = 0;
            if (!readContinuationByte(&b1, errorMessage)) {
                return false;
            }
            codePoint = ((lead & 0x1f) << 6) | (b1 & 0x3f);
        } else if (lead >= 0xe0 && lead <= 0xef) {
            unsigned char b1 = 0;
            unsigned char b2 = 0;
            if (!readContinuationByte(&b1, errorMessage) ||
                !readContinuationByte(&b2, errorMessage)) {
                return false;
            }
            if ((lead == 0xe0 && b1 < 0xa0) || (lead == 0xed && b1 >= 0xa0)) {
                *errorMessage = QStringLiteral("Invalid JSON UTF-8 string");
                return false;
            }
            codePoint = ((lead & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            unsigned char b1 = 0;
            unsigned char b2 = 0;
            unsigned char b3 = 0;
            if (!readContinuationByte(&b1, errorMessage) ||
                !readContinuationByte(&b2, errorMessage) ||
                !readContinuationByte(&b3, errorMessage)) {
                return false;
            }
            if ((lead == 0xf0 && b1 < 0x90) || (lead == 0xf4 && b1 > 0x8f)) {
                *errorMessage = QStringLiteral("Invalid JSON UTF-8 string");
                return false;
            }
            codePoint = ((lead & 0x07) << 18) |
                        ((b1 & 0x3f) << 12) |
                        ((b2 & 0x3f) << 6) |
                        (b3 & 0x3f);
        } else {
            *errorMessage = QStringLiteral("Invalid JSON UTF-8 string");
            return false;
        }

        const char32_t utf32[] = {static_cast<char32_t>(codePoint)};
        parsed->append(QString::fromUcs4(utf32, 1));
        return true;
    }

    bool readContinuationByte(unsigned char* byte, QString* errorMessage) {
        if (m_pos >= m_data.size()) {
            *errorMessage = QStringLiteral("Invalid JSON UTF-8 string");
            return false;
        }

        const auto continuation = static_cast<unsigned char>(m_data.at(m_pos++));
        if ((continuation & 0xc0) != 0x80) {
            *errorMessage = QStringLiteral("Invalid JSON UTF-8 string");
            return false;
        }

        *byte = continuation;
        return true;
    }

    bool skipPrimitive(QString* errorMessage) {
        const qsizetype start = m_pos;
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if (c == ',' || c == ']' || c == '}' ||
                c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                break;
            }
            ++m_pos;
        }

        if (m_pos == start) {
            *errorMessage = QStringLiteral("Invalid JSON value");
            return false;
        }
        return true;
    }

    const QByteArray& m_data;
    qsizetype m_pos = 0;
};

bool scanJsonKeys(const QByteArray& data, QString* message) {
    DuplicateKeyScanner scanner(data);
    return scanner.scan(message);
}

bool hasOnlyKeys(const QJsonObject& object,
                 const QSet<QString>& allowed,
                 const QString& path,
                 ipcraft::DiagnosticStore& diagnostics) {
    bool ok = true;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (allowed.contains(it.key())) {
            continue;
        }
        addDiagnostic(diagnostics,
                      QStringLiteral("package.unknown_field"),
                      QStringLiteral("Unknown package field '%1'.").arg(it.key()),
                      path + QStringLiteral(".") + it.key());
        ok = false;
    }
    return ok;
}

void validateArrayItemsAreObjects(const QJsonArray& array,
                                  const QString& path,
                                  ipcraft::DiagnosticStore& diagnostics) {
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (array.at(index).isObject()) {
            continue;
        }
        addDiagnostic(diagnostics,
                      QStringLiteral("package.type_mismatch"),
                      QStringLiteral("Array entries must be objects."),
                      QStringLiteral("%1[%2]").arg(path).arg(index));
    }
}

void normalizeFlowScopes(QJsonArray* flows) {
    for (qsizetype index = 0; index < flows->size(); ++index) {
        if (!flows->at(index).isObject()) {
            continue;
        }
        QJsonObject flow = flows->at(index).toObject();
        if (!flow.contains(QStringLiteral("scope"))) {
            flow.insert(QStringLiteral("scope"), QStringLiteral("instance"));
            (*flows)[index] = flow;
        }
    }
}

void validateFlows(const QJsonArray& flows,
                   ipcraft::DiagnosticStore& diagnostics) {
    const QSet<QString> allowedScopes{
        QStringLiteral("instance"),
        QStringLiteral("project")
    };
    for (qsizetype index = 0; index < flows.size(); ++index) {
        if (!flows.at(index).isObject()) {
            continue;
        }
        const QJsonObject flow = flows.at(index).toObject();
        const QString scopePath = QStringLiteral("$.flows[%1].scope").arg(index);
        const QJsonValue scopeValue = flow.value(QStringLiteral("scope"));
        if (!scopeValue.isString() || !allowedScopes.contains(scopeValue.toString())) {
            addDiagnostic(diagnostics,
                          QStringLiteral("package.invalid_flow"),
                          QStringLiteral("Flow scope must be 'instance' or 'project'."),
                          scopePath);
        }
    }
}

void validateNativeEditorMetadata(const QJsonObject& native,
                                  ipcraft::DiagnosticStore& diagnostics) {
    const QJsonObject ipcraftObject = native.value(QStringLiteral("ipcraft")).toObject();
    const QJsonObject editor = ipcraftObject.value(QStringLiteral("editor")).toObject();
    const QJsonObject instances = editor.value(QStringLiteral("instances")).toObject();
    const QJsonValue maxValue = instances.value(QStringLiteral("max"));
    if (maxValue.isUndefined()) {
        return;
    }

    bool valid = false;
    if (maxValue.isDouble()) {
        const double numericMax = maxValue.toDouble();
        const int integerMax = maxValue.toInt();
        valid = integerMax > 0 && numericMax == static_cast<double>(integerMax);
    }
    if (!valid) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.invalid_instance_policy"),
                      QStringLiteral("Editor instances.max must be a positive integer."),
                      QStringLiteral("$.native.ipcraft.editor.instances.max"));
    }
}

const QSet<QString>& knownExtensionIds() {
    static const QSet<QString> ids{
        QStringLiteral("ipcraft.config.params"),
        QStringLiteral("ipcraft.config.tables"),
        QStringLiteral("ipcraft.config.documents"),
        QStringLiteral("ipcraft.config.files"),
        QStringLiteral("ipcraft.interfaces"),
        QStringLiteral("ipcraft.composition"),
        QStringLiteral("ipcraft.layout"),
        QStringLiteral("ipcraft.emitters"),
        QStringLiteral("ipcraft.flows"),
        QStringLiteral("ipcraft.artifacts"),
        QStringLiteral("ipcraft.diagnostics"),
        QStringLiteral("ipcraft.views"),
        QStringLiteral("ipcraft.graph_config"),
        QStringLiteral("noc.v1")
    };
    return ids;
}

const QSet<QString>& knownRootKeys() {
    static const QSet<QString> keys{
        QStringLiteral("schema"),
        QStringLiteral("id"),
        QStringLiteral("version"),
        QStringLiteral("name"),
        QStringLiteral("display"),
        QStringLiteral("extensions"),
        QStringLiteral("config_schema"),
        QStringLiteral("interfaces"),
        QStringLiteral("connection_rules"),
        QStringLiteral("emitters"),
        QStringLiteral("flows"),
        QStringLiteral("artifacts"),
        QStringLiteral("diagnostics"),
        QStringLiteral("views"),
        QStringLiteral("plugin"),
        QStringLiteral("native_schema"),
        QStringLiteral("metadata"),
        QStringLiteral("native"),
        QStringLiteral("graph_config"),
    };
    return keys;
}

void collectUnknownRootSections(const QJsonObject& root, ipcraft::PackageSpec& spec) {
    const QSet<QString>& knownKeys = knownRootKeys();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!knownKeys.contains(it.key())) {
            spec.unknownSections.insert(it.key(), it.value());
        }
    }
}

void validateExtensionId(const QString& id,
                         const QString& path,
                         ipcraft::DiagnosticStore& diagnostics) {
    if (id.isEmpty()) {
        return;
    }
    if (knownExtensionIds().contains(id)) {
        return;
    }
    addDiagnostic(diagnostics,
                  QStringLiteral("package.unknown_extension"),
                  QStringLiteral("Package extension is not recognized by the V1 runtime."),
                  path);
}

void validateConfigSchema(const QJsonObject& configSchema,
                          ipcraft::DiagnosticStore& diagnostics) {
    hasOnlyKeys(configSchema,
                {QStringLiteral("parameters"),
                 QStringLiteral("tables"),
                 QStringLiteral("documents"),
                 QStringLiteral("files"),
                 QStringLiteral("metadata"),
                 QStringLiteral("native")},
                QStringLiteral("$.config_schema"),
                diagnostics);
    for (const QString& key : {QStringLiteral("parameters"),
                              QStringLiteral("tables"),
                              QStringLiteral("documents"),
                              QStringLiteral("files")}) {
        QJsonArray entries;
        if (optionalArray(configSchema,
                          key,
                          QStringLiteral("$.config_schema.%1").arg(key),
                          diagnostics,
                          &entries)) {
            validateArrayItemsAreObjects(entries,
                                         QStringLiteral("$.config_schema.%1").arg(key),
                                         diagnostics);
            if (key == QStringLiteral("tables")) {
                QSet<QString> seenTableIds;
                for (qsizetype index = 0; index < entries.size(); ++index) {
                    if (!entries.at(index).isObject()) {
                        continue;
                    }
                    const QString id = entries.at(index).toObject().value(QStringLiteral("id")).toString().trimmed();
                    if (id.isEmpty()) {
                        continue;
                    }
                    const QString idPath = QStringLiteral("$.config_schema.tables[%1].id").arg(index);
                    if (seenTableIds.contains(id)) {
                        addDiagnostic(diagnostics,
                                      QStringLiteral("package.duplicate_table"),
                                      QStringLiteral("Duplicate table id."),
                                      idPath);
                        addDiagnostic(diagnostics,
                                      QStringLiteral("package.duplicate_id"),
                                      QStringLiteral("Duplicate table id."),
                                      idPath);
                    }
                    seenTableIds.insert(id);
                }
            }
        }
    }
    QJsonObject ignored;
    optionalObject(configSchema,
                   QStringLiteral("metadata"),
                   QStringLiteral("$.config_schema.metadata"),
                   diagnostics,
                   &ignored);
    optionalObject(configSchema,
                   QStringLiteral("native"),
                   QStringLiteral("$.config_schema.native"),
                   diagnostics,
                   &ignored);
}

QString childPath(const QString& base, const QString& key) {
    return base + QStringLiteral(".") + key;
}

void validateGraphConfigProperties(const QJsonObject& graphConfig,
                                   const QString& path,
                                   ipcraft::DiagnosticStore& diagnostics) {
    hasOnlyKeys(graphConfig,
                {QStringLiteral("schema"),
                 QStringLiteral("objects"),
                 QStringLiteral("relationships"),
                 QStringLiteral("properties"),
                 QStringLiteral("native")},
                path,
                diagnostics);

    const QString schema = requiredString(graphConfig,
                                          QStringLiteral("schema"),
                                          childPath(path, QStringLiteral("schema")),
                                          diagnostics);
    if (!schema.isEmpty() && schema != ipcraft::schemaids::graphConfigV1) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.unsupported_schema"),
                      QStringLiteral("graph_config schema must be ipcraft.graph-config.v1."),
                      childPath(path, QStringLiteral("schema")));
    }

    QSet<QString> objectIds;
    QJsonArray objects;
    if (optionalArray(graphConfig,
                      QStringLiteral("objects"),
                      childPath(path, QStringLiteral("objects")),
                      diagnostics,
                      &objects)) {
        for (qsizetype index = 0; index < objects.size(); ++index) {
            const QString objectPath = QStringLiteral("%1.objects[%2]").arg(path).arg(index);
            if (!objects.at(index).isObject()) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.type_mismatch"),
                              QStringLiteral("Graph object entries must be objects."),
                              objectPath);
                continue;
            }
            const QJsonObject object = objects.at(index).toObject();
            hasOnlyKeys(object,
                        {QStringLiteral("id"),
                         QStringLiteral("type"),
                         QStringLiteral("properties")},
                        objectPath,
                        diagnostics);
            const QString id = requiredString(object,
                                              QStringLiteral("id"),
                                              childPath(objectPath, QStringLiteral("id")),
                                              diagnostics);
            requiredString(object,
                           QStringLiteral("type"),
                           childPath(objectPath, QStringLiteral("type")),
                           diagnostics);
            QJsonObject ignored;
            optionalObject(object,
                           QStringLiteral("properties"),
                           childPath(objectPath, QStringLiteral("properties")),
                           diagnostics,
                           &ignored);
            if (!id.isEmpty() && objectIds.contains(id)) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.duplicate_id"),
                              QStringLiteral("Duplicate graph object id '%1'.").arg(id),
                              childPath(objectPath, QStringLiteral("id")));
            }
            objectIds.insert(id);
        }
    } else if (!graphConfig.contains(QStringLiteral("objects"))) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.missing_required"),
                      QStringLiteral("Field 'objects' is required."),
                      childPath(path, QStringLiteral("objects")));
    }

    QJsonArray relationships;
    if (optionalArray(graphConfig,
                      QStringLiteral("relationships"),
                      childPath(path, QStringLiteral("relationships")),
                      diagnostics,
                      &relationships)) {
        QSet<QString> relationshipIds;
        for (qsizetype index = 0; index < relationships.size(); ++index) {
            const QString relationshipPath = QStringLiteral("%1.relationships[%2]").arg(path).arg(index);
            if (!relationships.at(index).isObject()) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.type_mismatch"),
                              QStringLiteral("Graph relationship entries must be objects."),
                              relationshipPath);
                continue;
            }
            const QJsonObject relationship = relationships.at(index).toObject();
            hasOnlyKeys(relationship,
                        {QStringLiteral("id"),
                         QStringLiteral("type"),
                         QStringLiteral("endpoints"),
                         QStringLiteral("properties")},
                        relationshipPath,
                        diagnostics);
            const QString id = requiredString(relationship,
                                              QStringLiteral("id"),
                                              childPath(relationshipPath, QStringLiteral("id")),
                                              diagnostics);
            requiredString(relationship,
                           QStringLiteral("type"),
                           childPath(relationshipPath, QStringLiteral("type")),
                           diagnostics);
            QJsonArray endpoints;
            if (optionalArray(relationship,
                              QStringLiteral("endpoints"),
                              childPath(relationshipPath, QStringLiteral("endpoints")),
                              diagnostics,
                              &endpoints)) {
                if (endpoints.size() < 2) {
                    addDiagnostic(diagnostics,
                                  QStringLiteral("package.invalid_value"),
                                  QStringLiteral("Graph relationship endpoints must contain at least two entries."),
                                  childPath(relationshipPath, QStringLiteral("endpoints")));
                }
                for (qsizetype endpointIndex = 0; endpointIndex < endpoints.size(); ++endpointIndex) {
                    const QString endpointPath =
                        QStringLiteral("%1.endpoints[%2]").arg(relationshipPath).arg(endpointIndex);
                    if (!endpoints.at(endpointIndex).isObject()) {
                        addDiagnostic(diagnostics,
                                      QStringLiteral("package.type_mismatch"),
                                      QStringLiteral("Graph relationship endpoint entries must be objects."),
                                      endpointPath);
                        continue;
                    }
                    const QJsonObject endpoint = endpoints.at(endpointIndex).toObject();
                    hasOnlyKeys(endpoint,
                                {QStringLiteral("object"),
                                 QStringLiteral("role"),
                                 QStringLiteral("properties")},
                                endpointPath,
                                diagnostics);
                    const QString objectId = requiredString(endpoint,
                                                            QStringLiteral("object"),
                                                            childPath(endpointPath, QStringLiteral("object")),
                                                            diagnostics);
                    if (!objectId.isEmpty() && !objectIds.contains(objectId)) {
                        addDiagnostic(diagnostics,
                                      QStringLiteral("package.invalid_value"),
                                      QStringLiteral("Graph relationship endpoint object must reference a declared graph object."),
                                      childPath(endpointPath, QStringLiteral("object")));
                    }
                    requiredString(endpoint,
                                   QStringLiteral("role"),
                                   childPath(endpointPath, QStringLiteral("role")),
                                   diagnostics);
                    QJsonObject ignored;
                    optionalObject(endpoint,
                                   QStringLiteral("properties"),
                                   childPath(endpointPath, QStringLiteral("properties")),
                                   diagnostics,
                                   &ignored);
                }
            } else if (!relationship.contains(QStringLiteral("endpoints"))) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.missing_required"),
                              QStringLiteral("Field 'endpoints' is required."),
                              childPath(relationshipPath, QStringLiteral("endpoints")));
            }
            QJsonObject ignored;
            optionalObject(relationship,
                           QStringLiteral("properties"),
                           childPath(relationshipPath, QStringLiteral("properties")),
                           diagnostics,
                           &ignored);
            if (!id.isEmpty() && relationshipIds.contains(id)) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.duplicate_id"),
                              QStringLiteral("Duplicate graph relationship id '%1'.").arg(id),
                              childPath(relationshipPath, QStringLiteral("id")));
            }
            relationshipIds.insert(id);
        }
    } else if (!graphConfig.contains(QStringLiteral("relationships"))) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.missing_required"),
                      QStringLiteral("Field 'relationships' is required."),
                      childPath(path, QStringLiteral("relationships")));
    }

    QJsonObject ignored;
    optionalObject(graphConfig,
                   QStringLiteral("properties"),
                   childPath(path, QStringLiteral("properties")),
                   diagnostics,
                   &ignored);
    optionalObject(graphConfig,
                   QStringLiteral("native"),
                   childPath(path, QStringLiteral("native")),
                   diagnostics,
                   &ignored);
}

QString rootCanonicalPath(const QString& packageRootPath) {
    const QFileInfo rootInfo(packageRootPath);
    const QString canonical = rootInfo.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return QDir::cleanPath(canonical);
    }
    return QDir::cleanPath(rootInfo.absoluteFilePath());
}

QString pathEscapeMessage() {
    return QStringLiteral("Package-local path must stay inside the package root.");
}

bool isWithinRoot(const QString& rootPath, const QString& candidatePath) {
    const QString cleanRoot = QDir::cleanPath(rootPath);
    const QString cleanCandidate = QDir::cleanPath(candidatePath);
    return cleanCandidate == cleanRoot ||
           cleanCandidate.startsWith(cleanRoot + QLatin1Char('/'));
}

bool validateResolvedPathInRoot(const QString& root,
                                const QString& candidatePath,
                                const QString& path,
                                ipcraft::DiagnosticStore& diagnostics) {
    if (!isWithinRoot(root, candidatePath)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.path_escape"),
                      pathEscapeMessage(),
                      path);
        return false;
    }
    return true;
}

void validatePackageRelativePath(const QString& packageRootPath,
                                 const QString& value,
                                 const QString& path,
                                 ipcraft::DiagnosticStore& diagnostics) {
    const QString normalized = QDir::fromNativeSeparators(value.trimmed());
    if (normalized.isEmpty()) {
        return;
    }
    if (QDir::isAbsolutePath(normalized)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.path_escape"),
                      QStringLiteral("Package-local path must be relative."),
                      path);
        return;
    }
    const QStringList segments = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral(".."))) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.path_escape"),
                      QStringLiteral("Package-local path must not contain '..'."),
                      path);
        return;
    }

    const QString root = rootCanonicalPath(packageRootPath);
    QString currentPath = root;
    for (qsizetype index = 0; index < segments.size(); ++index) {
        const QString nextPath = QDir(currentPath).filePath(segments.at(index));
        const QFileInfo nextInfo(nextPath);
        if (!nextInfo.exists()) {
            const QStringList remaining = segments.sliced(index);
            const QString projectedPath = QDir::cleanPath(QDir(currentPath).filePath(remaining.join(QLatin1Char('/'))));
            validateResolvedPathInRoot(root, projectedPath, path, diagnostics);
            return;
        }

        const QString canonical = nextInfo.canonicalFilePath();
        if (canonical.isEmpty() ||
            !validateResolvedPathInRoot(root, QDir::cleanPath(canonical), path, diagnostics)) {
            return;
        }
        currentPath = QDir::cleanPath(canonical);
    }

    if (currentPath.isEmpty() || !isWithinRoot(root, currentPath)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("package.path_escape"),
                      pathEscapeMessage(),
                      path);
    }
}

bool isPathKey(const QString& key) {
    const QString normalized = key.toLower();
    return normalized == QStringLiteral("path") ||
           normalized == QStringLiteral("file") ||
           normalized == QStringLiteral("executable") ||
           normalized == QStringLiteral("library") ||
           normalized == QStringLiteral("template") ||
           normalized == QStringLiteral("template_ref") ||
           normalized.endsWith(QStringLiteral("_path"));
}

void validatePathFields(const QString& packageRootPath,
                        const QJsonValue& value,
                        const QString& path,
                        ipcraft::DiagnosticStore& diagnostics) {
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            validatePathFields(packageRootPath,
                               array.at(index),
                               QStringLiteral("%1[%2]").arg(path).arg(index),
                               diagnostics);
        }
        return;
    }
    if (!value.isObject()) {
        return;
    }

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString currentPath = childPath(path, it.key());
        if (isPathKey(it.key()) && it.value().isString()) {
            validatePackageRelativePath(packageRootPath,
                                        it.value().toString(),
                                        currentPath,
                                        diagnostics);
        }
        validatePathFields(packageRootPath, it.value(), currentPath, diagnostics);
    }
}

void parseExtensions(const QJsonObject& root,
                     ipcraft::PackageSpec& spec,
                     ipcraft::DiagnosticStore& diagnostics) {
    const QJsonValue value = root.value(QStringLiteral("extensions"));
    if (value.isUndefined()) {
        return;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index) {
            const QJsonValue item = array.at(index);
            const QString itemPath = QStringLiteral("$.extensions[%1]").arg(index);
            if (item.isString()) {
                const QString id = item.toString().trimmed();
                if (id.isEmpty()) {
                    addDiagnostic(diagnostics,
                                  QStringLiteral("package.missing_required"),
                                  QStringLiteral("Extension declaration must be non-empty."),
                                  itemPath);
                    continue;
                }
                if (!id.isEmpty()) {
                    spec.extensions.append(id);
                    validateExtensionId(id, itemPath, diagnostics);
                }
                continue;
            }
            if (item.isObject()) {
                const QJsonObject extensionObject = item.toObject();
                hasOnlyKeys(extensionObject,
                            {QStringLiteral("id"),
                             QStringLiteral("version"),
                             QStringLiteral("metadata"),
                             QStringLiteral("native")},
                            itemPath,
                            diagnostics);
                const QString id = requiredString(extensionObject,
                                                  QStringLiteral("id"),
                                                  childPath(itemPath, QStringLiteral("id")),
                                                  diagnostics);
                optionalString(extensionObject,
                               QStringLiteral("version"),
                               childPath(itemPath, QStringLiteral("version")),
                               diagnostics);
                QJsonObject ignored;
                optionalObject(extensionObject,
                               QStringLiteral("metadata"),
                               childPath(itemPath, QStringLiteral("metadata")),
                               diagnostics,
                               &ignored);
                optionalObject(extensionObject,
                               QStringLiteral("native"),
                               childPath(itemPath, QStringLiteral("native")),
                               diagnostics,
                               &ignored);
                spec.extensions.append(id);
                validateExtensionId(id, childPath(itemPath, QStringLiteral("id")), diagnostics);
                continue;
            }
            addDiagnostic(diagnostics,
                          QStringLiteral("package.type_mismatch"),
                          QStringLiteral("Extension declaration must be a string or object."),
                          itemPath);
        }
        spec.extensions.removeDuplicates();
        return;
    }
    if (value.isObject()) {
        spec.extensionPayloads = value.toObject();
        for (auto it = spec.extensionPayloads.constBegin(); it != spec.extensionPayloads.constEnd(); ++it) {
            if (it.value().isObject()) {
                continue;
            }
            addDiagnostic(diagnostics,
                          QStringLiteral("package.type_mismatch"),
                          QStringLiteral("Extension payload values must be objects."),
                          childPath(QStringLiteral("$.extensions"), it.key()));
        }
        return;
    }
    addDiagnostic(diagnostics,
                  QStringLiteral("package.type_mismatch"),
                  QStringLiteral("extensions must be an array or object."),
                  QStringLiteral("$.extensions"));
}

void requireExtension(const ipcraft::PackageSpec& spec,
                      ipcraft::DiagnosticStore& diagnostics,
                      const QString& sectionPath,
                      const QString& extensionId) {
    if (spec.hasExtension(extensionId)) {
        return;
    }
    ipcraft::Diagnostic record;
    record.severity = QStringLiteral("error");
    record.source = QStringLiteral("package.parser");
    record.ruleId = QStringLiteral("package.extension_required");
    record.category = QStringLiteral("package");
    record.message = QStringLiteral("Section '%1' requires extension '%2'.")
                         .arg(sectionPath.mid(2), extensionId);
    record.locations.append(documentLocation(sectionPath));
    diagnostics.records.append(record);
}

void enforceExtensionRules(const QJsonObject& root,
                           const ipcraft::PackageSpec& spec,
                           ipcraft::DiagnosticStore& diagnostics) {
    const QJsonObject configSchema = root.value(QStringLiteral("config_schema")).toObject();
    if (configSchema.contains(QStringLiteral("parameters"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.config_schema.parameters"), QStringLiteral("ipcraft.config.params"));
    }
    if (configSchema.contains(QStringLiteral("tables"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.config_schema.tables"), QStringLiteral("ipcraft.config.tables"));
    }
    if (configSchema.contains(QStringLiteral("documents"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.config_schema.documents"), QStringLiteral("ipcraft.config.documents"));
    }
    if (configSchema.contains(QStringLiteral("files"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.config_schema.files"), QStringLiteral("ipcraft.config.files"));
    }
    if (root.contains(QStringLiteral("interfaces"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.interfaces"), QStringLiteral("ipcraft.interfaces"));
    }
    if (root.contains(QStringLiteral("connection_rules"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.connection_rules"), QStringLiteral("ipcraft.composition"));
    }
    if (root.contains(QStringLiteral("emitters"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.emitters"), QStringLiteral("ipcraft.emitters"));
    }
    if (root.contains(QStringLiteral("flows"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.flows"), QStringLiteral("ipcraft.flows"));
    }
    if (root.contains(QStringLiteral("artifacts"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.artifacts"), QStringLiteral("ipcraft.artifacts"));
    }
    if (root.contains(QStringLiteral("diagnostics"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.diagnostics"), QStringLiteral("ipcraft.diagnostics"));
    }
    if (root.contains(QStringLiteral("views"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.views"), QStringLiteral("ipcraft.views"));
    }
    if (root.contains(QStringLiteral("graph_config"))) {
        requireExtension(spec, diagnostics, QStringLiteral("$.graph_config"), QStringLiteral("ipcraft.graph_config"));
    }
}

QHash<QString, QString> stringMap(const QJsonObject& object,
                                  const QString& path,
                                  ipcraft::DiagnosticStore& diagnostics) {
    QHash<QString, QString> result;
    QHash<QString, QString> normalizedValues;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString() || it.value().toString().trimmed().isEmpty()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("package.type_mismatch"),
                          QStringLiteral("Alias values must be non-empty strings."),
                          childPath(path, it.key()));
            continue;
        }
        const QString value = it.value().toString();
        const QString normalizedKey = it.key().toLower();
        if (normalizedValues.contains(normalizedKey) &&
            normalizedValues.value(normalizedKey) != value) {
            addDiagnostic(diagnostics,
                          QStringLiteral("package.invalid_value"),
                          QStringLiteral("Alias keys that differ only by case must map to the same value."),
                          childPath(path, it.key()));
            continue;
        }
        normalizedValues.insert(normalizedKey, value);
        result.insert(it.key(), value);
    }
    return result;
}

ipcraft::PackageEndpointMatch endpointMatch(const QJsonObject& object,
                                            const QString& path,
                                            ipcraft::DiagnosticStore& diagnostics) {
    hasOnlyKeys(object,
                {QStringLiteral("kind"),
                 QStringLiteral("protocol"),
                 QStringLiteral("role"),
                 QStringLiteral("direction")},
                path,
                diagnostics);
    ipcraft::PackageEndpointMatch match;
    match.kind = optionalString(object, QStringLiteral("kind"), childPath(path, QStringLiteral("kind")), diagnostics, true);
    match.protocol = optionalString(object, QStringLiteral("protocol"), childPath(path, QStringLiteral("protocol")), diagnostics, true);
    match.role = optionalString(object, QStringLiteral("role"), childPath(path, QStringLiteral("role")), diagnostics, true);
    match.direction = optionalString(object, QStringLiteral("direction"), childPath(path, QStringLiteral("direction")), diagnostics, true);
    return match;
}

void parseConnectionRules(const QJsonObject& root,
                          ipcraft::PackageSpec& spec,
                          ipcraft::DiagnosticStore& diagnostics) {
    QJsonObject object;
    if (!optionalObject(root,
                        QStringLiteral("connection_rules"),
                        QStringLiteral("$.connection_rules"),
                        diagnostics,
                        &object)) {
        return;
    }
    hasOnlyKeys(object,
                {QStringLiteral("protocol_aliases"),
                 QStringLiteral("kind_aliases"),
                 QStringLiteral("compatibility"),
                 QStringLiteral("metadata"),
                 QStringLiteral("native")},
                QStringLiteral("$.connection_rules"),
                diagnostics);

    QJsonObject protocolAliases;
    if (optionalObject(object,
                       QStringLiteral("protocol_aliases"),
                       QStringLiteral("$.connection_rules.protocol_aliases"),
                       diagnostics,
                       &protocolAliases)) {
        spec.connectionRules.protocolAliases = stringMap(protocolAliases,
                                                         QStringLiteral("$.connection_rules.protocol_aliases"),
                                                         diagnostics);
    }
    QJsonObject kindAliases;
    if (optionalObject(object,
                       QStringLiteral("kind_aliases"),
                       QStringLiteral("$.connection_rules.kind_aliases"),
                       diagnostics,
                       &kindAliases)) {
        spec.connectionRules.kindAliases = stringMap(kindAliases,
                                                     QStringLiteral("$.connection_rules.kind_aliases"),
                                                     diagnostics);
    }
    QJsonArray compatibility;
    if (optionalArray(object,
                      QStringLiteral("compatibility"),
                      QStringLiteral("$.connection_rules.compatibility"),
                      diagnostics,
                      &compatibility)) {
        for (qsizetype index = 0; index < compatibility.size(); ++index) {
            if (!compatibility.at(index).isObject()) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.type_mismatch"),
                              QStringLiteral("Compatibility entries must be objects."),
                              QStringLiteral("$.connection_rules.compatibility[]"));
                continue;
            }
            const QJsonObject ruleObject = compatibility.at(index).toObject();
            const QString rulePath = QStringLiteral("$.connection_rules.compatibility[%1]").arg(index);
            hasOnlyKeys(ruleObject,
                        {QStringLiteral("connection_type"),
                         QStringLiteral("from"),
                         QStringLiteral("to"),
                         QStringLiteral("arity"),
                         QStringLiteral("metadata")},
                        rulePath,
                        diagnostics);
            ipcraft::PackageCompatibilityRule rule;
            rule.connectionType = requiredString(ruleObject,
                                                 QStringLiteral("connection_type"),
                                                 childPath(rulePath, QStringLiteral("connection_type")),
                                                 diagnostics);
            QJsonObject fromObject;
            requiredObject(ruleObject,
                           QStringLiteral("from"),
                           childPath(rulePath, QStringLiteral("from")),
                           diagnostics,
                           &fromObject);
            QJsonObject toObject;
            requiredObject(ruleObject,
                           QStringLiteral("to"),
                           childPath(rulePath, QStringLiteral("to")),
                           diagnostics,
                           &toObject);
            rule.from = endpointMatch(fromObject, childPath(rulePath, QStringLiteral("from")), diagnostics);
            rule.to = endpointMatch(toObject, childPath(rulePath, QStringLiteral("to")), diagnostics);
            rule.arity = requiredString(ruleObject,
                                        QStringLiteral("arity"),
                                        childPath(rulePath, QStringLiteral("arity")),
                                        diagnostics);
            if (!rule.arity.isEmpty() &&
                rule.arity != QStringLiteral("binary") &&
                rule.arity != QStringLiteral("fanout")) {
                addDiagnostic(diagnostics,
                              QStringLiteral("package.invalid_value"),
                              QStringLiteral("Compatibility arity must be 'binary' or 'fanout'."),
                              childPath(rulePath, QStringLiteral("arity")));
            }
            optionalObject(ruleObject,
                           QStringLiteral("metadata"),
                           childPath(rulePath, QStringLiteral("metadata")),
                           diagnostics,
                           &rule.metadata);
            spec.connectionRules.compatibility.append(rule);
        }
    }
    optionalObject(object,
                   QStringLiteral("metadata"),
                   QStringLiteral("$.connection_rules.metadata"),
                   diagnostics,
                   &spec.connectionRules.metadata);
    optionalObject(object,
                   QStringLiteral("native"),
                   QStringLiteral("$.connection_rules.native"),
                   diagnostics,
                   &spec.connectionRules.native);
}

void parseInterfaces(const QJsonObject& root,
                     ipcraft::PackageSpec& spec,
                     ipcraft::DiagnosticStore& diagnostics) {
    QJsonArray interfaces;
    if (!optionalArray(root,
                      QStringLiteral("interfaces"),
                      QStringLiteral("$.interfaces"),
                      diagnostics,
                      &interfaces)) {
        return;
    }
    QSet<QString> seen;
    for (const QJsonValue& value : interfaces) {
        if (!value.isObject()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("package.type_mismatch"),
                          QStringLiteral("Interface entries must be objects."),
                          QStringLiteral("$.interfaces[]"));
            continue;
        }
        const QJsonObject object = value.toObject();
        hasOnlyKeys(object,
                    {QStringLiteral("id"),
                     QStringLiteral("name"),
                     QStringLiteral("label"),
                     QStringLiteral("kind"),
                     QStringLiteral("protocol"),
                     QStringLiteral("role"),
                     QStringLiteral("direction"),
                     QStringLiteral("required"),
                     QStringLiteral("fanout"),
                     QStringLiteral("properties"),
                     QStringLiteral("metadata"),
                     QStringLiteral("native")},
                    QStringLiteral("$.interfaces[]"),
                    diagnostics);

        ipcraft::PackageInterfaceSpec interfaceSpec;
        interfaceSpec.id = requiredString(object, QStringLiteral("id"), QStringLiteral("$.interfaces[].id"), diagnostics);
        interfaceSpec.name = optionalString(object, QStringLiteral("name"), QStringLiteral("$.interfaces[].name"), diagnostics);
        interfaceSpec.label = optionalString(object, QStringLiteral("label"), QStringLiteral("$.interfaces[].label"), diagnostics);
        interfaceSpec.kind = optionalString(object, QStringLiteral("kind"), QStringLiteral("$.interfaces[].kind"), diagnostics, true);
        interfaceSpec.protocol = optionalString(object, QStringLiteral("protocol"), QStringLiteral("$.interfaces[].protocol"), diagnostics, true);
        interfaceSpec.role = optionalString(object, QStringLiteral("role"), QStringLiteral("$.interfaces[].role"), diagnostics, true);
        interfaceSpec.direction = optionalString(object, QStringLiteral("direction"), QStringLiteral("$.interfaces[].direction"), diagnostics, true);
        interfaceSpec.required = optionalBool(object, QStringLiteral("required"), QStringLiteral("$.interfaces[].required"), diagnostics);
        interfaceSpec.fanout = optionalString(object, QStringLiteral("fanout"), QStringLiteral("$.interfaces[].fanout"), diagnostics, true);
        optionalObject(object, QStringLiteral("properties"), QStringLiteral("$.interfaces[].properties"), diagnostics, &interfaceSpec.properties);
        optionalObject(object, QStringLiteral("metadata"), QStringLiteral("$.interfaces[].metadata"), diagnostics, &interfaceSpec.metadata);
        optionalObject(object, QStringLiteral("native"), QStringLiteral("$.interfaces[].native"), diagnostics, &interfaceSpec.native);
        if (!interfaceSpec.id.isEmpty() && seen.contains(interfaceSpec.id)) {
            addDiagnostic(diagnostics,
                          QStringLiteral("package.duplicate_id"),
                          QStringLiteral("Duplicate interface id '%1'.").arg(interfaceSpec.id),
                          QStringLiteral("$.interfaces"));
        }
        seen.insert(interfaceSpec.id);
        spec.interfaces.append(interfaceSpec);
    }
}

bool isDirectPackageRoot(const QString& rootPath) {
    return QFileInfo(QDir(rootPath).filePath(QString::fromLatin1(kPackageFileName))).isFile();
}

} // namespace

namespace ipcraft {

bool PackageSpec::hasExtension(const QString& extensionId) const {
    return extensions.contains(extensionId);
}

PackageSpecReadResult PackageSpecReader::readPackageRoot(const QString& packageRootPath) const {
    return readSpecFile(QDir(packageRootPath).filePath(QString::fromLatin1(kPackageFileName)));
}

PackageSpecReadResult PackageSpecReader::readSpecFile(const QString& specPath) const {
    PackageSpecReadResult result;
    const QFileInfo specInfo(specPath);
    result.spec.packageRootPath = specInfo.dir().absolutePath();

    if (!specInfo.isFile()) {
        addFileDiagnostic(result.diagnostics,
                          QStringLiteral("package.not_found"),
                          QStringLiteral("Missing ipcraft.json."),
                          specPath);
        return result;
    }
    if (specInfo.size() > kMaxPackageFileBytes) {
        addFileDiagnostic(result.diagnostics,
                          QStringLiteral("package.file_too_large"),
                          QStringLiteral("ipcraft.json is too large."),
                          specInfo.absoluteFilePath());
        return result;
    }

    QFile file(specInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        addFileDiagnostic(result.diagnostics,
                          QStringLiteral("package.read_failed"),
                          QStringLiteral("Could not open ipcraft.json."),
                          specInfo.absoluteFilePath());
        return result;
    }

    const QByteArray bytes = file.readAll();
    QString scanError;
    if (!scanJsonKeys(bytes, &scanError)) {
        addFileDiagnostic(result.diagnostics,
                          scanError.startsWith(QStringLiteral("Duplicate JSON key"))
                              ? QStringLiteral("package.duplicate_key")
                              : QStringLiteral("package.invalid_json"),
                          scanError,
                          specInfo.absoluteFilePath());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addFileDiagnostic(result.diagnostics,
                          QStringLiteral("package.invalid_json"),
                          QStringLiteral("Invalid ipcraft.json: %1").arg(parseError.errorString()),
                          specInfo.absoluteFilePath());
        return result;
    }
    if (!document.isObject()) {
        addFileDiagnostic(result.diagnostics,
                          QStringLiteral("package.invalid_json"),
                          QStringLiteral("Package spec root must be an object."),
                          specInfo.absoluteFilePath());
        return result;
    }

    const QJsonObject root = document.object();
    collectUnknownRootSections(root, result.spec);

    result.spec.schema = requiredString(root, QStringLiteral("schema"), QStringLiteral("$.schema"), result.diagnostics);
    if (!result.spec.schema.isEmpty() && result.spec.schema != schemaids::packageV1) {
        addDiagnostic(result.diagnostics,
                      QStringLiteral("package.unsupported_schema"),
                      QStringLiteral("Package schema must be ipcraft.package.v1."),
                      QStringLiteral("$.schema"));
    }
    result.spec.id = requiredString(root, QStringLiteral("id"), QStringLiteral("$.id"), result.diagnostics);
    result.spec.version = requiredString(root, QStringLiteral("version"), QStringLiteral("$.version"), result.diagnostics);
    result.spec.name = requiredString(root, QStringLiteral("name"), QStringLiteral("$.name"), result.diagnostics);

    optionalObject(root, QStringLiteral("display"), QStringLiteral("$.display"), result.diagnostics, &result.spec.display);
    parseExtensions(root, result.spec, result.diagnostics);
    if (optionalObject(root, QStringLiteral("config_schema"), QStringLiteral("$.config_schema"), result.diagnostics, &result.spec.configSchema)) {
        validateConfigSchema(result.spec.configSchema, result.diagnostics);
    }
    parseInterfaces(root, result.spec, result.diagnostics);
    parseConnectionRules(root, result.spec, result.diagnostics);
    if (optionalArray(root, QStringLiteral("emitters"), QStringLiteral("$.emitters"), result.diagnostics, &result.spec.emitters)) {
        validateArrayItemsAreObjects(result.spec.emitters, QStringLiteral("$.emitters"), result.diagnostics);
    }
    if (optionalArray(root, QStringLiteral("flows"), QStringLiteral("$.flows"), result.diagnostics, &result.spec.flows)) {
        validateArrayItemsAreObjects(result.spec.flows, QStringLiteral("$.flows"), result.diagnostics);
        normalizeFlowScopes(&result.spec.flows);
        validateFlows(result.spec.flows, result.diagnostics);
    }
    if (optionalArray(root, QStringLiteral("artifacts"), QStringLiteral("$.artifacts"), result.diagnostics, &result.spec.artifacts)) {
        validateArrayItemsAreObjects(result.spec.artifacts, QStringLiteral("$.artifacts"), result.diagnostics);
    }
    optionalObject(root, QStringLiteral("diagnostics"), QStringLiteral("$.diagnostics"), result.diagnostics, &result.spec.diagnostics);
    if (optionalArray(root, QStringLiteral("views"), QStringLiteral("$.views"), result.diagnostics, &result.spec.views)) {
        validateArrayItemsAreObjects(result.spec.views, QStringLiteral("$.views"), result.diagnostics);
    }
    if (optionalObject(root, QStringLiteral("graph_config"), QStringLiteral("$.graph_config"), result.diagnostics, &result.spec.graphConfig)) {
        validateGraphConfigProperties(result.spec.graphConfig, QStringLiteral("$.graph_config"), result.diagnostics);
    }
    optionalObject(root, QStringLiteral("native_schema"), QStringLiteral("$.native_schema"), result.diagnostics, &result.spec.nativeSchema);
    optionalObject(root, QStringLiteral("metadata"), QStringLiteral("$.metadata"), result.diagnostics, &result.spec.metadata);
    if (optionalObject(root, QStringLiteral("native"), QStringLiteral("$.native"), result.diagnostics, &result.spec.native)) {
        validateNativeEditorMetadata(result.spec.native, result.diagnostics);
    }

    const QJsonValue pluginValue = root.value(QStringLiteral("plugin"));
    if (pluginValue.isObject()) {
        result.spec.hasPlugin = true;
        result.spec.plugin = pluginValue.toObject();
    } else if (!pluginValue.isUndefined() && !pluginValue.isNull()) {
        addDiagnostic(result.diagnostics,
                      QStringLiteral("package.type_mismatch"),
                      QStringLiteral("plugin must be an object or null."),
                      QStringLiteral("$.plugin"));
    }

    enforceExtensionRules(root, result.spec, result.diagnostics);
    validatePathFields(result.spec.packageRootPath, QJsonValue(root), QStringLiteral("$"), result.diagnostics);

    result.ok = result.diagnostics.records.isEmpty();
    return result;
}

PackageSpecCollectionResult
PackageSpecReader::discoverPackageRoots(const QStringList& rootPaths) const {
    PackageSpecCollectionResult result;
    QSet<QString> seenVersions;

    auto readAndAppend = [&](const QString& specFile) {
        PackageSpecReadResult readResult = readSpecFile(specFile);
        result.diagnostics.records += readResult.diagnostics.records;
        if (!readResult.ok) {
            return;
        }
        const QString key = readResult.spec.id + QLatin1Char('@') + readResult.spec.version;
        if (seenVersions.contains(key)) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("package.duplicate_version"),
                          QStringLiteral("Duplicate package version '%1'.").arg(key),
                          QStringLiteral("$"),
                          QStringLiteral("package.resolver"));
            return;
        }
        seenVersions.insert(key);
        result.packages.append(readResult.spec);
    };

    for (const QString& rootPath : rootPaths) {
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.isDir()) {
            addFileDiagnostic(result.diagnostics,
                              QStringLiteral("package.not_found"),
                              QStringLiteral("Package root does not exist."),
                              rootPath,
                              QStringLiteral("package.resolver"));
            continue;
        }
        if (isDirectPackageRoot(rootInfo.absoluteFilePath())) {
            readAndAppend(QDir(rootInfo.absoluteFilePath()).filePath(QString::fromLatin1(kPackageFileName)));
            continue;
        }
        const QDir collection(rootInfo.absoluteFilePath());
        const QFileInfoList entries = collection.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                               QDir::Name);
        for (const QFileInfo& entry : entries) {
            const QString specFile = QDir(entry.absoluteFilePath()).filePath(QString::fromLatin1(kPackageFileName));
            if (QFileInfo(specFile).isFile()) {
                readAndAppend(specFile);
            }
        }
    }
    return result;
}

PackageSpecResolveResult resolvePackageSpec(const QVector<PackageSpec>& packages,
                                            const QString& packageId,
                                            const QString& version) {
    PackageSpecResolveResult result;
    if (packageId.trimmed().isEmpty()) {
        addDiagnostic(result.diagnostics,
                      QStringLiteral("package.not_found"),
                      QStringLiteral("Package id is required."),
                      QStringLiteral("$"),
                      QStringLiteral("package.resolver"));
        return result;
    }
    bool sawId = false;
    for (const PackageSpec& package : packages) {
        if (package.id != packageId) {
            continue;
        }
        sawId = true;
        if (package.version == version) {
            result.ok = true;
            result.spec = package;
            return result;
        }
    }
    addDiagnostic(result.diagnostics,
                  sawId ? QStringLiteral("package.version_not_found")
                        : QStringLiteral("package.not_found"),
                  sawId ? QStringLiteral("Package version was not found.")
                        : QStringLiteral("Package id was not found."),
                  QStringLiteral("$"),
                  QStringLiteral("package.resolver"));
    return result;
}

} // namespace ipcraft
