#include "package/package.h"
#include "storage/json.h"

#include <QByteArray>
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

int failures = 0;

QByteArray testExtensionSchema() {
    return QByteArrayLiteral(R"json({
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "Test extension",
  "type": "object"
}
)json");
}

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics, const QString& code) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

qsizetype diagnosticCount(const QVector<Diagnostic>& diagnostics,
                          const QString& code) {
    return std::count_if(
        diagnostics.cbegin(),
        diagnostics.cend(),
        [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

bool writeFixtureSchema(
    const QString& packageRoot,
    const QByteArray& contents,
    const QString& relativePath = QStringLiteral(
        "schemas/test-extension.schema.json")) {
    const QString schemaPath = QDir(packageRoot).filePath(
        relativePath);
    if (!QDir().mkpath(QFileInfo(schemaPath).absolutePath())) {
        return false;
    }
    QFile schemaFile(schemaPath);
    return schemaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && schemaFile.write(contents) == contents.size();
}

bool prepareFixture(const QString& packageRoot) {
    const QString executable = QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    if (!QDir().mkpath(QFileInfo(executable).absolutePath())) {
        return false;
    }
    QFile file(executable);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write("#!/bin/sh\nexit 0\n") < 0) {
        return false;
    }
    file.close();
    if (!QFile::setPermissions(
        executable,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther)) {
        return false;
    }

    return writeFixtureSchema(packageRoot, testExtensionSchema());
}

QJsonObject designExtension(const QString& id,
                            const QString& schema = QStringLiteral(
                                "schemas/test-extension.schema.json"),
                            int version = 1) {
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("schema"), schema},
        {QStringLiteral("version"), version}
    };
}

QJsonObject designExtensionWithEditor(
    const QString& id,
    const QString& editorKind = QStringLiteral("json-schema")) {
    QJsonObject extension = designExtension(id);
    extension.insert(
        QStringLiteral("editor"),
        QJsonObject{{QStringLiteral("kind"), editorKind}});
    return extension;
}

QJsonObject completeRuntimeCapabilities() {
    return QJsonObject{
        {QStringLiteral("domainConfiguration"), QJsonObject{
            {QStringLiteral("domains"), true},
            {QStringLiteral("memberships"), true},
            {QStringLiteral("relations"), true},
            {QStringLiteral("crossingPolicies"), true},
            {QStringLiteral("edgeOverrides"), true}
        }}
    };
}

QJsonObject baseManifest(int formatVersion) {
    QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
        {QStringLiteral("formatVersion"), formatVersion},
        {QStringLiteral("id"), QStringLiteral("test.package-domains")},
        {QStringLiteral("name"), QStringLiteral("Package Domain test")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 4},
                {QStringLiteral("default"), 2}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 4},
                {QStringLiteral("default"), 2}
            }}
        }},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("endpointTypes"), QJsonArray{}},
        {QStringLiteral("attachment"), QJsonObject{
            {QStringLiteral("maxPerRouter"), 1},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")}
        }},
        {QStringLiteral("generator"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("package-domain-test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/generate")},
            {QStringLiteral("supportsValidate"), false},
            {QStringLiteral("timeoutSeconds"), 10}
        }}
    };
    if (formatVersion >= 2) {
        manifest.insert(
            QStringLiteral("runtimeCapabilities"), completeRuntimeCapabilities());
    }
    return manifest;
}

QJsonArray completeDomainTypes() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("clock")},
            {QStringLiteral("label"), QStringLiteral("Clock Domain")},
            {QStringLiteral("appliesTo"), QJsonArray{
                QStringLiteral("router"), QStringLiteral("endpoint")
            }},
            {QStringLiteral("cardinality"), QStringLiteral("multiple")},
            {QStringLiteral("required"), true},
            {QStringLiteral("defaultInstance"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("clk-main")},
                {QStringLiteral("name"), QStringLiteral("Primary fabric clock")},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("frequencyMHz"), 1000},
                    {QStringLiteral("backupPowers"), QJsonArray{
                        QStringLiteral("pd-main")
                    }}
                }}
            }},
            {QStringLiteral("properties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("frequencyMHz")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("maximum"), 5000},
                    {QStringLiteral("default"), 800}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("backupPowers")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("multiple"), true},
                    {QStringLiteral("referenceDomainType"), QStringLiteral("power")}
                }
            }},
            {QStringLiteral("relations"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("synchronizedWith")},
                    {QStringLiteral("label"), QStringLiteral("Synchronized with")},
                    {QStringLiteral("targetTypes"), QJsonArray{QStringLiteral("clock")}},
                    {QStringLiteral("cardinality"), QStringLiteral("multiple")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("properties"), QJsonArray{
                        QJsonObject{
                            {QStringLiteral("id"), QStringLiteral("phaseOffset")},
                            {QStringLiteral("type"), QStringLiteral("integer")},
                            {QStringLiteral("default"), 0}
                        }
                    }}
                }
            }},
            {QStringLiteral("crossingProperties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("synchronizerStages")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("default"), 2}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("isolationDomains")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("multiple"), true},
                    {QStringLiteral("referenceDomainType"), QStringLiteral("power")}
                }
            }}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("power")},
            {QStringLiteral("label"), QStringLiteral("Power Domain")},
            {QStringLiteral("appliesTo"), QJsonArray{
                QStringLiteral("router"), QStringLiteral("endpoint")
            }},
            {QStringLiteral("cardinality"), QStringLiteral("single")},
            {QStringLiteral("required"), true},
            {QStringLiteral("defaultInstance"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("pd-main")},
                {QStringLiteral("name"), QStringLiteral("Main power rail")},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("voltageMv"), 850}
                }}
            }},
            {QStringLiteral("properties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("voltageMv")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("default"), 900}
                }
            }},
            {QStringLiteral("relations"), QJsonArray{}},
            {QStringLiteral("crossingProperties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("requiresIsolation")},
                    {QStringLiteral("type"), QStringLiteral("boolean")},
                    {QStringLiteral("default"), true}
                }
            }}
        }
    };
}

QJsonArray completeEndpointTypes() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("initiator")},
            {QStringLiteral("label"), QStringLiteral("Initiator")},
            {QStringLiteral("parameters"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("outstandingTransactions")},
                    {QStringLiteral("label"), QStringLiteral("Outstanding transactions")},
                    {QStringLiteral("description"),
                     QStringLiteral("Maximum requests accepted before backpressure")},
                    {QStringLiteral("unit"), QStringLiteral("transactions")},
                    {QStringLiteral("category"), QStringLiteral("Traffic")},
                    {QStringLiteral("advanced"), true},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("default"), 8},
                    {QStringLiteral("minimum"), 1},
                    {QStringLiteral("maximum"), 256}
                }
            }}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("target")},
            {QStringLiteral("label"), QStringLiteral("Target")},
            {QStringLiteral("parameters"), QJsonArray{}}
        }
    };
}

QJsonArray completeElementPropertySets() {
    return QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("fabric.microarchitecture")},
            {QStringLiteral("label"), QStringLiteral("Fabric microarchitecture")},
            {QStringLiteral("appliesTo"), QJsonArray{
                QStringLiteral("router"), QStringLiteral("router-link")
            }},
            {QStringLiteral("properties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("pipelineStages")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("default"), 2},
                    {QStringLiteral("minimum"), 0},
                    {QStringLiteral("maximum"), 8}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("routeBias")},
                    {QStringLiteral("type"), QStringLiteral("number")},
                    {QStringLiteral("default"), 0.5},
                    {QStringLiteral("minimum"), 0.0},
                    {QStringLiteral("maximum"), 1.0}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("cutThrough")},
                    {QStringLiteral("type"), QStringLiteral("boolean")},
                    {QStringLiteral("default"), true}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("implementation")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("default"), QStringLiteral("balanced")}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("arbitration")},
                    {QStringLiteral("type"), QStringLiteral("enum")},
                    {QStringLiteral("default"), QStringLiteral("round-robin")},
                    {QStringLiteral("values"), QJsonArray{
                        QStringLiteral("round-robin"), QStringLiteral("fixed-priority")
                    }}
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("virtualNetworks")},
                    {QStringLiteral("type"), QStringLiteral("enum")},
                    {QStringLiteral("multiple"), true},
                    {QStringLiteral("default"), QJsonArray{
                        QStringLiteral("request"), QStringLiteral("response")
                    }},
                    {QStringLiteral("values"), QJsonArray{
                        QStringLiteral("request"), QStringLiteral("response"),
                        QStringLiteral("snoop")
                    }}
                }
            }}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("initiator.attachment")},
            {QStringLiteral("appliesTo"), QJsonArray{
                QStringLiteral("endpoint-attachment")
            }},
            {QStringLiteral("endpointTypes"), QJsonArray{
                QStringLiteral("initiator")
            }},
            {QStringLiteral("properties"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("interfaceClass")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("default"), QStringLiteral("coherent")}
                }
            }}
        }
    };
}

QJsonObject completeV3Manifest() {
    QJsonObject manifest = baseManifest(3);
    manifest.insert(QStringLiteral("endpointTypes"), completeEndpointTypes());
    manifest.insert(QStringLiteral("domainTypes"), completeDomainTypes());
    manifest.insert(
        QStringLiteral("elementPropertySets"), completeElementPropertySets());
    return manifest;
}

PackageLoadResult loadManifest(const QString& packageRoot, const QJsonObject& manifest) {
    const QString path = QDir(packageRoot).filePath(QStringLiteral("package.json"));
    QVector<Diagnostic> diagnostics;
    if (!saveJsonObject(path, manifest, &diagnostics)) {
        PackageLoadResult result;
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    return loadPackage(packageRoot);
}

void expectFailure(const QString& packageRoot,
                   const QJsonObject& manifest,
                   const QString& code,
                   const QString& description) {
    const PackageLoadResult result = loadManifest(packageRoot, manifest);
    check(!result.success && hasDiagnosticCode(result.diagnostics, code),
          description + QStringLiteral(" reports ") + code);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    QTemporaryDir fixture(QStringLiteral("/tmp/finepaper-package-domain-test-XXXXXX"));
    check(fixture.isValid() && prepareFixture(fixture.path()),
          QStringLiteral("the Package Domain fixture is available"));
    if (!fixture.isValid()) {
        return 1;
    }

    QJsonObject v4Manifest = baseManifest(4);
    v4Manifest.insert(QStringLiteral("domainTypes"), QJsonArray{});
    v4Manifest.insert(QStringLiteral("elementPropertySets"), QJsonArray{});
    expectFailure(fixture.path(),
                  v4Manifest,
                  QStringLiteral("package.unsupported_version"),
                  QStringLiteral("Package V4"));

    const QJsonObject v1Manifest = baseManifest(1);
    const PackageLoadResult v1Result = loadManifest(fixture.path(), v1Manifest);
    check(v1Result.success && v1Result.package && v1Result.package->domainTypes.isEmpty(),
          QStringLiteral("Package V1 succeeds without domainTypes"));
    check(v1Result.package
              && !v1Result.package->runtimeCapabilities.domainConfiguration,
          QStringLiteral("Package V1 does not require a Domain runtime contract"));

    QJsonObject v1WithDesignExtension = v1Manifest;
    v1WithDesignExtension.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtensionWithEditor(
            QStringLiteral("test.package.settings"))});
    const PackageLoadResult designExtensionResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    const DesignExtensionDefinition* loadedExtension =
        designExtensionResult.package
        ? designExtensionResult.package->designExtension(
              QStringLiteral("test.package.settings"))
        : nullptr;
    check(designExtensionResult.success && designExtensionResult.package
              && designExtensionResult.package->designExtensionsDeclared
              && loadedExtension
              && loadedExtension->schema
                  == QStringLiteral("schemas/test-extension.schema.json")
              && loadedExtension->schemaDocument.value(QStringLiteral("title"))
                  == QStringLiteral("Test extension")
              && loadedExtension->version == 1
              && loadedExtension->editor
              && loadedExtension->editor->kind == QStringLiteral("json-schema"),
          QStringLiteral(
              "Package design extensions retain their loaded schema and editor capability"));

    QJsonObject v1WithoutExtensionEditor = v1Manifest;
    v1WithoutExtensionEditor.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package.settings"))});
    const PackageLoadResult noExtensionEditorResult = loadManifest(
        fixture.path(), v1WithoutExtensionEditor);
    const DesignExtensionDefinition* extensionWithoutEditor =
        noExtensionEditorResult.package
        ? noExtensionEditorResult.package->designExtension(
              QStringLiteral("test.package.settings"))
        : nullptr;
    check(noExtensionEditorResult.success && extensionWithoutEditor
              && !extensionWithoutEditor->editor,
          QStringLiteral(
              "an omitted editor capability remains absent instead of being inferred"));

    QJsonObject futureEditorExtension = designExtensionWithEditor(
        QStringLiteral("test.package.settings"),
        QStringLiteral("vendor.power-form-v2"));
    QJsonObject v1WithFutureEditor = v1Manifest;
    v1WithFutureEditor.insert(
        QStringLiteral("designExtensions"), QJsonArray{futureEditorExtension});
    const PackageLoadResult futureEditorResult = loadManifest(
        fixture.path(), v1WithFutureEditor);
    const DesignExtensionDefinition* futureEditor = futureEditorResult.package
        ? futureEditorResult.package->designExtension(
              QStringLiteral("test.package.settings"))
        : nullptr;
    check(futureEditorResult.success && futureEditor && futureEditor->editor
              && futureEditor->editor->kind
                  == QStringLiteral("vendor.power-form-v2"),
          QStringLiteral(
              "unknown editor capability ids are preserved for fail-closed clients"));

    QJsonObject nonObjectEditorExtension = designExtension(
        QStringLiteral("test.package.settings"));
    nonObjectEditorExtension.insert(QStringLiteral("editor"), true);
    QJsonObject v1WithNonObjectEditor = v1Manifest;
    v1WithNonObjectEditor.insert(
        QStringLiteral("designExtensions"), QJsonArray{nonObjectEditorExtension});
    expectFailure(
        fixture.path(),
        v1WithNonObjectEditor,
        QStringLiteral("package.invalid_design_extension_editor"),
        QStringLiteral("a non-object design extension editor"));

    QJsonObject missingEditorKindExtension = designExtension(
        QStringLiteral("test.package.settings"));
    missingEditorKindExtension.insert(QStringLiteral("editor"), QJsonObject{});
    QJsonObject v1WithMissingEditorKind = v1Manifest;
    v1WithMissingEditorKind.insert(
        QStringLiteral("designExtensions"), QJsonArray{missingEditorKindExtension});
    expectFailure(
        fixture.path(),
        v1WithMissingEditorKind,
        QStringLiteral("package.missing_field"),
        QStringLiteral("a design extension editor without a kind"));

    QJsonObject invalidEditorKindExtension = designExtension(
        QStringLiteral("test.package.settings"));
    invalidEditorKindExtension.insert(
        QStringLiteral("editor"),
        QJsonObject{{QStringLiteral("kind"), 7}});
    QJsonObject v1WithInvalidEditorKind = v1Manifest;
    v1WithInvalidEditorKind.insert(
        QStringLiteral("designExtensions"), QJsonArray{invalidEditorKindExtension});
    expectFailure(
        fixture.path(),
        v1WithInvalidEditorKind,
        QStringLiteral("package.missing_field"),
        QStringLiteral("a design extension editor with a non-string kind"));

    QJsonObject unknownEditorFieldExtension = designExtensionWithEditor(
        QStringLiteral("test.package.settings"));
    QJsonObject editorWithUnknownField = unknownEditorFieldExtension.value(
        QStringLiteral("editor")).toObject();
    editorWithUnknownField.insert(QStringLiteral("bad/~field"), true);
    unknownEditorFieldExtension.insert(
        QStringLiteral("editor"), editorWithUnknownField);
    QJsonObject v1WithUnknownEditorField = v1Manifest;
    v1WithUnknownEditorField.insert(
        QStringLiteral("designExtensions"), QJsonArray{unknownEditorFieldExtension});
    const PackageLoadResult unknownEditorFieldResult = loadManifest(
        fixture.path(), v1WithUnknownEditorField);
    const auto unknownEditorFieldDiagnostic = std::find_if(
        unknownEditorFieldResult.diagnostics.cbegin(),
        unknownEditorFieldResult.diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code
                == QStringLiteral(
                    "package.unknown_design_extension_editor_field");
        });
    check(!unknownEditorFieldResult.success
              && unknownEditorFieldDiagnostic
                  != unknownEditorFieldResult.diagnostics.cend()
              && unknownEditorFieldDiagnostic->path
                  == QStringLiteral(
                      "/designExtensions/0/editor/bad~1~0field"),
          QStringLiteral(
              "unknown editor field diagnostics escape JSON Pointer tokens"));

    QJsonObject invalidDesignExtensions = v1Manifest;
    invalidDesignExtensions.insert(
        QStringLiteral("designExtensions"), QJsonObject{});
    expectFailure(
        fixture.path(),
        invalidDesignExtensions,
        QStringLiteral("package.invalid_design_extensions"),
        QStringLiteral("a non-array design extension declaration"));

    QJsonObject extensionWithUnknownField = designExtension(
        QStringLiteral("test.package.settings"));
    extensionWithUnknownField.insert(
        QStringLiteral("bad/~field"), true);
    QJsonObject unknownDesignExtensionField = v1Manifest;
    unknownDesignExtensionField.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{extensionWithUnknownField});
    const PackageLoadResult unknownFieldResult = loadManifest(
        fixture.path(), unknownDesignExtensionField);
    const auto unknownFieldDiagnostic = std::find_if(
        unknownFieldResult.diagnostics.cbegin(),
        unknownFieldResult.diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code
                == QStringLiteral("package.unknown_design_extension_field");
        });
    check(!unknownFieldResult.success
              && unknownFieldDiagnostic != unknownFieldResult.diagnostics.cend()
              && unknownFieldDiagnostic->path
                  == QStringLiteral("/designExtensions/0/bad~1~0field"),
          QStringLiteral(
              "unknown extension field diagnostics escape JSON Pointer tokens"));

    QJsonObject duplicateDesignExtensions = v1Manifest;
    duplicateDesignExtensions.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{
            designExtension(QStringLiteral("test.package.settings")),
            designExtension(QStringLiteral("test.package.settings"),
                            QStringLiteral("schemas/test-extension.schema.json"),
                            2)});
    expectFailure(
        fixture.path(),
        duplicateDesignExtensions,
        QStringLiteral("package.duplicate_design_extension"),
        QStringLiteral("duplicate design extension namespaces"));

    QJsonObject emptyDesignExtensionId = v1Manifest;
    emptyDesignExtensionId.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("  "))});
    expectFailure(
        fixture.path(),
        emptyDesignExtensionId,
        QStringLiteral("package.missing_field"),
        QStringLiteral("an empty design extension namespace"));

    QJsonObject slashDesignExtensionId = v1Manifest;
    slashDesignExtensionId.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package/settings"))});
    expectFailure(
        fixture.path(),
        slashDesignExtensionId,
        QStringLiteral("package.invalid_design_extension_id"),
        QStringLiteral("a design extension namespace containing a slash"));

    QJsonObject whitespaceDesignExtensionId = v1Manifest;
    whitespaceDesignExtensionId.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package settings"))});
    expectFailure(
        fixture.path(),
        whitespaceDesignExtensionId,
        QStringLiteral("package.invalid_design_extension_id"),
        QStringLiteral("a design extension namespace containing whitespace"));

    QJsonObject emptyDesignExtensionSchema = v1Manifest;
    emptyDesignExtensionSchema.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package.settings"),
                                   QStringLiteral("  "))});
    expectFailure(
        fixture.path(),
        emptyDesignExtensionSchema,
        QStringLiteral("package.missing_field"),
        QStringLiteral("an empty design extension schema path"));

    QJsonObject invalidDesignExtensionVersion = v1Manifest;
    invalidDesignExtensionVersion.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package.settings"),
                                   QStringLiteral(
                                       "schemas/test-extension.schema.json"),
                                   0)});
    expectFailure(
        fixture.path(),
        invalidDesignExtensionVersion,
        QStringLiteral("package.invalid_design_extension_version"),
        QStringLiteral("a non-positive design extension version"));

    QJsonObject traversingDesignExtension = v1Manifest;
    traversingDesignExtension.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package.settings"),
                                   QStringLiteral("../outside.schema.json"))});
    expectFailure(
        fixture.path(),
        traversingDesignExtension,
        QStringLiteral("package.design_extension_schema_escape"),
        QStringLiteral("a traversing design extension schema path"));

    QJsonObject missingDesignExtensionSchema = v1Manifest;
    missingDesignExtensionSchema.insert(
        QStringLiteral("designExtensions"),
        QJsonArray{designExtension(QStringLiteral("test.package.settings"),
                                   QStringLiteral("schemas/missing.schema.json"))});
    expectFailure(
        fixture.path(),
        missingDesignExtensionSchema,
        QStringLiteral("package.design_extension_schema_missing"),
        QStringLiteral("a missing design extension schema file"));

    check(writeFixtureSchema(
              fixture.path(),
              QByteArrayLiteral(R"json({
  "$defs": {
    "payload": {
      "type": "object",
      "properties": {
        "name": {"type": "string"}
      }
    }
  },
  "$ref": "#/$defs/payload"
}
)json")),
          QStringLiteral("a schema with a local reference can be written"));
    const PackageLoadResult localReferenceSchemaResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    const DesignExtensionDefinition* localReferenceExtension =
        localReferenceSchemaResult.package
        ? localReferenceSchemaResult.package->designExtension(
              QStringLiteral("test.package.settings"))
        : nullptr;
    check(localReferenceSchemaResult.success && localReferenceExtension
              && localReferenceExtension->schemaDocument.value(
                     QStringLiteral("$ref"))
                  == QStringLiteral("#/$defs/payload"),
          QStringLiteral(
              "local schema references are retained without external resolution"));

    check(writeFixtureSchema(
              fixture.path(),
              QByteArrayLiteral(
                  R"json({"$dynamicAnchor":"node","$dynamicRef":"#node"})json")),
          QStringLiteral("a local dynamic-reference schema can be written"));
    const PackageLoadResult localDynamicReferenceResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    check(localDynamicReferenceResult.success,
          QStringLiteral("a same-document dynamic reference is accepted"));

    check(writeFixtureSchema(
              fixture.path(),
              QByteArrayLiteral(
                  R"json({"default":{"$ref":"literal user data"},"type":"object"})json")),
          QStringLiteral("a schema with literal $ref-shaped default data can be written"));
    const PackageLoadResult literalReferenceDataResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    check(literalReferenceDataResult.success,
          QStringLiteral(
              "$ref-shaped properties inside schema annotations remain ordinary data"));

    check(writeFixtureSchema(
              fixture.path(),
              QByteArrayLiteral(
                  R"json({"properties":{"nested":{"$ref":"https://example.invalid/external.json"}}})json")),
          QStringLiteral("an external-reference schema can be written"));
    const PackageLoadResult externalReferenceSchemaResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    const auto externalReferenceDiagnostic = std::find_if(
        externalReferenceSchemaResult.diagnostics.cbegin(),
        externalReferenceSchemaResult.diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code
                == QStringLiteral(
                    "package.design_extension_schema_external_ref");
        });
    check(!externalReferenceSchemaResult.success
              && externalReferenceDiagnostic
                  != externalReferenceSchemaResult.diagnostics.cend()
              && externalReferenceDiagnostic->path
                  == QStringLiteral(
                      "/designExtensions/0/schema#/properties/nested/$ref"),
          QStringLiteral(
              "external schema references fail at an unambiguous document path"));

    check(writeFixtureSchema(
              fixture.path(),
              QByteArrayLiteral(
                  R"json({"$dynamicRef":"https://example.invalid/dynamic.json#node"})json")),
          QStringLiteral("an external dynamic-reference schema can be written"));
    const PackageLoadResult externalDynamicReferenceResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    const auto externalDynamicReferenceDiagnostic = std::find_if(
        externalDynamicReferenceResult.diagnostics.cbegin(),
        externalDynamicReferenceResult.diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code
                == QStringLiteral(
                    "package.design_extension_schema_external_ref");
        });
    check(!externalDynamicReferenceResult.success
              && externalDynamicReferenceDiagnostic
                  != externalDynamicReferenceResult.diagnostics.cend()
              && externalDynamicReferenceDiagnostic->path
                  == QStringLiteral(
                      "/designExtensions/0/schema#/$dynamicRef"),
          QStringLiteral(
              "external dynamic references use the same local-only boundary"));

    check(writeFixtureSchema(
              fixture.path(),
              QByteArrayLiteral(
                  R"json({"allOf":[{"$recursiveRef":"legacy.schema.json#"}]})json")),
          QStringLiteral("an external recursive-reference schema can be written"));
    expectFailure(
        fixture.path(),
        v1WithDesignExtension,
        QStringLiteral("package.design_extension_schema_external_ref"),
        QStringLiteral("an external legacy recursive schema reference"));

    check(writeFixtureSchema(
              fixture.path(), QByteArrayLiteral(R"json({"$ref":7})json")),
          QStringLiteral("a non-string-reference schema can be written"));
    expectFailure(
        fixture.path(),
        v1WithDesignExtension,
        QStringLiteral("package.design_extension_schema_invalid_ref"),
        QStringLiteral("a non-string design extension schema reference"));

    check(writeFixtureSchema(
              fixture.path(), QByteArrayLiteral(R"json({"$ref":""})json")),
          QStringLiteral("an empty-reference schema can be written"));
    expectFailure(
        fixture.path(),
        v1WithDesignExtension,
        QStringLiteral("package.design_extension_schema_invalid_ref"),
        QStringLiteral("an empty design extension schema reference"));

    check(writeFixtureSchema(
              fixture.path(), QByteArrayLiteral("{\"type\":")),
          QStringLiteral("a malformed schema can be written"));
    expectFailure(
        fixture.path(),
        v1WithDesignExtension,
        QStringLiteral("package.design_extension_schema_invalid_json"),
        QStringLiteral("malformed design extension schema JSON"));

    check(writeFixtureSchema(
              fixture.path(), QByteArrayLiteral("[]\n")),
          QStringLiteral("a non-object schema can be written"));
    expectFailure(
        fixture.path(),
        v1WithDesignExtension,
        QStringLiteral("package.design_extension_schema_invalid_root"),
        QStringLiteral("a non-object design extension schema root"));

    QByteArray maximumSchema(kMaximumDesignExtensionSchemaBytes, ' ');
    maximumSchema.front() = '{';
    maximumSchema.back() = '}';
    check(writeFixtureSchema(fixture.path(), maximumSchema),
          QStringLiteral("a schema at the byte limit can be written"));
    const PackageLoadResult maximumSchemaResult = loadManifest(
        fixture.path(), v1WithDesignExtension);
    check(maximumSchemaResult.success,
          QStringLiteral("a valid schema at the byte limit is accepted"));

    maximumSchema.append(' ');
    check(writeFixtureSchema(fixture.path(), maximumSchema),
          QStringLiteral("an oversized schema can be written"));
    expectFailure(
        fixture.path(),
        v1WithDesignExtension,
        QStringLiteral("package.design_extension_schema_too_large"),
        QStringLiteral("a design extension schema above the byte limit"));

    check(writeFixtureSchema(fixture.path(), testExtensionSchema()),
          QStringLiteral("the shared design extension schema is restored"));

    QJsonArray excessiveDesignExtensions;
    for (int index = 0; index <= kMaximumDesignExtensionsPerPackage; ++index) {
        excessiveDesignExtensions.append(designExtension(
            QStringLiteral("test.package.settings.%1").arg(index)));
    }
    QJsonObject excessiveDesignExtensionManifest = v1Manifest;
    excessiveDesignExtensionManifest.insert(
        QStringLiteral("designExtensions"), excessiveDesignExtensions);
    expectFailure(
        fixture.path(),
        excessiveDesignExtensionManifest,
        QStringLiteral("package.too_many_design_extensions"),
        QStringLiteral("a Package with too many design extensions"));

    maximumSchema.chop(1);
    const QString sharedBudgetSchemaPath = QStringLiteral(
        "schemas/shared-budget.schema.json");
    check(writeFixtureSchema(
              fixture.path(), maximumSchema, sharedBudgetSchemaPath),
          QStringLiteral("a shared budget schema can be written"));
    constexpr int schemaReferencesBeyondBudget =
        kMaximumDesignExtensionSchemaTotalBytes
            / kMaximumDesignExtensionSchemaBytes
        + 1;
    QJsonArray sharedSchemaExtensions;
    for (int index = 0; index < schemaReferencesBeyondBudget; ++index) {
        sharedSchemaExtensions.append(designExtension(
            QStringLiteral("test.package.shared.%1").arg(index),
            sharedBudgetSchemaPath));
    }
    QJsonObject sharedSchemaManifest = v1Manifest;
    sharedSchemaManifest.insert(
        QStringLiteral("designExtensions"), sharedSchemaExtensions);
    const PackageLoadResult sharedSchemaResult = loadManifest(
        fixture.path(), sharedSchemaManifest);
    check(sharedSchemaResult.success,
          QStringLiteral(
              "extensions sharing one canonical schema consume the byte budget once"));

    QJsonArray aggregateSchemaExtensions;
    for (int index = 0; index < schemaReferencesBeyondBudget; ++index) {
        const QString schemaPath = QStringLiteral(
            "schemas/budget-%1.schema.json").arg(index);
        check(writeFixtureSchema(fixture.path(), maximumSchema, schemaPath),
              QStringLiteral("aggregate budget schema %1 can be written")
                  .arg(index));
        aggregateSchemaExtensions.append(designExtension(
            QStringLiteral("test.package.budget.%1").arg(index), schemaPath));
    }
    QJsonObject aggregateSchemaManifest = v1Manifest;
    aggregateSchemaManifest.insert(
        QStringLiteral("designExtensions"), aggregateSchemaExtensions);
    expectFailure(
        fixture.path(),
        aggregateSchemaManifest,
        QStringLiteral("package.design_extension_schema_budget_exceeded"),
        QStringLiteral("unique design extension schemas above the Package budget"));

    QByteArray invalidMaximumSchema(
        kMaximumDesignExtensionSchemaBytes, ' ');
    invalidMaximumSchema.front() = '{';
    const QString sharedInvalidSchemaPath = QStringLiteral(
        "schemas/shared-invalid.schema.json");
    check(writeFixtureSchema(
              fixture.path(), invalidMaximumSchema, sharedInvalidSchemaPath),
          QStringLiteral("a shared invalid schema can be written"));
    QJsonArray sharedInvalidExtensions;
    for (int index = 0; index < schemaReferencesBeyondBudget; ++index) {
        sharedInvalidExtensions.append(designExtension(
            QStringLiteral("test.package.shared-invalid.%1").arg(index),
            sharedInvalidSchemaPath));
    }
    QJsonObject sharedInvalidManifest = v1Manifest;
    sharedInvalidManifest.insert(
        QStringLiteral("designExtensions"), sharedInvalidExtensions);
    const PackageLoadResult sharedInvalidResult = loadManifest(
        fixture.path(), sharedInvalidManifest);
    check(!sharedInvalidResult.success
              && diagnosticCount(
                     sharedInvalidResult.diagnostics,
                     QStringLiteral(
                         "package.design_extension_schema_invalid_json"))
                  == 1,
          QStringLiteral(
              "a shared invalid schema is parsed and diagnosed only once"));

    QJsonArray aggregateInvalidExtensions;
    for (int index = 0; index < schemaReferencesBeyondBudget; ++index) {
        const QString schemaPath = QStringLiteral(
            "schemas/invalid-budget-%1.schema.json").arg(index);
        check(writeFixtureSchema(
                  fixture.path(), invalidMaximumSchema, schemaPath),
              QStringLiteral("invalid aggregate budget schema %1 can be written")
                  .arg(index));
        aggregateInvalidExtensions.append(designExtension(
            QStringLiteral("test.package.invalid-budget.%1").arg(index),
            schemaPath));
    }
    QJsonObject aggregateInvalidManifest = v1Manifest;
    aggregateInvalidManifest.insert(
        QStringLiteral("designExtensions"), aggregateInvalidExtensions);
    expectFailure(
        fixture.path(),
        aggregateInvalidManifest,
        QStringLiteral("package.design_extension_schema_budget_exceeded"),
        QStringLiteral(
            "invalid unique schemas still consume the Package byte budget"));

    QTemporaryDir externalSchemaFixture(QStringLiteral(
        "/tmp/finepaper-external-schema-test-XXXXXX"));
    const QString externalSchemaPath = QDir(externalSchemaFixture.path()).filePath(
        QStringLiteral("external.schema.json"));
    QFile externalSchemaFile(externalSchemaPath);
    bool externalSchemaSaved = false;
    if (externalSchemaFixture.isValid()
        && externalSchemaFile.open(
            QIODevice::WriteOnly | QIODevice::Truncate)) {
        externalSchemaSaved = externalSchemaFile.write("{}\n") >= 0;
    }
    externalSchemaFile.close();
    const QString linkedSchemaPath = QDir(fixture.path()).filePath(
        QStringLiteral("schemas/external.schema.json"));
    const bool symlinkCreated = externalSchemaSaved
        && QFile::link(externalSchemaPath, linkedSchemaPath)
        && QFileInfo(linkedSchemaPath).isSymLink();
#ifdef Q_OS_UNIX
    check(symlinkCreated,
          QStringLiteral(
              "the Unix security fixture creates an escaping schema symlink"));
#endif
    if (symlinkCreated) {
        QJsonObject escapingSymlinkExtension = v1Manifest;
        escapingSymlinkExtension.insert(
            QStringLiteral("designExtensions"),
            QJsonArray{designExtension(
                QStringLiteral("test.package.settings"),
                QStringLiteral("schemas/external.schema.json"))});
        expectFailure(
            fixture.path(),
            escapingSymlinkExtension,
            QStringLiteral("package.design_extension_schema_escape"),
            QStringLiteral("a design extension schema symlink escaping the Package root"));
    }

    QJsonObject v1WithPresentationMetadata = v1Manifest;
    v1WithPresentationMetadata.insert(
        QStringLiteral("parameters"),
        QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("payloadWidth")},
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("default"), 64},
            {QStringLiteral("description"),
             QStringLiteral("  Default payload width  ")},
            {QStringLiteral("unit"), QStringLiteral("bits")},
            {QStringLiteral("category"), QStringLiteral("Fabric")},
            {QStringLiteral("advanced"), true}
        }});
    const PackageLoadResult v1MetadataResult = loadManifest(
        fixture.path(), v1WithPresentationMetadata);
    const ParameterDefinition* v1MetadataParameter =
        v1MetadataResult.package
        ? v1MetadataResult.package->parameter(QStringLiteral("payloadWidth"))
        : nullptr;
    check(v1MetadataResult.success && v1MetadataResult.package
              && v1MetadataParameter
              && v1MetadataParameter->description
                  == QStringLiteral("Default payload width")
              && v1MetadataParameter->advanced,
          QStringLiteral(
              "parameter presentation metadata is optional in every Package format version"));

    QJsonObject v1WithElementProperties = v1Manifest;
    v1WithElementProperties.insert(
        QStringLiteral("elementPropertySets"), QJsonArray{});
    expectFailure(fixture.path(),
                  v1WithElementProperties,
                  QStringLiteral("package.element_property_sets_require_v3"),
                  QStringLiteral("Package V1 with elementPropertySets"));

    QJsonObject v1WithDomains = v1Manifest;
    v1WithDomains.insert(QStringLiteral("domainTypes"), QJsonArray{});
    expectFailure(fixture.path(),
                  v1WithDomains,
                  QStringLiteral("package.domain_types_require_v2"),
                  QStringLiteral("Package V1 with domainTypes"));

    QJsonObject v1WithRuntimeCapabilities = v1Manifest;
    v1WithRuntimeCapabilities.insert(
        QStringLiteral("runtimeCapabilities"), completeRuntimeCapabilities());
    expectFailure(fixture.path(),
                  v1WithRuntimeCapabilities,
                  QStringLiteral("package.runtime_capabilities_require_v2"),
                  QStringLiteral("Package V1 with Domain runtime capabilities"));

    const QJsonObject v2MissingDomains = baseManifest(2);
    expectFailure(fixture.path(),
                  v2MissingDomains,
                  QStringLiteral("package.invalid_domain_types"),
                  QStringLiteral("Package V2 without domainTypes"));

    QJsonObject v2MissingRuntimeCapabilities = baseManifest(2);
    v2MissingRuntimeCapabilities.insert(QStringLiteral("domainTypes"), QJsonArray{});
    v2MissingRuntimeCapabilities.remove(QStringLiteral("runtimeCapabilities"));
    expectFailure(fixture.path(),
                  v2MissingRuntimeCapabilities,
                  QStringLiteral("package.missing_runtime_capabilities"),
                  QStringLiteral("Package V2 without a Domain runtime contract"));

    QJsonObject invalidRuntimeCapabilities = baseManifest(2);
    invalidRuntimeCapabilities.insert(QStringLiteral("domainTypes"), QJsonArray{});
    invalidRuntimeCapabilities.insert(
        QStringLiteral("runtimeCapabilities"), QStringLiteral("all"));
    expectFailure(fixture.path(),
                  invalidRuntimeCapabilities,
                  QStringLiteral("package.invalid_runtime_capabilities"),
                  QStringLiteral("a non-object runtime capability contract"));

    QJsonObject missingDomainRuntimeCapabilities = baseManifest(2);
    missingDomainRuntimeCapabilities.insert(QStringLiteral("domainTypes"), QJsonArray{});
    missingDomainRuntimeCapabilities.insert(
        QStringLiteral("runtimeCapabilities"), QJsonObject{});
    expectFailure(fixture.path(),
                  missingDomainRuntimeCapabilities,
                  QStringLiteral("package.missing_domain_runtime_capabilities"),
                  QStringLiteral("runtimeCapabilities without domainConfiguration"));

    QJsonObject invalidDomainRuntimeCapabilities = baseManifest(2);
    invalidDomainRuntimeCapabilities.insert(QStringLiteral("domainTypes"), QJsonArray{});
    invalidDomainRuntimeCapabilities.insert(
        QStringLiteral("runtimeCapabilities"),
        QJsonObject{{QStringLiteral("domainConfiguration"), QJsonArray{}}});
    expectFailure(fixture.path(),
                  invalidDomainRuntimeCapabilities,
                  QStringLiteral("package.invalid_domain_runtime_capabilities"),
                  QStringLiteral("a non-object Domain runtime contract"));

    QJsonObject unknownRuntimeCapability = baseManifest(2);
    unknownRuntimeCapability.insert(QStringLiteral("domainTypes"), QJsonArray{});
    QJsonObject unknownRuntimeObject = completeRuntimeCapabilities();
    unknownRuntimeObject.insert(QStringLiteral("hardcodedTrick"), true);
    unknownRuntimeCapability.insert(
        QStringLiteral("runtimeCapabilities"), unknownRuntimeObject);
    expectFailure(fixture.path(),
                  unknownRuntimeCapability,
                  QStringLiteral("package.unknown_runtime_capability"),
                  QStringLiteral("an unknown runtime capability field"));

    QJsonObject unknownDomainRuntimeCapability = baseManifest(2);
    unknownDomainRuntimeCapability.insert(QStringLiteral("domainTypes"), QJsonArray{});
    QJsonObject unknownDomainRuntimeObject = completeRuntimeCapabilities();
    QJsonObject unknownDomainCapabilities = unknownDomainRuntimeObject.value(
        QStringLiteral("domainConfiguration")).toObject();
    unknownDomainCapabilities.insert(QStringLiteral("implicitClockTrick"), true);
    unknownDomainRuntimeObject.insert(
        QStringLiteral("domainConfiguration"), unknownDomainCapabilities);
    unknownDomainRuntimeCapability.insert(
        QStringLiteral("runtimeCapabilities"), unknownDomainRuntimeObject);
    expectFailure(fixture.path(),
                  unknownDomainRuntimeCapability,
                  QStringLiteral("package.unknown_domain_runtime_capability"),
                  QStringLiteral("an unknown Domain runtime capability field"));

    QJsonObject missingDomainRuntimeCapability = baseManifest(2);
    missingDomainRuntimeCapability.insert(QStringLiteral("domainTypes"), QJsonArray{});
    QJsonObject missingDomainRuntimeObject = completeRuntimeCapabilities();
    QJsonObject missingDomainCapabilities = missingDomainRuntimeObject.value(
        QStringLiteral("domainConfiguration")).toObject();
    missingDomainCapabilities.remove(QStringLiteral("relations"));
    missingDomainRuntimeObject.insert(
        QStringLiteral("domainConfiguration"), missingDomainCapabilities);
    missingDomainRuntimeCapability.insert(
        QStringLiteral("runtimeCapabilities"), missingDomainRuntimeObject);
    expectFailure(fixture.path(),
                  missingDomainRuntimeCapability,
                  QStringLiteral("package.missing_domain_runtime_capability"),
                  QStringLiteral("a missing Domain runtime capability flag"));

    QJsonObject invalidDomainRuntimeCapability = baseManifest(2);
    invalidDomainRuntimeCapability.insert(QStringLiteral("domainTypes"), QJsonArray{});
    QJsonObject invalidDomainRuntimeObject = completeRuntimeCapabilities();
    QJsonObject invalidDomainCapabilities = invalidDomainRuntimeObject.value(
        QStringLiteral("domainConfiguration")).toObject();
    invalidDomainCapabilities.insert(
        QStringLiteral("memberships"), QStringLiteral("supported"));
    invalidDomainRuntimeObject.insert(
        QStringLiteral("domainConfiguration"), invalidDomainCapabilities);
    invalidDomainRuntimeCapability.insert(
        QStringLiteral("runtimeCapabilities"), invalidDomainRuntimeObject);
    expectFailure(fixture.path(),
                  invalidDomainRuntimeCapability,
                  QStringLiteral("package.invalid_domain_runtime_capability"),
                  QStringLiteral("a non-boolean Domain runtime capability flag"));

    const auto withDomainCapabilities = [](QJsonObject manifest,
                                           bool domains,
                                           bool memberships,
                                           bool relations,
                                           bool crossingPolicies,
                                           bool edgeOverrides) {
        manifest.insert(
            QStringLiteral("runtimeCapabilities"),
            QJsonObject{{QStringLiteral("domainConfiguration"), QJsonObject{
                {QStringLiteral("domains"), domains},
                {QStringLiteral("memberships"), memberships},
                {QStringLiteral("relations"), relations},
                {QStringLiteral("crossingPolicies"), crossingPolicies},
                {QStringLiteral("edgeOverrides"), edgeOverrides}
            }}});
        return manifest;
    };

    QJsonObject dependencyManifest = baseManifest(2);
    dependencyManifest.insert(QStringLiteral("domainTypes"), QJsonArray{});
    expectFailure(
        fixture.path(),
        withDomainCapabilities(dependencyManifest, false, true, false, false, false),
        QStringLiteral("package.invalid_domain_runtime_capability_dependency"),
        QStringLiteral("memberships consumption without domains consumption"));
    expectFailure(
        fixture.path(),
        withDomainCapabilities(dependencyManifest, false, false, true, false, false),
        QStringLiteral("package.invalid_domain_runtime_capability_dependency"),
        QStringLiteral("relations consumption without domains consumption"));
    expectFailure(
        fixture.path(),
        withDomainCapabilities(dependencyManifest, false, false, false, true, false),
        QStringLiteral("package.invalid_domain_runtime_capability_dependency"),
        QStringLiteral("crossing policy consumption without domains consumption"));
    expectFailure(
        fixture.path(),
        withDomainCapabilities(dependencyManifest, true, false, false, false, true),
        QStringLiteral("package.invalid_domain_runtime_capability_dependency"),
        QStringLiteral("edge override consumption without crossing policy consumption"));
    expectFailure(
        fixture.path(),
        withDomainCapabilities(dependencyManifest, true, false, false, true, true),
        QStringLiteral("package.invalid_domain_runtime_capability_dependency"),
        QStringLiteral("edge override consumption without membership consumption"));

    const PackageLoadResult partialCapabilityResult = loadManifest(
        fixture.path(),
        withDomainCapabilities(dependencyManifest, true, false, false, true, false));
    const auto partialCapabilities = partialCapabilityResult.package
        ? partialCapabilityResult.package->runtimeCapabilities.domainConfiguration
        : std::nullopt;
    check(partialCapabilityResult.success && partialCapabilities
              && partialCapabilities->domains
              && !partialCapabilities->memberships
              && !partialCapabilities->relations
              && partialCapabilities->crossingPolicies
              && !partialCapabilities->edgeOverrides,
          QStringLiteral("a valid partial Domain runtime contract is retained exactly"));

    QJsonObject v2EmptyDomains = baseManifest(2);
    v2EmptyDomains.insert(QStringLiteral("domainTypes"), QJsonArray{});
    const PackageLoadResult v2EmptyResult = loadManifest(fixture.path(), v2EmptyDomains);
    check(v2EmptyResult.success && v2EmptyResult.package
              && v2EmptyResult.package->domainTypes.isEmpty()
              && v2EmptyResult.package->runtimeCapabilities.domainConfiguration,
          QStringLiteral(
              "Package V2 accepts an explicit empty domainTypes array with a runtime contract"));

    QJsonObject v2WithElementProperties = v2EmptyDomains;
    v2WithElementProperties.insert(
        QStringLiteral("elementPropertySets"), QJsonArray{});
    expectFailure(fixture.path(),
                  v2WithElementProperties,
                  QStringLiteral("package.element_property_sets_require_v3"),
                  QStringLiteral("Package V2 with elementPropertySets"));

    const QJsonObject v3MissingSchemas = baseManifest(3);
    expectFailure(fixture.path(),
                  v3MissingSchemas,
                  QStringLiteral("package.invalid_domain_types"),
                  QStringLiteral("Package V3 without domainTypes"));
    expectFailure(fixture.path(),
                  v3MissingSchemas,
                  QStringLiteral("package.invalid_element_property_sets"),
                  QStringLiteral("Package V3 without elementPropertySets"));

    QJsonObject v3EmptySchemas = baseManifest(3);
    v3EmptySchemas.insert(QStringLiteral("domainTypes"), QJsonArray{});
    v3EmptySchemas.insert(QStringLiteral("elementPropertySets"), QJsonArray{});
    const PackageLoadResult v3EmptyResult = loadManifest(
        fixture.path(), v3EmptySchemas);
    check(v3EmptyResult.success && v3EmptyResult.package
              && v3EmptyResult.package->domainTypes.isEmpty()
              && v3EmptyResult.package->elementPropertySets.isEmpty(),
          QStringLiteral("Package V3 accepts explicit empty schema arrays"));

    QJsonObject v3MissingRuntimeCapabilities = v3EmptySchemas;
    v3MissingRuntimeCapabilities.remove(QStringLiteral("runtimeCapabilities"));
    expectFailure(fixture.path(),
                  v3MissingRuntimeCapabilities,
                  QStringLiteral("package.missing_runtime_capabilities"),
                  QStringLiteral("Package V3 without a Domain runtime contract"));

    const QJsonObject completeV3 = completeV3Manifest();
    const PackageLoadResult completeV3Result = loadManifest(
        fixture.path(), completeV3);
    check(completeV3Result.success && completeV3Result.package,
          QStringLiteral("a complete Package V3 element property schema loads"));
    if (completeV3Result.package) {
        const auto& domainCapabilities =
            completeV3Result.package->runtimeCapabilities.domainConfiguration;
        check(domainCapabilities
                  && domainCapabilities->domains
                  && domainCapabilities->memberships
                  && domainCapabilities->relations
                  && domainCapabilities->crossingPolicies
                  && domainCapabilities->edgeOverrides,
              QStringLiteral(
                  "a complete Package V3 retains every Domain runtime capability"));
        const ElementPropertySetDefinition* fabric =
            completeV3Result.package->elementPropertySet(
                QStringLiteral("fabric.microarchitecture"));
        check(fabric
                  && fabric->label == QStringLiteral("Fabric microarchitecture")
                  && fabric->appliesTo
                      == QVector<ElementKind>{ElementKind::Router,
                                              ElementKind::RouterLink}
                  && fabric->endpointTypes.isEmpty()
                  && fabric->properties.size() == 6,
              QStringLiteral("Package-defined set ids, labels, targets, and properties are parsed"));
        if (fabric) {
            const ElementPropertyDefinition* pipeline = fabric->property(
                QStringLiteral("pipelineStages"));
            const ElementPropertyDefinition* routeBias = fabric->property(
                QStringLiteral("routeBias"));
            const ElementPropertyDefinition* cutThrough = fabric->property(
                QStringLiteral("cutThrough"));
            const ElementPropertyDefinition* implementation = fabric->property(
                QStringLiteral("implementation"));
            const ElementPropertyDefinition* arbitration = fabric->property(
                QStringLiteral("arbitration"));
            const ElementPropertyDefinition* virtualNetworks = fabric->property(
                QStringLiteral("virtualNetworks"));
            check(pipeline && pipeline->type == ParameterType::Integer
                      && pipeline->hasDefault && pipeline->defaultValue.toInt() == 2
                      && pipeline->minimum == 0.0 && pipeline->maximum == 8.0,
                  QStringLiteral("integer element properties retain defaults and ranges"));
            check(routeBias && routeBias->type == ParameterType::Number
                      && routeBias->defaultValue.toDouble() == 0.5,
                  QStringLiteral("number element properties are supported"));
            check(cutThrough && cutThrough->type == ParameterType::Boolean
                      && cutThrough->defaultValue.toBool(),
                  QStringLiteral("boolean element properties are supported"));
            check(implementation && implementation->type == ParameterType::String
                      && implementation->defaultValue.toString()
                          == QStringLiteral("balanced"),
                  QStringLiteral("string element properties are supported"));
            check(arbitration
                      && arbitration->type == ParameterType::Enumeration
                      && arbitration->values
                          == QStringList{QStringLiteral("round-robin"),
                                         QStringLiteral("fixed-priority")},
                  QStringLiteral("enum element properties retain their choices"));
            check(virtualNetworks && virtualNetworks->multiple
                      && virtualNetworks->defaultValue.toArray()
                          == QJsonArray{QStringLiteral("request"),
                                        QStringLiteral("response")},
                  QStringLiteral("multiple element properties use array-shaped defaults"));
        }

        const ElementPropertySetDefinition* attachment =
            completeV3Result.package->elementPropertySet(
                QStringLiteral("initiator.attachment"));
        check(attachment
                  && attachment->appliesTo
                      == QVector<ElementKind>{ElementKind::EndpointAttachment}
                  && attachment->endpointTypes
                      == QStringList{QStringLiteral("initiator")},
              QStringLiteral("EndpointAttachment sets retain declared Endpoint type filters"));
        const EndpointTypeDefinition* initiator =
            completeV3Result.package->endpointType(QStringLiteral("initiator"));
        const ParameterDefinition* endpointParameter =
            initiator && !initiator->parameters.isEmpty()
            ? &initiator->parameters.front()
            : nullptr;
        check(endpointParameter
                  && endpointParameter->id
                      == QStringLiteral("outstandingTransactions")
                  && endpointParameter->description
                      == QStringLiteral(
                          "Maximum requests accepted before backpressure")
                  && endpointParameter->unit
                      == QStringLiteral("transactions")
                  && endpointParameter->category
                      == QStringLiteral("Traffic")
                  && endpointParameter->advanced,
              QStringLiteral(
                  "Endpoint parameters retain Package-owned presentation metadata"));
    }

    QJsonObject invalidParameterMetadataManifest = completeV3;
    QJsonArray invalidMetadataEndpointTypes =
        invalidParameterMetadataManifest.value(
            QStringLiteral("endpointTypes")).toArray();
    QJsonObject invalidMetadataEndpoint =
        invalidMetadataEndpointTypes[0].toObject();
    QJsonArray invalidMetadataParameters =
        invalidMetadataEndpoint.value(QStringLiteral("parameters")).toArray();
    QJsonObject invalidMetadataParameter =
        invalidMetadataParameters[0].toObject();
    invalidMetadataParameter.insert(QStringLiteral("description"), 7);
    invalidMetadataParameter.insert(QStringLiteral("category"),
                                    QStringLiteral("   "));
    invalidMetadataParameter.insert(QStringLiteral("advanced"),
                                    QStringLiteral("yes"));
    invalidMetadataParameters[0] = invalidMetadataParameter;
    invalidMetadataEndpoint.insert(QStringLiteral("parameters"),
                                   invalidMetadataParameters);
    invalidMetadataEndpointTypes[0] = invalidMetadataEndpoint;
    invalidParameterMetadataManifest.insert(QStringLiteral("endpointTypes"),
                                            invalidMetadataEndpointTypes);
    expectFailure(fixture.path(),
                  invalidParameterMetadataManifest,
                  QStringLiteral("package.invalid_string"),
                  QStringLiteral("invalid or empty parameter presentation strings"));
    expectFailure(fixture.path(),
                  invalidParameterMetadataManifest,
                  QStringLiteral("package.invalid_boolean"),
                  QStringLiteral("a non-boolean advanced parameter flag"));

    QJsonObject malformedElementSets = completeV3;
    malformedElementSets.insert(
        QStringLiteral("elementPropertySets"), QJsonObject{});
    expectFailure(fixture.path(),
                  malformedElementSets,
                  QStringLiteral("package.invalid_element_property_sets"),
                  QStringLiteral("a non-array elementPropertySets field"));

    QJsonObject endpointTargetManifest = completeV3;
    QJsonArray endpointTargetSets = endpointTargetManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject endpointTargetSet = endpointTargetSets[0].toObject();
    endpointTargetSet.insert(
        QStringLiteral("appliesTo"), QJsonArray{QStringLiteral("endpoint")});
    endpointTargetSets[0] = endpointTargetSet;
    endpointTargetManifest.insert(
        QStringLiteral("elementPropertySets"), endpointTargetSets);
    expectFailure(
        fixture.path(),
        endpointTargetManifest,
        QStringLiteral("package.unsupported_element_property_element_kind"),
        QStringLiteral("Endpoint element properties that bypass endpoint parameters"));

    QJsonObject duplicateTargetManifest = completeV3;
    QJsonArray duplicateTargetSets = duplicateTargetManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject duplicateTargetSet = duplicateTargetSets[0].toObject();
    duplicateTargetSet.insert(
        QStringLiteral("appliesTo"),
        QJsonArray{QStringLiteral("router"), QStringLiteral("router")});
    duplicateTargetSets[0] = duplicateTargetSet;
    duplicateTargetManifest.insert(
        QStringLiteral("elementPropertySets"), duplicateTargetSets);
    expectFailure(
        fixture.path(),
        duplicateTargetManifest,
        QStringLiteral("package.duplicate_element_property_element_kind"),
        QStringLiteral("duplicated element property targets"));

    QJsonObject misplacedFilterManifest = completeV3;
    QJsonArray misplacedFilterSets = misplacedFilterManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject misplacedFilterSet = misplacedFilterSets[0].toObject();
    misplacedFilterSet.insert(
        QStringLiteral("endpointTypes"),
        QJsonArray{QStringLiteral("initiator")});
    misplacedFilterSets[0] = misplacedFilterSet;
    misplacedFilterManifest.insert(
        QStringLiteral("elementPropertySets"), misplacedFilterSets);
    expectFailure(
        fixture.path(),
        misplacedFilterManifest,
        QStringLiteral("package.element_property_endpoint_types_require_attachment"),
        QStringLiteral("an Endpoint type filter on a non-attachment set"));

    QJsonObject unknownFilterManifest = completeV3;
    QJsonArray unknownFilterSets = unknownFilterManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject unknownFilterSet = unknownFilterSets[1].toObject();
    unknownFilterSet.insert(
        QStringLiteral("endpointTypes"), QJsonArray{QStringLiteral("missing")});
    unknownFilterSets[1] = unknownFilterSet;
    unknownFilterManifest.insert(
        QStringLiteral("elementPropertySets"), unknownFilterSets);
    expectFailure(
        fixture.path(),
        unknownFilterManifest,
        QStringLiteral("package.unknown_element_property_endpoint_type"),
        QStringLiteral("an undeclared Endpoint type filter"));

    QJsonObject duplicateFilterManifest = completeV3;
    QJsonArray duplicateFilterSets = duplicateFilterManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject duplicateFilterSet = duplicateFilterSets[1].toObject();
    duplicateFilterSet.insert(
        QStringLiteral("endpointTypes"),
        QJsonArray{QStringLiteral("initiator"), QStringLiteral("initiator")});
    duplicateFilterSets[1] = duplicateFilterSet;
    duplicateFilterManifest.insert(
        QStringLiteral("elementPropertySets"), duplicateFilterSets);
    expectFailure(
        fixture.path(),
        duplicateFilterManifest,
        QStringLiteral("package.duplicate_element_property_endpoint_type"),
        QStringLiteral("a duplicated Endpoint type filter"));

    QJsonObject duplicateSetManifest = completeV3;
    QJsonArray duplicateSets = duplicateSetManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    duplicateSets.append(duplicateSets[0]);
    duplicateSetManifest.insert(
        QStringLiteral("elementPropertySets"), duplicateSets);
    expectFailure(fixture.path(),
                  duplicateSetManifest,
                  QStringLiteral("package.duplicate_element_property_set"),
                  QStringLiteral("duplicated element property set ids"));

    QJsonObject duplicateElementPropertyManifest = completeV3;
    QJsonArray duplicateElementPropertySets = duplicateElementPropertyManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject duplicateElementPropertySet =
        duplicateElementPropertySets[0].toObject();
    QJsonArray duplicateElementProperties = duplicateElementPropertySet.value(
        QStringLiteral("properties")).toArray();
    duplicateElementProperties.append(duplicateElementProperties[0]);
    duplicateElementPropertySet.insert(
        QStringLiteral("properties"), duplicateElementProperties);
    duplicateElementPropertySets[0] = duplicateElementPropertySet;
    duplicateElementPropertyManifest.insert(
        QStringLiteral("elementPropertySets"), duplicateElementPropertySets);
    expectFailure(fixture.path(),
                  duplicateElementPropertyManifest,
                  QStringLiteral("package.duplicate_element_property"),
                  QStringLiteral("duplicated element property ids"));

    QJsonObject missingDefaultManifest = completeV3;
    QJsonArray missingDefaultSets = missingDefaultManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject missingDefaultSet = missingDefaultSets[0].toObject();
    QJsonArray missingDefaultProperties = missingDefaultSet.value(
        QStringLiteral("properties")).toArray();
    QJsonObject missingDefaultProperty = missingDefaultProperties[0].toObject();
    missingDefaultProperty.remove(QStringLiteral("default"));
    missingDefaultProperties[0] = missingDefaultProperty;
    missingDefaultSet.insert(
        QStringLiteral("properties"), missingDefaultProperties);
    missingDefaultSets[0] = missingDefaultSet;
    missingDefaultManifest.insert(
        QStringLiteral("elementPropertySets"), missingDefaultSets);
    expectFailure(fixture.path(),
                  missingDefaultManifest,
                  QStringLiteral("package.missing_parameter_default"),
                  QStringLiteral("an element property without a default"));

    QJsonObject invalidElementMultipleDefaultManifest = completeV3;
    QJsonArray invalidMultipleDefaultSets = invalidElementMultipleDefaultManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject invalidMultipleDefaultSet =
        invalidMultipleDefaultSets[0].toObject();
    QJsonArray invalidElementMultipleDefaultProperties =
        invalidMultipleDefaultSet.value(QStringLiteral("properties")).toArray();
    QJsonObject invalidElementMultipleDefaultProperty =
        invalidElementMultipleDefaultProperties[5].toObject();
    invalidElementMultipleDefaultProperty.insert(
        QStringLiteral("default"), QStringLiteral("request"));
    invalidElementMultipleDefaultProperties[5] =
        invalidElementMultipleDefaultProperty;
    invalidMultipleDefaultSet.insert(
        QStringLiteral("properties"), invalidElementMultipleDefaultProperties);
    invalidMultipleDefaultSets[0] = invalidMultipleDefaultSet;
    invalidElementMultipleDefaultManifest.insert(
        QStringLiteral("elementPropertySets"), invalidMultipleDefaultSets);
    expectFailure(fixture.path(),
                  invalidElementMultipleDefaultManifest,
                  QStringLiteral("package.invalid_parameter_default"),
                  QStringLiteral("a multiple element property with a scalar default"));

    QJsonObject emptyPropertiesManifest = completeV3;
    QJsonArray emptyPropertiesSets = emptyPropertiesManifest.value(
        QStringLiteral("elementPropertySets")).toArray();
    QJsonObject emptyPropertiesSet = emptyPropertiesSets[0].toObject();
    emptyPropertiesSet.insert(QStringLiteral("properties"), QJsonArray{});
    emptyPropertiesSets[0] = emptyPropertiesSet;
    emptyPropertiesManifest.insert(
        QStringLiteral("elementPropertySets"), emptyPropertiesSets);
    expectFailure(fixture.path(),
                  emptyPropertiesManifest,
                  QStringLiteral("package.invalid_element_properties"),
                  QStringLiteral("an empty declared element property set"));

    QJsonObject completeManifest = baseManifest(2);
    completeManifest.insert(QStringLiteral("domainTypes"), completeDomainTypes());
    const PackageLoadResult completeResult = loadManifest(fixture.path(), completeManifest);
    check(completeResult.success && completeResult.package,
          QStringLiteral("a complete Package Domain schema loads successfully"));
    if (completeResult.package) {
        const DomainTypeDefinition* clock = completeResult.package->domainType(
            QStringLiteral("clock"));
        check(clock != nullptr, QStringLiteral("Domain types are addressable by id"));
        if (clock) {
            check(clock->required && clock->cardinality == DomainCardinality::Multiple,
                  QStringLiteral("Domain required and multiple cardinality are parsed"));
            check(clock->appliesTo.contains(ElementKind::Router)
                      && clock->appliesTo.contains(ElementKind::Endpoint),
                  QStringLiteral("Domain element applicability is parsed"));

            const auto reference = std::find_if(
                clock->properties.cbegin(),
                clock->properties.cend(),
                [](const DomainPropertyDefinition& property) {
                    return property.id == QStringLiteral("backupPowers");
                });
            check(reference != clock->properties.cend()
                      && reference->required
                      && reference->multiple
                      && reference->referenceDomainType == QStringLiteral("power")
                      && !reference->hasDefault,
                  QStringLiteral("required, multiple Domain references are parsed"));

            check(clock->relations.size() == 1
                      && clock->relations[0].id == QStringLiteral("synchronizedWith")
                      && clock->relations[0].required
                      && clock->relations[0].cardinality == DomainCardinality::Multiple
                      && clock->relations[0].targetTypes
                          == QStringList{QStringLiteral("clock")}
                      && clock->relations[0].properties.size() == 1,
                  QStringLiteral("Domain relation schemas and their properties are parsed"));
            check(clock->crossingProperties.size() == 2
                      && clock->crossingProperties[0].id
                          == QStringLiteral("synchronizerStages")
                      && clock->crossingProperties[0].required
                      && clock->crossingProperties[1].referenceDomainType
                          == QStringLiteral("power"),
                  QStringLiteral("Domain crossing properties are parsed"));
            check(clock->defaultInstance
                      && clock->defaultInstance->id == QStringLiteral("clk-main")
                      && clock->defaultInstance->name
                          == QStringLiteral("Primary fabric clock")
                      && clock->defaultInstance->properties.value(
                             QStringLiteral("frequencyMHz")).toInt() == 1000
                      && clock->defaultInstance->properties.value(
                             QStringLiteral("backupPowers")).toArray()
                          == QJsonArray{QStringLiteral("pd-main")},
                  QStringLiteral("explicit required-Domain scaffolds are parsed and resolved"));
        }
    }

    QJsonObject partialScaffoldManifest = completeManifest;
    QJsonArray partialScaffoldTypes = partialScaffoldManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject partialClock = partialScaffoldTypes[0].toObject();
    QJsonObject partialClockScaffold = partialClock.value(
        QStringLiteral("defaultInstance")).toObject();
    partialClockScaffold.remove(QStringLiteral("id"));
    partialClockScaffold.remove(QStringLiteral("name"));
    partialClock.insert(
        QStringLiteral("defaultInstance"), partialClockScaffold);
    partialScaffoldTypes[0] = partialClock;
    partialScaffoldManifest.insert(
        QStringLiteral("domainTypes"), partialScaffoldTypes);
    const PackageLoadResult partialScaffoldResult = loadManifest(
        fixture.path(), partialScaffoldManifest);
    const DomainTypeDefinition* partialClockType = partialScaffoldResult.package
        ? partialScaffoldResult.package->domainType(QStringLiteral("clock"))
        : nullptr;
    check(partialScaffoldResult.success && partialClockType
              && partialClockType->defaultInstance
              && partialClockType->defaultInstance->id
                  == QStringLiteral("clock-default")
              && partialClockType->defaultInstance->name
                  == QStringLiteral("Clock Domain")
              && partialClockType->defaultInstance->properties.value(
                     QStringLiteral("frequencyMHz")).toInt() == 1000
              && partialClockType->defaultInstance->properties.value(
                     QStringLiteral("backupPowers")).toArray()
                  == QJsonArray{QStringLiteral("pd-main")},
          QStringLiteral("an explicit scaffold may inherit its canonical id and name"));

    QJsonObject legacyScaffoldManifest = completeManifest;
    QJsonArray legacyScaffoldTypes = legacyScaffoldManifest.value(
        QStringLiteral("domainTypes")).toArray();
    for (qsizetype index = 0; index < legacyScaffoldTypes.size(); ++index) {
        QJsonObject type = legacyScaffoldTypes.at(index).toObject();
        type.remove(QStringLiteral("defaultInstance"));
        legacyScaffoldTypes[index] = type;
    }
    legacyScaffoldManifest.insert(
        QStringLiteral("domainTypes"), legacyScaffoldTypes);
    const PackageLoadResult legacyScaffoldResult = loadManifest(
        fixture.path(), legacyScaffoldManifest);
    const DomainTypeDefinition* legacyClock = legacyScaffoldResult.package
        ? legacyScaffoldResult.package->domainType(QStringLiteral("clock"))
        : nullptr;
    check(legacyScaffoldResult.success && legacyClock
              && legacyClock->defaultInstance
              && legacyClock->defaultInstance->id
                  == QStringLiteral("clock-default")
              && legacyClock->defaultInstance->name
                  == QStringLiteral("Clock Domain")
              && legacyClock->defaultInstance->properties.value(
                     QStringLiteral("frequencyMHz")).toInt() == 800
              && !legacyClock->defaultInstance->properties.contains(
                  QStringLiteral("backupPowers")),
          QStringLiteral("legacy required Domain conventions resolve in the Package layer"));

    QJsonObject duplicateTypeManifest = completeManifest;
    QJsonArray duplicateTypes = duplicateTypeManifest.value(
        QStringLiteral("domainTypes")).toArray();
    duplicateTypes.append(duplicateTypes[0]);
    duplicateTypeManifest.insert(QStringLiteral("domainTypes"), duplicateTypes);
    expectFailure(fixture.path(),
                  duplicateTypeManifest,
                  QStringLiteral("package.duplicate_domain_type"),
                  QStringLiteral("duplicated Domain type ids"));

    QJsonObject duplicatePropertyManifest = completeManifest;
    QJsonArray duplicatePropertyTypes = duplicatePropertyManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithDuplicateProperty = duplicatePropertyTypes[0].toObject();
    QJsonArray duplicateProperties = clockWithDuplicateProperty.value(
        QStringLiteral("properties")).toArray();
    duplicateProperties.append(duplicateProperties[0]);
    clockWithDuplicateProperty.insert(QStringLiteral("properties"), duplicateProperties);
    duplicatePropertyTypes[0] = clockWithDuplicateProperty;
    duplicatePropertyManifest.insert(QStringLiteral("domainTypes"), duplicatePropertyTypes);
    expectFailure(fixture.path(),
                  duplicatePropertyManifest,
                  QStringLiteral("package.duplicate_domain_property"),
                  QStringLiteral("duplicated Domain property ids"));

    QJsonObject unknownPropertyReferenceManifest = completeManifest;
    QJsonArray unknownPropertyTypes = unknownPropertyReferenceManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithUnknownPropertyReference = unknownPropertyTypes[0].toObject();
    QJsonArray unknownReferenceProperties = clockWithUnknownPropertyReference.value(
        QStringLiteral("properties")).toArray();
    QJsonObject unknownReferenceProperty = unknownReferenceProperties[1].toObject();
    unknownReferenceProperty.insert(
        QStringLiteral("referenceDomainType"), QStringLiteral("missing"));
    unknownReferenceProperties[1] = unknownReferenceProperty;
    clockWithUnknownPropertyReference.insert(
        QStringLiteral("properties"), unknownReferenceProperties);
    unknownPropertyTypes[0] = clockWithUnknownPropertyReference;
    unknownPropertyReferenceManifest.insert(
        QStringLiteral("domainTypes"), unknownPropertyTypes);
    expectFailure(fixture.path(),
                  unknownPropertyReferenceManifest,
                  QStringLiteral("package.unknown_domain_property_reference"),
                  QStringLiteral("an unknown Domain property reference"));

    QJsonObject unknownRelationTargetManifest = completeManifest;
    QJsonArray unknownRelationTypes = unknownRelationTargetManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithUnknownRelationTarget = unknownRelationTypes[0].toObject();
    QJsonArray unknownTargetRelations = clockWithUnknownRelationTarget.value(
        QStringLiteral("relations")).toArray();
    QJsonObject unknownTargetRelation = unknownTargetRelations[0].toObject();
    unknownTargetRelation.insert(
        QStringLiteral("targetTypes"), QJsonArray{QStringLiteral("missing")});
    unknownTargetRelations[0] = unknownTargetRelation;
    clockWithUnknownRelationTarget.insert(
        QStringLiteral("relations"), unknownTargetRelations);
    unknownRelationTypes[0] = clockWithUnknownRelationTarget;
    unknownRelationTargetManifest.insert(
        QStringLiteral("domainTypes"), unknownRelationTypes);
    expectFailure(fixture.path(),
                  unknownRelationTargetManifest,
                  QStringLiteral("package.unknown_domain_relation_target"),
                  QStringLiteral("an unknown Domain relation target"));

    QJsonObject invalidDefaultManifest = completeManifest;
    QJsonArray invalidDefaultTypes = invalidDefaultManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithInvalidDefault = invalidDefaultTypes[0].toObject();
    QJsonArray invalidDefaultProperties = clockWithInvalidDefault.value(
        QStringLiteral("properties")).toArray();
    QJsonObject invalidDefaultProperty = invalidDefaultProperties[1].toObject();
    invalidDefaultProperty.insert(
        QStringLiteral("default"), QJsonArray{QStringLiteral("pd_main")});
    invalidDefaultProperties[1] = invalidDefaultProperty;
    clockWithInvalidDefault.insert(
        QStringLiteral("properties"), invalidDefaultProperties);
    invalidDefaultTypes[0] = clockWithInvalidDefault;
    invalidDefaultManifest.insert(QStringLiteral("domainTypes"), invalidDefaultTypes);
    expectFailure(fixture.path(),
                  invalidDefaultManifest,
                  QStringLiteral("package.domain_reference_default_unsupported"),
                  QStringLiteral("a Domain reference property with an instance-id default"));

    QJsonObject invalidMultipleDefaultManifest = completeManifest;
    QJsonArray invalidMultipleDefaultTypes = invalidMultipleDefaultManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject clockWithInvalidMultipleDefault =
        invalidMultipleDefaultTypes[0].toObject();
    QJsonArray invalidMultipleDefaultProperties =
        clockWithInvalidMultipleDefault.value(QStringLiteral("properties")).toArray();
    QJsonObject invalidMultipleDefaultProperty =
        invalidMultipleDefaultProperties[1].toObject();
    invalidMultipleDefaultProperty.remove(QStringLiteral("referenceDomainType"));
    invalidMultipleDefaultProperty.insert(
        QStringLiteral("default"), QStringLiteral("not-an-array"));
    invalidMultipleDefaultProperties[1] = invalidMultipleDefaultProperty;
    clockWithInvalidMultipleDefault.insert(
        QStringLiteral("properties"), invalidMultipleDefaultProperties);
    invalidMultipleDefaultTypes[0] = clockWithInvalidMultipleDefault;
    invalidMultipleDefaultManifest.insert(
        QStringLiteral("domainTypes"), invalidMultipleDefaultTypes);
    expectFailure(fixture.path(),
                  invalidMultipleDefaultManifest,
                  QStringLiteral("package.invalid_parameter_default"),
                  QStringLiteral("a multiple Domain property with a scalar default"));

    QJsonObject nonObjectScaffoldManifest = completeManifest;
    QJsonArray nonObjectScaffoldTypes = nonObjectScaffoldManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject nonObjectClock = nonObjectScaffoldTypes[0].toObject();
    nonObjectClock.insert(QStringLiteral("defaultInstance"), QStringLiteral("implicit"));
    nonObjectScaffoldTypes[0] = nonObjectClock;
    nonObjectScaffoldManifest.insert(
        QStringLiteral("domainTypes"), nonObjectScaffoldTypes);
    expectFailure(
        fixture.path(), nonObjectScaffoldManifest,
        QStringLiteral("package.invalid_default_domain_instance"),
        QStringLiteral("a non-object default Domain scaffold"));

    QJsonObject unknownScaffoldFieldManifest = completeManifest;
    QJsonArray unknownScaffoldFieldTypes = unknownScaffoldFieldManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject unknownFieldClock = unknownScaffoldFieldTypes[0].toObject();
    QJsonObject unknownFieldScaffold = unknownFieldClock.value(
        QStringLiteral("defaultInstance")).toObject();
    unknownFieldScaffold.insert(QStringLiteral("magicClock"), true);
    unknownFieldClock.insert(QStringLiteral("defaultInstance"), unknownFieldScaffold);
    unknownScaffoldFieldTypes[0] = unknownFieldClock;
    unknownScaffoldFieldManifest.insert(
        QStringLiteral("domainTypes"), unknownScaffoldFieldTypes);
    expectFailure(
        fixture.path(), unknownScaffoldFieldManifest,
        QStringLiteral("package.unknown_default_domain_instance_field"),
        QStringLiteral("an unknown default Domain scaffold field"));

    QJsonObject invalidScaffoldPropertiesManifest = completeManifest;
    QJsonArray invalidScaffoldPropertiesTypes =
        invalidScaffoldPropertiesManifest.value(
            QStringLiteral("domainTypes")).toArray();
    QJsonObject invalidPropertiesClock =
        invalidScaffoldPropertiesTypes[0].toObject();
    QJsonObject invalidPropertiesScaffold = invalidPropertiesClock.value(
        QStringLiteral("defaultInstance")).toObject();
    invalidPropertiesScaffold.insert(
        QStringLiteral("properties"), QJsonArray{});
    invalidPropertiesClock.insert(
        QStringLiteral("defaultInstance"), invalidPropertiesScaffold);
    invalidScaffoldPropertiesTypes[0] = invalidPropertiesClock;
    invalidScaffoldPropertiesManifest.insert(
        QStringLiteral("domainTypes"), invalidScaffoldPropertiesTypes);
    expectFailure(
        fixture.path(), invalidScaffoldPropertiesManifest,
        QStringLiteral("package.invalid_default_domain_instance_properties"),
        QStringLiteral("non-object default Domain properties"));

    QJsonObject optionalScaffoldManifest = completeManifest;
    QJsonArray optionalScaffoldTypes = optionalScaffoldManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject optionalType = optionalScaffoldTypes[1].toObject();
    optionalType.insert(QStringLiteral("id"), QStringLiteral("optional-power"));
    optionalType.insert(QStringLiteral("required"), false);
    optionalType.insert(QStringLiteral("defaultInstance"), QJsonObject{
        {QStringLiteral("id"), QStringLiteral("optional-power-default")}
    });
    optionalScaffoldTypes.append(optionalType);
    optionalScaffoldManifest.insert(
        QStringLiteral("domainTypes"), optionalScaffoldTypes);
    expectFailure(
        fixture.path(), optionalScaffoldManifest,
        QStringLiteral("package.default_domain_instance_requires_required_type"),
        QStringLiteral("defaultInstance on an optional Domain Type"));

    QJsonObject duplicateScaffoldIdManifest = completeManifest;
    QJsonArray duplicateScaffoldIdTypes = duplicateScaffoldIdManifest.value(
        QStringLiteral("domainTypes")).toArray();
    QJsonObject duplicateIdPower = duplicateScaffoldIdTypes[1].toObject();
    QJsonObject duplicateIdScaffold = duplicateIdPower.value(
        QStringLiteral("defaultInstance")).toObject();
    duplicateIdScaffold.insert(QStringLiteral("id"), QStringLiteral("clk-main"));
    duplicateIdPower.insert(QStringLiteral("defaultInstance"), duplicateIdScaffold);
    duplicateScaffoldIdTypes[1] = duplicateIdPower;
    duplicateScaffoldIdManifest.insert(
        QStringLiteral("domainTypes"), duplicateScaffoldIdTypes);
    expectFailure(
        fixture.path(), duplicateScaffoldIdManifest,
        QStringLiteral("package.duplicate_default_domain_instance_id"),
        QStringLiteral("duplicated materialized default Domain ids"));

    const auto withClockScaffoldProperties = [](
        QJsonObject manifest, const QJsonObject& properties) {
        QJsonArray types = manifest.value(QStringLiteral("domainTypes")).toArray();
        QJsonObject clock = types[0].toObject();
        QJsonObject scaffold = clock.value(
            QStringLiteral("defaultInstance")).toObject();
        scaffold.insert(QStringLiteral("properties"), properties);
        clock.insert(QStringLiteral("defaultInstance"), scaffold);
        types[0] = clock;
        manifest.insert(QStringLiteral("domainTypes"), types);
        return manifest;
    };

    expectFailure(
        fixture.path(),
        withClockScaffoldProperties(
            completeManifest,
            QJsonObject{
                {QStringLiteral("frequencyMHz"), 1000},
                {QStringLiteral("backupPowers"), QJsonArray{QStringLiteral("pd-main")}},
                {QStringLiteral("hardcodedTrick"), true}
            }),
        QStringLiteral("package.unknown_default_domain_property"),
        QStringLiteral("an undeclared default Domain property"));

    expectFailure(
        fixture.path(),
        withClockScaffoldProperties(
            completeManifest,
            QJsonObject{{QStringLiteral("frequencyMHz"), 1000}}),
        QStringLiteral("package.missing_required_default_domain_property"),
        QStringLiteral("an explicit scaffold missing a required property"));

    expectFailure(
        fixture.path(),
        withClockScaffoldProperties(
            completeManifest,
            QJsonObject{
                {QStringLiteral("frequencyMHz"), QStringLiteral("fast")},
                {QStringLiteral("backupPowers"), QJsonArray{QStringLiteral("pd-main")}}
            }),
        QStringLiteral("package.invalid_default_domain_property"),
        QStringLiteral("a default Domain property with the wrong type"));

    expectFailure(
        fixture.path(),
        withClockScaffoldProperties(
            completeManifest,
            QJsonObject{
                {QStringLiteral("frequencyMHz"), 0},
                {QStringLiteral("backupPowers"), QJsonArray{QStringLiteral("pd-main")}}
            }),
        QStringLiteral("package.default_domain_property_below_minimum"),
        QStringLiteral("a default Domain property outside its numeric range"));

    expectFailure(
        fixture.path(),
        withClockScaffoldProperties(
            completeManifest,
            QJsonObject{
                {QStringLiteral("frequencyMHz"), 1000},
                {QStringLiteral("backupPowers"), QJsonArray{QStringLiteral("missing-power")}}
            }),
        QStringLiteral("package.unknown_default_domain_reference"),
        QStringLiteral("a scaffold reference to a non-materialized Domain"));

    expectFailure(
        fixture.path(),
        withClockScaffoldProperties(
            completeManifest,
            QJsonObject{
                {QStringLiteral("frequencyMHz"), 1000},
                {QStringLiteral("backupPowers"), QJsonArray{QStringLiteral("clk-main")}}
            }),
        QStringLiteral("package.default_domain_reference_type_mismatch"),
        QStringLiteral("a scaffold reference to the wrong Domain Type"));

    if (failures == 0) {
        QTextStream(stdout) << "Package Domain tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " Package Domain test(s) failed" << Qt::endl;
    return 1;
}
