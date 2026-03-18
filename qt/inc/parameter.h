#pragma once

#include <QString>
#include <variant>

class Parameter {
public:
    using Value = std::variant<QString, int, double, bool>;

    Parameter() = default;
    Parameter(const QString& name, Value value);

    QString name() const { return m_name; }
    Value value() const { return m_value; }
    void setValue(Value value) { m_value = value; }

private:
    QString m_name;
    Value m_value;
};
