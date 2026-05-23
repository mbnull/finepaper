#include "ipcraft/configschema.h"

#include "ipcraft/jsonhelpers.h"
#include "ipcraft/value.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace {

ipcraft::DiagnosticLocation documentLocation(const QString& path) {
    ipcraft::DiagnosticLocation location;
    location.kind = QStringLiteral("document_path");
    location.path = path;
    return location;
}

ipcraft::Diagnostic diagnostic(const QString& ruleId,
                               const QString& message,
                               const QString& path) {
    ipcraft::Diagnostic record;
    record.severity = QStringLiteral("error");
    record.source = QStringLiteral("core");
    record.ruleId = ruleId;
    record.category = QStringLiteral("config");
    record.message = message;
    record.locations.append(documentLocation(path));
    return record;
}

void addDiagnostic(ipcraft::DiagnosticStore& store,
                   const QString& ruleId,
                   const QString& message,
                   const QString& path) {
    store.records.append(diagnostic(ruleId, message, path));
}

void addUnsupportedExpression(ipcraft::DiagnosticStore& store, const QString& path) {
    addDiagnostic(store,
                  QStringLiteral("config.expression_unsupported"),
                  QStringLiteral("Expression form is not supported in V1"),
                  path);
}

QString childPath(const QString& base, const QString& key) {
    return base + QStringLiteral(".") + key;
}

QString indexPath(const QString& base, qsizetype index) {
    return QStringLiteral("%1[%2]").arg(base).arg(index);
}

bool isSafePathKey(const QString& key) {
    if (key.isEmpty()) {
        return false;
    }
    const auto first = key.front();
    if (!(first.isLetter() || first == QLatin1Char('_'))) {
        return false;
    }
    for (const auto ch : key) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('_'))) {
            return false;
        }
    }
    return true;
}

QString escapedPathKey(const QString& key) {
    QString escaped;
    escaped.reserve(key.size());
    for (const auto ch : key) {
        const uint code = ch.unicode();
        switch (code) {
        case '\\':
            escaped += QStringLiteral("\\\\");
            break;
        case '"':
            escaped += QStringLiteral("\\\"");
            break;
        case '\b':
            escaped += QStringLiteral("\\b");
            break;
        case '\f':
            escaped += QStringLiteral("\\f");
            break;
        case '\n':
            escaped += QStringLiteral("\\n");
            break;
        case '\r':
            escaped += QStringLiteral("\\r");
            break;
        case '\t':
            escaped += QStringLiteral("\\t");
            break;
        default:
            if (code < 0x20) {
                escaped += QStringLiteral("\\u%1")
                               .arg(code, 4, 16, QLatin1Char('0'));
            } else {
                escaped += ch;
            }
            break;
        }
    }
    return QStringLiteral("[\"%1\"]").arg(escaped);
}

QString keyPath(const QString& base, const QString& key) {
    if (isSafePathKey(key)) {
        return childPath(base, key);
    }
    return base + escapedPathKey(key);
}

bool hasExactKeys(const QJsonObject& object, std::initializer_list<QString> keys) {
    if (object.size() != static_cast<qsizetype>(keys.size())) {
        return false;
    }
    for (const QString& key : keys) {
        if (!object.contains(key)) {
            return false;
        }
    }
    return true;
}

bool isNonEmptyString(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
}

QString requiredString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       ipcraft::DiagnosticStore& diagnostics,
                       const QString& ruleId = QStringLiteral("config.required_missing")) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || !isNonEmptyString(value)) {
        addDiagnostic(diagnostics,
                      ruleId,
                      QStringLiteral("Field '%1' is required.").arg(key),
                      path);
        return {};
    }
    return value.toString().trimmed();
}

QString optionalString(const QJsonObject& object,
                       const QString& key,
                       const QString& path,
                       ipcraft::DiagnosticStore& diagnostics) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return {};
    }
    if (!value.isString()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("Field '%1' must be a string.").arg(key),
                      path);
        return {};
    }
    return value.toString();
}

bool optionalBool(const QJsonObject& object,
                  const QString& key,
                  const QString& path,
                  ipcraft::DiagnosticStore& diagnostics,
                  bool fallback = false) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        return fallback;
    }
    if (!value.isBool()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("Field '%1' must be a boolean.").arg(key),
                      path);
        return fallback;
    }
    return value.toBool();
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
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("Field '%1' must be an object.").arg(key),
                      path);
        *output = {};
        return false;
    }
    *output = value.toObject();
    return true;
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
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("Field '%1' must be an array.").arg(key),
                      path);
        *output = {};
        return false;
    }
    *output = value.toArray();
    return true;
}

QStringList allowedParameterTypes() {
    return {QStringLiteral("int"),
            QStringLiteral("bool"),
            QStringLiteral("double"),
            QStringLiteral("string"),
            QStringLiteral("enum"),
            QStringLiteral("path"),
            QStringLiteral("object"),
            QStringLiteral("array")};
}

QStringList allowedDocumentFormats() {
    return {QStringLiteral("yaml"),
            QStringLiteral("json"),
            QStringLiteral("tcl"),
            QStringLiteral("xml"),
            QStringLiteral("ini"),
            QStringLiteral("text"),
            QStringLiteral("raw")};
}

bool isValueTypeCompatible(const QString& type, const QJsonValue& value) {
    if (value.isUndefined()) {
        return false;
    }
    if (type == QStringLiteral("int")) {
        return ipcraft::isInt64Value(value);
    }
    if (type == QStringLiteral("bool")) {
        return value.isBool();
    }
    if (type == QStringLiteral("double")) {
        return value.isDouble();
    }
    if (type == QStringLiteral("string") || type == QStringLiteral("path")) {
        return value.isString();
    }
    if (type == QStringLiteral("object")) {
        return value.isObject();
    }
    if (type == QStringLiteral("array")) {
        return value.isArray();
    }
    return false;
}

bool jsonValuesEqual(const QJsonValue& left, const QJsonValue& right) {
    if (left.type() != right.type()) {
        if (left.isDouble() && right.isDouble()) {
            return left.toDouble() == right.toDouble();
        }
        return false;
    }
    return left == right;
}

bool enumContains(const QJsonArray& values, const QJsonValue& value) {
    for (const QJsonValue& enumValue : values) {
        if (jsonValuesEqual(enumValue, value)) {
            return true;
        }
    }
    return false;
}

bool isPathConfined(const QString& rootPath, const QString& path) {
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized)) {
        return false;
    }
    const QStringList segments = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral(".."))) {
        return false;
    }
    const QFileInfo rootInfo(rootPath);
    const QString root = QDir::cleanPath(rootInfo.canonicalFilePath().isEmpty()
                                             ? rootInfo.absoluteFilePath()
                                             : rootInfo.canonicalFilePath());
    QString currentPath = root;
    for (qsizetype index = 0; index < segments.size(); ++index) {
        const QFileInfo nextInfo(QDir(currentPath).filePath(segments.at(index)));
        if (!nextInfo.exists()) {
            const QString projected = QDir::cleanPath(
                QDir(currentPath).filePath(segments.sliced(index).join(QLatin1Char('/'))));
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

QJsonValue expressionValue(const QJsonObject& expression,
                           const ipcraft::ConfigBundle& bundle,
                           ipcraft::DiagnosticStore* diagnostics,
                           const QString& path,
                           bool* ok);

bool expressionBoolValue(const QJsonValue& value,
                         const ipcraft::ConfigBundle& bundle,
                         ipcraft::DiagnosticStore* diagnostics,
                         const QString& path,
                         bool* ok);

bool expressionIsObject(const QJsonValue& value,
                        ipcraft::DiagnosticStore* diagnostics,
                        const QString& path,
                        QJsonObject* object) {
    if (!value.isObject()) {
        addUnsupportedExpression(*diagnostics, path);
        return false;
    }
    *object = value.toObject();
    return true;
}

bool expressionBool(const QJsonObject& expression,
                    const ipcraft::ConfigBundle& bundle,
                    ipcraft::DiagnosticStore* diagnostics,
                    const QString& path,
                    bool* ok) {
    if (expression.contains(QStringLiteral("literal"))) {
        if (!hasExactKeys(expression, {QStringLiteral("literal")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        const QJsonValue literal = expression.value(QStringLiteral("literal"));
        if (literal.isBool()) {
            return literal.toBool();
        }
        addUnsupportedExpression(*diagnostics, path);
        *ok = false;
        return false;
    }

    const QString op = expression.value(QStringLiteral("op")).toString();
    if (op == QStringLiteral("exists")) {
        if (!hasExactKeys(expression, {QStringLiteral("op"), QStringLiteral("param")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        const QString param = expression.value(QStringLiteral("param")).toString();
        if (param.isEmpty()) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        return bundle.parameters.contains(param);
    }
    if (op == QStringLiteral("eq") || op == QStringLiteral("ne")) {
        if (!hasExactKeys(expression, {QStringLiteral("op"), QStringLiteral("left"), QStringLiteral("right")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        bool leftOk = true;
        bool rightOk = true;
        QJsonObject leftObject;
        QJsonObject rightObject;
        if (!expressionIsObject(expression.value(QStringLiteral("left")),
                                diagnostics,
                                childPath(path, QStringLiteral("left")),
                                &leftObject) ||
            !expressionIsObject(expression.value(QStringLiteral("right")),
                                diagnostics,
                                childPath(path, QStringLiteral("right")),
                                &rightObject)) {
            *ok = false;
            return false;
        }
        const QJsonValue left = expressionValue(leftObject,
                                               bundle,
                                               diagnostics,
                                               childPath(path, QStringLiteral("left")),
                                               &leftOk);
        const QJsonValue right = expressionValue(rightObject,
                                                bundle,
                                                diagnostics,
                                                childPath(path, QStringLiteral("right")),
                                                &rightOk);
        *ok = leftOk && rightOk;
        const bool equal = jsonValuesEqual(left, right);
        return op == QStringLiteral("eq") ? equal : !equal;
    }
    if (op == QStringLiteral("and") || op == QStringLiteral("or")) {
        if (!hasExactKeys(expression, {QStringLiteral("op"), QStringLiteral("args")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        const QJsonValue argsValue = expression.value(QStringLiteral("args"));
        if (!argsValue.isArray() || argsValue.toArray().isEmpty()) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        const QJsonArray args = argsValue.toArray();
        if (op == QStringLiteral("and")) {
            bool result = true;
            for (qsizetype index = 0; index < args.size(); ++index) {
                bool childOk = true;
                const QString childExpressionPath = indexPath(childPath(path, QStringLiteral("args")), index);
                result = expressionBoolValue(args.at(index),
                                             bundle,
                                             diagnostics,
                                             childExpressionPath,
                                             &childOk) && result;
                *ok = *ok && childOk;
            }
            return result;
        }
        bool result = false;
        for (qsizetype index = 0; index < args.size(); ++index) {
            bool childOk = true;
            const QString childExpressionPath = indexPath(childPath(path, QStringLiteral("args")), index);
            result = expressionBoolValue(args.at(index),
                                         bundle,
                                         diagnostics,
                                         childExpressionPath,
                                         &childOk) || result;
            *ok = *ok && childOk;
        }
        return result;
    }
    if (op == QStringLiteral("not")) {
        if (!hasExactKeys(expression, {QStringLiteral("op"), QStringLiteral("arg")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return false;
        }
        bool childOk = true;
        const bool value = expressionBoolValue(expression.value(QStringLiteral("arg")),
                                               bundle,
                                               diagnostics,
                                               childPath(path, QStringLiteral("arg")),
                                               &childOk);
        *ok = childOk;
        return !value;
    }

    addUnsupportedExpression(*diagnostics, path);
    *ok = false;
    return false;
}

bool expressionBoolValue(const QJsonValue& value,
                         const ipcraft::ConfigBundle& bundle,
                         ipcraft::DiagnosticStore* diagnostics,
                         const QString& path,
                         bool* ok) {
    if (value.isBool()) {
        return value.toBool();
    }
    QJsonObject object;
    if (!expressionIsObject(value, diagnostics, path, &object)) {
        *ok = false;
        return false;
    }
    return expressionBool(object, bundle, diagnostics, path, ok);
}

void validateExpressionUseSite(const QJsonObject& expression,
                               bool present,
                               ipcraft::DiagnosticStore& diagnostics,
                               const QString& path) {
    if (!present) {
        return;
    }
    if (expression.isEmpty()) {
        addUnsupportedExpression(diagnostics, path);
        return;
    }
    ipcraft::ConfigBundle emptyBundle;
    (void)ipcraft::evaluateConfigExpression(expression, emptyBundle, &diagnostics, path);
}

bool optionalExpression(const QJsonObject& object,
                        const QString& key,
                        const QString& path,
                        ipcraft::DiagnosticStore& diagnostics,
                        QJsonObject* output) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) {
        *output = {};
        return false;
    }
    if (value.isBool()) {
        output->insert(QStringLiteral("literal"), value);
        return true;
    }
    if (!value.isObject()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("Field '%1' must be a boolean literal or expression object.").arg(key),
                      path);
        *output = {};
        return false;
    }
    *output = value.toObject();
    return true;
}

QJsonValue expressionValue(const QJsonObject& expression,
                           const ipcraft::ConfigBundle& bundle,
                           ipcraft::DiagnosticStore* diagnostics,
                           const QString& path,
                           bool* ok) {
    if (expression.contains(QStringLiteral("literal"))) {
        if (!hasExactKeys(expression, {QStringLiteral("literal")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return {};
        }
        return expression.value(QStringLiteral("literal"));
    }
    if (expression.contains(QStringLiteral("param"))) {
        if (!hasExactKeys(expression, {QStringLiteral("param")})) {
            addUnsupportedExpression(*diagnostics, path);
            *ok = false;
            return {};
        }
        const QString param = expression.value(QStringLiteral("param")).toString();
        if (!param.isEmpty()) {
            return bundle.parameters.value(param);
        }
    }
    addUnsupportedExpression(*diagnostics, path);
    *ok = false;
    return {};
}

ipcraft::ParameterDef parseParameter(const QJsonObject& object,
                                     const QString& path,
                                     ipcraft::DiagnosticStore& diagnostics) {
    ipcraft::ParameterDef def;
    def.id = requiredString(object, QStringLiteral("id"), childPath(path, QStringLiteral("id")), diagnostics);
    def.type = requiredString(object, QStringLiteral("type"), childPath(path, QStringLiteral("type")), diagnostics);
    if (!def.type.isEmpty() && !allowedParameterTypes().contains(def.type)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("Parameter type is not supported."),
                      childPath(path, QStringLiteral("type")));
    }
    def.required = optionalBool(object, QStringLiteral("required"), childPath(path, QStringLiteral("required")), diagnostics);
    def.valueType = optionalString(object, QStringLiteral("value_type"), childPath(path, QStringLiteral("value_type")), diagnostics);
    optionalArray(object, QStringLiteral("values"), childPath(path, QStringLiteral("values")), diagnostics, &def.enumValues);
    def.defaultValue = object.value(QStringLiteral("default"));
    const bool hasDefaultWhen = optionalExpression(object, QStringLiteral("default_when"), childPath(path, QStringLiteral("default_when")), diagnostics, &def.defaultWhen);
    const bool hasVisibleWhen = optionalExpression(object, QStringLiteral("visible_when"), childPath(path, QStringLiteral("visible_when")), diagnostics, &def.visibleWhen);
    const bool hasEnabledWhen = optionalExpression(object, QStringLiteral("enabled_when"), childPath(path, QStringLiteral("enabled_when")), diagnostics, &def.enabledWhen);
    const bool hasRequiredWhen = optionalExpression(object, QStringLiteral("required_when"), childPath(path, QStringLiteral("required_when")), diagnostics, &def.requiredWhen);
    validateExpressionUseSite(def.defaultWhen, hasDefaultWhen, diagnostics, childPath(path, QStringLiteral("default_when")));
    validateExpressionUseSite(def.visibleWhen, hasVisibleWhen, diagnostics, childPath(path, QStringLiteral("visible_when")));
    validateExpressionUseSite(def.enabledWhen, hasEnabledWhen, diagnostics, childPath(path, QStringLiteral("enabled_when")));
    validateExpressionUseSite(def.requiredWhen, hasRequiredWhen, diagnostics, childPath(path, QStringLiteral("required_when")));

    const QJsonValue range = object.value(QStringLiteral("range"));
    if (range.isObject()) {
        const QJsonObject rangeObject = range.toObject();
        if (rangeObject.value(QStringLiteral("min")).isDouble()) {
            def.hasRangeMin = true;
            def.rangeMin = rangeObject.value(QStringLiteral("min")).toDouble();
        }
        if (rangeObject.value(QStringLiteral("max")).isDouble()) {
            def.hasRangeMax = true;
            def.rangeMax = rangeObject.value(QStringLiteral("max")).toDouble();
        }
    }

    if (def.type == QStringLiteral("enum")) {
        if (def.valueType != QStringLiteral("string") && def.valueType != QStringLiteral("int64")) {
            addDiagnostic(diagnostics,
                          QStringLiteral("config.enum_invalid"),
                          QStringLiteral("Enum declaration must choose value_type 'string' or 'int64'."),
                          childPath(path, QStringLiteral("value_type")));
        }
        if (def.enumValues.isEmpty()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("config.enum_invalid"),
                          QStringLiteral("Enum declaration requires values."),
                          childPath(path, QStringLiteral("values")));
        }
        for (qsizetype index = 0; index < def.enumValues.size(); ++index) {
            const QJsonValue value = def.enumValues.at(index);
            const bool ok = def.valueType == QStringLiteral("string")
                ? value.isString()
                : ipcraft::isInt64Value(value);
            if (!ok) {
                addDiagnostic(diagnostics,
                              QStringLiteral("config.enum_invalid"),
                              QStringLiteral("Enum value does not match declared value_type."),
                              indexPath(childPath(path, QStringLiteral("values")), index));
            }
        }
    }
    return def;
}

ipcraft::TableColumnDef parseColumn(const QJsonObject& object,
                                    const QString& path,
                                    ipcraft::DiagnosticStore& diagnostics) {
    ipcraft::TableColumnDef column;
    column.id = requiredString(object, QStringLiteral("id"), childPath(path, QStringLiteral("id")), diagnostics);
    column.type = requiredString(object, QStringLiteral("type"), childPath(path, QStringLiteral("type")), diagnostics);
    column.required = optionalBool(object, QStringLiteral("required"), childPath(path, QStringLiteral("required")), diagnostics);
    return column;
}

void appendAllowedExtensions(const QJsonArray& values,
                             const QString& path,
                             ipcraft::DiagnosticStore& diagnostics,
                             QStringList* output) {
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QJsonValue extension = values.at(index);
        if (!extension.isString()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("config.type_mismatch"),
                          QStringLiteral("File extension allowlist entries must be strings."),
                          indexPath(path, index));
            continue;
        }
        output->append(extension.toString());
    }
}

} // namespace

namespace ipcraft {

ConfigBundle ConfigBundle::fromJson(const QJsonObject& object) {
    ConfigBundle bundle;
    bundle.parameters = object.value(QStringLiteral("parameters")).isObject()
        ? object.value(QStringLiteral("parameters")).toObject()
        : QJsonObject{};
    bundle.tables = object.value(QStringLiteral("tables")).isObject()
        ? object.value(QStringLiteral("tables")).toObject()
        : QJsonObject{};
    bundle.documents = object.value(QStringLiteral("documents")).isObject()
        ? object.value(QStringLiteral("documents")).toObject()
        : QJsonObject{};
    bundle.files = object.value(QStringLiteral("files")).isObject()
        ? object.value(QStringLiteral("files")).toObject()
        : QJsonObject{};
    bundle.preserved = object.value(QStringLiteral("preserved")).isObject()
        ? object.value(QStringLiteral("preserved")).toObject()
        : QJsonObject{};
    return bundle;
}

QJsonObject ConfigBundle::toJson() const {
    QJsonObject object;
    if (!parameters.isEmpty()) {
        object.insert(QStringLiteral("parameters"), sortedJsonObject(parameters));
    }
    if (!tables.isEmpty()) {
        object.insert(QStringLiteral("tables"), sortedJsonObject(tables));
    }
    if (!documents.isEmpty()) {
        object.insert(QStringLiteral("documents"), sortedJsonObject(documents));
    }
    if (!files.isEmpty()) {
        object.insert(QStringLiteral("files"), sortedJsonObject(files));
    }
    if (!preserved.isEmpty()) {
        object.insert(QStringLiteral("preserved"), sortedJsonObject(preserved));
    }
    return sortedJsonObject(object);
}

ConfigSchemaReadResult ConfigSchema::fromJson(const QJsonObject& object) {
    ConfigSchemaReadResult result;
    QJsonArray parameters;
    if (optionalArray(object, QStringLiteral("parameters"), QStringLiteral("$.parameters"), result.diagnostics, &parameters)) {
        QSet<QString> seen;
        for (qsizetype index = 0; index < parameters.size(); ++index) {
            const QString path = indexPath(QStringLiteral("$.parameters"), index);
            if (!parameters.at(index).isObject()) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.type_mismatch"),
                              QStringLiteral("Parameter definitions must be objects."),
                              path);
                continue;
            }
            ParameterDef def = parseParameter(parameters.at(index).toObject(), path, result.diagnostics);
            if (!def.id.isEmpty() && seen.contains(def.id)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.duplicate_id"),
                              QStringLiteral("Duplicate parameter id."),
                              childPath(path, QStringLiteral("id")));
            }
            seen.insert(def.id);
            result.schema.parameters.append(def);
        }
    }

    QJsonArray tables;
    if (optionalArray(object, QStringLiteral("tables"), QStringLiteral("$.tables"), result.diagnostics, &tables)) {
        QSet<QString> seen;
        for (qsizetype index = 0; index < tables.size(); ++index) {
            const QString path = indexPath(QStringLiteral("$.tables"), index);
            if (!tables.at(index).isObject()) {
                addDiagnostic(result.diagnostics, QStringLiteral("config.type_mismatch"), QStringLiteral("Table definitions must be objects."), path);
                continue;
            }
            const QJsonObject tableObject = tables.at(index).toObject();
            TableDef table;
            table.id = requiredString(tableObject, QStringLiteral("id"), childPath(path, QStringLiteral("id")), result.diagnostics);
            if (!table.id.isEmpty() && seen.contains(table.id)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.duplicate_id"),
                              QStringLiteral("Duplicate table id."),
                              childPath(path, QStringLiteral("id")));
            }
            seen.insert(table.id);
            table.allowAddRemove = optionalBool(tableObject, QStringLiteral("allow_add_remove"), childPath(path, QStringLiteral("allow_add_remove")), result.diagnostics, true);
            table.preserveUnknownColumns = optionalBool(tableObject, QStringLiteral("preserve_unknown_columns"), childPath(path, QStringLiteral("preserve_unknown_columns")), result.diagnostics);
            QJsonArray columns;
            if (optionalArray(tableObject, QStringLiteral("columns"), childPath(path, QStringLiteral("columns")), result.diagnostics, &columns)) {
                for (qsizetype columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
                    const QString columnPath = indexPath(childPath(path, QStringLiteral("columns")), columnIndex);
                    if (!columns.at(columnIndex).isObject()) {
                        addDiagnostic(result.diagnostics, QStringLiteral("config.type_mismatch"), QStringLiteral("Table columns must be objects."), columnPath);
                        continue;
                    }
                    table.columns.append(parseColumn(columns.at(columnIndex).toObject(), columnPath, result.diagnostics));
                }
            }
            result.schema.tables.append(table);
        }
    }

    QJsonArray documents;
    if (optionalArray(object, QStringLiteral("documents"), QStringLiteral("$.documents"), result.diagnostics, &documents)) {
        QSet<QString> seen;
        for (qsizetype index = 0; index < documents.size(); ++index) {
            const QString path = indexPath(QStringLiteral("$.documents"), index);
            if (!documents.at(index).isObject()) {
                addDiagnostic(result.diagnostics, QStringLiteral("config.type_mismatch"), QStringLiteral("Document definitions must be objects."), path);
                continue;
            }
            const QJsonObject documentObject = documents.at(index).toObject();
            ConfigDocumentDef document;
            document.id = requiredString(documentObject, QStringLiteral("id"), childPath(path, QStringLiteral("id")), result.diagnostics);
            if (!document.id.isEmpty() && seen.contains(document.id)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.duplicate_id"),
                              QStringLiteral("Duplicate document id."),
                              childPath(path, QStringLiteral("id")));
            }
            seen.insert(document.id);
            document.format = requiredString(documentObject, QStringLiteral("format"), childPath(path, QStringLiteral("format")), result.diagnostics);
            if (!document.format.isEmpty() && !allowedDocumentFormats().contains(document.format)) {
                addDiagnostic(result.diagnostics, QStringLiteral("config.document_format_invalid"), QStringLiteral("Unsupported document format."), childPath(path, QStringLiteral("format")));
            }
            document.outputPath = optionalString(documentObject, QStringLiteral("output_path"), childPath(path, QStringLiteral("output_path")), result.diagnostics);
            document.editable = optionalBool(documentObject, QStringLiteral("editable"), childPath(path, QStringLiteral("editable")), result.diagnostics, true);
            document.preserveUnknownFields = optionalBool(documentObject, QStringLiteral("preserve_unknown_fields"), childPath(path, QStringLiteral("preserve_unknown_fields")), result.diagnostics);
            result.schema.documents.append(document);
        }
    }

    QJsonArray files;
    if (optionalArray(object, QStringLiteral("files"), QStringLiteral("$.files"), result.diagnostics, &files)) {
        QSet<QString> seen;
        for (qsizetype index = 0; index < files.size(); ++index) {
            const QString path = indexPath(QStringLiteral("$.files"), index);
            if (!files.at(index).isObject()) {
                addDiagnostic(result.diagnostics, QStringLiteral("config.type_mismatch"), QStringLiteral("File input definitions must be objects."), path);
                continue;
            }
            const QJsonObject fileObject = files.at(index).toObject();
            FileInputDef file;
            file.id = requiredString(fileObject, QStringLiteral("id"), childPath(path, QStringLiteral("id")), result.diagnostics);
            if (!file.id.isEmpty() && seen.contains(file.id)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.duplicate_id"),
                              QStringLiteral("Duplicate file input id."),
                              childPath(path, QStringLiteral("id")));
            }
            seen.insert(file.id);
            file.kind = optionalString(fileObject, QStringLiteral("kind"), childPath(path, QStringLiteral("kind")), result.diagnostics);
            file.required = optionalBool(fileObject, QStringLiteral("required"), childPath(path, QStringLiteral("required")), result.diagnostics);
            QJsonArray extensions;
            if (optionalArray(fileObject, QStringLiteral("allowed_extensions"), childPath(path, QStringLiteral("allowed_extensions")), result.diagnostics, &extensions)) {
                appendAllowedExtensions(extensions,
                                        childPath(path, QStringLiteral("allowed_extensions")),
                                        result.diagnostics,
                                        &file.allowedExtensions);
            }
            QJsonArray allowed;
            if (optionalArray(fileObject, QStringLiteral("allowed"), childPath(path, QStringLiteral("allowed")), result.diagnostics, &allowed)) {
                appendAllowedExtensions(allowed,
                                        childPath(path, QStringLiteral("allowed")),
                                        result.diagnostics,
                                        &file.allowedExtensions);
            }
            result.schema.files.append(file);
        }
    }

    optionalObject(object, QStringLiteral("metadata"), QStringLiteral("$.metadata"), result.diagnostics, &result.schema.metadata);
    optionalObject(object, QStringLiteral("native"), QStringLiteral("$.native"), result.diagnostics, &result.schema.native);
    result.ok = result.diagnostics.records.isEmpty();
    return result;
}

bool evaluateConfigExpression(const QJsonObject& expression,
                              const ConfigBundle& bundle,
                              DiagnosticStore* diagnostics,
                              const QString& path) {
    DiagnosticStore localDiagnostics;
    DiagnosticStore* outputDiagnostics = diagnostics == nullptr ? &localDiagnostics : diagnostics;
    bool ok = true;
    const bool value = expressionBool(expression, bundle, outputDiagnostics, path, &ok);
    return ok && value;
}

ConfigValidationResult validateConfigBundle(const ConfigSchema& schema,
                                            const ConfigBundle& bundle,
                                            const ConfigValidationOptions& options) {
    ConfigValidationResult result;
    result.normalized.preserved = bundle.preserved;
    const QString validationRoot = options.projectRootPath.isEmpty()
        ? QDir::currentPath()
        : options.projectRootPath;

    QSet<QString> parameterIds;
    for (const ParameterDef& parameter : schema.parameters) {
        parameterIds.insert(parameter.id);
    }
    for (auto it = bundle.parameters.constBegin(); it != bundle.parameters.constEnd(); ++it) {
        if (!parameterIds.contains(it.key())) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.unknown_parameter"),
                          QStringLiteral("Config parameter is not declared by the package schema."),
                          keyPath(QStringLiteral("$.parameters"), it.key()));
        }
    }

    for (const ParameterDef& parameter : schema.parameters) {
        const QString path = keyPath(QStringLiteral("$.parameters"), parameter.id);
        const QJsonValue value = bundle.parameters.value(parameter.id);
        bool required = parameter.required;
        if (!parameter.requiredWhen.isEmpty()) {
            required = required || evaluateConfigExpression(parameter.requiredWhen,
                                                            bundle,
                                                            &result.diagnostics,
                                                            keyPath(QStringLiteral("$.schema.parameters"), parameter.id) +
                                                                QStringLiteral(".required_when"));
        }
        if (value.isUndefined()) {
            if (required) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.required_missing"),
                              QStringLiteral("Required parameter is missing."),
                              path);
            }
            continue;
        }

        if (parameter.type == QStringLiteral("enum")) {
            const bool typeOk = parameter.valueType == QStringLiteral("string")
                ? value.isString()
                : isInt64Value(value);
            if (!typeOk) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.type_mismatch"),
                              QStringLiteral("Enum parameter has the wrong value type."),
                              path);
                continue;
            }
            if (!enumContains(parameter.enumValues, value)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.enum_invalid"),
                              QStringLiteral("Enum parameter value is not allowed."),
                              path);
                continue;
            }
            result.normalized.parameters.insert(parameter.id, value);
            continue;
        }

        if (!isValueTypeCompatible(parameter.type, value)) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.type_mismatch"),
                          QStringLiteral("Parameter has the wrong value type."),
                          path);
            continue;
        }
        bool parameterValid = true;
        if (parameter.type == QStringLiteral("path")) {
            if (!isPathConfined(validationRoot, value.toString())) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.path_escape"),
                              QStringLiteral("Path parameter must stay inside the project root."),
                              path);
                parameterValid = false;
            }
        }
        if ((parameter.type == QStringLiteral("int") || parameter.type == QStringLiteral("double")) &&
            value.isDouble()) {
            const double number = value.toDouble();
            if ((parameter.hasRangeMin && number < parameter.rangeMin) ||
                (parameter.hasRangeMax && number > parameter.rangeMax)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.range_invalid"),
                              QStringLiteral("Parameter is outside its allowed range."),
                              path);
                parameterValid = false;
            }
        }
        if (parameterValid) {
            result.normalized.parameters.insert(parameter.id, value);
        }
    }

    for (const TableDef& table : schema.tables) {
        const QString tablePath = keyPath(QStringLiteral("$.tables"), table.id);
        const QJsonValue tableValue = bundle.tables.value(table.id);
        if (!tableValue.isUndefined() && !tableValue.isObject()) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.type_mismatch"),
                          QStringLiteral("Table state must be an object."),
                          tablePath);
            continue;
        }

        const QJsonObject tableState = tableValue.toObject();
        const QJsonValue rowsValue = tableState.value(QStringLiteral("rows"));
        if (!rowsValue.isUndefined() && !rowsValue.isArray()) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.type_mismatch"),
                          QStringLiteral("Table rows must be an array."),
                          keyPath(tablePath, QStringLiteral("rows")));
            continue;
        }

        const QJsonArray rows = rowsValue.toArray();
        QJsonArray normalizedRows;
        QSet<QString> columnIds;
        for (const TableColumnDef& column : table.columns) {
            columnIds.insert(column.id);
        }
        for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const QString rowPath = indexPath(keyPath(tablePath, QStringLiteral("rows")), rowIndex);
            if (!rows.at(rowIndex).isObject()) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.type_mismatch"),
                              QStringLiteral("Table row must be an object."),
                              rowPath);
                continue;
            }

            const QJsonObject row = rows.at(rowIndex).toObject();
            QJsonObject normalizedRow = table.preserveUnknownColumns ? row : QJsonObject{};
            if (!table.preserveUnknownColumns) {
                for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
                    if (!columnIds.contains(it.key())) {
                        addDiagnostic(result.diagnostics,
                                      QStringLiteral("config.unknown_table_column"),
                                      QStringLiteral("Table row contains a column that is not declared by the package schema."),
                                      keyPath(rowPath, it.key()));
                    }
                }
            }
            for (const TableColumnDef& column : table.columns) {
                const QJsonValue value = row.value(column.id);
                if (value.isUndefined()) {
                    if (column.required) {
                        addDiagnostic(result.diagnostics,
                                      QStringLiteral("config.table_column_missing"),
                                      QStringLiteral("Required table column is missing."),
                                      keyPath(rowPath, column.id));
                    }
                    continue;
                }
                normalizedRow.insert(column.id, value);
                if (!isValueTypeCompatible(column.type, value)) {
                    addDiagnostic(result.diagnostics,
                                  QStringLiteral("config.type_mismatch"),
                                  QStringLiteral("Table column has the wrong value type."),
                                  keyPath(rowPath, column.id));
                }
            }
            normalizedRows.append(normalizedRow);
        }
        QJsonObject normalizedTable;
        normalizedTable.insert(QStringLiteral("rows"), normalizedRows);
        result.normalized.tables.insert(table.id, normalizedTable);
    }

    QSet<QString> documentIds;
    for (const ConfigDocumentDef& document : schema.documents) {
        documentIds.insert(document.id);
    }
    for (auto it = bundle.documents.constBegin(); it != bundle.documents.constEnd(); ++it) {
        if (!documentIds.contains(it.key())) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.unknown_document"),
                          QStringLiteral("Config document is not declared by the package schema."),
                          keyPath(QStringLiteral("$.documents"), it.key()));
        }
    }

    for (const ConfigDocumentDef& document : schema.documents) {
        const QString documentPath = keyPath(QStringLiteral("$.documents"), document.id);
        const QJsonValue documentValue = bundle.documents.value(document.id);
        if (!documentValue.isUndefined() && !documentValue.isObject()) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.type_mismatch"),
                          QStringLiteral("Document state must be an object."),
                          documentPath);
            continue;
        }
        const QJsonObject documentState = documentValue.toObject();
        QJsonObject normalizedDocument;
        const QString actualFormat = documentState.value(QStringLiteral("format")).toString();
        if (!actualFormat.isEmpty() && actualFormat != document.format) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.document_format_invalid"),
                          QStringLiteral("Document format does not match schema."),
                          keyPath(documentPath, QStringLiteral("format")));
        } else if (!actualFormat.isEmpty()) {
            normalizedDocument.insert(QStringLiteral("format"), actualFormat);
        }
        if (documentState.contains(QStringLiteral("content"))) {
            const QJsonValue content = documentState.value(QStringLiteral("content"));
            if (document.preserveUnknownFields) {
                normalizedDocument.insert(QStringLiteral("content"), content);
            } else if (content.isObject()) {
                normalizedDocument.insert(QStringLiteral("content"), QJsonObject{});
            }
        }
        result.normalized.documents.insert(document.id, normalizedDocument);
    }

    QSet<QString> fileIds;
    for (const FileInputDef& file : schema.files) {
        fileIds.insert(file.id);
    }
    for (auto it = bundle.files.constBegin(); it != bundle.files.constEnd(); ++it) {
        if (!fileIds.contains(it.key())) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.unknown_file"),
                          QStringLiteral("File input is not declared by the package schema."),
                          keyPath(QStringLiteral("$.files"), it.key()));
        }
    }

    for (const FileInputDef& file : schema.files) {
        const QString filePath = keyPath(QStringLiteral("$.files"), file.id);
        const QJsonValue fileValue = bundle.files.value(file.id);
        if (!fileValue.isUndefined() && !fileValue.isObject()) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.type_mismatch"),
                          QStringLiteral("File input state must be an object."),
                          filePath);
            continue;
        }
        const QJsonObject fileState = fileValue.toObject();
        const QString path = fileState.value(QStringLiteral("path")).toString();
        bool fileValid = true;
        if (path.isEmpty()) {
            if (file.required) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.required_missing"),
                              QStringLiteral("Required file input is missing."),
                              keyPath(filePath, QStringLiteral("path")));
            }
            continue;
        }
        if (!isPathConfined(validationRoot, path)) {
            addDiagnostic(result.diagnostics,
                          QStringLiteral("config.path_escape"),
                          QStringLiteral("File input path must stay inside the project root."),
                          keyPath(filePath, QStringLiteral("path")));
            fileValid = false;
        }
        if (!file.allowedExtensions.isEmpty()) {
            const QString suffix = QStringLiteral(".") + QFileInfo(path).suffix();
            if (!file.allowedExtensions.contains(suffix, Qt::CaseInsensitive)) {
                addDiagnostic(result.diagnostics,
                              QStringLiteral("config.file_extension_invalid"),
                              QStringLiteral("File input extension is not allowed."),
                              keyPath(filePath, QStringLiteral("path")));
                fileValid = false;
            }
        }
        if (fileValid) {
            result.normalized.files.insert(file.id,
                                           QJsonObject{{QStringLiteral("path"), path}});
        }
    }

    result.ok = result.diagnostics.records.isEmpty();
    return result;
}

} // namespace ipcraft
