#include "schema/json_schema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>

namespace {

using finepaper::json_schema::CompileResult;
using finepaper::json_schema::CompileStatus;
using finepaper::json_schema::CompiledSchema;
using finepaper::json_schema::Issue;
using finepaper::json_schema::Limits;
using finepaper::json_schema::ValidationResult;

int failures = 0;

void check(bool condition, const QString& description) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << description << Qt::endl;
}

bool hasIssue(const QVector<Issue>& issues, const QString& code) {
    return std::any_of(
        issues.cbegin(), issues.cend(), [&](const Issue& issue) {
            return issue.code == code;
        });
}

const Issue* issueWithCode(const QVector<Issue>& issues,
                           const QString& code) {
    const auto found = std::find_if(
        issues.cbegin(), issues.cend(), [&](const Issue& issue) {
            return issue.code == code;
        });
    return found == issues.cend() ? nullptr : &*found;
}

std::shared_ptr<const CompiledSchema> readySchema(
    const QJsonObject& document,
    const QString& description,
    Limits limits = {}) {
    CompileResult result = finepaper::json_schema::compile(document, limits);
    check(result.status == CompileStatus::Ready && result.schema,
          description + QStringLiteral(" compiles"));
    if (result.status != CompileStatus::Ready) {
        for (const Issue& issue : result.issues) {
            QTextStream(stderr)
                << "  " << issue.code << " " << issue.schemaPointer
                << ": " << issue.message << Qt::endl;
        }
    }
    return result.schema;
}

QJsonObject loadBundledPowerSchema() {
    const QString relative = QStringLiteral(
        "packages/finepaper-noc-v3/runtime/power-intent.schema.json");
    const QStringList candidates = {
        QDir::current().absoluteFilePath(relative),
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../../../../") + relative)
    };
    for (const QString& candidate : candidates) {
        QFile file(QDir::cleanPath(candidate));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(
            file.readAll(), &error);
        if (error.error == QJsonParseError::NoError
            && document.isObject()) {
            return document.object();
        }
    }
    check(false, QStringLiteral("bundled Power schema can be loaded"));
    return {};
}

QJsonObject minimalPowerIntent() {
    const QJsonObject supplyState{
        {QStringLiteral("id"), QStringLiteral("on")},
        {QStringLiteral("condition"), QStringLiteral("full-on")},
        {QStringLiteral("voltageMv"), 900}};
    const QJsonObject supply{
        {QStringLiteral("id"), QStringLiteral("vdd")},
        {QStringLiteral("kind"), QStringLiteral("power")},
        {QStringLiteral("exposure"), QStringLiteral("external-port")},
        {QStringLiteral("port"), QStringLiteral("VDD")},
        {QStringLiteral("net"), QStringLiteral("VDD")},
        {QStringLiteral("states"), QJsonArray{supplyState}}};
    const QJsonObject domainState{
        {QStringLiteral("id"), QStringLiteral("on")},
        {QStringLiteral("powerState"), QStringLiteral("on")},
        {QStringLiteral("groundState"), QStringLiteral("on")},
        {QStringLiteral("behavior"), QStringLiteral("operational")}};
    const QJsonObject domain{
        {QStringLiteral("domain"), QStringLiteral("pd-main")},
        {QStringLiteral("primaryPower"), QStringLiteral("vdd")},
        {QStringLiteral("primaryGround"), QStringLiteral("vss")},
        {QStringLiteral("mode"), QStringLiteral("always-on")},
        {QStringLiteral("defaultState"), QStringLiteral("on")},
        {QStringLiteral("states"), QJsonArray{domainState}}};
    const QJsonObject systemState{
        {QStringLiteral("id"), QStringLiteral("run")},
        {QStringLiteral("domainStates"), QJsonArray{}}};

    return QJsonObject{
        {QStringLiteral("format"),
         QStringLiteral("finepaper.noc-power-intent")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("supplies"), QJsonArray{supply}},
        {QStringLiteral("controls"), QJsonArray{}},
        {QStringLiteral("domains"), QJsonArray{domain}},
        {QStringLiteral("defaultSystemState"), QStringLiteral("run")},
        {QStringLiteral("systemStates"), QJsonArray{systemState}}
    };
}

void bundledSchemaAndPatternProfileWork() {
    const QJsonObject document = loadBundledPowerSchema();
    const auto schema = readySchema(
        document, QStringLiteral("bundled Power schema"));
    if (!schema) {
        return;
    }

    const ValidationResult valid = finepaper::json_schema::validate(
        *schema, minimalPowerIntent());
    check(valid.success && valid.issues.isEmpty(),
          QStringLiteral("minimal Power intent satisfies bundled schema"));

    const QJsonValue packageDefault = document.value(
        QStringLiteral("default"));
    const ValidationResult defaultValidation = finepaper::json_schema::validate(
        *schema, packageDefault);
    check(packageDefault.isObject() && defaultValidation.success
              && packageDefault.toObject().value(
                     QStringLiteral("format"))
                     == QStringLiteral("finepaper.noc-power-intent"),
          QStringLiteral(
              "bundled Power schema owns a structurally valid editor default"));

    QJsonObject whitespaceId = minimalPowerIntent();
    QJsonArray supplies = whitespaceId.value(
        QStringLiteral("supplies")).toArray();
    QJsonObject supply = supplies.at(0).toObject();
    supply.insert(QStringLiteral("id"), QStringLiteral("   "));
    supplies[0] = supply;
    whitespaceId.insert(QStringLiteral("supplies"), supplies);
    const ValidationResult whitespace = finepaper::json_schema::validate(
        *schema, whitespaceId);
    check(!whitespace.success
              && hasIssue(whitespace.issues,
                          QStringLiteral("json_schema.pattern")),
          QStringLiteral("ECMA whitespace lookahead is enforced"));

    QJsonObject controlId = minimalPowerIntent();
    supplies = controlId.value(QStringLiteral("supplies")).toArray();
    supply = supplies.at(0).toObject();
    supply.insert(
        QStringLiteral("id"), QStringLiteral("bad\nidentifier"));
    supplies[0] = supply;
    controlId.insert(QStringLiteral("supplies"), supplies);
    const ValidationResult control = finepaper::json_schema::validate(
        *schema, controlId);
    check(!control.success
              && hasIssue(control.issues,
                          QStringLiteral("json_schema.pattern")),
          QStringLiteral("translated ECMA Unicode control ranges are enforced"));

    QJsonObject trailingControlId = minimalPowerIntent();
    supplies = trailingControlId.value(QStringLiteral("supplies")).toArray();
    supply = supplies.at(0).toObject();
    supply.insert(QStringLiteral("id"), QStringLiteral("bad\n"));
    supplies[0] = supply;
    trailingControlId.insert(QStringLiteral("supplies"), supplies);
    const ValidationResult trailingControl = finepaper::json_schema::validate(
        *schema, trailingControlId);
    check(!trailingControl.success
              && hasIssue(trailingControl.issues,
                          QStringLiteral("json_schema.pattern")),
          QStringLiteral("ECMA $ does not stop before a final line feed"));

    QJsonObject unicodeWhitespaceId = minimalPowerIntent();
    supplies = unicodeWhitespaceId.value(QStringLiteral("supplies")).toArray();
    supply = supplies.at(0).toObject();
    supply.insert(QStringLiteral("id"), QString(QChar(0x00A0)));
    supplies[0] = supply;
    unicodeWhitespaceId.insert(QStringLiteral("supplies"), supplies);
    const ValidationResult unicodeWhitespace =
        finepaper::json_schema::validate(*schema, unicodeWhitespaceId);
    check(!unicodeWhitespace.success
              && hasIssue(unicodeWhitespace.issues,
                          QStringLiteral("json_schema.pattern")),
          QStringLiteral("ECMA Unicode whitespace remains whitespace"));
}

void ecmaPatternDifferencesAreTranslated() {
    const auto wordSchema = readySchema(
        QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("pattern"), QStringLiteral("^\\w+$")}},
        QStringLiteral("ECMA word schema"));
    if (!wordSchema) {
        return;
    }
    check(finepaper::json_schema::validate(
              *wordSchema, QStringLiteral("router_01"))
              .success,
          QStringLiteral("ECMA ASCII word characters are accepted"));
    check(!finepaper::json_schema::validate(
               *wordSchema, QString::fromUtf8("é"))
               .success,
          QStringLiteral("ECMA word characters do not expand under PCRE UCP"));

    const auto classSchema = readySchema(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^[^^].$")}},
        QStringLiteral("character-class state schema"));
    if (classSchema) {
        check(finepaper::json_schema::validate(
                  *classSchema, QStringLiteral("ax"))
                  .success
                  && !finepaper::json_schema::validate(
                      *classSchema,
                      QStringLiteral("a") + QChar(0x2028))
                      .success,
              QStringLiteral(
                  "a literal caret closes its class before ECMA dot translation"));
    }

    const auto anyCharacterSchema = readySchema(
        QJsonObject{{QStringLiteral("pattern"), QStringLiteral("^[^]$")}},
        QStringLiteral("negated empty character-class schema"));
    if (anyCharacterSchema) {
        check(finepaper::json_schema::validate(
                  *anyCharacterSchema, QStringLiteral("\n"))
                  .success,
              QStringLiteral("ECMA [^] continues to match every character"));
    }

    const CompileResult repeatedGroup = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^(a+)+$")}});
    const CompileResult unanchoredRepetition =
        finepaper::json_schema::compile(
            QJsonObject{{QStringLiteral("pattern"),
                         QStringLiteral("a*Z")}});
    const CompileResult optionalChain = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^a?a?a?a?a?aaaaa$")}});
    const CompileResult separatedGroups = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^(a*)(a*)b$")}});
    const CompileResult pcreExtension = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^(?>a+)$")}});
    const CompileResult pcreControlVerb = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^(*ACCEPT)impossible$")}});
    const CompileResult pcreEscape = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^\\Kimpossible$")}});
    const CompileResult pcrePosixClass = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^[[:digit:]]+$")}});
    const CompileResult pcreBracedHex = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("pattern"),
                     QStringLiteral("^\\x{41}$")}});
    const CompileResult malformedRepetition =
        finepaper::json_schema::compile(
            QJsonObject{{QStringLiteral("pattern"),
                         QStringLiteral("a**")}});
    const CompileResult fixedLazyQuantifier =
        finepaper::json_schema::compile(
            QJsonObject{{QStringLiteral("pattern"),
                         QStringLiteral("^a{2}?$")}});
    const CompileResult fixedPossessiveQuantifier =
        finepaper::json_schema::compile(
            QJsonObject{{QStringLiteral("pattern"),
                         QStringLiteral("^a{2}+$")}});
    const CompileResult ambiguousGroupChain =
        finepaper::json_schema::compile(
            QJsonObject{{QStringLiteral("pattern"),
                         QStringLiteral("^(a|aa)(a|aa)X$")}});
    check(repeatedGroup.status == CompileStatus::Unsupported
              && unanchoredRepetition.status == CompileStatus::Unsupported
              && optionalChain.status == CompileStatus::Unsupported
              && separatedGroups.status == CompileStatus::Unsupported
              && pcreExtension.status == CompileStatus::Unsupported
              && pcreControlVerb.status == CompileStatus::Unsupported
              && pcreEscape.status == CompileStatus::Unsupported
              && pcrePosixClass.status == CompileStatus::Unsupported
              && pcreBracedHex.status == CompileStatus::Invalid
              && malformedRepetition.status == CompileStatus::Invalid
              && fixedLazyQuantifier.status == CompileStatus::Ready
              && fixedPossessiveQuantifier.status
                  == CompileStatus::Unsupported
              && ambiguousGroupChain.status == CompileStatus::Unsupported
              && hasIssue(
                  repeatedGroup.issues,
                  QStringLiteral(
                      "json_schema.unsafe_pattern_unsupported")),
          QStringLiteral("backtracking-prone patterns fail closed"));
}

QJsonObject conditionalProfileSchema() {
    const QJsonObject itemSchema{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("pattern"),
         QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")}};
    const QJsonObject properties{
        {QStringLiteral("kind"), QJsonObject{
            {QStringLiteral("enum"), QJsonArray{
                QStringLiteral("a"), QStringLiteral("b")}}}},
        {QStringLiteral("version"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("minimum"), 1},
            {QStringLiteral("exclusiveMinimum"), 0},
            {QStringLiteral("const"), 1}}},
        {QStringLiteral("values"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("minItems"), 1},
            {QStringLiteral("maxItems"), 2},
            {QStringLiteral("uniqueItems"), true},
            {QStringLiteral("items"), itemSchema}}},
        {QStringLiteral("special"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")}}}};
    const QJsonObject condition{
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("kind"), QJsonObject{
                {QStringLiteral("const"), QStringLiteral("a")}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("kind")}}};
    const QJsonObject branch{
        {QStringLiteral("if"), condition},
        {QStringLiteral("then"), QJsonObject{
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("special")}}}},
        {QStringLiteral("else"), QJsonObject{
            {QStringLiteral("not"), QJsonObject{
                {QStringLiteral("required"),
                 QJsonArray{QStringLiteral("special")}}}}}}};

    return QJsonObject{
        {QStringLiteral("$schema"),
         QStringLiteral("https://json-schema.org/draft/2020-12/schema")},
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("required"), QJsonArray{
            QStringLiteral("kind"), QStringLiteral("version"),
            QStringLiteral("values")}},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("allOf"), QJsonArray{branch}}
    };
}

void assertionsConditionsAndNotWork() {
    const auto schema = readySchema(
        conditionalProfileSchema(), QStringLiteral("conditional profile schema"));
    if (!schema) {
        return;
    }
    const QJsonObject a{
        {QStringLiteral("kind"), QStringLiteral("a")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("values"), QJsonArray{QStringLiteral("alpha")}},
        {QStringLiteral("special"), true}};
    check(finepaper::json_schema::validate(*schema, a).success,
          QStringLiteral("then branch accepts its required property"));

    QJsonObject missing = a;
    missing.remove(QStringLiteral("special"));
    const ValidationResult missingResult = finepaper::json_schema::validate(
        *schema, missing);
    const Issue* required = issueWithCode(
        missingResult.issues, QStringLiteral("json_schema.required"));
    check(!missingResult.success && required
              && required->instancePointer == QStringLiteral("/special")
              && required->keyword == QStringLiteral("required"),
          QStringLiteral("then failure has deterministic instance/schema metadata"));

    QJsonObject b = a;
    b.insert(QStringLiteral("kind"), QStringLiteral("b"));
    const ValidationResult forbidden = finepaper::json_schema::validate(
        *schema, b);
    check(!forbidden.success
              && hasIssue(forbidden.issues, QStringLiteral("json_schema.not"))
              && !hasIssue(forbidden.issues,
                           QStringLiteral("json_schema.const")),
          QStringLiteral("else/not applies without leaking if probe diagnostics"));

    b.remove(QStringLiteral("special"));
    b.insert(QStringLiteral("values"),
             QJsonArray{QStringLiteral("same"), QStringLiteral("same")});
    const ValidationResult duplicate = finepaper::json_schema::validate(
        *schema, b);
    const Issue* unique = issueWithCode(
        duplicate.issues, QStringLiteral("json_schema.unique_items"));
    check(!duplicate.success && unique
              && unique->instancePointer == QStringLiteral("/values/1"),
          QStringLiteral("uniqueItems reports the later duplicate deterministically"));

    b.insert(QStringLiteral("values"), QJsonArray{
        QStringLiteral("ok"), QStringLiteral("also_ok"),
        QStringLiteral("too_many")});
    b.insert(QStringLiteral("extra"), true);
    const ValidationResult bounds = finepaper::json_schema::validate(*schema, b);
    check(!bounds.success
              && hasIssue(bounds.issues, QStringLiteral("json_schema.max_items"))
              && hasIssue(bounds.issues,
                          QStringLiteral("json_schema.additional_property")),
          QStringLiteral("array bounds and closed object properties are enforced"));
}

void localReferencesAndSiblingAssertionsWork() {
    const QJsonObject schemaDocument{
        {QStringLiteral("$defs"), QJsonObject{
            {QStringLiteral("a/b~c"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("space key"), QJsonObject{
                {QStringLiteral("const"), QStringLiteral("space")}}}}},
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("escaped"), QJsonObject{
                {QStringLiteral("$ref"),
                 QStringLiteral("#/$defs/a~1b~0c")},
                {QStringLiteral("const"), QStringLiteral("expected")}}},
            {QStringLiteral("percent"), QJsonObject{
                {QStringLiteral("$ref"),
                 QStringLiteral("#/$defs/space%20key")}}}}}};
    const auto schema = readySchema(
        schemaDocument, QStringLiteral("local-reference schema"));
    if (!schema) {
        return;
    }
    check(finepaper::json_schema::validate(
              *schema,
              QJsonObject{
                  {QStringLiteral("escaped"), QStringLiteral("expected")},
                  {QStringLiteral("percent"), QStringLiteral("space")}})
              .success,
          QStringLiteral("escaped and percent-encoded local pointers resolve"));

    const ValidationResult sibling = finepaper::json_schema::validate(
        *schema,
        QJsonObject{{QStringLiteral("escaped"), QStringLiteral("other")}});
    check(!sibling.success
              && hasIssue(sibling.issues, QStringLiteral("json_schema.const")),
          QStringLiteral("Draft 2020-12 $ref siblings remain active"));

    const CompileResult unresolved = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("$ref"), QStringLiteral("#/$defs/missing")}});
    check(unresolved.status == CompileStatus::Invalid
              && hasIssue(unresolved.issues,
                          QStringLiteral("json_schema.unresolved_ref")),
          QStringLiteral("unresolved local references are invalid"));

    const CompileResult external = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("$ref"),
                     QStringLiteral("other.json#/value")}});
    check(external.status == CompileStatus::Unsupported
              && hasIssue(external.issues,
                          QStringLiteral("json_schema.external_ref_unsupported")),
          QStringLiteral("external references fail closed as unsupported"));

    const auto booleanTarget = readySchema(
        QJsonObject{
            {QStringLiteral("$ref"),
             QStringLiteral("#/additionalProperties")},
            {QStringLiteral("additionalProperties"), false}},
        QStringLiteral("boolean local-reference target"));
    check(booleanTarget
              && !finepaper::json_schema::validate(
                      *booleanTarget, QJsonValue(1)).success,
          QStringLiteral("boolean schema keyword values remain addressable by $ref"));
}

void unsupportedKeywordsAndSchemaShapeFailClosed() {
    const CompileResult unknown = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("anyOf"), QJsonArray{QJsonObject{}}}});
    const Issue* unsupported = issueWithCode(
        unknown.issues, QStringLiteral("json_schema.unsupported_keyword"));
    check(unknown.status == CompileStatus::Unsupported && unsupported
              && unsupported->schemaPointer == QStringLiteral("/anyOf")
              && unsupported->keyword == QStringLiteral("anyOf"),
          QStringLiteral("unknown keyword is unsupported with stable metadata"));

    const CompileResult invalid = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("type"), 7}});
    check(invalid.status == CompileStatus::Invalid
              && hasIssue(invalid.issues,
                          QStringLiteral("json_schema.invalid_type")),
          QStringLiteral("malformed supported keyword is invalid"));

    const CompileResult dataObject = finepaper::json_schema::compile(
        QJsonObject{
            {QStringLiteral("const"), QJsonObject{
                {QStringLiteral("notAKeyword"), true},
                {QStringLiteral("$ref"), QStringLiteral("external.json")}}},
            {QStringLiteral("default"), QJsonObject{
                {QStringLiteral("alsoData"), true}}}});
    check(dataObject.status == CompileStatus::Ready && dataObject.schema,
          QStringLiteral("const/default objects are data, not schema nodes"));

    const auto emptySchema = readySchema(
        QJsonObject{}, QStringLiteral("empty JSON schema"));
    if (emptySchema) {
        const ValidationResult undefined = finepaper::json_schema::validate(
            *emptySchema, QJsonValue(QJsonValue::Undefined));
        check(!undefined.success
                  && hasIssue(
                      undefined.issues,
                      QStringLiteral("json_schema.undefined_instance")),
              QStringLiteral("Undefined is rejected at the public API boundary"));
    }
}

void recursiveSchemasAndBudgetsFailSafely() {
    const CompileResult ignoredThen = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("then"), QJsonObject{
            {QStringLiteral("$ref"), QStringLiteral("#")}}}});
    check(ignoredThen.status == CompileStatus::Ready && ignoredThen.schema
              && finepaper::json_schema::validate(
                     *ignoredThen.schema, QJsonValue(1)).success,
          QStringLiteral("then without if is a no-op, not a false cycle"));

    const CompileResult zeroCycle = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("allOf"), QJsonArray{
            QJsonObject{{QStringLiteral("$ref"), QStringLiteral("#")}}}}});
    check(zeroCycle.status == CompileStatus::Unsupported
              && hasIssue(
                  zeroCycle.issues,
                  QStringLiteral(
                      "json_schema.zero_progress_cycle_unsupported")),
          QStringLiteral("non-progressing reference/applicator cycle is rejected"));

    const auto recursive = readySchema(
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("next"), QJsonObject{
                    {QStringLiteral("$ref"), QStringLiteral("#")}}}}}},
        QStringLiteral("instance-descending recursive schema"));
    if (recursive) {
        check(finepaper::json_schema::validate(
                  *recursive,
                  QJsonObject{{QStringLiteral("next"), QJsonObject{
                      {QStringLiteral("next"), QJsonObject{}}}}})
                  .success,
              QStringLiteral("recursive schema may descend through instance data"));

        Limits depthLimit;
        depthLimit.maximumInstanceDepth = 1;
        const ValidationResult depth = finepaper::json_schema::validate(
            *recursive,
            QJsonObject{{QStringLiteral("next"), QJsonObject{
                {QStringLiteral("next"), QJsonObject{}}}}},
            depthLimit);
        check(!depth.success
                  && hasIssue(depth.issues,
                              QStringLiteral(
                                  "json_schema.instance_depth_exceeded")),
              QStringLiteral("recursive validation is depth bounded"));
    }

    const auto conditional = readySchema(
        QJsonObject{
            {QStringLiteral("if"), QJsonObject{
                {QStringLiteral("const"), 1}}},
            {QStringLiteral("then"), false},
            {QStringLiteral("else"), true}},
        QStringLiteral("budget conditional schema"));
    if (conditional) {
        Limits budget;
        budget.maximumEvaluationSteps = 1;
        const ValidationResult exhausted = finepaper::json_schema::validate(
            *conditional, QJsonValue(2), budget);
        check(!exhausted.success
                  && hasIssue(
                      exhausted.issues,
                      QStringLiteral(
                          "json_schema.validation_budget_exceeded")),
              QStringLiteral("budget abort is not mistaken for a false if probe"));
    }

    QJsonObject definitions;
    constexpr int referenceChainLength = 64;
    for (int index = 0; index < referenceChainLength; ++index) {
        const QString id = QStringLiteral("n%1").arg(index);
        definitions.insert(
            id,
            index + 1 == referenceChainLength
                ? QJsonValue(true)
                : QJsonValue(QJsonObject{{
                      QStringLiteral("$ref"),
                      QStringLiteral("#/$defs/n%1").arg(index + 1)}}));
    }
    const auto referenceChain = readySchema(
        QJsonObject{{QStringLiteral("$defs"), definitions},
                    {QStringLiteral("$ref"),
                     QStringLiteral("#/$defs/n0")}},
        QStringLiteral("acyclic reference-chain schema"));
    if (referenceChain) {
        Limits evaluationDepth;
        evaluationDepth.maximumEvaluationDepth = 16;
        const ValidationResult tooDeep = finepaper::json_schema::validate(
            *referenceChain, QJsonObject{}, evaluationDepth);
        check(!tooDeep.success
                  && hasIssue(
                      tooDeep.issues,
                      QStringLiteral(
                          "json_schema.evaluation_depth_exceeded")),
              QStringLiteral("acyclic schema recursion is stack-depth bounded"));
    }
}

void exactNumbersAndSchemaDataBudgetsWork() {
    QJsonParseError boundaryError;
    const QJsonDocument boundaryDocument = QJsonDocument::fromJson(
        QByteArrayLiteral(
            R"json({"exclusiveMinimum":9007199254740992})json"),
        &boundaryError);
    const bool boundaryParsed = boundaryError.error
            == QJsonParseError::NoError
        && boundaryDocument.isObject();
    check(boundaryParsed,
          QStringLiteral("exact boundary fixture parses as a JSON object"));
    if (!boundaryParsed) {
        return;
    }

    QJsonParseError instanceError;
    const QJsonDocument instanceDocument = QJsonDocument::fromJson(
        QByteArrayLiteral(R"json([9007199254740993])json"),
        &instanceError);
    const bool instanceParsed = instanceError.error
            == QJsonParseError::NoError
        && instanceDocument.isArray();
    check(instanceParsed,
          QStringLiteral("exact integer fixture parses as a JSON array"));
    if (!instanceParsed) {
        return;
    }
    const auto boundarySchema = readySchema(
        boundaryDocument.object(), QStringLiteral("exact integer boundary schema"));
    if (boundarySchema) {
        const QJsonValue largeInteger = instanceDocument.array().at(0);
        check(largeInteger.toInteger() == 9'007'199'254'740'993LL,
              QStringLiteral("Qt retains the parsed 64-bit integer exactly"));
        check(finepaper::json_schema::validate(
                  *boundarySchema, largeInteger)
                  .success,
              QStringLiteral(
                  "exclusiveMinimum compares 64-bit integers without double rounding"));
    }

    Limits enumLimit;
    enumLimit.maximumEnumValues = 2;
    const CompileResult oversizedEnum = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("enum"), QJsonArray{1, 2, 3}}},
        enumLimit);
    check(oversizedEnum.status == CompileStatus::Unsupported
              && hasIssue(
                  oversizedEnum.issues,
                  QStringLiteral("json_schema.enum_value_budget_exceeded")),
          QStringLiteral("enum cardinality is compile-budgeted"));

    Limits dataLimit;
    dataLimit.maximumSchemaDataNodes = 2;
    const CompileResult oversizedConst = finepaper::json_schema::compile(
        QJsonObject{{QStringLiteral("const"), QJsonArray{1, 2}}},
        dataLimit);
    check(oversizedConst.status == CompileStatus::Unsupported
              && hasIssue(
                  oversizedConst.issues,
                  QStringLiteral("json_schema.schema_data_budget_exceeded")),
          QStringLiteral("nested const/enum data is node-budgeted"));

    const auto enumSchema = readySchema(
        QJsonObject{{QStringLiteral("enum"), QJsonArray{1, 2, 3}}},
        QStringLiteral("enum evaluation budget schema"));
    if (enumSchema) {
        Limits evaluationLimit;
        evaluationLimit.maximumEvaluationSteps = 2;
        const ValidationResult exhausted = finepaper::json_schema::validate(
            *enumSchema, QJsonValue(3), evaluationLimit);
        check(!exhausted.success
                  && hasIssue(
                      exhausted.issues,
                      QStringLiteral(
                          "json_schema.validation_budget_exceeded")),
              QStringLiteral("each enum comparison consumes evaluation budget"));
    }

    const auto constSchema = readySchema(
        QJsonObject{{QStringLiteral("const"),
                     QJsonArray{QJsonArray{1, 2, 3}}}},
        QStringLiteral("deep equality budget schema"));
    if (constSchema) {
        Limits equalityLimit;
        equalityLimit.maximumEvaluationSteps = 2;
        const ValidationResult exhausted = finepaper::json_schema::validate(
            *constSchema, QJsonArray{QJsonArray{1, 2, 3}}, equalityLimit);
        check(!exhausted.success
                  && hasIssue(
                      exhausted.issues,
                      QStringLiteral(
                          "json_schema.validation_budget_exceeded")),
              QStringLiteral("deep const equality consumes per-node budget"));
    }
}

void instanceWidthsAndUniqueItemsAreBounded() {
    const auto uniqueSchema = readySchema(
        QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                    {QStringLiteral("uniqueItems"), true}},
        QStringLiteral("linear uniqueItems schema"));
    if (!uniqueSchema) {
        return;
    }

    QJsonArray uniqueValues;
    for (int value = 0; value < 2'000; ++value) {
        uniqueValues.append(value);
    }
    Limits linearBudget;
    linearBudget.maximumEvaluationSteps = 5'000;
    check(finepaper::json_schema::validate(
              *uniqueSchema, uniqueValues, linearBudget)
              .success,
          QStringLiteral("uniqueItems scales linearly for distinct values"));

    const QJsonObject first{{QStringLiteral("a"), 1},
                            {QStringLiteral("b"), 2}};
    QJsonObject second;
    second.insert(QStringLiteral("b"), 2);
    second.insert(QStringLiteral("a"), 1);
    const ValidationResult objectDuplicate = finepaper::json_schema::validate(
        *uniqueSchema, QJsonArray{first, second});
    const Issue* duplicate = issueWithCode(
        objectDuplicate.issues, QStringLiteral("json_schema.unique_items"));
    check(!objectDuplicate.success && duplicate
              && duplicate->instancePointer == QStringLiteral("/1"),
          QStringLiteral(
              "structurally equal objects hash to the later duplicate path"));

    const ValidationResult numericDuplicate = finepaper::json_schema::validate(
        *uniqueSchema, QJsonArray{QJsonValue(1), QJsonValue(1.0)});
    const Issue* numericIssue = issueWithCode(
        numericDuplicate.issues,
        QStringLiteral("json_schema.unique_items"));
    check(!numericDuplicate.success && numericIssue
              && numericIssue->instancePointer == QStringLiteral("/1"),
          QStringLiteral(
              "mathematically equal integer and real values are duplicates"));

    const auto objectSchema = readySchema(
        QJsonObject{{QStringLiteral("additionalProperties"), false}},
        QStringLiteral("object width schema"));
    if (objectSchema) {
        QJsonObject wideObject;
        for (int index = 0; index < 16; ++index) {
            wideObject.insert(QString::number(index), index);
        }
        Limits widthLimit;
        widthLimit.maximumObjectProperties = 8;
        const ValidationResult tooWide = finepaper::json_schema::validate(
            *objectSchema, wideObject, widthLimit);
        check(!tooWide.success
                  && hasIssue(
                      tooWide.issues,
                      QStringLiteral(
                          "json_schema.instance_object_too_large")),
          QStringLiteral("object width is rejected before key sorting"));

    }

    const auto permissiveSchema = readySchema(
        QJsonObject{}, QStringLiteral("permissive object-key schema"));
    if (permissiveSchema) {
        QJsonObject longKeyObject;
        longKeyObject.insert(QString(16, QLatin1Char('a')), true);
        Limits keyLengthLimit;
        keyLengthLimit.maximumObjectKeyCodeUnits = 8;
        const ValidationResult longKey = finepaper::json_schema::validate(
            *permissiveSchema, longKeyObject, keyLengthLimit);
        check(!longKey.success
                  && hasIssue(
                      longKey.issues,
                      QStringLiteral(
                          "json_schema.instance_object_key_too_large")),
              QStringLiteral(
                  "object key length applies to permissive schemas"));

        QJsonObject excessiveKeys;
        excessiveKeys.insert(QStringLiteral("aaaaaaaaaa0"), true);
        excessiveKeys.insert(QStringLiteral("aaaaaaaaaa1"), true);
        Limits totalKeyLimit;
        totalKeyLimit.maximumObjectKeyCodeUnitsTotal = 16;
        const ValidationResult excessiveKeyUnits =
            finepaper::json_schema::validate(
                *permissiveSchema, excessiveKeys, totalKeyLimit);
        check(!excessiveKeyUnits.success
                  && hasIssue(
                      excessiveKeyUnits.issues,
                      QStringLiteral(
                          "json_schema.instance_object_keys_too_large")),
              QStringLiteral(
                  "aggregate key length applies to permissive schemas"));
    }

    const auto negatedConstSchema = readySchema(
        QJsonObject{
            {QStringLiteral("not"),
             QJsonObject{{QStringLiteral("const"),
                          QJsonObject{{QStringLiteral("outer"), 0}}}}}},
        QStringLiteral("nested key-limit comparison schema"));
    if (negatedConstSchema) {
        const QJsonObject nestedObject{
            {QStringLiteral("outer"),
             QJsonObject{{QString(16, QLatin1Char('k')), true}}}};
        Limits nestedKeyLimit;
        nestedKeyLimit.maximumObjectKeyCodeUnits = 8;
        const ValidationResult nestedKey = finepaper::json_schema::validate(
            *negatedConstSchema, nestedObject, nestedKeyLimit);
        check(!nestedKey.success
                  && hasIssue(
                      nestedKey.issues,
                      QStringLiteral(
                          "json_schema.instance_object_key_too_large")),
              QStringLiteral(
                  "nested equality checks cannot reuse an outer key-limit cache entry"));
    }

    const QString wideParentKey(256, QLatin1Char('p'));
    const auto pointerBudgetSchema = readySchema(
        QJsonObject{
            {QStringLiteral("properties"),
             QJsonObject{
                 {wideParentKey,
                  QJsonObject{
                      {QStringLiteral("additionalProperties"),
                       QJsonObject{}}}}}},
            {QStringLiteral("additionalProperties"), false}},
        QStringLiteral("child-pointer budget schema"));
    if (pointerBudgetSchema) {
        QJsonObject wideChildren;
        for (int index = 0; index < 64; ++index) {
            wideChildren.insert(QString::number(index), QJsonObject{});
        }
        Limits pointerBudget;
        pointerBudget.maximumEvaluationSteps = 200;
        const ValidationResult pointerExpansion =
            finepaper::json_schema::validate(
                *pointerBudgetSchema,
                QJsonObject{{wideParentKey, wideChildren}},
                pointerBudget);
        check(!pointerExpansion.success
                  && hasIssue(
                      pointerExpansion.issues,
                      QStringLiteral(
                          "json_schema.validation_budget_exceeded")),
              QStringLiteral(
                  "wide child paths are budgeted before pointer materialization"));
    }

    const auto declaredOnlySchema = readySchema(
        QJsonObject{
            {QStringLiteral("properties"),
             QJsonObject{{QStringLiteral("a"), QJsonObject{}}}},
            {QStringLiteral("additionalProperties"), false}},
        QStringLiteral("declared-only pointer budget schema"));
    if (declaredOnlySchema) {
        Limits exactDeclaredBudget;
        exactDeclaredBudget.maximumEvaluationSteps = 10;
        check(finepaper::json_schema::validate(
                  *declaredOnlySchema,
                  QJsonObject{{QStringLiteral("a"), true}},
                  exactDeclaredBudget)
                  .success,
              QStringLiteral(
                  "additionalProperties does not charge declared child paths twice"));
    }

    const QString longDefinitionKey(256, QLatin1Char('s'));
    const auto schemaPointerBudgetSchema = readySchema(
        QJsonObject{
            {QStringLiteral("$defs"),
             QJsonObject{
                 {longDefinitionKey,
                  QJsonObject{
                      {QStringLiteral("enum"), QJsonArray{0}}}}}},
            {QStringLiteral("$ref"),
             QStringLiteral("#/$defs/") + longDefinitionKey}},
        QStringLiteral("schema pointer budget schema"));
    if (schemaPointerBudgetSchema) {
        Limits schemaPointerBudget;
        schemaPointerBudget.maximumEvaluationSteps = 3;
        const ValidationResult schemaPointerExpansion =
            finepaper::json_schema::validate(
                *schemaPointerBudgetSchema, 1, schemaPointerBudget);
        check(!schemaPointerExpansion.success
                  && hasIssue(
                      schemaPointerExpansion.issues,
                      QStringLiteral(
                          "json_schema.validation_budget_exceeded")),
              QStringLiteral(
                  "runtime schema keyword pointers consume evaluation budget"));
    }

    const auto stringSchema = readySchema(
        QJsonObject{{QStringLiteral("pattern"), QStringLiteral("^a+$")}},
        QStringLiteral("pattern input limit schema"));
    if (stringSchema) {
        Limits stringLimit;
        stringLimit.maximumStringCodeUnits = 4;
        const ValidationResult tooLong = finepaper::json_schema::validate(
            *stringSchema, QStringLiteral("aaaaaaaa"), stringLimit);
        check(!tooLong.success
                  && hasIssue(
                      tooLong.issues,
                      QStringLiteral(
                          "json_schema.instance_string_too_large")),
              QStringLiteral("regex input length is bounded before matching"));

        Limits patternBudget;
        patternBudget.maximumEvaluationSteps = 4;
        const ValidationResult expensiveMatch =
            finepaper::json_schema::validate(
                *stringSchema, QString(1'024, QLatin1Char('a')), patternBudget);
        check(!expensiveMatch.success
                  && hasIssue(
                      expensiveMatch.issues,
                      QStringLiteral(
                          "json_schema.validation_budget_exceeded")),
              QStringLiteral(
                  "pattern and subject work is budgeted before matching"));
    }
}

void silentConditionsShortCircuitDefinitiveFailure() {
    const auto schema = readySchema(
        QJsonObject{
            {QStringLiteral("if"),
             QJsonObject{{QStringLiteral("uniqueItems"), true}}},
            {QStringLiteral("then"), false},
            {QStringLiteral("else"), true}},
        QStringLiteral("silent condition short-circuit schema"));
    if (!schema) {
        return;
    }

    QJsonArray values;
    values.append(0);
    values.append(0);
    for (int value = 1; value < 1'500; ++value) {
        values.append(value);
    }
    const ValidationResult result = finepaper::json_schema::validate(
        *schema, values);
    check(result.success && result.issues.isEmpty(),
          QStringLiteral(
              "a failed silent if probe selects else before exhausting work budget"));
}

void diagnosticOrderingAndPointerEscapingAreDeterministic() {
    const auto schema = readySchema(
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("a/b~c")}},
            {QStringLiteral("additionalProperties"), false}},
        QStringLiteral("pointer schema"));
    if (!schema) {
        return;
    }
    const QJsonObject instance{
        {QStringLiteral("z/value"), true},
        {QStringLiteral("b~value"), true}};
    const ValidationResult first = finepaper::json_schema::validate(
        *schema, instance);
    const ValidationResult second = finepaper::json_schema::validate(
        *schema, QJsonObject{
            {QStringLiteral("b~value"), true},
            {QStringLiteral("z/value"), true}});
    check(first.issues.size() == 3 && second.issues.size() == 3,
          QStringLiteral("closed pointer schema emits three issues"));
    if (first.issues.size() == 3 && second.issues.size() == 3) {
        check(first.issues.at(0).instancePointer
                  == QStringLiteral("/a~1b~0c")
                  && first.issues.at(1).instancePointer
                      == QStringLiteral("/b~0value")
                  && first.issues.at(2).instancePointer
                      == QStringLiteral("/z~1value"),
              QStringLiteral("instance pointers are escaped and key-sorted"));
        bool same = true;
        for (qsizetype index = 0; index < first.issues.size(); ++index) {
            same = same
                && first.issues.at(index).code == second.issues.at(index).code
                && first.issues.at(index).instancePointer
                    == second.issues.at(index).instancePointer
                && first.issues.at(index).schemaPointer
                    == second.issues.at(index).schemaPointer;
        }
        check(same,
              QStringLiteral("diagnostic order is independent of object insertion order"));
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    bundledSchemaAndPatternProfileWork();
    ecmaPatternDifferencesAreTranslated();
    assertionsConditionsAndNotWork();
    localReferencesAndSiblingAssertionsWork();
    unsupportedKeywordsAndSchemaShapeFailClosed();
    recursiveSchemasAndBudgetsFailSafely();
    exactNumbersAndSchemaDataBudgetsWork();
    instanceWidthsAndUniqueItemsAreBounded();
    silentConditionsShortCircuitDefinitiveFailure();
    diagnosticOrderingAndPointerEscapingAreDeterministic();

    if (failures == 0) {
        QTextStream(stdout) << "JSON schema tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " JSON schema test(s) failed"
                        << Qt::endl;
    return 1;
}
