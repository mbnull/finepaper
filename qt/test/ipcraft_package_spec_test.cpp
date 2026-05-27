// Ipcraft Package V1 parser contract smoke tests.
#include "ipcraft/packagespec.h"
#include "ipcraft/schemaids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    require(opened, "failed to open fixture for writing");
    require(file.write(content) == content.size(), "failed to write fixture");
}

bool hasDiagnosticRule(const ipcraft::DiagnosticStore& store, const QString& ruleId) {
    for (const ipcraft::Diagnostic& diagnostic : store.records) {
        if (diagnostic.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

bool hasDiagnosticLocationPath(const ipcraft::DiagnosticStore& store, const QString& path) {
    for (const ipcraft::Diagnostic& diagnostic : store.records) {
        for (const ipcraft::DiagnosticLocation& location : diagnostic.locations) {
            if (location.path == path) {
                return true;
            }
        }
    }
    return false;
}

bool hasPackageParserDiagnosticAt(const ipcraft::DiagnosticStore& store,
                                  const QString& ruleId,
                                  const QString& path) {
    for (const ipcraft::Diagnostic& diagnostic : store.records) {
        if (diagnostic.ruleId != ruleId ||
            diagnostic.source != QStringLiteral("package.parser") ||
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

QJsonObject minimalPackageSpec(const QString& id = QStringLiteral("vendor.example.simple"),
                               const QString& version = QStringLiteral("1.0.0")) {
    return QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::packageV1},
        {QStringLiteral("id"), id},
        {QStringLiteral("version"), version},
        {QStringLiteral("name"), QStringLiteral("Simple IP")},
        {QStringLiteral("extensions"), QJsonArray{}}
    };
}

QString writePackage(QDir& root, const QJsonObject& object) {
    const QString path = root.filePath(QStringLiteral("ipcraft.json"));
    writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
    return path;
}

void testRuntimeLoadsPackageV1WithoutIpcoreYml() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    writePackage(root, minimalPackageSpec());

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "package v1 should load without ipcore.yml");
    require(result.spec.schema == ipcraft::schemaids::packageV1,
            "package schema should parse");
    require(result.spec.id == QStringLiteral("vendor.example.simple"),
            "package id should parse");
    require(result.spec.version == QStringLiteral("1.0.0"),
            "package version should parse");
}

void testRuntimeIgnoresIpcoreYmlWhenPackageV1Exists() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    writePackage(root, minimalPackageSpec());
    writeFile(root.filePath(QStringLiteral("ipcore.yml")),
              QByteArrayLiteral("this: is authoring input, not runtime input\n"));

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "runtime loader should ignore ipcore.yml");
    require(result.spec.id == QStringLiteral("vendor.example.simple"),
            "runtime loader should use ipcraft.package.v1");
}

void testRejectsUnsupportedPackageSchema() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("schema"), QStringLiteral("ipcraft.manifest.v1"));
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "old runtime package schema should be rejected");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.unsupported_schema"),
                                         QStringLiteral("$.schema")),
            "unsupported schema diagnostic should be stable");
}

void testRejectsDuplicateJsonKeys() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    writeFile(root.filePath(QStringLiteral("ipcraft.json")),
              QByteArrayLiteral(R"json({
  "schema": "ipcraft.package.v1",
  "id": "vendor.example.first",
  "id": "vendor.example.second",
  "version": "1.0.0",
  "name": "Duplicate Key"
})json"));

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "duplicate JSON keys should fail");
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.duplicate_key")),
            "duplicate JSON keys should emit package.duplicate_key");
}

void testOptionalSectionRequiresExplicitExtension() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("config_schema"), QJsonObject{
        {QStringLiteral("tables"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("rows")}}}}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "optional sections without extension should fail");
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.extension_required")),
            "missing extension should emit package.extension_required");
    require(hasDiagnosticLocationPath(result.diagnostics, QStringLiteral("$.config_schema.tables")),
            "missing extension diagnostic should point at table section");
}

void testEmptyOptionalSectionStillRequiresExplicitExtension() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("interfaces"), QJsonArray{});
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "empty optional sections should still require extension declaration");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.extension_required"),
                                         QStringLiteral("$.interfaces")),
            "empty optional section diagnostic should point at interfaces");
}

void testExtensionArrayObjectDeclarationSatisfiesRequirement() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("ipcraft.config.tables")}}
    });
    spec.insert(QStringLiteral("config_schema"), QJsonObject{
        {QStringLiteral("tables"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("rows")}}}}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "extension array object declaration should satisfy section requirement");
    require(result.spec.hasExtension(QStringLiteral("ipcraft.config.tables")),
            "extension object declaration should be parsed by id");
}

void testExtensionPayloadObjectDoesNotEnableCapability() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonObject{
        {QStringLiteral("ipcraft.config.tables"), QJsonObject{{QStringLiteral("owner"), QStringLiteral("payload")}}}
    });
    spec.insert(QStringLiteral("config_schema"), QJsonObject{
        {QStringLiteral("tables"), QJsonArray{}}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "extension payload object should not implicitly declare capability");
    require(!result.spec.hasExtension(QStringLiteral("ipcraft.config.tables")),
            "extension payload namespace should not populate declared extensions");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.extension_required"),
                                         QStringLiteral("$.config_schema.tables")),
            "payload object without declaration should emit extension_required");
}

void testExtensionDeclarationsMatchSchemaContract() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral(""),
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("ipcraft.config.params")},
            {QStringLiteral("extra"), true}
        }
    });
    writePackage(root, spec);

    ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "extension declarations should follow schema shape");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.missing_required"),
                                         QStringLiteral("$.extensions[0]")),
            "empty extension declaration should be diagnosed");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.unknown_field"),
                                         QStringLiteral("$.extensions[1].extra")),
            "unknown extension declaration field should be diagnosed");

    spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonObject{
        {QStringLiteral("ipcraft.config.params"), true}
    });
    writePackage(root, spec);
    result = ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "extension payload map values should be objects");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.type_mismatch"),
                                         QStringLiteral("$.extensions.ipcraft.config.params")),
            "extension payload value type should be diagnosed");
}

void testUnknownExtensionIsRejected() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("vendor.example.magic")
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "unknown package extension should fail");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.unknown_extension"),
                                         QStringLiteral("$.extensions[0]")),
            "unknown extension should emit package.unknown_extension at declaration path");
}

void testDuplicateTableIdsAreRejected() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.config.tables")
    });
    spec.insert(QStringLiteral("config_schema"), QJsonObject{
        {QStringLiteral("tables"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("regions")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("regions")}}
        }}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "duplicate table ids should fail package parsing");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.duplicate_table"),
                                         QStringLiteral("$.config_schema.tables[1].id")),
            "duplicate table id should emit package.duplicate_table");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.duplicate_id"),
                                         QStringLiteral("$.config_schema.tables[1].id")),
            "duplicate table id should also emit generic package.duplicate_id");
}

void testConnectionRulesParseAliasesAndCompatibility() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.composition")
    });
    spec.insert(QStringLiteral("connection_rules"), QJsonObject{
        {QStringLiteral("protocol_aliases"), QJsonObject{
            {QStringLiteral("AMBA_AXI4"), QStringLiteral("axi4")}
        }},
        {QStringLiteral("compatibility"), QJsonArray{
            QJsonObject{
                {QStringLiteral("connection_type"), QStringLiteral("interface")},
                {QStringLiteral("from"), QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("bus")},
                    {QStringLiteral("role"), QStringLiteral("master")},
                    {QStringLiteral("protocol"), QStringLiteral("axi4")}
                }},
                {QStringLiteral("to"), QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("bus")},
                    {QStringLiteral("role"), QStringLiteral("slave")},
                    {QStringLiteral("protocol"), QStringLiteral("axi4")}
                }},
                {QStringLiteral("arity"), QStringLiteral("binary")}
            }
        }}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "connection rules package should load");
    require(result.spec.connectionRules.protocolAliases.value(QStringLiteral("AMBA_AXI4")) ==
                QStringLiteral("axi4"),
            "protocol aliases should parse");
    require(result.spec.connectionRules.compatibility.size() == 1,
            "compatibility rules should parse");
    require(result.spec.connectionRules.compatibility.first().from.role ==
                QStringLiteral("master"),
            "compatibility endpoint should parse");
}

void testConnectionRulesRejectMalformedCompatibility() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.composition")
    });
    spec.insert(QStringLiteral("connection_rules"), QJsonObject{
        {QStringLiteral("compatibility"), QJsonArray{
            QJsonObject{
                {QStringLiteral("connection_type"), QStringLiteral("interface")},
                {QStringLiteral("to"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("bus")}}},
                {QStringLiteral("arity"), QStringLiteral("many")}
            }
        }}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "malformed compatibility rule should fail");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.missing_required"),
                                         QStringLiteral("$.connection_rules.compatibility[0].from")),
            "missing compatibility endpoint should be diagnosed");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.invalid_value"),
                                         QStringLiteral("$.connection_rules.compatibility[0].arity")),
            "invalid compatibility arity should be diagnosed");
}

void testConnectionRulesRejectAliasCaseCollisionsAndUnknownGraphEndpoints() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.composition"),
        QStringLiteral("ipcraft.graph_config")
    });
    spec.insert(QStringLiteral("connection_rules"), QJsonObject{
        {QStringLiteral("protocol_aliases"), QJsonObject{
            {QStringLiteral("AXI"), QStringLiteral("axi4")},
            {QStringLiteral("axi"), QStringLiteral("axi3")}
        }}
    });
    spec.insert(QStringLiteral("graph_config"), QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("obj0")},
                        {QStringLiteral("type"), QStringLiteral("vendor.node")}}
        }},
        {QStringLiteral("relationships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("rel0")},
                {QStringLiteral("type"), QStringLiteral("vendor.link")},
                {QStringLiteral("endpoints"), QJsonArray{
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("obj0")},
                                {QStringLiteral("role"), QStringLiteral("source")}},
                    QJsonObject{{QStringLiteral("object"), QStringLiteral("missing")},
                                {QStringLiteral("role"), QStringLiteral("target")}}
                }}
            }
        }}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "alias collisions and unknown graph endpoint objects should fail");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.invalid_value"),
                                         QStringLiteral("$.connection_rules.protocol_aliases.axi")),
            "case-folded alias collision should be diagnosed");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.invalid_value"),
                                         QStringLiteral("$.graph_config.relationships[0].endpoints[1].object")),
            "package graph_config endpoint should reference a declared object");
}

void testFlowScopeDefaultsToInstanceAndRejectsInvalidValues() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.flows")
    });
    spec.insert(QStringLiteral("flows"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("generate")},
            {QStringLiteral("steps"), QJsonArray{}}
        }
    });
    writePackage(root, spec);

    ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "flow without scope should default to instance scope");
    const QJsonObject defaultedFlow = result.spec.flows.first().toObject();
    require(defaultedFlow.value(QStringLiteral("scope")).toString() == QStringLiteral("instance"),
            "missing flow scope should be normalized to instance");

    QJsonObject flow = spec.value(QStringLiteral("flows")).toArray().first().toObject();
    flow.insert(QStringLiteral("scope"), QStringLiteral("system"));
    spec.insert(QStringLiteral("flows"), QJsonArray{flow});
    writePackage(root, spec);

    result = ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "flow with invalid scope should fail");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.invalid_flow"),
                                         QStringLiteral("$.flows[0].scope")),
            "invalid flow scope should emit package.invalid_flow");

    flow.insert(QStringLiteral("scope"), QStringLiteral("instance"));
    spec.insert(QStringLiteral("flows"), QJsonArray{flow});
    writePackage(root, spec);

    result = ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "flow with explicit instance scope should load");
}

void testPluginMetadataIsSeparateFromExtensions() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.config.params")
    });
    spec.insert(QStringLiteral("plugin"), QJsonObject{
        {QStringLiteral("library"), QStringLiteral("plugins/libdemo.so")},
        {QStringLiteral("entry"), QStringLiteral("DemoPlugin")}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "plugin package should load");
    require(result.spec.hasPlugin, "plugin should be present");
    require(result.spec.hasExtension(QStringLiteral("ipcraft.config.params")),
            "extension should be present");
    require(!result.spec.hasExtension(QStringLiteral("plugins/libdemo.so")),
            "plugin metadata should not become an extension");
}

void testGraphConfigRequiresExplicitExtension() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("graph_config"), QJsonObject{
        {QStringLiteral("schema"), ipcraft::schemaids::graphConfigV1},
        {QStringLiteral("objects"), QJsonArray{}},
        {QStringLiteral("relationships"), QJsonArray{}}
    });
    writePackage(root, spec);

    ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "graph_config should require explicit extension");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.extension_required"),
                                         QStringLiteral("$.graph_config")),
            "graph_config missing extension should be diagnosed");

    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.graph_config")
    });
    writePackage(root, spec);
    result = ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(result.ok, "graph_config should load when extension is declared");
    require(result.spec.graphConfig.value(QStringLiteral("schema")).toString() ==
                ipcraft::schemaids::graphConfigV1,
            "graph_config payload should be preserved");
}

void testGraphConfigRejectsNonContractShape() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.graph_config")
    });
    spec.insert(QStringLiteral("graph_config"), QJsonObject{
        {QStringLiteral("object_types"), QJsonArray{}}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "graph_config should match ipcraft.graph-config.v1 shape");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.missing_required"),
                                         QStringLiteral("$.graph_config.schema")),
            "missing graph_config schema should be diagnosed");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.unknown_field"),
                                         QStringLiteral("$.graph_config.object_types")),
            "unknown graph_config field should be diagnosed");
}

void testConfigSchemaRejectsUnknownFields() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.config.params")
    });
    spec.insert(QStringLiteral("config_schema"), QJsonObject{
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("surprise"), true}
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "unknown config_schema field should fail");
    require(hasPackageParserDiagnosticAt(result.diagnostics,
                                         QStringLiteral("package.unknown_field"),
                                         QStringLiteral("$.config_schema.surprise")),
            "unknown config_schema field should be diagnosed");
}

void testPackageParserRejectsPathTraversal() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.emitters")
    });
    spec.insert(QStringLiteral("emitters"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("bad")},
            {QStringLiteral("path"), QStringLiteral("../escape.yml")}
        }
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "path traversal should fail");
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.path_escape")),
            "path traversal should emit package.path_escape");
}

void testPackageParserRejectsAbsoluteAndSymlinkEscapePaths() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QTemporaryDir outside;
    require(outside.isValid(), "outside temporary directory should be valid");
    QDir root(temp.path());

    QJsonObject absoluteSpec = minimalPackageSpec();
    absoluteSpec.insert(QStringLiteral("plugin"), QJsonObject{
        {QStringLiteral("library"), QFileInfo(outside.filePath(QStringLiteral("libdemo.so"))).absoluteFilePath()}
    });
    writePackage(root, absoluteSpec);
    ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "absolute package-local path should fail");
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.path_escape")),
            "absolute path should emit package.path_escape");

    writeFile(outside.filePath(QStringLiteral("libdemo.so")), QByteArrayLiteral("plugin"));
    const QString linkPath = root.filePath(QStringLiteral("libdemo.so"));
    if (!QFile::link(outside.filePath(QStringLiteral("libdemo.so")), linkPath)) {
        return;
    }

    QJsonObject symlinkSpec = minimalPackageSpec();
    symlinkSpec.insert(QStringLiteral("plugin"), QJsonObject{
        {QStringLiteral("library"), QStringLiteral("libdemo.so")}
    });
    writePackage(root, symlinkSpec);
    result = ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "symlink escape should fail");
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.path_escape")),
            "symlink escape should emit package.path_escape");
}

void testPackageParserRejectsSymlinkAncestorWithMissingLeaf() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QTemporaryDir outside;
    require(outside.isValid(), "outside temporary directory should be valid");
    QDir root(temp.path());

    const QString linkPath = root.filePath(QStringLiteral("outside-link"));
    std::error_code error;
    std::filesystem::create_directory_symlink(outside.path().toStdString(),
                                              linkPath.toStdString(),
                                              error);
    if (error) {
        return;
    }

    QJsonObject spec = minimalPackageSpec();
    spec.insert(QStringLiteral("extensions"), QJsonArray{
        QStringLiteral("ipcraft.emitters")
    });
    spec.insert(QStringLiteral("emitters"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("bad")},
            {QStringLiteral("path"), QStringLiteral("outside-link/missing.yml")}
        }
    });
    writePackage(root, spec);

    const ipcraft::PackageSpecReadResult result =
        ipcraft::PackageSpecReader().readPackageRoot(root.absolutePath());
    require(!result.ok, "symlink ancestor with missing leaf should fail");
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.path_escape")),
            "symlink ancestor escape should emit package.path_escape");
}

void testPackageResolverRequiresExactVersion() {
    ipcraft::PackageSpec spec;
    spec.id = QStringLiteral("vendor.example.simple");
    spec.version = QStringLiteral("1.0.0");
    QVector<ipcraft::PackageSpec> packages{spec};

    const ipcraft::PackageSpecResolveResult ok =
        ipcraft::resolvePackageSpec(packages,
                                    QStringLiteral("vendor.example.simple"),
                                    QStringLiteral("1.0.0"));
    require(ok.ok, "exact package version should resolve");

    const ipcraft::PackageSpecResolveResult missingVersion =
        ipcraft::resolvePackageSpec(packages,
                                    QStringLiteral("vendor.example.simple"),
                                    QStringLiteral("2.0.0"));
    require(!missingVersion.ok, "missing exact version should fail");
    require(hasDiagnosticRule(missingVersion.diagnostics,
                              QStringLiteral("package.version_not_found")),
            "missing exact version should emit package.version_not_found");
}

void testPackageResolverRequiresPackageId() {
    const ipcraft::PackageSpecResolveResult missingId =
        ipcraft::resolvePackageSpec({},
                                    QStringLiteral("vendor.example.missing"),
                                    QStringLiteral("1.0.0"));
    require(!missingId.ok, "missing package id should fail");
    require(hasDiagnosticRule(missingId.diagnostics,
                              QStringLiteral("package.not_found")),
            "missing package id should emit package.not_found");
}

void testPackageDiscoveryReadsDirectAndOneLevelCollectionRoots() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir root(temp.path());
    require(root.mkpath(QStringLiteral("direct")), "failed to create direct package");
    require(root.mkpath(QStringLiteral("collection/child")), "failed to create collection package");
    require(root.mkpath(QStringLiteral("collection/nested/grandchild")),
            "failed to create nested package");

    QDir direct(root.filePath(QStringLiteral("direct")));
    QDir child(root.filePath(QStringLiteral("collection/child")));
    QDir grandchild(root.filePath(QStringLiteral("collection/nested/grandchild")));
    writePackage(direct, minimalPackageSpec(QStringLiteral("vendor.example.direct"),
                                            QStringLiteral("1.0.0")));
    writePackage(child, minimalPackageSpec(QStringLiteral("vendor.example.child"),
                                           QStringLiteral("1.0.0")));
    writePackage(grandchild, minimalPackageSpec(QStringLiteral("vendor.example.grandchild"),
                                                QStringLiteral("1.0.0")));

    const ipcraft::PackageSpecCollectionResult result =
        ipcraft::PackageSpecReader().discoverPackageRoots({
            direct.absolutePath(),
            root.filePath(QStringLiteral("collection"))
        });
    require(result.packages.size() == 2, "discovery should read direct and one-level roots only");
}

void testPackageResolverRejectsAmbiguousPackageRoots() {
    QTemporaryDir temp;
    require(temp.isValid(), "temporary directory should be valid");
    QDir collection(temp.path());
    require(collection.mkpath(QStringLiteral("a")), "failed to create package a");
    require(collection.mkpath(QStringLiteral("b")), "failed to create package b");

    QDir a(collection.filePath(QStringLiteral("a")));
    QDir b(collection.filePath(QStringLiteral("b")));
    writePackage(a, minimalPackageSpec(QStringLiteral("vendor.example.simple"),
                                       QStringLiteral("1.0.0")));
    writePackage(b, minimalPackageSpec(QStringLiteral("vendor.example.simple"),
                                       QStringLiteral("1.0.0")));

    const ipcraft::PackageSpecCollectionResult result =
        ipcraft::PackageSpecReader().discoverPackageRoots({collection.absolutePath()});
    require(hasDiagnosticRule(result.diagnostics, QStringLiteral("package.duplicate_version")),
            "duplicate package id/version should emit package.duplicate_version");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        testRuntimeLoadsPackageV1WithoutIpcoreYml();
        testRuntimeIgnoresIpcoreYmlWhenPackageV1Exists();
        testRejectsUnsupportedPackageSchema();
        testRejectsDuplicateJsonKeys();
        testOptionalSectionRequiresExplicitExtension();
        testEmptyOptionalSectionStillRequiresExplicitExtension();
        testExtensionArrayObjectDeclarationSatisfiesRequirement();
        testExtensionPayloadObjectDoesNotEnableCapability();
        testExtensionDeclarationsMatchSchemaContract();
        testUnknownExtensionIsRejected();
        testDuplicateTableIdsAreRejected();
        testConnectionRulesParseAliasesAndCompatibility();
        testConnectionRulesRejectMalformedCompatibility();
        testConnectionRulesRejectAliasCaseCollisionsAndUnknownGraphEndpoints();
        testFlowScopeDefaultsToInstanceAndRejectsInvalidValues();
        testPluginMetadataIsSeparateFromExtensions();
        testGraphConfigRequiresExplicitExtension();
        testGraphConfigRejectsNonContractShape();
        testConfigSchemaRejectsUnknownFields();
        testPackageParserRejectsPathTraversal();
        testPackageParserRejectsAbsoluteAndSymlinkEscapePaths();
        testPackageParserRejectsSymlinkAncestorWithMissingLeaf();
        testPackageResolverRequiresExactVersion();
        testPackageResolverRequiresPackageId();
        testPackageDiscoveryReadsDirectAndOneLevelCollectionRoots();
        testPackageResolverRejectsAmbiguousPackageRoots();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "ipcraft_package_spec_test passed\n";
    return 0;
}
