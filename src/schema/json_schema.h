#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <memory>

namespace finepaper::json_schema {

// This module implements a deliberately closed Draft 2020-12 profile. A
// schema using a vocabulary outside that profile is reported as Unsupported;
// malformed uses of a supported keyword are Invalid.
enum class CompileStatus {
    Invalid,
    Unsupported,
    Ready
};

struct Issue {
    QString code;
    QString message;
    // RFC 6901 pointers relative to the instance and schema roots. The empty
    // string denotes a root. Callers can rebase them onto Design paths.
    QString instancePointer;
    QString schemaPointer;
    QString keyword;
};

struct Limits {
    qsizetype maximumSchemaNodes = 8'192;
    int maximumSchemaDepth = 128;
    qsizetype maximumSchemaDataNodes = 65'536;
    int maximumSchemaDataDepth = 256;
    qsizetype maximumEnumValues = 4'096;
    qsizetype maximumReferences = 8'192;
    qsizetype maximumPatternLength = 4'096;
    quint64 maximumEvaluationSteps = 1'000'000;
    int maximumEvaluationDepth = 256;
    int maximumInstanceDepth = 256;
    qsizetype maximumObjectProperties = 65'536;
    qsizetype maximumObjectKeyCodeUnits = 4'096;
    qsizetype maximumObjectKeyCodeUnitsTotal = 1'048'576;
    qsizetype maximumArrayItems = 65'536;
    qsizetype maximumStringCodeUnits = 65'536;
    qsizetype maximumDiagnostics = 256;
};

namespace detail {
struct Program;
}

struct CompileResult;
struct ValidationResult;

// A compiled schema is immutable and can be shared across validation calls.
// Each validate() invocation owns its evaluation budget and recursion state.
class CompiledSchema final {
public:
    ~CompiledSchema();

    CompiledSchema(const CompiledSchema&) = delete;
    CompiledSchema& operator=(const CompiledSchema&) = delete;
    CompiledSchema(CompiledSchema&&) = delete;
    CompiledSchema& operator=(CompiledSchema&&) = delete;

private:
    explicit CompiledSchema(std::shared_ptr<const detail::Program> program);

    std::shared_ptr<const detail::Program> m_program;

    friend CompileResult compile(const QJsonObject&, Limits);
    friend ValidationResult validate(const CompiledSchema&,
                                     const QJsonValue&,
                                     Limits);
};

struct CompileResult {
    CompileStatus status = CompileStatus::Invalid;
    std::shared_ptr<const CompiledSchema> schema;
    QVector<Issue> issues;
};

struct ValidationResult {
    bool success = false;
    QVector<Issue> issues;
};

[[nodiscard]] CompileResult compile(const QJsonObject& schema,
                                    Limits limits = {});

[[nodiscard]] ValidationResult validate(const CompiledSchema& schema,
                                        const QJsonValue& instance,
                                        Limits limits = {});

} // namespace finepaper::json_schema
