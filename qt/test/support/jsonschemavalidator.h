#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class JsonSchemaValidator {
public:
    explicit JsonSchemaValidator(QJsonObject schema);

    static JsonSchemaValidator fromFile(const QString& path);
    bool validate(const QJsonValue& value, QString* error = nullptr) const;

private:
    bool validateAgainst(const QJsonValue& value,
                         const QJsonValue& schema,
                         const QString& path,
                         QString* error,
                         bool* schemaError = nullptr) const;
    QJsonValue resolveRef(const QString& ref) const;

    QJsonObject m_schema;
};
