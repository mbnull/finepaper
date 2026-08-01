#include "application/application.h"
#include "application/design_extension_references.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {

using namespace finepaper;

const QString kSettingsExtensionId = QStringLiteral("test.design.settings");
const QString kMetadataExtensionId = QStringLiteral("test.design.metadata");
const QString kDomainBindingsExtensionId =
    QStringLiteral("test.design.domain-bindings");
const QString kUnsupportedExtensionId =
    QStringLiteral("test.design.future-editor");
const QString kSchemaViolationCode =
    QStringLiteral("design.extension_schema_violation");
const QString kUnknownDomainReferenceCode =
    QStringLiteral("design.extension_unknown_domain_reference");
const QString kDomainReferenceTypeMismatchCode =
    QStringLiteral("design.extension_domain_reference_type_mismatch");
const QString kDomainReferenceInvalidContainerCode =
    QStringLiteral("design.extension_domain_reference_invalid_container");
const QString kDomainReferenceDiagnosticsTruncatedCode = QStringLiteral(
    "design.extension_domain_reference_diagnostics_truncated");

int failures = 0;

void check(bool condition, const QString &message) {
  if (!condition) {
    QTextStream(stderr) << "FAILED: " << message << Qt::endl;
    ++failures;
  }
}

bool sameDesign(const NocDesign &lhs, const NocDesign &rhs) {
  return designToJson(lhs) == designToJson(rhs);
}

const Diagnostic *findDiagnostic(const QVector<Diagnostic> &diagnostics,
                                 const QString &code) {
  const auto diagnostic =
      std::find_if(diagnostics.cbegin(), diagnostics.cend(),
                   [&](const Diagnostic &value) { return value.code == code; });
  return diagnostic == diagnostics.cend() ? nullptr : &(*diagnostic);
}

bool hasDiagnostic(const QVector<Diagnostic> &diagnostics, const QString &code,
                   const QString &path = {}) {
  const auto diagnostic = std::find_if(
      diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic &value) {
        return value.code == code && (path.isEmpty() || value.path == path);
      });
  return diagnostic != diagnostics.cend();
}

void checkAtomicFailure(const DesignResult &result, const NocDesign &original,
                        const QString &code, const QString &path,
                        const QString &message) {
  check(!result.success && sameDesign(result.design, original) &&
            hasDiagnostic(result.diagnostics, code, path),
        message);
}

QJsonObject settingsValue(const QString &mode = QStringLiteral("safe")) {
  return QJsonObject{
      {QStringLiteral("mode"), mode},
      {QStringLiteral("lanes"),
       QJsonArray{
           QJsonObject{{QStringLiteral("id"), QStringLiteral("request")},
                       {QStringLiteral("role"), QStringLiteral("request")}},
           QJsonObject{{QStringLiteral("id"), QStringLiteral("response")},
                       {QStringLiteral("role"), QStringLiteral("response")}}}}};
}

QJsonObject domainBindingsValue(
    const QString &clockDomain = QStringLiteral("clk-main"),
    const QString &securityDomain = QStringLiteral("sec-main"),
    const QString &primaryDomain = QStringLiteral("clk-main")) {
  return QJsonObject{
      {QStringLiteral("bindings"),
       QJsonArray{QJsonObject{{QStringLiteral("domain"), clockDomain}}}},
      {QStringLiteral("groups/special"),
       QJsonArray{QJsonObject{
           {QStringLiteral("domain~id"), securityDomain}}}},
      {QStringLiteral("primary"),
       QJsonArray{QJsonObject{{QStringLiteral("domain"), primaryDomain}}}}};
}

QJsonObject settingsSchema() {
  return QJsonObject{
      {QStringLiteral("$schema"),
       QStringLiteral("https://json-schema.org/draft/2020-12/schema")},
      {QStringLiteral("title"), QStringLiteral("Design settings")},
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), false},
      {QStringLiteral("required"),
       QJsonArray{QStringLiteral("mode"), QStringLiteral("lanes")}},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("mode"),
            QJsonObject{
                {QStringLiteral("enum"),
                 QJsonArray{QStringLiteral("safe"), QStringLiteral("fast")}}}},
           {QStringLiteral("lanes"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                        {QStringLiteral("minItems"), 1},
                        {QStringLiteral("items"),
                         QJsonObject{{QStringLiteral("$ref"),
                                      QStringLiteral("#/$defs/lane")}}}}}}},
      {QStringLiteral("$defs"),
       QJsonObject{
           {QStringLiteral("lane"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("additionalProperties"), false},
                {QStringLiteral("required"),
                 QJsonArray{QStringLiteral("id"), QStringLiteral("role")}},
                {QStringLiteral("properties"),
                 QJsonObject{
                     {QStringLiteral("id"),
                      QJsonObject{
                          {QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("role"),
                      QJsonObject{
                          {QStringLiteral("enum"),
                           QJsonArray{QStringLiteral("request"),
                                      QStringLiteral("response")}}}}}}}}}}};
}

QJsonObject metadataSchema() {
  return QJsonObject{
      {QStringLiteral("$schema"),
       QStringLiteral("https://json-schema.org/draft/2020-12/schema")},
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), false},
      {QStringLiteral("required"), QJsonArray{QStringLiteral("note")}},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("note"),
            QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}};
}

QJsonObject unsupportedSchema() {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("oneOf"),
       QJsonArray{QJsonObject{{QStringLiteral("required"),
                              QJsonArray{QStringLiteral("a")}}},
                  QJsonObject{{QStringLiteral("required"),
                               QJsonArray{QStringLiteral("b")}}}}}};
}

QJsonObject domainBindingsSchema() {
  const QJsonObject domainProperty{
      {QStringLiteral("type"), QStringLiteral("string")}};
  const auto bindingItems = [&](const QString &property) {
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("additionalProperties"), false},
        {QStringLiteral("required"), QJsonArray{property}},
        {QStringLiteral("properties"),
         QJsonObject{{property, domainProperty}}}};
  };
  return QJsonObject{
      {QStringLiteral("$schema"),
       QStringLiteral("https://json-schema.org/draft/2020-12/schema")},
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), false},
      {QStringLiteral("properties"),
       QJsonObject{
           {QStringLiteral("bindings"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("items"),
                 bindingItems(QStringLiteral("domain"))}}},
           {QStringLiteral("groups/special"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("items"),
                 bindingItems(QStringLiteral("domain~id"))}}},
           {QStringLiteral("primary"),
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("items"),
                 bindingItems(QStringLiteral("domain"))}}},
           {QStringLiteral("loose"), QJsonObject{}}}}};
}

QJsonObject optionalDomainType(const QString &id, const QString &label) {
  return QJsonObject{
      {QStringLiteral("id"), id},
      {QStringLiteral("label"), label},
      {QStringLiteral("appliesTo"),
       QJsonArray{QStringLiteral("router"), QStringLiteral("endpoint")}},
      {QStringLiteral("cardinality"), QStringLiteral("multiple")},
      {QStringLiteral("required"), false},
      {QStringLiteral("properties"), QJsonArray{}},
      {QStringLiteral("relations"), QJsonArray{}},
      {QStringLiteral("crossingProperties"), QJsonArray{}}};
}

QJsonObject meshManifestFields() {
  return QJsonObject{
      {QStringLiteral("rows"), QJsonObject{{QStringLiteral("min"), 1},
                                           {QStringLiteral("max"), 2},
                                           {QStringLiteral("default"), 1}}},
      {QStringLiteral("columns"), QJsonObject{{QStringLiteral("min"), 1},
                                              {QStringLiteral("max"), 2},
                                              {QStringLiteral("default"), 1}}}};
}

QJsonObject generatorManifestFields() {
  return QJsonObject{
      {QStringLiteral("name"), QStringLiteral("design-extension-test")},
      {QStringLiteral("version"), QStringLiteral("1.0.0")},
      {QStringLiteral("executable"), QStringLiteral("runtime/bin/generate")},
      {QStringLiteral("supportsValidate"), false},
      {QStringLiteral("timeoutSeconds"), 10}};
}

QJsonObject strictPackageManifest() {
  return QJsonObject{
      {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
      {QStringLiteral("formatVersion"), 3},
      {QStringLiteral("id"), QStringLiteral("test.design-extensions")},
      {QStringLiteral("name"), QStringLiteral("Design extension test")},
      {QStringLiteral("version"), QStringLiteral("1.0.0")},
      {QStringLiteral("mesh"), meshManifestFields()},
      {QStringLiteral("parameters"),
       QJsonArray{QJsonObject{
           {QStringLiteral("id"), QStringLiteral("routing")},
           {QStringLiteral("type"), QStringLiteral("enum")},
           {QStringLiteral("default"), QStringLiteral("xy")},
           {QStringLiteral("values"),
            QJsonArray{QStringLiteral("xy"), QStringLiteral("yx")}}}}},
      {QStringLiteral("endpointTypes"), QJsonArray{}},
      {QStringLiteral("domainTypes"),
       QJsonArray{
           optionalDomainType(QStringLiteral("clock"),
                              QStringLiteral("Clock Domain")),
           optionalDomainType(QStringLiteral("security"),
                              QStringLiteral("Security Domain"))}},
      {QStringLiteral("runtimeCapabilities"),
       QJsonObject{{QStringLiteral("domainConfiguration"),
                    QJsonObject{{QStringLiteral("domains"), false},
                                {QStringLiteral("memberships"), false},
                                {QStringLiteral("relations"), false},
                                {QStringLiteral("crossingPolicies"), false},
                                {QStringLiteral("edgeOverrides"), false}}}}},
      {QStringLiteral("elementPropertySets"), QJsonArray{}},
      {QStringLiteral("designExtensions"),
       QJsonArray{QJsonObject{{QStringLiteral("id"), kSettingsExtensionId},
                              {QStringLiteral("schema"),
                               QStringLiteral("schemas/settings.schema.json")},
                              {QStringLiteral("version"), 1},
                              {QStringLiteral("editor"),
                               QJsonObject{{QStringLiteral("kind"),
                                            QStringLiteral("json-schema")}}}},
                  QJsonObject{{QStringLiteral("id"), kMetadataExtensionId},
                              {QStringLiteral("schema"),
                               QStringLiteral("schemas/metadata.schema.json")},
                              {QStringLiteral("version"), 1}},
                  QJsonObject{
                      {QStringLiteral("id"), kDomainBindingsExtensionId},
                      {QStringLiteral("schema"),
                       QStringLiteral("schemas/domain-bindings.schema.json")},
                      {QStringLiteral("version"), 1},
                      {QStringLiteral("domainReferences"),
                       QJsonArray{
                           QJsonObject{
                               {QStringLiteral("pointer"),
                                QStringLiteral("/bindings/*/domain")},
                               {QStringLiteral("domainType"),
                                QStringLiteral("clock")}},
                           QJsonObject{
                               {QStringLiteral("pointer"),
                                QStringLiteral(
                                    "/groups~1special/*/domain~0id")},
                               {QStringLiteral("domainType"),
                                QStringLiteral("security")}},
                           QJsonObject{
                               {QStringLiteral("pointer"),
                                QStringLiteral("/primary/0/domain")},
                               {QStringLiteral("domainType"),
                                QStringLiteral("clock")}},
                           QJsonObject{
                               {QStringLiteral("pointer"),
                                QStringLiteral("/loose/domain")},
                               {QStringLiteral("domainType"),
                                QStringLiteral("clock")}}}}},
                  QJsonObject{
                      {QStringLiteral("id"), kUnsupportedExtensionId},
                      {QStringLiteral("schema"),
                       QStringLiteral("schemas/unsupported.schema.json")},
                      {QStringLiteral("version"), 1}}}},
      {QStringLiteral("attachment"),
       QJsonObject{{QStringLiteral("maxPerRouter"), 4},
                   {QStringLiteral("slotMode"), QStringLiteral("automatic")}}},
      {QStringLiteral("generator"), generatorManifestFields()}};
}

QJsonObject legacyPackageManifest() {
  return QJsonObject{
      {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
      {QStringLiteral("formatVersion"), 1},
      {QStringLiteral("id"), QStringLiteral("test.legacy-engine")},
      {QStringLiteral("name"), QStringLiteral("Legacy opaque Engine test")},
      {QStringLiteral("version"), QStringLiteral("1.0.0")},
      {QStringLiteral("mesh"), meshManifestFields()},
      {QStringLiteral("parameters"), QJsonArray{}},
      {QStringLiteral("endpointTypes"), QJsonArray{}},
      {QStringLiteral("attachment"),
       QJsonObject{{QStringLiteral("maxPerRouter"), 4},
                   {QStringLiteral("slotMode"), QStringLiteral("automatic")}}},
      {QStringLiteral("generator"), generatorManifestFields()},
      {QStringLiteral("engine"),
       QJsonObject{{QStringLiteral("executable"),
                    QStringLiteral("runtime/bin/generate")},
                   {QStringLiteral("providesValidation"), false},
                   {QStringLiteral("timeoutSeconds"), 10}}}};
}

bool writeExecutable(const QString &path) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write("#!/bin/sh\nexit 0\n") < 0) {
    return false;
  }
  file.close();
  return QFile::setPermissions(
      path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                QFileDevice::ExeOther);
}

bool prepareFixture(const QString &rootPath) {
  const QString strictRoot = QDir(rootPath).filePath(QStringLiteral("strict"));
  const QString legacyRoot = QDir(rootPath).filePath(QStringLiteral("legacy"));
  const QString strictSchemaRoot =
      QDir(strictRoot).filePath(QStringLiteral("schemas"));
  if (!QDir().mkpath(strictSchemaRoot) ||
      !writeExecutable(
          QDir(strictRoot).filePath(QStringLiteral("runtime/bin/generate"))) ||
      !writeExecutable(
          QDir(legacyRoot).filePath(QStringLiteral("runtime/bin/generate")))) {
    return false;
  }
  return saveJsonObject(
             QDir(strictRoot).filePath(QStringLiteral("package.json")),
             strictPackageManifest()) &&
         saveJsonObject(QDir(strictSchemaRoot)
                            .filePath(QStringLiteral("settings.schema.json")),
                        settingsSchema()) &&
         saveJsonObject(QDir(strictSchemaRoot)
                            .filePath(QStringLiteral("metadata.schema.json")),
                        metadataSchema()) &&
         saveJsonObject(
             QDir(strictSchemaRoot)
                 .filePath(QStringLiteral("domain-bindings.schema.json")),
             domainBindingsSchema()) &&
         saveJsonObject(QDir(strictSchemaRoot)
                            .filePath(QStringLiteral("unsupported.schema.json")),
                        unsupportedSchema()) &&
         saveJsonObject(
             QDir(legacyRoot).filePath(QStringLiteral("package.json")),
             legacyPackageManifest());
}

QJsonObject strictCreateRequest(const QJsonObject &packageData = {}) {
  QJsonObject request = {
      {QStringLiteral("id"), QStringLiteral("extension_contract")},
      {QStringLiteral("name"), QStringLiteral("Extension Contract")},
      {QStringLiteral("package"),
       QJsonObject{
           {QStringLiteral("id"), QStringLiteral("test.design-extensions")},
           {QStringLiteral("version"), QStringLiteral("1.0.0")}}},
      {QStringLiteral("topology"),
       QJsonObject{{QStringLiteral("type"), QStringLiteral("mesh")},
                   {QStringLiteral("rows"), 1},
                   {QStringLiteral("columns"), 1}}},
      {QStringLiteral("endpoints"), QJsonArray{}}};
  if (!packageData.isEmpty()) {
    request.insert(QStringLiteral("packageData"), packageData);
  }
  return request;
}

QJsonObject legacyCreateRequest() {
  return QJsonObject{
      {QStringLiteral("id"), QStringLiteral("legacy_opaque")},
      {QStringLiteral("name"), QStringLiteral("Legacy Opaque")},
      {QStringLiteral("package"),
       QJsonObject{{QStringLiteral("id"), QStringLiteral("test.legacy-engine")},
                   {QStringLiteral("version"), QStringLiteral("1.0.0")}}},
      {QStringLiteral("topology"),
       QJsonObject{{QStringLiteral("type"), QStringLiteral("mesh")},
                   {QStringLiteral("rows"), 1},
                   {QStringLiteral("columns"), 1}}},
      {QStringLiteral("endpoints"), QJsonArray{}},
      {QStringLiteral("packageData"),
       QJsonObject{
           {QStringLiteral("vendor.opaque"),
            QJsonObject{{QStringLiteral("hardcodedTrick"), true},
                        {QStringLiteral("payload"), QJsonArray{1, 2, 3}}}}}}};
}

QString extensionPath(const QString &extensionId) {
  return QStringLiteral("/packageData/") + extensionId;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication qtApplication(argc, argv);

  QTemporaryDir fixture(
      QStringLiteral("/tmp/finepaper-design-extension-test-XXXXXX"));
  check(fixture.isValid() && prepareFixture(fixture.path()),
        QStringLiteral(
            "self-contained Design Extension Package fixtures are available"));
  if (!fixture.isValid()) {
    return 1;
  }

  FinepaperApplication application;
  const PackageCatalogReloadResult packageReload =
      application.reloadPackages(QStringList{fixture.path()});
  check(packageReload.committed() && !hasErrors(packageReload.diagnostics)
            && application.packages().size() == 2,
        QStringLiteral("strict and legacy Design Extension Packages load"));
  if (hasErrors(packageReload.diagnostics)) {
    for (const Diagnostic &diagnostic : packageReload.diagnostics) {
      QTextStream(stderr) << diagnostic.code << ' ' << diagnostic.path << ' '
                          << diagnostic.message << Qt::endl;
    }
    return 1;
  }

  const DesignResult created = application.createDesign(strictCreateRequest());
  check(created.success && created.design.packageData.isEmpty() &&
            created.design.parameters.value(QStringLiteral("routing"))
                    .toString() == QStringLiteral("xy"),
        QStringLiteral(
            "a declared Design Extension remains optional at creation"));
  if (!created.success) {
    return 1;
  }
  const NocDesign base = created.design;

  const DesignResult emptyDomainBindings = application.setDesignExtension(
      base, kDomainBindingsExtensionId, QJsonObject{});
  check(emptyDomainBindings.success,
        QStringLiteral(
            "a missing optional Domain-reference path has zero matches"));

  const DesignResult withClock = application.addDomain(
      base,
      DomainDefinition{QStringLiteral("clk-main"), QStringLiteral("clock"),
                       QStringLiteral("Main clock"), QJsonObject{}});
  const DesignResult withDomains =
      withClock.success
          ? application.addDomain(
                withClock.design,
                DomainDefinition{QStringLiteral("sec-main"),
                                 QStringLiteral("security"),
                                 QStringLiteral("Main security boundary"),
                                 QJsonObject{}})
          : DesignResult{};
  check(withDomains.success,
        QStringLiteral(
            "Package-defined clock and security Domains can be prepared"));
  if (!withDomains.success) {
    return 1;
  }

  DesignExtensionDefinition rootReferenceDefinition;
  DesignExtensionDomainReferenceDefinition rootReference;
  rootReference.domainType = QStringLiteral("clock");
  rootReferenceDefinition.domainReferences.append(rootReference);
  check(validateDesignExtensionDomainReferences(
            QJsonValue(QStringLiteral("clk-main")),
            rootReferenceDefinition,
            withDomains.design.domains,
            QStringLiteral("/packageData/test.root-domain")).isEmpty(),
        QStringLiteral(
            "an empty RFC 6901 pointer validates a Domain id at the extension root"));

  const QVector<Diagnostic> missingRootReference =
      validateDesignExtensionDomainReferences(
          QJsonValue(QStringLiteral("missing-clock")),
          rootReferenceDefinition,
          withDomains.design.domains,
          QStringLiteral("/packageData/test.root-domain"));
  check(hasDiagnostic(
            missingRootReference,
            kUnknownDomainReferenceCode,
            QStringLiteral("/packageData/test.root-domain")),
        QStringLiteral(
            "a root Domain reference reports the extension namespace path"));

  const DesignResult schemaFirst = application.setDesignExtension(
      withDomains.design, kDomainBindingsExtensionId,
      QJsonObject{{QStringLiteral("bindings"), QJsonObject{}}});
  check(!schemaFirst.success &&
            hasDiagnostic(
                schemaFirst.diagnostics, kSchemaViolationCode,
                extensionPath(kDomainBindingsExtensionId) +
                    QStringLiteral("/bindings")) &&
            !hasDiagnostic(schemaFirst.diagnostics,
                           kDomainReferenceInvalidContainerCode),
        QStringLiteral(
            "Domain references are checked only after their JSON structure satisfies the schema"));

  QJsonObject invalidReferenceContainer = domainBindingsValue();
  invalidReferenceContainer.insert(QStringLiteral("loose"), 42);
  checkAtomicFailure(
      application.setDesignExtension(
          withDomains.design,
          kDomainBindingsExtensionId,
          invalidReferenceContainer),
      withDomains.design,
      kDomainReferenceInvalidContainerCode,
      extensionPath(kDomainBindingsExtensionId) + QStringLiteral("/loose"),
      QStringLiteral(
          "an existing scalar prefix cannot turn a declared reference into a silent zero match"));

  const DesignResult emptyDomainReference = application.setDesignExtension(
      withDomains.design, kDomainBindingsExtensionId,
      domainBindingsValue(QString{}));
  const Diagnostic *emptyDomainDiagnostic = findDiagnostic(
      emptyDomainReference.diagnostics, kUnknownDomainReferenceCode);
  check(!emptyDomainReference.success && emptyDomainDiagnostic &&
            emptyDomainDiagnostic->path ==
                extensionPath(kDomainBindingsExtensionId) +
                    QStringLiteral("/bindings/0/domain") &&
            emptyDomainDiagnostic->message.contains(QStringLiteral("\"\"")),
        QStringLiteral(
            "an empty Domain id is rejected with an unambiguous diagnostic"));

  checkAtomicFailure(
      application.setDesignExtension(
          withDomains.design, kDomainBindingsExtensionId,
          domainBindingsValue(QStringLiteral("missing-clock"))),
      withDomains.design, kUnknownDomainReferenceCode,
      extensionPath(kDomainBindingsExtensionId) +
          QStringLiteral("/bindings/0/domain"),
      QStringLiteral(
          "a wildcard reference reports the exact unknown Domain path"));

  QJsonArray manyInvalidBindings;
  for (int index = 0; index < 300; ++index) {
    manyInvalidBindings.append(QJsonObject{
        {QStringLiteral("domain"),
         QStringLiteral("missing-clock-%1").arg(index)}});
  }
  QJsonObject manyInvalidReferences = domainBindingsValue();
  manyInvalidReferences.insert(
      QStringLiteral("bindings"), manyInvalidBindings);
  const DesignResult boundedReferenceDiagnostics =
      application.setDesignExtension(
          withDomains.design,
          kDomainBindingsExtensionId,
          manyInvalidReferences);
  check(!boundedReferenceDiagnostics.success &&
            sameDesign(boundedReferenceDiagnostics.design,
                       withDomains.design) &&
            boundedReferenceDiagnostics.diagnostics.size() ==
                kMaximumDesignExtensionDomainReferenceDiagnostics &&
            boundedReferenceDiagnostics.diagnostics.constLast().code ==
                kDomainReferenceDiagnosticsTruncatedCode,
        QStringLiteral(
            "wildcard validation caps diagnostics and reports truncation atomically"));

  checkAtomicFailure(
      application.setDesignExtension(
          withDomains.design, kDomainBindingsExtensionId,
          domainBindingsValue(QStringLiteral("sec-main"))),
      withDomains.design, kDomainReferenceTypeMismatchCode,
      extensionPath(kDomainBindingsExtensionId) +
          QStringLiteral("/bindings/0/domain"),
      QStringLiteral(
          "a reference to an existing Domain of the wrong type is rejected"));

  checkAtomicFailure(
      application.setDesignExtension(
          withDomains.design, kDomainBindingsExtensionId,
          domainBindingsValue(QStringLiteral("clk-main"),
                              QStringLiteral("missing-security"))),
      withDomains.design, kUnknownDomainReferenceCode,
      extensionPath(kDomainBindingsExtensionId) +
          QStringLiteral("/groups~1special/0/domain~0id"),
      QStringLiteral(
          "dynamic object tokens use escaped JSON Pointer diagnostic paths"));

  checkAtomicFailure(
      application.setDesignExtension(
          withDomains.design, kDomainBindingsExtensionId,
          domainBindingsValue(QStringLiteral("clk-main"),
                              QStringLiteral("sec-main"),
                              QStringLiteral("missing-primary"))),
      withDomains.design, kUnknownDomainReferenceCode,
      extensionPath(kDomainBindingsExtensionId) +
          QStringLiteral("/primary/0/domain"),
      QStringLiteral(
          "a canonical exact array index resolves to its terminal value"));

  const DesignResult withDomainBindings = application.setDesignExtension(
      withDomains.design, kDomainBindingsExtensionId, domainBindingsValue());
  check(withDomainBindings.success,
        QStringLiteral(
            "Package-declared references accept existing Domains of each declared type"));
  if (!withDomainBindings.success) {
    return 1;
  }

  checkAtomicFailure(
      application.removeDomain(withDomainBindings.design,
                               QStringLiteral("clk-main")),
      withDomainBindings.design, kUnknownDomainReferenceCode,
      extensionPath(kDomainBindingsExtensionId) +
          QStringLiteral("/bindings/0/domain"),
      QStringLiteral(
          "removing a referenced Domain is rejected without changing the Design"));

  DomainConfiguration withoutClock =
      domain_configuration::fromDesign(withDomainBindings.design);
  withoutClock.domains.erase(
      std::remove_if(
          withoutClock.domains.begin(), withoutClock.domains.end(),
          [](const DomainDefinition &domain) {
            return domain.id == QStringLiteral("clk-main");
          }),
      withoutClock.domains.end());
  checkAtomicFailure(
      application.replaceDomainConfiguration(withDomainBindings.design,
                                             withoutClock),
      withDomainBindings.design, kUnknownDomainReferenceCode,
      extensionPath(kDomainBindingsExtensionId) +
          QStringLiteral("/bindings/0/domain"),
      QStringLiteral(
          "replacing Domain configuration cannot leave extension references dangling"));

  const DesignResult renamedClock = application.updateDomain(
      withDomainBindings.design, QStringLiteral("clk-main"),
      DomainDefinition{QStringLiteral("clk-main"), QStringLiteral("clock"),
                       QStringLiteral("Renamed main clock"), QJsonObject{}});
  check(renamedClock.success &&
            renamedClock.design.packageData.value(kDomainBindingsExtensionId) ==
                domainBindingsValue(),
        QStringLiteral(
            "updating a referenced Domain without changing its id or type remains valid"));

  checkAtomicFailure(
      application.setDesignExtension(
          base, kUnsupportedExtensionId,
          QJsonObject{{QStringLiteral("a"), true}}),
      base, QStringLiteral("design.extension_schema_unsupported"),
      extensionPath(kUnsupportedExtensionId),
      QStringLiteral(
          "an unsupported Package schema is fail-closed for typed writes"));

  NocDesign unsupportedExisting = base;
  unsupportedExisting.packageData.insert(
      kUnsupportedExtensionId,
      QJsonObject{{QStringLiteral("a"), true}});
  const ValidationResult unsupportedValidation =
      application.validate(unsupportedExisting, false);
  check(!unsupportedValidation.success &&
            hasDiagnostic(
                unsupportedValidation.diagnostics,
                QStringLiteral("design.extension_schema_unsupported"),
                extensionPath(kUnsupportedExtensionId)),
        QStringLiteral(
            "an existing value with an unsupported schema is explicitly read-only-invalid"));
  const DesignResult removedUnsupported = application.removeDesignExtension(
      unsupportedExisting, kUnsupportedExtensionId);
  check(removedUnsupported.success &&
            !removedUnsupported.design.packageData.contains(
                kUnsupportedExtensionId),
        QStringLiteral(
            "removeDesignExtension can repair an unsupported existing value"));

  const QJsonObject metadataValue{
      {QStringLiteral("note"), QStringLiteral("preserve exactly")}};
  const DesignResult withMetadata =
      application.setDesignExtension(base, kMetadataExtensionId, metadataValue);
  check(withMetadata.success && withMetadata.design.packageData.value(
                                    kMetadataExtensionId) == metadataValue,
        QStringLiteral("setDesignExtension adds a Package-declared namespace"));

  const DesignResult configured =
      withMetadata.success
          ? application.setDesignExtension(
                withMetadata.design, kSettingsExtensionId, settingsValue())
          : DesignResult{};
  check(
      configured.success &&
          configured.design.packageData.value(kSettingsExtensionId) ==
              settingsValue() &&
          configured.design.packageData.value(kMetadataExtensionId) ==
              metadataValue,
      QStringLiteral("setting one extension preserves every other namespace"));
  if (!configured.success) {
    return 1;
  }

  const QJsonObject fastSettings = settingsValue(QStringLiteral("fast"));
  const DesignResult replaced = application.setDesignExtension(
      configured.design, kSettingsExtensionId, fastSettings);
  check(replaced.success &&
            replaced.design.packageData.value(kSettingsExtensionId) ==
                fastSettings &&
            replaced.design.packageData.value(kMetadataExtensionId) ==
                metadataValue,
        QStringLiteral(
            "setDesignExtension atomically replaces only its namespace"));

  const DesignResult absentRemoval =
      application.removeDesignExtension(base, kSettingsExtensionId);
  check(absentRemoval.success && sameDesign(absentRemoval.design, base),
        QStringLiteral("removing an absent declared extension is idempotent"));

  const DesignResult removed = application.removeDesignExtension(
      configured.design, kSettingsExtensionId);
  check(removed.success &&
            !removed.design.packageData.contains(kSettingsExtensionId) &&
            removed.design.packageData.value(kMetadataExtensionId) ==
                metadataValue,
        QStringLiteral(
            "removeDesignExtension removes only its target namespace"));

  QJsonObject missingLanes = settingsValue();
  missingLanes.remove(QStringLiteral("lanes"));
  checkAtomicFailure(
      application.setDesignExtension(configured.design, kSettingsExtensionId,
                                     missingLanes),
      configured.design, kSchemaViolationCode,
      extensionPath(kSettingsExtensionId) + QStringLiteral("/lanes"),
      QStringLiteral(
          "required-property failures roll back the complete Design"));

  checkAtomicFailure(
      application.setDesignExtension(configured.design, kSettingsExtensionId,
                                     settingsValue(QStringLiteral("unsafe"))),
      configured.design, kSchemaViolationCode,
      extensionPath(kSettingsExtensionId) + QStringLiteral("/mode"),
      QStringLiteral(
          "enum failures report the offending instance path and roll back"));

  QJsonObject extraProperty = settingsValue();
  extraProperty.insert(QStringLiteral("hardcodedTrick"), true);
  checkAtomicFailure(
      application.setDesignExtension(configured.design, kSettingsExtensionId,
                                     extraProperty),
      configured.design, kSchemaViolationCode,
      extensionPath(kSettingsExtensionId) + QStringLiteral("/hardcodedTrick"),
      QStringLiteral("additionalProperties=false is enforced atomically"));

  QJsonObject invalidLocalReference = settingsValue();
  QJsonArray invalidLanes;
  invalidLanes.append(
      QJsonObject{{QStringLiteral("id"), QStringLiteral("request")}});
  invalidLocalReference.insert(QStringLiteral("lanes"), invalidLanes);
  checkAtomicFailure(
      application.setDesignExtension(configured.design, kSettingsExtensionId,
                                     invalidLocalReference),
      configured.design, kSchemaViolationCode,
      extensionPath(kSettingsExtensionId) + QStringLiteral("/lanes/0/role"),
      QStringLiteral(
          "array items reached through a local ref retain precise paths"));

  checkAtomicFailure(
      application.setDesignExtension(configured.design, kSettingsExtensionId,
                                     QJsonValue(QJsonValue::Null)),
      configured.design, kSchemaViolationCode,
      extensionPath(kSettingsExtensionId),
      QStringLiteral("the extension root object contract is enforced"));

  checkAtomicFailure(
      application.setDesignExtension(configured.design, kSettingsExtensionId,
                                     QJsonValue(QJsonValue::Undefined)),
      configured.design, QStringLiteral("design.extension_value_undefined"),
      extensionPath(kSettingsExtensionId),
      QStringLiteral(
          "Undefined cannot turn setDesignExtension into an implicit remove"));

  const QString undeclaredId = QStringLiteral("test.unknown/~");
  const QString undeclaredPath =
      QStringLiteral("/packageData/test.unknown~1~0");
  checkAtomicFailure(
      application.setDesignExtension(configured.design, undeclaredId,
                                     QJsonObject{}),
      configured.design,
      QStringLiteral("design.undeclared_package_data_extension"),
      undeclaredPath,
      QStringLiteral(
          "set rejects undeclared namespaces with escaped JSON Pointer paths"));
  checkAtomicFailure(
      application.removeDesignExtension(configured.design, undeclaredId),
      configured.design,
      QStringLiteral("design.undeclared_package_data_extension"),
      undeclaredPath,
      QStringLiteral(
          "remove cannot mutate an undeclared Package-owned namespace"));

  NocDesign missingPackage = configured.design;
  missingPackage.package.id = QStringLiteral("test.missing");
  checkAtomicFailure(
      application.setDesignExtension(missingPackage, kSettingsExtensionId,
                                     settingsValue()),
      missingPackage, QStringLiteral("package.not_found"),
      QStringLiteral("/package"),
      QStringLiteral("extension mutation requires the exact Design Package"));

  NocDesign invalidExisting = configured.design;
  invalidExisting.packageData.insert(kSettingsExtensionId, missingLanes);
  const DesignResult repaired = application.setDesignExtension(
      invalidExisting, kSettingsExtensionId, fastSettings);
  check(repaired.success &&
            repaired.design.packageData.value(kSettingsExtensionId) ==
                fastSettings &&
            repaired.design.packageData.value(kMetadataExtensionId) ==
                metadataValue,
        QStringLiteral("set validates the candidate and can repair an invalid "
                       "existing value"));

  const DesignResult removedInvalid =
      application.removeDesignExtension(invalidExisting, kSettingsExtensionId);
  check(removedInvalid.success &&
            !removedInvalid.design.packageData.contains(kSettingsExtensionId) &&
            removedInvalid.design.packageData.value(kMetadataExtensionId) ==
                metadataValue,
        QStringLiteral(
            "remove validates the candidate and can remove an invalid value"));

  QJsonObject invalidCreateData = {
      {kSettingsExtensionId, missingLanes},
      {kMetadataExtensionId, metadataValue}};
  const DesignResult rejectedCreate =
      application.createDesign(strictCreateRequest(invalidCreateData));
  check(!rejectedCreate.success &&
            hasDiagnostic(rejectedCreate.diagnostics, kSchemaViolationCode,
                          extensionPath(kSettingsExtensionId) +
                              QStringLiteral("/lanes")),
        QStringLiteral("createDesign cannot bypass Design Extension schemas"));

  const ValidationResult rejectedValidation =
      application.validate(invalidExisting, false);
  check(!rejectedValidation.success &&
            hasDiagnostic(rejectedValidation.diagnostics, kSchemaViolationCode,
                          extensionPath(kSettingsExtensionId) +
                              QStringLiteral("/lanes")),
        QStringLiteral(
            "local validation checks every existing Design Extension"));

  QTemporaryDir saveFixture(
      QStringLiteral("/tmp/finepaper-design-extension-save-test-XXXXXX"));
  check(saveFixture.isValid(),
        QStringLiteral("the save rollback fixture is available"));
  if (saveFixture.isValid()) {
    const QString savePath =
        QDir(saveFixture.path()).filePath(QStringLiteral("design.json"));
    QFile sentinel(savePath);
    const QByteArray sentinelData("do-not-overwrite\n");
    bool sentinelWritten = false;
    if (sentinel.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      sentinelWritten = sentinel.write(sentinelData) == sentinelData.size();
      sentinel.close();
    }
    QVector<Diagnostic> saveDiagnostics;
    const bool invalidSaved =
        sentinelWritten &&
        application.saveDesignFile(savePath, invalidExisting, &saveDiagnostics);
    QFile preservedFile(savePath);
    QByteArray preservedData;
    if (preservedFile.open(QIODevice::ReadOnly)) {
      preservedData = preservedFile.readAll();
    }
    check(sentinelWritten && !invalidSaved && preservedData == sentinelData &&
              hasDiagnostic(saveDiagnostics, kSchemaViolationCode,
                            extensionPath(kSettingsExtensionId) +
                                QStringLiteral("/lanes")),
          QStringLiteral(
              "save validates extensions before touching the destination"));
  }

  QJsonObject changedParameters = invalidExisting.parameters;
  changedParameters.insert(QStringLiteral("routing"), QStringLiteral("yx"));
  checkAtomicFailure(
      application.updateParameters(invalidExisting, changedParameters),
      invalidExisting, kSchemaViolationCode,
      extensionPath(kSettingsExtensionId) + QStringLiteral("/lanes"),
      QStringLiteral("an unrelated parameter mutation cannot bypass an invalid "
                     "extension"));

  const DesignResult legacyCreated =
      application.createDesign(legacyCreateRequest());
  const QJsonObject legacyPackageData =
      legacyCreateRequest().value(QStringLiteral("packageData")).toObject();
  const ValidationResult legacyValidation =
      legacyCreated.success ? application.validate(legacyCreated.design, false)
                            : ValidationResult{};
  const DesignResult legacyParameterUpdate =
      legacyCreated.success
          ? application.updateParameters(legacyCreated.design, QJsonObject{})
          : DesignResult{};
  check(legacyCreated.success && legacyCreated.design.formatVersion == 1 &&
            legacyCreated.design.packageData == legacyPackageData &&
            legacyValidation.success && legacyParameterUpdate.success &&
            legacyParameterUpdate.design.packageData == legacyPackageData,
        QStringLiteral("a legacy Engine Package without a declaration retains "
                       "opaque packageData"));

  if (legacyCreated.success) {
    checkAtomicFailure(
        application.setDesignExtension(
            legacyCreated.design, QStringLiteral("vendor.opaque"),
            QJsonObject{{QStringLiteral("replacement"), true}}),
        legacyCreated.design,
        QStringLiteral("design.undeclared_package_data_extension"),
        QStringLiteral("/packageData/vendor.opaque"),
        QStringLiteral(
            "the typed mutation API does not claim legacy opaque namespaces"));
  }

  const Diagnostic *schemaDiagnostic =
      findDiagnostic(rejectedValidation.diagnostics, kSchemaViolationCode);
  check(schemaDiagnostic &&
            schemaDiagnostic->source == QStringLiteral("package"),
        QStringLiteral(
            "schema violations identify the Package-owned contract source"));

  if (failures == 0) {
    QTextStream(stdout) << "Finepaper application Design Extension tests passed"
                        << Qt::endl;
  }
  return failures == 0 ? 0 : 1;
}
