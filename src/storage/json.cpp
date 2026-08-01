#include "storage/json.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace finepaper {
namespace {

void appendError(QVector<Diagnostic>& diagnostics,
                 const QString& code,
                 const QString& message,
                 const QString& path) {
    diagnostics.append(Diagnostic{
        QStringLiteral("error"),
        code,
        message,
        path,
        QStringLiteral("storage")
    });
}

QString stringValue(const QJsonObject& object,
                    const QString& key,
                    const QString& path,
                    QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_string"),
                    QStringLiteral("%1 must be a string").arg(key),
                    path + QLatin1Char('/') + key);
        return {};
    }
    return value.toString();
}

int integerValue(const QJsonObject& object,
                 const QString& key,
                 const QString& path,
                 QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_integer"),
                    QStringLiteral("%1 must be an integer").arg(key),
                    path + QLatin1Char('/') + key);
        return 0;
    }
    const int integer = value.toInt();
    if (value.toDouble() != static_cast<double>(integer)) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_integer"),
                    QStringLiteral("%1 must be an integer").arg(key),
                    path + QLatin1Char('/') + key);
    }
    return integer;
}

std::optional<RouterPosition> routerPositionFromJson(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    if (!value.isObject()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("router must be an object"),
                    path);
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    return RouterPosition{
        integerValue(object, QStringLiteral("x"), path, diagnostics),
        integerValue(object, QStringLiteral("y"), path, diagnostics)
    };
}

QJsonObject elementRefToJson(const ElementRef& reference) {
    return QJsonObject{
        {QStringLiteral("kind"), elementKindId(reference.kind)},
        {QStringLiteral("id"), reference.id}
    };
}

std::optional<ElementRef> elementRefFromJson(
    const QJsonValue& value,
    const QString& path,
    QVector<Diagnostic>& diagnostics) {
    if (!value.isObject()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("element reference must be an object"),
                    path);
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QString kindId = stringValue(
        object, QStringLiteral("kind"), path, diagnostics);
    const ElementKind kind = elementKindFromId(kindId);
    if (!kindId.isEmpty() && kind == ElementKind::Invalid) {
        appendError(diagnostics,
                    QStringLiteral("json.invalid_element_kind"),
                    QStringLiteral("unsupported element kind %1").arg(kindId),
                    path + QStringLiteral("/kind"));
    }
    return ElementRef{
        kind,
        stringValue(object, QStringLiteral("id"), path, diagnostics)
    };
}

QJsonObject requiredObject(const QJsonObject& object,
                           const QString& key,
                           const QString& path,
                           QVector<Diagnostic>& diagnostics) {
    const QJsonValue value = object.value(key);
    if (!value.isObject()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("%1 must be an object").arg(key),
                    path + QLatin1Char('/') + key);
        return {};
    }
    return value.toObject();
}

std::optional<QJsonArray> optionalArray(const QJsonObject& object,
                                        const QString& key,
                                        QVector<Diagnostic>& diagnostics) {
    if (!object.contains(key)) {
        return std::nullopt;
    }
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        appendError(diagnostics,
                    QStringLiteral("json.expected_array"),
                    QStringLiteral("%1 must be an array").arg(key),
                    QLatin1Char('/') + key);
        return std::nullopt;
    }
    return value.toArray();
}

bool hasDomainData(const NocDesign& design) {
    return !design.domains.isEmpty()
        || !design.domainMemberships.isEmpty()
        || !design.domainRelations.isEmpty()
        || !design.crossingPolicies.isEmpty()
        || !design.edgeOverrides.isEmpty();
}

bool hasElementConfigurationData(const NocDesign& design) {
    return !design.elementConfigurations.isEmpty();
}

class CancellableJsonWriter final {
public:
    CancellableJsonWriter(
        QSaveFile& file,
        const ValidationCancellationCheck& cancellationRequested)
        : m_file(file),
          m_cancellationRequested(cancellationRequested) {}

    bool write(const QJsonValue& value) {
        if (cancelled()) {
            return false;
        }
        switch (value.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            return writeBytes(QByteArrayLiteral("null"));
        case QJsonValue::Bool:
            return writeBytes(
                value.toBool() ? QByteArrayLiteral("true")
                               : QByteArrayLiteral("false"));
        case QJsonValue::Double: {
            const double number = value.toDouble();
            return writeBytes(
                std::isfinite(number)
                    ? QByteArray::number(number, 'g', 17)
                    : QByteArrayLiteral("null"));
        }
        case QJsonValue::String:
            return writeString(value.toString());
        case QJsonValue::Array:
            return writeArray(value.toArray());
        case QJsonValue::Object:
            return writeObject(value.toObject());
        }
        return false;
    }

    [[nodiscard]] bool cancellationObserved() const {
        return m_cancellationObserved;
    }

    bool finish() {
        return !cancelled() && flushBuffer();
    }

private:
    bool cancelled() {
        if (m_cancellationRequested && m_cancellationRequested()) {
            m_cancellationObserved = true;
            return true;
        }
        return false;
    }

    bool writeBytes(const QByteArray& bytes) {
        if (cancelled()) {
            return false;
        }
        m_buffer += bytes;
        return m_buffer.size() < 64 * 1024 || flushBuffer();
    }

    bool flushBuffer() {
        if (m_buffer.isEmpty()) {
            return true;
        }
        const bool written = m_file.write(m_buffer) == m_buffer.size();
        m_buffer.clear();
        return written;
    }

    bool writeString(const QString& value) {
        if (!writeBytes(QByteArrayLiteral("\""))) {
            return false;
        }
        QString plainText;
        plainText.reserve(2048);
        const auto flushPlainText = [&]() {
            if (plainText.isEmpty()) {
                return true;
            }
            const bool written = writeBytes(plainText.toUtf8());
            plainText.clear();
            return written;
        };
        for (qsizetype index = 0; index < value.size(); ++index) {
            if ((index & 0xff) == 0 && cancelled()) {
                return false;
            }
            const char16_t character = value.at(index).unicode();
            QByteArray escape;
            switch (character) {
            case '"':
                escape = QByteArrayLiteral("\\\"");
                break;
            case '\\':
                escape = QByteArrayLiteral("\\\\");
                break;
            case '\b':
                escape = QByteArrayLiteral("\\b");
                break;
            case '\f':
                escape = QByteArrayLiteral("\\f");
                break;
            case '\n':
                escape = QByteArrayLiteral("\\n");
                break;
            case '\r':
                escape = QByteArrayLiteral("\\r");
                break;
            case '\t':
                escape = QByteArrayLiteral("\\t");
                break;
            default:
                if (character < 0x20) {
                    escape = QStringLiteral("\\u%1")
                                 .arg(static_cast<uint>(character),
                                      4, 16,
                                      QLatin1Char('0'))
                                 .toLatin1();
                }
                break;
            }
            if (!escape.isEmpty()) {
                if (!flushPlainText() || !writeBytes(escape)) {
                    return false;
                }
                continue;
            }
            plainText.append(character);
            if (QChar::isHighSurrogate(character)
                && index + 1 < value.size()
                && QChar::isLowSurrogate(value.at(index + 1).unicode())) {
                plainText.append(value.at(++index));
            }
            if (plainText.size() >= 2048 && !flushPlainText()) {
                return false;
            }
        }
        return flushPlainText() && writeBytes(QByteArrayLiteral("\""));
    }

    bool writeArray(const QJsonArray& array) {
        if (!writeBytes(QByteArrayLiteral("["))) {
            return false;
        }
        for (qsizetype index = 0; index < array.size(); ++index) {
            if (index > 0 && !writeBytes(QByteArrayLiteral(","))) {
                return false;
            }
            if (!write(array.at(index))) {
                return false;
            }
        }
        return writeBytes(QByteArrayLiteral("]"));
    }

    bool writeObject(const QJsonObject& object) {
        if (!writeBytes(QByteArrayLiteral("{"))) {
            return false;
        }
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        for (qsizetype index = 0; index < keys.size(); ++index) {
            if (index > 0 && !writeBytes(QByteArrayLiteral(","))) {
                return false;
            }
            const QString& key = keys.at(index);
            if (!writeString(key)
                || !writeBytes(QByteArrayLiteral(":"))
                || !write(object.value(key))) {
                return false;
            }
        }
        return writeBytes(QByteArrayLiteral("}"));
    }

    QSaveFile& m_file;
    const ValidationCancellationCheck& m_cancellationRequested;
    QByteArray m_buffer;
    bool m_cancellationObserved = false;
};

bool saveJsonObjectCancellable(
    const QString& path,
    const QJsonObject& object,
    QVector<Diagnostic>* diagnostics,
    const ValidationCancellationCheck& cancellationRequested) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.create_directory_failed"),
                        QStringLiteral("could not create output directory"),
                        info.absolutePath());
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not open file for writing"),
                        path);
        }
        return false;
    }
    CancellableJsonWriter writer(file, cancellationRequested);
    if (!writer.write(QJsonValue(object)) || !writer.finish()) {
        file.cancelWriting();
        if (!writer.cancellationObserved() && diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not write JSON file"),
                        path);
        }
        return false;
    }
    if (cancellationRequested && cancellationRequested()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not write JSON file"),
                        path);
        }
        return false;
    }
    return true;
}

} // namespace

JsonObjectLoadResult loadJsonObject(const QString& path) {
    JsonObjectLoadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        appendError(result.diagnostics,
                    QStringLiteral("json.read_failed"),
                    QStringLiteral("could not read %1").arg(path),
                    path);
        return result;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.parse_failed"),
                    error.errorString(),
                    path);
        return result;
    }
    result.success = true;
    result.object = document.object();
    return result;
}

bool saveJsonObject(const QString& path,
                    const QJsonObject& object,
                    QVector<Diagnostic>* diagnostics) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.create_directory_failed"),
                        QStringLiteral("could not create output directory"),
                        info.absolutePath());
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not open file for writing"),
                        path);
        }
        return false;
    }
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size() || !file.commit()) {
        if (diagnostics) {
            appendError(*diagnostics,
                        QStringLiteral("json.write_failed"),
                        QStringLiteral("could not write JSON file"),
                        path);
        }
        return false;
    }
    return true;
}

ElementConfigurationsParseResult parseElementConfigurations(
    const QJsonValue& value,
    const QString& basePath) {
    ElementConfigurationsParseResult result;
    if (!value.isArray()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_array"),
                    QStringLiteral("elementConfigurations must be an array"),
                    basePath);
        return result;
    }
    const QJsonArray configurations = value.toArray();
    result.configurations.reserve(configurations.size());
    for (qsizetype index = 0; index < configurations.size(); ++index) {
        const QString base = QStringLiteral("%1/%2").arg(basePath).arg(index);
        if (!configurations.at(index).isObject()) {
            appendError(result.diagnostics,
                        QStringLiteral("json.expected_object"),
                        QStringLiteral("Element configuration must be an object"),
                        base);
            continue;
        }
        const QJsonObject configurationObject =
            configurations.at(index).toObject();
        ElementConfiguration configuration;
        if (const auto element = elementRefFromJson(
                configurationObject.value(QStringLiteral("element")),
                base + QStringLiteral("/element"),
                result.diagnostics)) {
            configuration.element = *element;
        }
        configuration.propertySet = stringValue(
            configurationObject,
            QStringLiteral("propertySet"),
            base,
            result.diagnostics);
        configuration.properties = requiredObject(
            configurationObject,
            QStringLiteral("properties"),
            base,
            result.diagnostics);
        result.configurations.append(std::move(configuration));
    }
    result.success = !hasErrors(result.diagnostics);
    return result;
}

QJsonObject designToJson(
    const NocDesign& design,
    const ValidationCancellationCheck& cancellationRequested) {
    const auto cancelled = [&] {
        return cancellationRequested && cancellationRequested();
    };
    QJsonObject package = {
        {QStringLiteral("id"), design.package.id},
        {QStringLiteral("version"), design.package.version}
    };
    QJsonObject topology = {
        {QStringLiteral("type"), design.topology.type},
        {QStringLiteral("rows"), design.topology.rows},
        {QStringLiteral("columns"), design.topology.columns}
    };
    QJsonArray endpoints;
    for (const EndpointInstance& endpoint : design.endpoints) {
        if (cancelled()) {
            return {};
        }
        QJsonObject router = {
            {QStringLiteral("x"), endpoint.attachment.router.x},
            {QStringLiteral("y"), endpoint.attachment.router.y}
        };
        QJsonObject attachment = {{QStringLiteral("router"), router}};
        if (endpoint.attachment.slot && !endpoint.attachment.slot->isEmpty()) {
            attachment.insert(QStringLiteral("slot"), *endpoint.attachment.slot);
        }
        endpoints.append(QJsonObject{
            {QStringLiteral("id"), endpoint.id},
            {QStringLiteral("type"), endpoint.type},
            {QStringLiteral("attachment"), attachment},
            {QStringLiteral("parameters"), endpoint.parameters}
        });
    }

    QJsonObject object = {
        {QStringLiteral("format"), design.format},
        {QStringLiteral("formatVersion"), design.formatVersion},
        {QStringLiteral("id"), design.id},
        {QStringLiteral("name"), design.name},
        {QStringLiteral("package"), package},
        {QStringLiteral("topology"), topology},
        {QStringLiteral("parameters"), design.parameters},
        {QStringLiteral("endpoints"), endpoints}
    };
    if (formatVersionSupportsDomains(design.formatVersion)
        || hasDomainData(design)) {
        QJsonArray domains;
        for (const DomainDefinition& domain : design.domains) {
            if (cancelled()) {
                return {};
            }
            domains.append(QJsonObject{
                {QStringLiteral("id"), domain.id},
                {QStringLiteral("type"), domain.type},
                {QStringLiteral("name"), domain.name},
                {QStringLiteral("properties"), domain.properties}
            });
        }

        QJsonArray memberships;
        for (const DomainMembership& membership : design.domainMemberships) {
            if (cancelled()) {
                return {};
            }
            QJsonObject assignments;
            QStringList assignmentTypes = membership.assignments.keys();
            std::sort(assignmentTypes.begin(), assignmentTypes.end());
            for (const QString& assignmentType : std::as_const(assignmentTypes)) {
                if (cancelled()) {
                    return {};
                }
                QJsonArray domainIds;
                for (const QString& domainId
                     : membership.assignments.value(assignmentType)) {
                    if (cancelled()) {
                        return {};
                    }
                    domainIds.append(domainId);
                }
                assignments.insert(assignmentType, domainIds);
            }
            memberships.append(QJsonObject{
                {QStringLiteral("element"), elementRefToJson(membership.element)},
                {QStringLiteral("assignments"), assignments}
            });
        }

        QJsonArray relations;
        for (const DomainRelation& relation : design.domainRelations) {
            if (cancelled()) {
                return {};
            }
            relations.append(QJsonObject{
                {QStringLiteral("type"), relation.type},
                {QStringLiteral("from"), relation.from},
                {QStringLiteral("to"), relation.to},
                {QStringLiteral("properties"), relation.properties}
            });
        }

        QJsonArray crossingPolicies;
        for (const DomainCrossingPolicy& policy : design.crossingPolicies) {
            if (cancelled()) {
                return {};
            }
            crossingPolicies.append(QJsonObject{
                {QStringLiteral("id"), policy.id},
                {QStringLiteral("domainType"), policy.domainType},
                {QStringLiteral("from"), policy.from},
                {QStringLiteral("to"), policy.to},
                {QStringLiteral("properties"), policy.properties}
            });
        }

        QJsonArray edgeOverrides;
        for (const DomainEdgeOverride& edgeOverride : design.edgeOverrides) {
            if (cancelled()) {
                return {};
            }
            edgeOverrides.append(QJsonObject{
                {QStringLiteral("edge"), elementRefToJson(edgeOverride.edge)},
                {QStringLiteral("domainType"), edgeOverride.domainType},
                {QStringLiteral("policy"), edgeOverride.policy},
                {QStringLiteral("properties"), edgeOverride.properties}
            });
        }

        object.insert(QStringLiteral("domains"), domains);
        object.insert(QStringLiteral("domainMemberships"), memberships);
        object.insert(QStringLiteral("domainRelations"), relations);
        object.insert(QStringLiteral("crossingPolicies"), crossingPolicies);
        object.insert(QStringLiteral("edgeOverrides"), edgeOverrides);
    }
    if (formatVersionSupportsElementConfigurations(design.formatVersion)
        || hasElementConfigurationData(design)) {
        QJsonArray elementConfigurations;
        for (const ElementConfiguration& configuration
             : design.elementConfigurations) {
            if (cancelled()) {
                return {};
            }
            elementConfigurations.append(QJsonObject{
                {QStringLiteral("element"),
                 elementRefToJson(configuration.element)},
                {QStringLiteral("propertySet"), configuration.propertySet},
                {QStringLiteral("properties"), configuration.properties}
            });
        }
        object.insert(QStringLiteral("elementConfigurations"),
                      elementConfigurations);
    }
    if (!design.packageData.isEmpty()) {
        object.insert(QStringLiteral("packageData"), design.packageData);
    }
    return object;
}

DesignLoadResult designFromJson(const QJsonObject& object) {
    DesignLoadResult result;
    NocDesign design;
    design.format = stringValue(object,
                                QStringLiteral("format"),
                                QString(),
                                result.diagnostics);
    design.formatVersion = integerValue(object,
                                        QStringLiteral("formatVersion"),
                                        QString(),
                                        result.diagnostics);
    const QStringList v2Fields{
        QStringLiteral("domains"),
        QStringLiteral("domainMemberships"),
        QStringLiteral("domainRelations"),
        QStringLiteral("crossingPolicies"),
        QStringLiteral("edgeOverrides")
    };
    const QString elementConfigurationsField =
        QStringLiteral("elementConfigurations");
    const bool supportedVersion =
        design.formatVersion >= kMinimumDesignFormatVersion
        && design.formatVersion <= kMaximumDesignFormatVersion;
    if (supportedVersion
        && !formatVersionSupportsDomains(design.formatVersion)) {
        for (const QString& field : v2Fields) {
            if (object.contains(field)) {
                appendError(result.diagnostics,
                            QStringLiteral("design.domains_require_v2"),
                            QStringLiteral("%1 requires formatVersion 2").arg(field),
                            QLatin1Char('/') + field);
            }
        }
    }
    if (supportedVersion
        && formatVersionSupportsDomains(design.formatVersion)) {
        for (const QString& field : v2Fields) {
            if (!object.contains(field)) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_array"),
                            QStringLiteral(
                                "%1 must be present as an array in formatVersion %2")
                                .arg(field)
                                .arg(design.formatVersion),
                            QLatin1Char('/') + field);
            }
        }
    }
    if (supportedVersion
        && !formatVersionSupportsElementConfigurations(design.formatVersion)
        && object.contains(elementConfigurationsField)) {
        appendError(
            result.diagnostics,
            QStringLiteral("design.element_configurations_require_v3"),
            QStringLiteral("elementConfigurations requires formatVersion 3"),
            QStringLiteral("/elementConfigurations"));
    } else if (supportedVersion
               && formatVersionSupportsElementConfigurations(
                   design.formatVersion)
               && !object.contains(elementConfigurationsField)) {
        appendError(
            result.diagnostics,
            QStringLiteral("json.expected_array"),
            QStringLiteral(
                "elementConfigurations must be present as an array in formatVersion 3"),
            QStringLiteral("/elementConfigurations"));
    }
    design.id = stringValue(object, QStringLiteral("id"), QString(), result.diagnostics);
    design.name = stringValue(object, QStringLiteral("name"), QString(), result.diagnostics);

    const QJsonValue packageValue = object.value(QStringLiteral("package"));
    if (!packageValue.isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("package must be an object"),
                    QStringLiteral("/package"));
    } else {
        const QJsonObject package = packageValue.toObject();
        design.package.id = stringValue(package,
                                        QStringLiteral("id"),
                                        QStringLiteral("/package"),
                                        result.diagnostics);
        design.package.version = stringValue(package,
                                             QStringLiteral("version"),
                                             QStringLiteral("/package"),
                                             result.diagnostics);
    }

    const QJsonValue topologyValue = object.value(QStringLiteral("topology"));
    if (!topologyValue.isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("topology must be an object"),
                    QStringLiteral("/topology"));
    } else {
        const QJsonObject topology = topologyValue.toObject();
        design.topology.type = stringValue(topology,
                                           QStringLiteral("type"),
                                           QStringLiteral("/topology"),
                                           result.diagnostics);
        design.topology.rows = integerValue(topology,
                                            QStringLiteral("rows"),
                                            QStringLiteral("/topology"),
                                            result.diagnostics);
        design.topology.columns = integerValue(topology,
                                               QStringLiteral("columns"),
                                               QStringLiteral("/topology"),
                                               result.diagnostics);
    }

    if (!object.value(QStringLiteral("parameters")).isObject()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_object"),
                    QStringLiteral("parameters must be an object"),
                    QStringLiteral("/parameters"));
    } else {
        design.parameters = object.value(QStringLiteral("parameters")).toObject();
    }

    const QJsonValue endpointsValue = object.value(QStringLiteral("endpoints"));
    if (!endpointsValue.isArray()) {
        appendError(result.diagnostics,
                    QStringLiteral("json.expected_array"),
                    QStringLiteral("endpoints must be an array"),
                    QStringLiteral("/endpoints"));
    } else {
        const QJsonArray endpoints = endpointsValue.toArray();
        for (qsizetype index = 0; index < endpoints.size(); ++index) {
            const QString base = QStringLiteral("/endpoints/%1").arg(index);
            if (!endpoints.at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("endpoint must be an object"),
                            base);
                continue;
            }
            const QJsonObject object = endpoints.at(index).toObject();
            EndpointInstance endpoint;
            endpoint.id = stringValue(object,
                                      QStringLiteral("id"),
                                      base,
                                      result.diagnostics);
            endpoint.type = stringValue(object,
                                        QStringLiteral("type"),
                                        base,
                                        result.diagnostics);
            const QJsonValue attachmentValue = object.value(QStringLiteral("attachment"));
            if (!attachmentValue.isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("attachment must be an object"),
                            base + QStringLiteral("/attachment"));
            } else {
                const QJsonObject attachment = attachmentValue.toObject();
                const auto router = routerPositionFromJson(
                    attachment.value(QStringLiteral("router")),
                    base + QStringLiteral("/attachment/router"),
                    result.diagnostics);
                if (router) {
                    endpoint.attachment.router = *router;
                }
                if (attachment.contains(QStringLiteral("slot"))) {
                    if (!attachment.value(QStringLiteral("slot")).isString()) {
                        appendError(result.diagnostics,
                                    QStringLiteral("json.expected_string"),
                                    QStringLiteral("slot must be a string"),
                                    base + QStringLiteral("/attachment/slot"));
                    } else {
                        endpoint.attachment.slot = attachment.value(QStringLiteral("slot")).toString();
                    }
                }
            }
            if (!object.value(QStringLiteral("parameters")).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("parameters must be an object"),
                            base + QStringLiteral("/parameters"));
            } else {
                endpoint.parameters = object.value(QStringLiteral("parameters")).toObject();
            }
            design.endpoints.append(std::move(endpoint));
        }
    }

    if (const auto domains = optionalArray(
            object, QStringLiteral("domains"), result.diagnostics)) {
        for (qsizetype index = 0; index < domains->size(); ++index) {
            const QString base = QStringLiteral("/domains/%1").arg(index);
            if (!domains->at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("Domain definition must be an object"),
                            base);
                continue;
            }
            const QJsonObject domainObject = domains->at(index).toObject();
            design.domains.append(DomainDefinition{
                stringValue(domainObject,
                            QStringLiteral("id"),
                            base,
                            result.diagnostics),
                stringValue(domainObject,
                            QStringLiteral("type"),
                            base,
                            result.diagnostics),
                stringValue(domainObject,
                            QStringLiteral("name"),
                            base,
                            result.diagnostics),
                requiredObject(domainObject,
                               QStringLiteral("properties"),
                               base,
                               result.diagnostics)
            });
        }
    }

    if (const auto memberships = optionalArray(
            object, QStringLiteral("domainMemberships"), result.diagnostics)) {
        for (qsizetype index = 0; index < memberships->size(); ++index) {
            const QString base = QStringLiteral("/domainMemberships/%1").arg(index);
            if (!memberships->at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("Domain membership must be an object"),
                            base);
                continue;
            }
            const QJsonObject membershipObject = memberships->at(index).toObject();
            DomainMembership membership;
            if (const auto element = elementRefFromJson(
                    membershipObject.value(QStringLiteral("element")),
                    base + QStringLiteral("/element"),
                    result.diagnostics)) {
                membership.element = *element;
            }
            const QJsonObject assignments = requiredObject(
                membershipObject,
                QStringLiteral("assignments"),
                base,
                result.diagnostics);
            for (auto iterator = assignments.constBegin();
                 iterator != assignments.constEnd(); ++iterator) {
                const QString assignmentPath = base
                    + QStringLiteral("/assignments/") + iterator.key();
                if (!iterator.value().isArray()) {
                    appendError(result.diagnostics,
                                QStringLiteral("json.expected_array"),
                                QStringLiteral("Domain assignments must be arrays"),
                                assignmentPath);
                    continue;
                }
                QStringList domainIds;
                const QJsonArray values = iterator.value().toArray();
                domainIds.reserve(values.size());
                for (qsizetype assignmentIndex = 0;
                     assignmentIndex < values.size(); ++assignmentIndex) {
                    if (!values.at(assignmentIndex).isString()) {
                        appendError(result.diagnostics,
                                    QStringLiteral("json.expected_string"),
                                    QStringLiteral("assigned Domain id must be a string"),
                                    assignmentPath
                                        + QStringLiteral("/%1").arg(assignmentIndex));
                        continue;
                    }
                    domainIds.append(values.at(assignmentIndex).toString());
                }
                membership.assignments.insert(iterator.key(), std::move(domainIds));
            }
            design.domainMemberships.append(std::move(membership));
        }
    }

    if (const auto relations = optionalArray(
            object, QStringLiteral("domainRelations"), result.diagnostics)) {
        for (qsizetype index = 0; index < relations->size(); ++index) {
            const QString base = QStringLiteral("/domainRelations/%1").arg(index);
            if (!relations->at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("Domain relation must be an object"),
                            base);
                continue;
            }
            const QJsonObject relationObject = relations->at(index).toObject();
            design.domainRelations.append(DomainRelation{
                stringValue(relationObject,
                            QStringLiteral("type"),
                            base,
                            result.diagnostics),
                stringValue(relationObject,
                            QStringLiteral("from"),
                            base,
                            result.diagnostics),
                stringValue(relationObject,
                            QStringLiteral("to"),
                            base,
                            result.diagnostics),
                requiredObject(relationObject,
                               QStringLiteral("properties"),
                               base,
                               result.diagnostics)
            });
        }
    }

    if (const auto policies = optionalArray(
            object, QStringLiteral("crossingPolicies"), result.diagnostics)) {
        for (qsizetype index = 0; index < policies->size(); ++index) {
            const QString base = QStringLiteral("/crossingPolicies/%1").arg(index);
            if (!policies->at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("Domain crossing policy must be an object"),
                            base);
                continue;
            }
            const QJsonObject policyObject = policies->at(index).toObject();
            design.crossingPolicies.append(DomainCrossingPolicy{
                stringValue(policyObject,
                            QStringLiteral("id"),
                            base,
                            result.diagnostics),
                stringValue(policyObject,
                            QStringLiteral("domainType"),
                            base,
                            result.diagnostics),
                stringValue(policyObject,
                            QStringLiteral("from"),
                            base,
                            result.diagnostics),
                stringValue(policyObject,
                            QStringLiteral("to"),
                            base,
                            result.diagnostics),
                requiredObject(policyObject,
                               QStringLiteral("properties"),
                               base,
                               result.diagnostics)
            });
        }
    }

    if (const auto edgeOverrides = optionalArray(
            object, QStringLiteral("edgeOverrides"), result.diagnostics)) {
        for (qsizetype index = 0; index < edgeOverrides->size(); ++index) {
            const QString base = QStringLiteral("/edgeOverrides/%1").arg(index);
            if (!edgeOverrides->at(index).isObject()) {
                appendError(result.diagnostics,
                            QStringLiteral("json.expected_object"),
                            QStringLiteral("Domain edge override must be an object"),
                            base);
                continue;
            }
            const QJsonObject overrideObject = edgeOverrides->at(index).toObject();
            DomainEdgeOverride edgeOverride;
            if (const auto edge = elementRefFromJson(
                    overrideObject.value(QStringLiteral("edge")),
                    base + QStringLiteral("/edge"),
                    result.diagnostics)) {
                edgeOverride.edge = *edge;
            }
            edgeOverride.domainType = stringValue(
                overrideObject,
                QStringLiteral("domainType"),
                base,
                result.diagnostics);
            edgeOverride.policy = stringValue(
                overrideObject,
                QStringLiteral("policy"),
                base,
                result.diagnostics);
            edgeOverride.properties = requiredObject(
                overrideObject,
                QStringLiteral("properties"),
                base,
                result.diagnostics);
            design.edgeOverrides.append(std::move(edgeOverride));
        }
    }

    if (object.contains(QStringLiteral("elementConfigurations"))) {
        ElementConfigurationsParseResult parsed = parseElementConfigurations(
            object.value(QStringLiteral("elementConfigurations")));
        design.elementConfigurations = std::move(parsed.configurations);
        result.diagnostics += std::move(parsed.diagnostics);
    }

    if (object.contains(QStringLiteral("packageData"))) {
        if (!object.value(QStringLiteral("packageData")).isObject()) {
            appendError(result.diagnostics,
                        QStringLiteral("json.expected_object"),
                        QStringLiteral("packageData must be an object"),
                        QStringLiteral("/packageData"));
        } else {
            design.packageData = object.value(QStringLiteral("packageData")).toObject();
        }
    }

    result.diagnostics += validateDesignStructure(design);
    result.success = !hasErrors(result.diagnostics);
    result.design = std::move(design);
    return result;
}

DesignLoadResult loadDesign(const QString& path) {
    const JsonObjectLoadResult json = loadJsonObject(path);
    if (!json.success) {
        DesignLoadResult result;
        result.diagnostics = json.diagnostics;
        return result;
    }
    return designFromJson(json.object);
}

bool saveDesign(const QString& path,
                const NocDesign& design,
                QVector<Diagnostic>* diagnostics,
                const ValidationCancellationCheck& cancellationRequested) {
    const QJsonObject object = designToJson(
        design, cancellationRequested);
    if (cancellationRequested && cancellationRequested()) {
        return false;
    }
    return cancellationRequested
        ? saveJsonObjectCancellable(
              path, object, diagnostics, cancellationRequested)
        : saveJsonObject(path, object, diagnostics);
}

QJsonObject diagnosticToJson(const Diagnostic& diagnostic) {
    return QJsonObject{
        {QStringLiteral("severity"), diagnostic.severity},
        {QStringLiteral("code"), diagnostic.code},
        {QStringLiteral("message"), diagnostic.message},
        {QStringLiteral("path"), diagnostic.path},
        {QStringLiteral("source"), diagnostic.source}
    };
}

QJsonArray diagnosticsToJson(const QVector<Diagnostic>& diagnostics) {
    QJsonArray array;
    for (const Diagnostic& diagnostic : diagnostics) {
        array.append(diagnosticToJson(diagnostic));
    }
    return array;
}

} // namespace finepaper
