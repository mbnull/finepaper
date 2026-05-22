// Ipcraft V1 ConfigSchema / ConfigBundle contract tests.
#include "ipcraft/configschema.h"
#include "ipcraft/schemaids.h"
#include "ipcraft/value.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasRule(const ipcraft::DiagnosticStore& diagnostics, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

bool hasRuleAt(const ipcraft::DiagnosticStore& diagnostics,
               const QString& ruleId,
               const QString& path) {
    for (const ipcraft::Diagnostic& diagnostic : diagnostics.records) {
        if (diagnostic.ruleId != ruleId ||
            diagnostic.source != QStringLiteral("core") ||
            diagnostic.severity != QStringLiteral("error")) {
            continue;
        }
        for (const ipcraft::DiagnosticLocation& location : diagnostic.locations) {
            if (location.kind == QStringLiteral("document_path") &&
                location.path == path) {
                return true;
            }
        }
    }
    return false;
}

QJsonObject schemaWithParameters(const QJsonArray& parameters) {
    return QJsonObject{
        {QStringLiteral("parameters"), parameters}
    };
}

QJsonObject bundleWithParameters(const QJsonObject& parameters) {
    return QJsonObject{
        {QStringLiteral("parameters"), parameters}
    };
}

ipcraft::ConfigSchema readSchemaOrThrow(const QJsonObject& object) {
    const ipcraft::ConfigSchemaReadResult result =
        ipcraft::ConfigSchema::fromJson(object);
    require(result.ok, "schema should parse");
    return result.schema;
}

void testParameterTypesMapToJsonValues() {
    const QJsonObject schemaObject = schemaWithParameters(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("width")},
                    {QStringLiteral("type"), QStringLiteral("int")},
                    {QStringLiteral("required"), true}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("enabled")},
                    {QStringLiteral("type"), QStringLiteral("bool")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("ratio")},
                    {QStringLiteral("type"), QStringLiteral("double")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("label")},
                    {QStringLiteral("type"), QStringLiteral("string")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("mode")},
                    {QStringLiteral("type"), QStringLiteral("enum")},
                    {QStringLiteral("value_type"), QStringLiteral("string")},
                    {QStringLiteral("values"), QJsonArray{QStringLiteral("fast"), QStringLiteral("slow")}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("lanes")},
                    {QStringLiteral("type"), QStringLiteral("enum")},
                    {QStringLiteral("value_type"), QStringLiteral("int64")},
                    {QStringLiteral("values"), QJsonArray{1, 2, 4}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("metadata")},
                    {QStringLiteral("type"), QStringLiteral("object")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("items")},
                    {QStringLiteral("type"), QStringLiteral("array")}}
    });
    const ipcraft::ConfigSchema schema = readSchemaOrThrow(schemaObject);

    const ipcraft::ConfigBundle goodBundle = ipcraft::ConfigBundle::fromJson(
        bundleWithParameters(QJsonObject{
            {QStringLiteral("width"), 64},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("ratio"), 1.25},
            {QStringLiteral("label"), QStringLiteral("demo")},
            {QStringLiteral("mode"), QStringLiteral("fast")},
            {QStringLiteral("lanes"), 4},
            {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("test")}}},
            {QStringLiteral("items"), QJsonArray{QStringLiteral("a")}}
        }));
    ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schema, goodBundle);
    require(result.ok, "valid parameter value types should pass");

    const ipcraft::ConfigBundle badBundle = ipcraft::ConfigBundle::fromJson(
        bundleWithParameters(QJsonObject{
            {QStringLiteral("width"), 1.5},
            {QStringLiteral("enabled"), QStringLiteral("true")},
            {QStringLiteral("mode"), QStringLiteral("turbo")}
        }));
    result = ipcraft::validateConfigBundle(schema, badBundle);
    require(!result.ok, "invalid parameter value types should fail");
    require(hasRule(result.diagnostics, QStringLiteral("config.type_mismatch")),
            "type mismatch should be diagnosed");
    require(hasRule(result.diagnostics, QStringLiteral("config.enum_invalid")),
            "invalid enum value should be diagnosed");

    const QJsonDocument tooLargeIntegerDocument = QJsonDocument::fromJson(
        QByteArrayLiteral(R"json({"parameters":{"width":9223372036854775808}})json"));
    require(tooLargeIntegerDocument.isObject(), "large integer fixture should parse");
    const QJsonValue tooLargeInteger =
        tooLargeIntegerDocument.object()
            .value(QStringLiteral("parameters")).toObject()
            .value(QStringLiteral("width"));
    require(!ipcraft::isInt64Value(tooLargeInteger),
            "2^63 should not be accepted as int64");
    result = ipcraft::validateConfigBundle(
        schema,
        ipcraft::ConfigBundle::fromJson(tooLargeIntegerDocument.object()));
    require(!result.ok, "out-of-range integer should fail int64 validation");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("$.parameters.width")),
            "out-of-range integer should emit stable type mismatch");

    const QJsonDocument tooSmallIntegerDocument = QJsonDocument::fromJson(
        QByteArrayLiteral(R"json({"parameters":{"width":-9223372036854775809}})json"));
    require(tooSmallIntegerDocument.isObject(), "small integer fixture should parse");
    const QJsonValue tooSmallInteger =
        tooSmallIntegerDocument.object()
            .value(QStringLiteral("parameters")).toObject()
            .value(QStringLiteral("width"));
    require(!ipcraft::isInt64Value(tooSmallInteger),
            "int64 min minus one should not be accepted as int64");
    result = ipcraft::validateConfigBundle(
        schema,
        ipcraft::ConfigBundle::fromJson(tooSmallIntegerDocument.object()));
    require(!result.ok, "underflow integer should fail int64 validation");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("$.parameters.width")),
            "underflow integer should emit stable type mismatch");
}

void testEnumDeclarationMustChooseStringOrInt64() {
    const ipcraft::ConfigSchemaReadResult missingType =
        ipcraft::ConfigSchema::fromJson(schemaWithParameters(QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("mode")},
                        {QStringLiteral("type"), QStringLiteral("enum")},
                        {QStringLiteral("values"), QJsonArray{QStringLiteral("fast")}}}
        }));
    require(!missingType.ok, "enum declarations must choose a value type");
    require(hasRuleAt(missingType.diagnostics,
                      QStringLiteral("config.enum_invalid"),
                      QStringLiteral("$.parameters[0].value_type")),
            "missing enum value_type should be diagnosed");

    const ipcraft::ConfigSchemaReadResult mixedValues =
        ipcraft::ConfigSchema::fromJson(schemaWithParameters(QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("mode")},
                        {QStringLiteral("type"), QStringLiteral("enum")},
                        {QStringLiteral("value_type"), QStringLiteral("int64")},
                        {QStringLiteral("values"), QJsonArray{1, QStringLiteral("2")}}}
        }));
    require(!mixedValues.ok, "enum values must match the chosen value type");
    require(hasRule(mixedValues.diagnostics, QStringLiteral("config.enum_invalid")),
            "bad enum values should be diagnosed");
}

void testSimpleExpressionsEvaluateWithoutScripts() {
    const ipcraft::ConfigBundle bundle = ipcraft::ConfigBundle::fromJson(
        bundleWithParameters(QJsonObject{
            {QStringLiteral("advanced"), true},
            {QStringLiteral("mode"), QStringLiteral("fast")}
        }));

    ipcraft::DiagnosticStore diagnostics;
    const bool value = ipcraft::evaluateConfigExpression(
        QJsonObject{
            {QStringLiteral("op"), QStringLiteral("and")},
            {QStringLiteral("args"), QJsonArray{
                QJsonObject{{QStringLiteral("op"), QStringLiteral("exists")},
                            {QStringLiteral("param"), QStringLiteral("advanced")}},
                QJsonObject{{QStringLiteral("op"), QStringLiteral("eq")},
                            {QStringLiteral("left"), QJsonObject{{QStringLiteral("param"), QStringLiteral("mode")}}},
                            {QStringLiteral("right"), QJsonObject{{QStringLiteral("literal"), QStringLiteral("fast")}}}}
            }}
        },
        bundle,
        &diagnostics,
        QStringLiteral("$.parameters[0].visible_when"));
    require(value, "simple side-effect-free expression should evaluate true");
    require(diagnostics.records.isEmpty(), "allowed expression should not emit diagnostics");

    const ipcraft::ConfigSchema schema = readSchemaOrThrow(schemaWithParameters(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("advanced")},
                    {QStringLiteral("type"), QStringLiteral("bool")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("width")},
                    {QStringLiteral("type"), QStringLiteral("int")},
                    {QStringLiteral("required_when"), QJsonObject{
                        {QStringLiteral("op"), QStringLiteral("eq")},
                        {QStringLiteral("left"), QJsonObject{{QStringLiteral("param"), QStringLiteral("advanced")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("literal"), true}}}
                    }}}
    }));
    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schema,
                                      ipcraft::ConfigBundle::fromJson(
                                          bundleWithParameters(QJsonObject{
                                              {QStringLiteral("advanced"), true}
                                          })));
    require(!result.ok, "required_when should require a missing parameter");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.required_missing"),
                      QStringLiteral("$.parameters.width")),
            "required_when missing parameter should be diagnosed");
}

void testExpressionRejectsFileProcessNetworkAndEnvironmentAccess() {
    const QJsonArray badExpressions{
        QJsonObject{{QStringLiteral("op"), QStringLiteral("exec")},
                    {QStringLiteral("args"), QJsonArray{QStringLiteral("ls")}}},
        QJsonObject{{QStringLiteral("op"), QStringLiteral("env")},
                    {QStringLiteral("name"), QStringLiteral("HOME")}},
        QJsonObject{{QStringLiteral("op"), QStringLiteral("read_file")},
                    {QStringLiteral("path"), QStringLiteral("x")}},
        QJsonObject{{QStringLiteral("op"), QStringLiteral("python")},
                    {QStringLiteral("code"), QStringLiteral("1+1")}}
    };

    for (qsizetype index = 0; index < badExpressions.size(); ++index) {
        ipcraft::DiagnosticStore diagnostics;
        const bool value = ipcraft::evaluateConfigExpression(
            badExpressions.at(index).toObject(),
            ipcraft::ConfigBundle{},
            &diagnostics,
            QStringLiteral("$.expr[%1]").arg(index));
        require(!value, "unsupported expressions should evaluate false");
        require(hasRule(diagnostics, QStringLiteral("config.expression_unsupported")),
                "unsupported expression should emit config.expression_unsupported");
    }

    ipcraft::DiagnosticStore mixedDiagnostics;
    const bool mixedValue = ipcraft::evaluateConfigExpression(
        QJsonObject{
            {QStringLiteral("literal"), true},
            {QStringLiteral("op"), QStringLiteral("exec")}
        },
        ipcraft::ConfigBundle{},
        &mixedDiagnostics,
        QStringLiteral("$.expr.mixed"));
    require(!mixedValue, "expressions with extra unsupported keys should fail");
    require(hasRuleAt(mixedDiagnostics,
                      QStringLiteral("config.expression_unsupported"),
                      QStringLiteral("$.expr.mixed")),
            "mixed literal/op expression should be diagnosed at expression path");

    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(schemaWithParameters(QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("bad_visible")},
                        {QStringLiteral("type"), QStringLiteral("bool")},
                        {QStringLiteral("visible_when"), QJsonObject{
                            {QStringLiteral("op"), QStringLiteral("exec")},
                            {QStringLiteral("args"), QJsonArray{QStringLiteral("ls")}}
                        }}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("bad_enabled")},
                        {QStringLiteral("type"), QStringLiteral("bool")},
                        {QStringLiteral("enabled_when"), QJsonObject{
                            {QStringLiteral("op"), QStringLiteral("env")},
                            {QStringLiteral("name"), QStringLiteral("HOME")}
                        }}}
        }));
    require(!schemaResult.ok, "unsupported visible/enabled expressions should fail schema parse");
    require(hasRule(schemaResult.diagnostics,
                    QStringLiteral("config.expression_unsupported")),
            "visible/enabled unsupported expressions should be diagnosed");

    const ipcraft::ConfigSchemaReadResult defaultWhenResult =
        ipcraft::ConfigSchema::fromJson(schemaWithParameters(QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("bad_default")},
                        {QStringLiteral("type"), QStringLiteral("bool")},
                        {QStringLiteral("default_when"), QJsonObject{
                            {QStringLiteral("op"), QStringLiteral("exec")},
                            {QStringLiteral("args"), QJsonArray{QStringLiteral("ls")}}
                        }}}
        }));
    require(!defaultWhenResult.ok, "unsupported default_when expressions should fail schema parse");
    require(hasRuleAt(defaultWhenResult.diagnostics,
                      QStringLiteral("config.expression_unsupported"),
                      QStringLiteral("$.parameters[0].default_when")),
            "default_when unsupported expression should be diagnosed");

    const ipcraft::ConfigSchemaReadResult emptyExpressionResult =
        ipcraft::ConfigSchema::fromJson(schemaWithParameters(QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("empty_visible")},
                        {QStringLiteral("type"), QStringLiteral("bool")},
                        {QStringLiteral("visible_when"), QJsonObject{}}}
        }));
    require(!emptyExpressionResult.ok, "empty expression objects should fail schema parse");
    require(hasRuleAt(emptyExpressionResult.diagnostics,
                      QStringLiteral("config.expression_unsupported"),
                      QStringLiteral("$.parameters[0].visible_when")),
            "empty expression object should be diagnosed at its use site");
}

void testLiteralBooleanExpressionsAreAccepted() {
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(schemaWithParameters(QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("visible")},
                        {QStringLiteral("type"), QStringLiteral("bool")},
                        {QStringLiteral("visible_when"), true}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("enabled")},
                        {QStringLiteral("type"), QStringLiteral("bool")},
                        {QStringLiteral("enabled_when"), QJsonObject{
                            {QStringLiteral("op"), QStringLiteral("not")},
                            {QStringLiteral("arg"), false}
                        }}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("required")},
                        {QStringLiteral("type"), QStringLiteral("string")},
                        {QStringLiteral("required_when"), QJsonObject{
                            {QStringLiteral("op"), QStringLiteral("and")},
                            {QStringLiteral("args"), QJsonArray{true}}
                        }}}
        }));
    require(schemaResult.ok, "literal boolean expressions should parse");

    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema, ipcraft::ConfigBundle{});
    require(!result.ok, "true required_when should require a missing parameter");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.required_missing"),
                      QStringLiteral("$.parameters.required")),
            "literal true expression should affect validation");
}

void testUnknownConfigEntriesAreRejected() {
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("parameters"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("width")},
                            {QStringLiteral("type"), QStringLiteral("int")}}
            }},
            {QStringLiteral("tables"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("regions")},
                    {QStringLiteral("preserve_unknown_columns"), false},
                    {QStringLiteral("columns"), QJsonArray{
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("base")},
                                    {QStringLiteral("type"), QStringLiteral("int")}}
                    }}
                }
            }},
            {QStringLiteral("documents"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("system")},
                            {QStringLiteral("format"), QStringLiteral("json")}}
            }}
        });
    require(schemaResult.ok, "schema fixture should parse");

    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("parameters"), QJsonObject{
                                              {QStringLiteral("depth"), 16}
                                          }},
                                          {QStringLiteral("tables"), QJsonObject{
                                              {QStringLiteral("regions"), QJsonObject{
                                                  {QStringLiteral("rows"), QJsonArray{
                                                      QJsonObject{{QStringLiteral("base"), 0},
                                                                  {QStringLiteral("owner"), QStringLiteral("boot")}}
                                                  }}
                                              }}
                                          }},
                                          {QStringLiteral("documents"), QJsonObject{
                                              {QStringLiteral("unknown_doc"), QJsonObject{
                                                  {QStringLiteral("format"), QStringLiteral("json")}
                                              }}
                                          }}
                                      }));
    require(!result.ok, "unknown config entries should fail validation");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.unknown_parameter"),
                      QStringLiteral("$.parameters.depth")),
            "unknown parameter should be diagnosed");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.unknown_table_column"),
                      QStringLiteral("$.tables.regions.rows[0].owner")),
            "strict unknown table column should be diagnosed");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.unknown_document"),
                      QStringLiteral("$.documents.unknown_doc")),
            "unknown document should be diagnosed");
}

void testTableAndDocumentUnknownFieldsPreserveWhenDeclared() {
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("tables"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("regions")},
                    {QStringLiteral("preserve_unknown_columns"), true},
                    {QStringLiteral("columns"), QJsonArray{
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("base")},
                                    {QStringLiteral("type"), QStringLiteral("int")},
                                    {QStringLiteral("required"), true}},
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("size")},
                                    {QStringLiteral("type"), QStringLiteral("int")},
                                    {QStringLiteral("required"), true}}
                    }}
                }
            }},
            {QStringLiteral("documents"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("system")},
                    {QStringLiteral("format"), QStringLiteral("json")},
                    {QStringLiteral("preserve_unknown_fields"), true}
                }
            }}
        });
    require(schemaResult.ok, "table/document schema should parse");

    const ipcraft::ConfigBundle bundle = ipcraft::ConfigBundle::fromJson(QJsonObject{
        {QStringLiteral("tables"), QJsonObject{
            {QStringLiteral("regions"), QJsonObject{
                {QStringLiteral("rows"), QJsonArray{
                    QJsonObject{{QStringLiteral("base"), 0},
                                {QStringLiteral("size"), 4096},
                                {QStringLiteral("owner"), QStringLiteral("boot")}}
                }}
            }}
        }},
        {QStringLiteral("documents"), QJsonObject{
            {QStringLiteral("system"), QJsonObject{
                {QStringLiteral("format"), QStringLiteral("json")},
                {QStringLiteral("content"), QJsonObject{
                    {QStringLiteral("known"), true},
                    {QStringLiteral("vendor.extra"), 7}
                }}
            }}
        }}
    });

    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema, bundle);
    require(result.ok, "unknown table/document fields should preserve when declared");
    require(result.normalized.toJson()
                .value(QStringLiteral("tables")).toObject()
                .value(QStringLiteral("regions")).toObject()
                .value(QStringLiteral("rows")).toArray().first().toObject()
                .contains(QStringLiteral("owner")),
            "unknown table columns should remain in normalized config");
    require(result.normalized.toJson()
                .value(QStringLiteral("documents")).toObject()
                .value(QStringLiteral("system")).toObject()
                .value(QStringLiteral("content")).toObject()
                .contains(QStringLiteral("vendor.extra")),
            "unknown document fields should remain in normalized config");

    const ipcraft::ConfigSchemaReadResult strictSchema =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("tables"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("regions")},
                    {QStringLiteral("columns"), QJsonArray{
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("base")},
                                    {QStringLiteral("type"), QStringLiteral("int")}}
                    }}
                }
            }},
            {QStringLiteral("documents"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("system")},
                    {QStringLiteral("format"), QStringLiteral("json")}
                }
            }}
        });
    require(strictSchema.ok, "strict table/document schema should parse");
    const ipcraft::ConfigValidationResult strictResult =
        ipcraft::validateConfigBundle(strictSchema.schema, bundle);
    require(!strictResult.ok, "strict normalization should reject unknown table columns");
    require(hasRuleAt(strictResult.diagnostics,
                      QStringLiteral("config.unknown_table_column"),
                      QStringLiteral("$.tables.regions.rows[0].owner")),
            "strict unknown table columns should be diagnosed");
    require(!strictResult.normalized.toJson()
                 .value(QStringLiteral("tables")).toObject()
                 .value(QStringLiteral("regions")).toObject()
                 .value(QStringLiteral("rows")).toArray().first().toObject()
                 .contains(QStringLiteral("owner")),
            "unknown table columns should not remain when preservation is not declared");
    require(!strictResult.normalized.toJson()
                 .value(QStringLiteral("documents")).toObject()
                 .value(QStringLiteral("system")).toObject()
                 .value(QStringLiteral("content")).toObject()
                 .contains(QStringLiteral("vendor.extra")),
            "unknown document fields should not remain when preservation is not declared");

    const ipcraft::ConfigValidationResult missingColumn =
        ipcraft::validateConfigBundle(schemaResult.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("tables"), QJsonObject{
                                              {QStringLiteral("regions"), QJsonObject{
                                                  {QStringLiteral("rows"), QJsonArray{
                                                      QJsonObject{{QStringLiteral("base"), 0}}
                                                  }}
                                              }}
                                          }}
                                      }));
    require(!missingColumn.ok, "missing required table column should fail");
    require(hasRule(missingColumn.diagnostics,
                    QStringLiteral("config.table_column_missing")),
            "missing table column should be diagnosed");

    const ipcraft::ConfigSchemaReadResult shapeSchema =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("tables"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("regions")},
                    {QStringLiteral("columns"), QJsonArray{
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("base")},
                                    {QStringLiteral("type"), QStringLiteral("int")}}
                    }}
                }
            }}
        });
    require(shapeSchema.ok, "table shape schema should parse");
    const ipcraft::ConfigValidationResult badRows =
        ipcraft::validateConfigBundle(shapeSchema.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("tables"), QJsonObject{
                                              {QStringLiteral("regions"), QJsonObject{
                                                  {QStringLiteral("rows"), QJsonArray{
                                                      QStringLiteral("not-an-object")
                                                  }}
                                              }}
                                          }}
                                      }));
    require(!badRows.ok, "non-object table rows should fail");
    require(hasRuleAt(badRows.diagnostics,
                      QStringLiteral("config.type_mismatch"),
                      QStringLiteral("$.tables.regions.rows[0]")),
            "non-object table row should be diagnosed at stable row path");
}

void testPathParametersRejectTraversal() {
    QTemporaryDir root;
    require(root.isValid(), "temporary root should be valid");
    const ipcraft::ConfigSchema schema = readSchemaOrThrow(schemaWithParameters(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("cfg")},
                    {QStringLiteral("type"), QStringLiteral("path")},
                    {QStringLiteral("required"), true}}
    }));

    const ipcraft::ConfigValidationOptions options{.projectRootPath = root.path()};
    ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schema,
                                      ipcraft::ConfigBundle::fromJson(
                                          bundleWithParameters(QJsonObject{
                                              {QStringLiteral("cfg"), QStringLiteral("../escape.yml")}
                                          })),
                                      options);
    require(!result.ok, "path traversal should fail");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.path_escape"),
                      QStringLiteral("$.parameters.cfg")),
            "path traversal should emit config.path_escape");

    result = ipcraft::validateConfigBundle(schema,
                                           ipcraft::ConfigBundle::fromJson(
                                               bundleWithParameters(QJsonObject{
                                                   {QStringLiteral("cfg"), QDir(root.path()).filePath(QStringLiteral("absolute.yml"))}
                                               })),
                                           options);
    require(!result.ok, "absolute paths should fail");
    require(hasRule(result.diagnostics, QStringLiteral("config.path_escape")),
            "absolute path should emit config.path_escape");
}

void testDiagnosticPathsUseStableEscapingForArbitraryIds() {
    const ipcraft::ConfigSchema schema = readSchemaOrThrow(schemaWithParameters(QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("foo.bar")},
                    {QStringLiteral("type"), QStringLiteral("int")},
                    {QStringLiteral("required"), true}}
    }));

    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schema, ipcraft::ConfigBundle{});
    require(!result.ok, "missing parameter with dotted id should fail");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.required_missing"),
                      QStringLiteral("$.parameters[\"foo.bar\"]")),
            "diagnostic path should escape arbitrary parameter ids");
}

void testDocumentFormatAndFileExtensionValidation() {
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("documents"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("raw_cfg")},
                            {QStringLiteral("format"), QStringLiteral("raw")}}
            }},
            {QStringLiteral("files"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("constraints")},
                            {QStringLiteral("allowed_extensions"), QJsonArray{QStringLiteral(".xdc")}}}
            }}
        });
    require(schemaResult.ok, "document/file schema should parse");

    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("documents"), QJsonObject{
                                              {QStringLiteral("raw_cfg"), QJsonObject{
                                                  {QStringLiteral("format"), QStringLiteral("json")},
                                                  {QStringLiteral("content"), QJsonObject{}}
                                              }}
                                          }},
                                          {QStringLiteral("files"), QJsonObject{
                                              {QStringLiteral("constraints"), QJsonObject{
                                                  {QStringLiteral("path"), QStringLiteral("constraints/top.sdc")}
                                              }}
                                          }}
                                      }));
    require(!result.ok, "document format and file extension mismatches should fail");
    require(hasRule(result.diagnostics,
                    QStringLiteral("config.document_format_invalid")),
            "document format mismatch should be diagnosed");
    require(hasRule(result.diagnostics,
                    QStringLiteral("config.file_extension_invalid")),
            "file extension mismatch should be diagnosed");
}

void testFileInputAllowedAliasIsValidated() {
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("files"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("constraints")},
                            {QStringLiteral("allowed"), QJsonArray{QStringLiteral(".xdc")}}}
            }}
        });
    require(schemaResult.ok, "file allowed alias schema should parse");

    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("files"), QJsonObject{
                                              {QStringLiteral("constraints"), QJsonObject{
                                                  {QStringLiteral("path"), QStringLiteral("constraints/top.sdc")}
                                              }}
                                          }}
                                      }));
    require(!result.ok, "file allowed alias should enforce extension");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.file_extension_invalid"),
                      QStringLiteral("$.files.constraints.path")),
            "invalid extension from allowed alias should be diagnosed");
}

void testFileInputPathsRejectTraversal() {
    QTemporaryDir root;
    require(root.isValid(), "temporary root should be valid");
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("files"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("constraints")},
                            {QStringLiteral("required"), true},
                            {QStringLiteral("allowed_extensions"), QJsonArray{QStringLiteral(".xdc")}}}
            }}
        });
    require(schemaResult.ok, "file input schema should parse");

    const ipcraft::ConfigValidationOptions options{.projectRootPath = root.path()};
    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("files"), QJsonObject{
                                              {QStringLiteral("constraints"), QJsonObject{
                                                  {QStringLiteral("path"), QStringLiteral("../top.xdc")}
                                              }}
                                          }}
                                      }),
                                      options);
    require(!result.ok, "file input traversal should fail");
    require(hasRuleAt(result.diagnostics,
                      QStringLiteral("config.path_escape"),
                      QStringLiteral("$.files.constraints.path")),
            "file input traversal should emit config.path_escape");
}

void testFileInputNormalizationDropsUnknownFields() {
    QTemporaryDir root;
    require(root.isValid(), "temporary root should be valid");
    const ipcraft::ConfigSchemaReadResult schemaResult =
        ipcraft::ConfigSchema::fromJson(QJsonObject{
            {QStringLiteral("files"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("constraints")},
                            {QStringLiteral("allowed_extensions"), QJsonArray{QStringLiteral(".xdc")}}}
            }}
        });
    require(schemaResult.ok, "file input schema should parse");

    const ipcraft::ConfigValidationOptions options{.projectRootPath = root.path()};
    const ipcraft::ConfigValidationResult result =
        ipcraft::validateConfigBundle(schemaResult.schema,
                                      ipcraft::ConfigBundle::fromJson(QJsonObject{
                                          {QStringLiteral("files"), QJsonObject{
                                              {QStringLiteral("constraints"), QJsonObject{
                                                  {QStringLiteral("path"), QStringLiteral("constraints/top.xdc")},
                                                  {QStringLiteral("vendor.extra"), true}
                                              }}
                                          }}
                                      }),
                                      options);
    require(result.ok, "valid file input should pass");
    const QJsonObject normalizedFile =
        result.normalized.toJson()
            .value(QStringLiteral("files")).toObject()
            .value(QStringLiteral("constraints")).toObject();
    require(normalizedFile.value(QStringLiteral("path")).toString() ==
                QStringLiteral("constraints/top.xdc"),
            "file input path should remain in normalized config");
    require(!normalizedFile.contains(QStringLiteral("vendor.extra")),
            "file input unknown fields should not remain in normalized config");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testParameterTypesMapToJsonValues();
        testEnumDeclarationMustChooseStringOrInt64();
        testSimpleExpressionsEvaluateWithoutScripts();
        testExpressionRejectsFileProcessNetworkAndEnvironmentAccess();
        testLiteralBooleanExpressionsAreAccepted();
        testUnknownConfigEntriesAreRejected();
        testTableAndDocumentUnknownFieldsPreserveWhenDeclared();
        testPathParametersRejectTraversal();
        testDiagnosticPathsUseStableEscapingForArbitraryIds();
        testDocumentFormatAndFileExtensionValidation();
        testFileInputAllowedAliasIsValidated();
        testFileInputPathsRejectTraversal();
        testFileInputNormalizationDropsUnknownFields();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "ipcraft_config_validation_test passed\n";
    return 0;
}
