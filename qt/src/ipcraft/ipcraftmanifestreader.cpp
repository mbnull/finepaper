#include "ipcraft/ipcraftmanifestreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace {

constexpr auto kManifestFileName = "ipcraft.json";
constexpr auto kManifestSchema = "ipcraft.manifest.v1";

void addDiagnostic(QVector<IpcraftDiagnostic>& diagnostics,
                   const QString& packageRootPath,
                   const QString& path,
                   const QString& message) {
    IpcraftDiagnostic diagnostic;
    diagnostic.packageRootPath = packageRootPath;
    diagnostic.path = path;
    diagnostic.message = message;
    diagnostics.append(diagnostic);
}

QString resolvePath(const QString& rootPath, const QString& path) {
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    const QFileInfo info(trimmedPath);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }
    return QFileInfo(QDir(rootPath).filePath(trimmedPath)).absoluteFilePath();
}

QString fieldPath(const QString& context, const QString& key) {
    if (context.isEmpty()) {
        return key;
    }
    return context + QLatin1Char('.') + key;
}

bool isWithinPath(const QString& rootPath, const QString& candidatePath) {
    const QString normalizedRoot = QDir::cleanPath(rootPath);
    const QString normalizedCandidate = QDir::cleanPath(candidatePath);
    return normalizedCandidate == normalizedRoot ||
           normalizedCandidate.startsWith(normalizedRoot + QLatin1Char('/'));
}

QString resolvePackageRelativePath(const QString& packageRootPath,
                                   const QString& path,
                                   const QString& context,
                                   QVector<IpcraftDiagnostic>& diagnostics) {
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    const QString normalizedPath = QDir::fromNativeSeparators(trimmedPath);
    if (QDir::isAbsolutePath(normalizedPath)) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      context,
                      QStringLiteral("Path must be relative to the package root"));
        return {};
    }

    const QStringList segments = normalizedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral(".."))) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      context,
                      QStringLiteral("Path must not contain '..' traversal"));
        return {};
    }

    const QString rootPath = QDir::cleanPath(QDir(packageRootPath).absolutePath());
    const QString resolvedPath =
        QDir::cleanPath(QFileInfo(QDir(rootPath).filePath(QDir::cleanPath(normalizedPath)))
                            .absoluteFilePath());
    if (!isWithinPath(rootPath, resolvedPath)) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      context,
                      QStringLiteral("Path must stay inside the package root"));
        return {};
    }

    return resolvedPath;
}

bool hasJsonField(const QJsonObject& object, const QString& key) {
    return !object.value(key).isUndefined();
}

bool isAllowedFrameworkTool(const QString& tool) {
    return tool == QStringLiteral("ipcraft-generate");
}

QStringList stringArray(const QJsonObject& object,
                        const QString& key,
                        const QString& context,
                        const QString& packageRootPath,
                        QVector<IpcraftDiagnostic>& diagnostics) {
    QStringList strings;
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return strings;
    }

    if (!value.isArray()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Field '%1' must be an array").arg(key));
        return strings;
    }

    const QJsonArray array = value.toArray();
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QJsonValue item = array.at(i);
        if (!item.isString()) {
            addDiagnostic(diagnostics,
                          packageRootPath,
                          QStringLiteral("%1[%2]").arg(fieldPath(context, key)).arg(i),
                          QStringLiteral("Field '%1' entries must be strings").arg(key));
            continue;
        }
        strings.append(item.toString().trimmed());
    }
    return strings;
}

bool jsonBool(const QJsonObject& object,
              const QString& key,
              const QString& context,
              const QString& packageRootPath,
              QVector<IpcraftDiagnostic>& diagnostics,
              bool defaultValue = false) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return defaultValue;
    }
    if (!value.isBool()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Field '%1' must be a boolean").arg(key));
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
            } else {
                if (!appendUtf8(c, &parsed, errorMessage)) {
                    return false;
                }
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

bool hasDuplicateJsonKey(const QByteArray& data, QString* message) {
    DuplicateKeyScanner scanner(data);
    return !scanner.scan(message);
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       const QString& context,
                       const QString& packageRootPath,
                       QVector<IpcraftDiagnostic>& diagnostics) {
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Missing required field '%1'").arg(key));
        return {};
    }
    if (!jsonValue.isString()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Field '%1' must be a string").arg(key));
        return {};
    }

    const QString value = jsonValue.toString().trimmed();
    if (value.isEmpty()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Missing required field '%1'").arg(key));
    }
    return value;
}

QString optionalString(const QJsonObject& object,
                       const QString& key,
                       const QString& context,
                       const QString& packageRootPath,
                       QVector<IpcraftDiagnostic>& diagnostics,
                       const QString& defaultValue = {}) {
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined()) {
        return defaultValue;
    }
    if (!jsonValue.isString()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Field '%1' must be a string").arg(key));
        return defaultValue;
    }
    return jsonValue.toString().trimmed();
}

bool optionalObject(const QJsonObject& object,
                    const QString& key,
                    const QString& context,
                    const QString& packageRootPath,
                    QVector<IpcraftDiagnostic>& diagnostics,
                    QJsonObject* parsed) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return false;
    }
    if (!value.isObject()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Field '%1' must be an object").arg(key));
        return false;
    }
    *parsed = value.toObject();
    return true;
}

bool optionalArray(const QJsonObject& object,
                   const QString& key,
                   const QString& context,
                   const QString& packageRootPath,
                   QVector<IpcraftDiagnostic>& diagnostics,
                   QJsonArray* parsed) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return false;
    }
    if (!value.isArray()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      fieldPath(context, key),
                      QStringLiteral("Field '%1' must be an array").arg(key));
        return false;
    }
    *parsed = value.toArray();
    return true;
}

IpcraftDynamicPluginMetadata pluginFromJson(const QJsonObject& object,
                                            const QString& packageRootPath,
                                            QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftDynamicPluginMetadata plugin;
    plugin.id = optionalString(object,
                               QStringLiteral("id"),
                               QStringLiteral("plugin"),
                               packageRootPath,
                               diagnostics);
    plugin.libraryPath = requiredString(object,
                                        QStringLiteral("library"),
                                        QStringLiteral("plugin"),
                                        packageRootPath,
                                        diagnostics);
    plugin.resolvedLibraryPath = resolvePath(packageRootPath, plugin.libraryPath);
    plugin.entrypoint = requiredString(object,
                                       QStringLiteral("entry"),
                                       QStringLiteral("plugin"),
                                       packageRootPath,
                                       diagnostics);
    return plugin;
}

IpcraftConnectionClass connectionClassFromJson(const QJsonObject& object,
                                               const QString& packageRootPath,
                                               QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftConnectionClass descriptor;
    descriptor.id = requiredString(object,
                                   QStringLiteral("id"),
                                   QStringLiteral("connection_classes[]"),
                                   packageRootPath,
                                   diagnostics);
    const QString context = descriptor.id.isEmpty()
                                ? QStringLiteral("connection_classes[]")
                                : QStringLiteral("connection_classes.%1").arg(descriptor.id);
    descriptor.roles = stringArray(object,
                                   QStringLiteral("roles"),
                                   context,
                                   packageRootPath,
                                   diagnostics);
    descriptor.symmetric = jsonBool(object,
                                    QStringLiteral("symmetric"),
                                    context,
                                    packageRootPath,
                                    diagnostics);
    QJsonObject ipxact;
    if (optionalObject(object,
                       QStringLiteral("ipxact"),
                       context,
                       packageRootPath,
                       diagnostics,
                       &ipxact)) {
        descriptor.ipxact = ipxact;
    }
    if (descriptor.roles.isEmpty()) {
        addDiagnostic(diagnostics,
                      packageRootPath,
                      context,
                      QStringLiteral("Connection class must declare roles"));
    }
    return descriptor;
}

IpcraftInterfaceAcceptRule acceptRuleFromJson(const QJsonObject& object,
                                              const QString& packageRootPath,
                                              const QString& context,
                                              QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftInterfaceAcceptRule rule;
    rule.connectionClassId = requiredString(object,
                                            QStringLiteral("class"),
                                            context,
                                            packageRootPath,
                                            diagnostics);
    rule.role = requiredString(object,
                               QStringLiteral("role"),
                               context,
                               packageRootPath,
                               diagnostics);
    return rule;
}

IpcraftInterfaceDescriptor interfaceFromJson(const QJsonObject& object,
                                             const QString& packageRootPath,
                                             const QString& moduleId,
                                             QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftInterfaceDescriptor descriptor;
    const QString context = QStringLiteral("modules.%1.interfaces[]").arg(moduleId);
    descriptor.id = requiredString(object,
                                   QStringLiteral("id"),
                                   context,
                                   packageRootPath,
                                   diagnostics);
    descriptor.label = optionalString(object,
                                      QStringLiteral("label"),
                                      context,
                                      packageRootPath,
                                      diagnostics);
    descriptor.modes = stringArray(object,
                                   QStringLiteral("modes"),
                                   context,
                                   packageRootPath,
                                   diagnostics);
    descriptor.multiConnection = jsonBool(object,
                                          QStringLiteral("multi_connection"),
                                          context,
                                          packageRootPath,
                                          diagnostics);

    QJsonObject topology;
    if (optionalObject(object,
                       QStringLiteral("topology"),
                       context,
                       packageRootPath,
                       diagnostics,
                       &topology)) {
        const QString topologyContext =
            descriptor.id.isEmpty()
                ? fieldPath(context, QStringLiteral("topology"))
                : QStringLiteral("modules.%1.interfaces.%2.topology")
                      .arg(moduleId, descriptor.id);
        descriptor.topology.side = optionalString(topology,
                                                  QStringLiteral("side"),
                                                  topologyContext,
                                                  packageRootPath,
                                                  diagnostics);
        descriptor.topology.oppositeInterfaceId =
            optionalString(topology,
                           QStringLiteral("opposite"),
                           topologyContext,
                           packageRootPath,
                           diagnostics);
        descriptor.topology.role = optionalString(topology,
                                                  QStringLiteral("role"),
                                                  topologyContext,
                                                  packageRootPath,
                                                  diagnostics);
    }

    QJsonObject ipxact;
    if (optionalObject(object,
                       QStringLiteral("ipxact"),
                       context,
                       packageRootPath,
                       diagnostics,
                       &ipxact)) {
        descriptor.ipxact = ipxact;
        descriptor.ipxactBusInterface = optionalString(ipxact,
                                                       QStringLiteral("bus_interface"),
                                                       fieldPath(context, QStringLiteral("ipxact")),
                                                       packageRootPath,
                                                       diagnostics);
    }

    QJsonArray accepts;
    if (optionalArray(object,
                      QStringLiteral("accepts"),
                      context,
                      packageRootPath,
                      diagnostics,
                      &accepts)) {
        for (const QJsonValue& value : accepts) {
            if (!value.isObject()) {
                addDiagnostic(diagnostics,
                              packageRootPath,
                              QStringLiteral("modules.%1.interfaces.%2.accepts")
                                  .arg(moduleId, descriptor.id),
                              QStringLiteral("Interface accept rule must be an object"));
                continue;
            }
            descriptor.accepts.append(
                acceptRuleFromJson(value.toObject(),
                                   packageRootPath,
                                   QStringLiteral("modules.%1.interfaces.%2.accepts[]")
                                       .arg(moduleId, descriptor.id),
                                   diagnostics));
        }
    }

    return descriptor;
}

IpcraftModuleDescriptor moduleFromJson(const QJsonObject& object,
                                       const QString& packageRootPath,
                                       QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftModuleDescriptor descriptor;
    descriptor.id = requiredString(object,
                                   QStringLiteral("id"),
                                   QStringLiteral("modules[]"),
                                   packageRootPath,
                                   diagnostics);
    const QString context = descriptor.id.isEmpty()
                                ? QStringLiteral("modules[]")
                                : QStringLiteral("modules.%1").arg(descriptor.id);
    descriptor.name = optionalString(object,
                                     QStringLiteral("name"),
                                     context,
                                     packageRootPath,
                                     diagnostics,
                                     descriptor.id);
    descriptor.description = optionalString(object,
                                            QStringLiteral("description"),
                                            context,
                                            packageRootPath,
                                            diagnostics);
    descriptor.graphRole = optionalString(object,
                                          QStringLiteral("graph_role"),
                                          context,
                                          packageRootPath,
                                          diagnostics);

    QJsonObject display;
    if (optionalObject(object,
                       QStringLiteral("display"),
                       context,
                       packageRootPath,
                       diagnostics,
                       &display)) {
        const QString displayContext = fieldPath(context, QStringLiteral("display"));
        descriptor.displayLabelParameter =
            optionalString(display,
                           QStringLiteral("label_parameter"),
                           displayContext,
                           packageRootPath,
                           diagnostics);
        descriptor.shortLabelParameter =
            optionalString(display,
                           QStringLiteral("short_label_parameter"),
                           displayContext,
                           packageRootPath,
                           diagnostics);
    }

    QJsonObject attach;
    if (optionalObject(object,
                       QStringLiteral("attach"),
                       context,
                       packageRootPath,
                       diagnostics,
                       &attach)) {
        descriptor.attach = attach;
    }

    QJsonObject parameters;
    if (optionalObject(object,
                       QStringLiteral("parameters"),
                       context,
                       packageRootPath,
                       diagnostics,
                       &parameters)) {
        descriptor.parameters = parameters;
    }

    QSet<QString> seenInterfaceIds;
    QJsonArray interfaces;
    if (optionalArray(object,
                      QStringLiteral("interfaces"),
                      context,
                      packageRootPath,
                      diagnostics,
                      &interfaces)) {
        for (const QJsonValue& value : interfaces) {
            if (!value.isObject()) {
                addDiagnostic(diagnostics,
                              packageRootPath,
                              QStringLiteral("modules.%1.interfaces").arg(descriptor.id),
                              QStringLiteral("Interface descriptor must be an object"));
                continue;
            }

            IpcraftInterfaceDescriptor interfaceDescriptor =
                interfaceFromJson(value.toObject(), packageRootPath, descriptor.id, diagnostics);
            if (!interfaceDescriptor.id.isEmpty() && seenInterfaceIds.contains(interfaceDescriptor.id)) {
                addDiagnostic(diagnostics,
                              packageRootPath,
                              QStringLiteral("modules.%1.interfaces.%2")
                                  .arg(descriptor.id, interfaceDescriptor.id),
                              QStringLiteral("Duplicate interface id '%1'").arg(interfaceDescriptor.id));
            }
            seenInterfaceIds.insert(interfaceDescriptor.id);
            descriptor.interfaces.append(interfaceDescriptor);
        }
    }

    return descriptor;
}

IpcraftViewDescriptor viewFromJson(const QJsonObject& object,
                                   const QString& packageRootPath,
                                   QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftViewDescriptor descriptor;
    descriptor.moduleId = requiredString(object,
                                         QStringLiteral("module"),
                                         QStringLiteral("views[]"),
                                         packageRootPath,
                                         diagnostics);
    const QString context = descriptor.moduleId.isEmpty()
                                ? QStringLiteral("views[]")
                                : QStringLiteral("views.%1").arg(descriptor.moduleId);
    descriptor.filePath = requiredString(object,
                                         QStringLiteral("file"),
                                         context,
                                         packageRootPath,
                                         diagnostics);
    descriptor.resolvedFilePath =
        resolvePackageRelativePath(packageRootPath,
                                   descriptor.filePath,
                                   fieldPath(context, QStringLiteral("file")),
                                   diagnostics);

    const QJsonValue requiredShapeValue = object.value(QStringLiteral("required_shape"));
    if (!requiredShapeValue.isUndefined()) {
        if (!requiredShapeValue.isObject()) {
            addDiagnostic(diagnostics,
                          packageRootPath,
                          QStringLiteral("views.%1.required_shape").arg(descriptor.moduleId),
                          QStringLiteral("required_shape must be an object"));
        } else {
            const QSet<QString> allowedFields = {
                QStringLiteral("module"),
                QStringLiteral("interfaces"),
                QStringLiteral("anchors"),
                QStringLiteral("attachment_zones"),
                QStringLiteral("states"),
                QStringLiteral("geometry"),
                QStringLiteral("collapse")
            };
            const QJsonObject requiredShape = requiredShapeValue.toObject();
            for (auto it = requiredShape.constBegin(); it != requiredShape.constEnd(); ++it) {
                if (!allowedFields.contains(it.key())) {
                    addDiagnostic(diagnostics,
                                  packageRootPath,
                                  QStringLiteral("views.%1.required_shape.%2")
                                      .arg(descriptor.moduleId, it.key()),
                                  QStringLiteral("Unknown required_shape field '%1'").arg(it.key()));
                }
                descriptor.requiredShapeFields.append(it.key());
            }
        }
    }

    return descriptor;
}

IpcraftCommandDescriptor commandFromJson(const QString& commandName,
                                         const QJsonObject& object,
                                         const QString& packageRootPath,
                                         QVector<IpcraftDiagnostic>& diagnostics) {
    IpcraftCommandDescriptor descriptor;
    descriptor.name = commandName;
    const QString context = QStringLiteral("commands.%1").arg(commandName);
    const bool hasExecutable = hasJsonField(object, QStringLiteral("executable"));
    const bool hasFrameworkTool = hasJsonField(object, QStringLiteral("framework_tool"));
    if (hasExecutable == hasFrameworkTool) {
        const QString diagnosticPath = hasFrameworkTool
            ? fieldPath(context, QStringLiteral("framework_tool"))
            : context;
        addDiagnostic(diagnostics,
                      packageRootPath,
                      diagnosticPath,
                      QStringLiteral("Command must declare exactly one of 'executable' or 'framework_tool'"));
    }

    if (hasExecutable) {
        descriptor.executablePath = requiredString(object,
                                                   QStringLiteral("executable"),
                                                   context,
                                                   packageRootPath,
                                                   diagnostics);
        descriptor.resolvedExecutablePath =
            resolvePackageRelativePath(packageRootPath,
                                       descriptor.executablePath,
                                       fieldPath(context, QStringLiteral("executable")),
                                       diagnostics);
    }

    if (hasFrameworkTool) {
        descriptor.frameworkTool = requiredString(object,
                                                  QStringLiteral("framework_tool"),
                                                  context,
                                                  packageRootPath,
                                                  diagnostics);
        if (!descriptor.frameworkTool.isEmpty()
            && !isAllowedFrameworkTool(descriptor.frameworkTool)) {
            addDiagnostic(diagnostics,
                          packageRootPath,
                          fieldPath(context, QStringLiteral("framework_tool")),
                          QStringLiteral("Unknown framework_tool '%1'").arg(descriptor.frameworkTool));
        }
    }

    descriptor.inputSchema = requiredString(object,
                                            QStringLiteral("input_schema"),
                                            context,
                                            packageRootPath,
                                            diagnostics);
    descriptor.args = stringArray(object,
                                  QStringLiteral("args"),
                                  context,
                                  packageRootPath,
                                  diagnostics);
    return descriptor;
}

void parseExtensions(const QJsonObject& manifestObject,
                     IpcraftPackageManifest& manifest,
                     QVector<IpcraftDiagnostic>& diagnostics) {
    QJsonObject extensions;
    if (!optionalObject(manifestObject,
                        QStringLiteral("extensions"),
                        QString(),
                        manifest.packageRootPath,
                        diagnostics,
                        &extensions)) {
        return;
    }

    for (auto it = extensions.constBegin(); it != extensions.constEnd(); ++it) {
        if (!it.value().isObject()) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          QStringLiteral("extensions.%1").arg(it.key()),
                          QStringLiteral("Extension configuration must be an object"));
            continue;
        }

        IpcraftExtensionDescriptor descriptor;
        descriptor.id = it.key();
        descriptor.configuration = it.value().toObject();
        descriptor.enabled = jsonBool(descriptor.configuration,
                                      QStringLiteral("enabled"),
                                      QStringLiteral("extensions.%1").arg(it.key()),
                                      manifest.packageRootPath,
                                      diagnostics);
        manifest.extensions.insert(descriptor.id, descriptor);
    }
}

void parseTopologies(const QJsonObject& manifestObject,
                     IpcraftPackageManifest& manifest,
                     QVector<IpcraftDiagnostic>& diagnostics) {
    QJsonArray topologies;
    if (!optionalArray(manifestObject,
                       QStringLiteral("topologies"),
                       QString(),
                       manifest.packageRootPath,
                       diagnostics,
                       &topologies)) {
        return;
    }

    for (qsizetype i = 0; i < topologies.size(); ++i) {
        const QJsonValue value = topologies.at(i);
        if (!value.isObject()) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          QStringLiteral("topologies[%1]").arg(i),
                          QStringLiteral("Topology descriptor must be an object"));
            continue;
        }
        manifest.topologies.append(value.toObject());
    }
}

void parseGeneration(const QJsonObject& manifestObject,
                     IpcraftPackageManifest& manifest,
                     QVector<IpcraftDiagnostic>& diagnostics) {
    QJsonObject generation;
    if (!optionalObject(manifestObject,
                        QStringLiteral("generation"),
                        QString(),
                        manifest.packageRootPath,
                        diagnostics,
                        &generation)) {
        return;
    }

    manifest.generation.engine = optionalString(generation,
                                                QStringLiteral("engine"),
                                                QStringLiteral("generation"),
                                                manifest.packageRootPath,
                                                diagnostics);
    manifest.generation.metadata = generation;
    manifest.generation.metadata.remove(QStringLiteral("engine"));
}

void validateReferences(const IpcraftPackageManifest& manifest,
                        QVector<IpcraftDiagnostic>& diagnostics) {
    QSet<QString> classIds;
    for (const IpcraftConnectionClass& connectionClass : manifest.connectionClasses) {
        if (connectionClass.id.isEmpty()) {
            continue;
        }
        if (classIds.contains(connectionClass.id)) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          QStringLiteral("connection_classes.%1").arg(connectionClass.id),
                          QStringLiteral("Duplicate connection class id '%1'").arg(connectionClass.id));
        }
        classIds.insert(connectionClass.id);
    }

    QSet<QString> moduleIds;
    for (const IpcraftModuleDescriptor& module : manifest.modules) {
        if (module.id.isEmpty()) {
            continue;
        }
        if (moduleIds.contains(module.id)) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          QStringLiteral("modules.%1").arg(module.id),
                          QStringLiteral("Duplicate module id '%1'").arg(module.id));
        }
        moduleIds.insert(module.id);

        for (const IpcraftInterfaceDescriptor& interfaceDescriptor : module.interfaces) {
            for (const IpcraftInterfaceAcceptRule& rule : interfaceDescriptor.accepts) {
                const IpcraftConnectionClass* connectionClass =
                    manifest.connectionClass(rule.connectionClassId);
                if (connectionClass == nullptr) {
                    addDiagnostic(diagnostics,
                                  manifest.packageRootPath,
                                  QStringLiteral("modules.%1.interfaces.%2.accepts.%3")
                                      .arg(module.id,
                                           interfaceDescriptor.id,
                                           rule.connectionClassId),
                                  QStringLiteral("Interface references missing connection class '%1'")
                                      .arg(rule.connectionClassId));
                    continue;
                }
                if (!rule.role.isEmpty() && !connectionClass->roles.contains(rule.role)) {
                    addDiagnostic(diagnostics,
                                  manifest.packageRootPath,
                                  QStringLiteral("modules.%1.interfaces.%2.accepts.%3")
                                      .arg(module.id,
                                           interfaceDescriptor.id,
                                           rule.connectionClassId),
                                  QStringLiteral("Interface accept rule role '%1' is not declared by connection class '%2'")
                                      .arg(rule.role, rule.connectionClassId));
                }
            }
        }
    }

    for (const IpcraftViewDescriptor& view : manifest.views) {
        if (!moduleIds.contains(view.moduleId)) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          QStringLiteral("views.%1").arg(view.moduleId),
                          QStringLiteral("View references missing module '%1'").arg(view.moduleId));
        }
        if (view.resolvedFilePath.isEmpty() || !QFileInfo(view.resolvedFilePath).isFile()) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          QStringLiteral("views.%1").arg(view.moduleId),
                          QStringLiteral("View references missing file '%1'").arg(view.filePath));
        }
    }

    for (const QJsonObject& topology : manifest.topologies) {
        const QString topologyId = optionalString(topology,
                                                  QStringLiteral("id"),
                                                  QStringLiteral("topologies[]"),
                                                  manifest.packageRootPath,
                                                  diagnostics);
        const QString topologyContext = topologyId.isEmpty()
                                            ? QStringLiteral("topologies[]")
                                            : QStringLiteral("topologies.%1").arg(topologyId);
        const QString moduleId = optionalString(topology,
                                                QStringLiteral("module"),
                                                topologyContext,
                                                manifest.packageRootPath,
                                                diagnostics);
        if (!moduleId.isEmpty() && !moduleIds.contains(moduleId)) {
            addDiagnostic(diagnostics,
                          manifest.packageRootPath,
                          topologyContext,
                          QStringLiteral("Topology references missing module '%1'").arg(moduleId));
            continue;
        }

        QJsonObject ports;
        if (!optionalObject(topology,
                            QStringLiteral("ports"),
                            topologyContext,
                            manifest.packageRootPath,
                            diagnostics,
                            &ports)) {
            continue;
        }
        for (auto it = ports.constBegin(); it != ports.constEnd(); ++it) {
            if (!it.value().isString()) {
                addDiagnostic(diagnostics,
                              manifest.packageRootPath,
                              QStringLiteral("%1.ports.%2").arg(topologyContext, it.key()),
                              QStringLiteral("Topology port reference must be a string"));
                continue;
            }

            const QString interfaceId = it.value().toString().trimmed();
            if (moduleId.isEmpty() || interfaceId.isEmpty()) {
                continue;
            }
            if (manifest.interfaceDescriptor(moduleId, interfaceId) == nullptr) {
                addDiagnostic(diagnostics,
                              manifest.packageRootPath,
                              QStringLiteral("topologies.%1.ports.%2")
                                  .arg(topologyId, it.key()),
                              QStringLiteral("Topology references missing interface '%1' on module '%2'")
                                  .arg(interfaceId, moduleId));
            }
        }
    }
}

} // namespace

IpcraftManifestReadResult
IpcraftManifestReader::readPackage(const QString& packageRootPath) const {
    const QString manifestPath =
        QDir(packageRootPath).filePath(QString::fromLatin1(kManifestFileName));
    return readManifestFile(manifestPath);
}

IpcraftManifestReadResult
IpcraftManifestReader::readManifestFile(const QString& manifestPath) const {
    IpcraftManifestReadResult result;
    const QFileInfo manifestInfo(manifestPath);
    const QString packageRootPath = manifestInfo.dir().absolutePath();
    result.manifest.packageRootPath = packageRootPath;

    if (!manifestInfo.isFile()) {
        addDiagnostic(result.diagnostics,
                      packageRootPath,
                      manifestPath,
                      QStringLiteral("Missing ipcraft.json"));
        return result;
    }

    QFile file(manifestInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        addDiagnostic(result.diagnostics,
                      packageRootPath,
                      manifestInfo.absoluteFilePath(),
                      QStringLiteral("Could not open ipcraft.json"));
        return result;
    }

    const QByteArray bytes = file.readAll();
    QString duplicateKeyMessage;
    if (hasDuplicateJsonKey(bytes, &duplicateKeyMessage)) {
        addDiagnostic(result.diagnostics,
                      packageRootPath,
                      manifestInfo.absoluteFilePath(),
                      duplicateKeyMessage);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addDiagnostic(result.diagnostics,
                      packageRootPath,
                      manifestInfo.absoluteFilePath(),
                      QStringLiteral("Invalid ipcraft.json: %1").arg(parseError.errorString()));
        return result;
    }

    const QJsonObject object = document.object();
    result.manifest.schema = requiredString(object,
                                            QStringLiteral("schema"),
                                            QStringLiteral("manifest"),
                                            packageRootPath,
                                            result.diagnostics);
    if (!result.manifest.schema.isEmpty() &&
        result.manifest.schema != QString::fromLatin1(kManifestSchema)) {
        addDiagnostic(result.diagnostics,
                      packageRootPath,
                      QStringLiteral("schema"),
                      QStringLiteral("Manifest schema must be ipcraft.manifest.v1"));
    }

    result.manifest.id = requiredString(object,
                                        QStringLiteral("id"),
                                        QStringLiteral("manifest"),
                                        packageRootPath,
                                        result.diagnostics);
    result.manifest.name = requiredString(object,
                                          QStringLiteral("name"),
                                          QStringLiteral("manifest"),
                                          packageRootPath,
                                          result.diagnostics);
    result.manifest.version = requiredString(object,
                                             QStringLiteral("version"),
                                             QStringLiteral("manifest"),
                                             packageRootPath,
                                             result.diagnostics);

    QJsonObject parameters;
    if (optionalObject(object,
                       QStringLiteral("parameters"),
                       QString(),
                       packageRootPath,
                       result.diagnostics,
                       &parameters)) {
        result.manifest.parameters = parameters;
    }

    QJsonObject pluginObject;
    if (optionalObject(object,
                       QStringLiteral("plugin"),
                       QString(),
                       packageRootPath,
                       result.diagnostics,
                       &pluginObject)) {
        result.manifest.plugin = pluginFromJson(pluginObject,
                                                packageRootPath,
                                                result.diagnostics);
    }
    parseExtensions(object, result.manifest, result.diagnostics);
    parseGeneration(object, result.manifest, result.diagnostics);

    QJsonObject ipxactObject;
    if (optionalObject(object,
                       QStringLiteral("ipxact"),
                       QString(),
                       packageRootPath,
                       result.diagnostics,
                       &ipxactObject)) {
        IpcraftIpxactDescriptor ipxact;
        ipxact.rootPath = requiredString(ipxactObject,
                                         QStringLiteral("root"),
                                         QStringLiteral("ipxact"),
                                         packageRootPath,
                                         result.diagnostics);
        ipxact.resolvedRootPath = resolvePath(packageRootPath, ipxact.rootPath);
        ipxact.generated = jsonBool(ipxactObject,
                                    QStringLiteral("generated"),
                                    QStringLiteral("ipxact"),
                                    packageRootPath,
                                    result.diagnostics);
        result.manifest.ipxact = ipxact;
    }

    QJsonArray connectionClasses;
    if (optionalArray(object,
                      QStringLiteral("connection_classes"),
                      QString(),
                      packageRootPath,
                      result.diagnostics,
                      &connectionClasses)) {
        for (const QJsonValue& value : connectionClasses) {
            if (!value.isObject()) {
                addDiagnostic(result.diagnostics,
                              packageRootPath,
                              QStringLiteral("connection_classes"),
                              QStringLiteral("Connection class descriptor must be an object"));
                continue;
            }
            result.manifest.connectionClasses.append(
                connectionClassFromJson(value.toObject(), packageRootPath, result.diagnostics));
        }
    }

    QJsonArray modules;
    if (optionalArray(object,
                      QStringLiteral("modules"),
                      QString(),
                      packageRootPath,
                      result.diagnostics,
                      &modules)) {
        for (const QJsonValue& value : modules) {
            if (!value.isObject()) {
                addDiagnostic(result.diagnostics,
                              packageRootPath,
                              QStringLiteral("modules"),
                              QStringLiteral("Module descriptor must be an object"));
                continue;
            }
            result.manifest.modules.append(
                moduleFromJson(value.toObject(), packageRootPath, result.diagnostics));
        }
    }

    QJsonArray views;
    if (optionalArray(object,
                      QStringLiteral("views"),
                      QString(),
                      packageRootPath,
                      result.diagnostics,
                      &views)) {
        for (const QJsonValue& value : views) {
            if (!value.isObject()) {
                addDiagnostic(result.diagnostics,
                              packageRootPath,
                              QStringLiteral("views"),
                              QStringLiteral("View descriptor must be an object"));
                continue;
            }
            result.manifest.views.append(
                viewFromJson(value.toObject(), packageRootPath, result.diagnostics));
        }
    }

    QJsonObject commands;
    if (optionalObject(object,
                       QStringLiteral("commands"),
                       QString(),
                       packageRootPath,
                       result.diagnostics,
                       &commands)) {
        for (auto it = commands.constBegin(); it != commands.constEnd(); ++it) {
            if (!it.value().isObject()) {
                addDiagnostic(result.diagnostics,
                              packageRootPath,
                              QStringLiteral("commands.%1").arg(it.key()),
                              QStringLiteral("Command descriptor must be an object"));
                continue;
            }
            result.manifest.commands.insert(
                it.key(),
                commandFromJson(it.key(),
                                it.value().toObject(),
                                packageRootPath,
                                result.diagnostics));
        }
    }

    parseTopologies(object, result.manifest, result.diagnostics);
    validateReferences(result.manifest, result.diagnostics);

    result.ok = result.diagnostics.isEmpty();
    return result;
}
