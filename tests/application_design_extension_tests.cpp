#include "application/application.h"
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
const QString kUnsupportedExtensionId =
    QStringLiteral("test.design.future-editor");
const QString kSchemaViolationCode =
    QStringLiteral("design.extension_schema_violation");

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
      {QStringLiteral("domainTypes"), QJsonArray{}},
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
  const QVector<Diagnostic> packageDiagnostics =
      application.reloadPackages(QStringList{fixture.path()});
  check(!hasErrors(packageDiagnostics) && application.packages().size() == 2,
        QStringLiteral("strict and legacy Design Extension Packages load"));
  if (hasErrors(packageDiagnostics)) {
    for (const Diagnostic &diagnostic : packageDiagnostics) {
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
