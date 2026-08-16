#include "noc/model.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>

namespace {

using namespace finepaper;

int failures = 0;

const QStringList kDomainArrayFields{
    QStringLiteral("domains"),
    QStringLiteral("domainMemberships"),
    QStringLiteral("domainRelations"),
    QStringLiteral("crossingPolicies"),
    QStringLiteral("edgeOverrides")
};

const QString kElementConfigurationsField =
    QStringLiteral("elementConfigurations");

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool hasDiagnostic(const QVector<Diagnostic>& diagnostics,
                   const QString& code,
                   const QString& path = {}) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code && (path.isEmpty() || diagnostic.path == path);
    });
}

bool writeBytes(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QJsonObject withUnknownRecordField(QJsonObject document,
                                   const QString& arrayField,
                                   const QString& nestedObjectField = {}) {
    QJsonArray records = document.value(arrayField).toArray();
    if (records.isEmpty() || !records.at(0).isObject()) {
        return document;
    }
    QJsonObject record = records.at(0).toObject();
    if (nestedObjectField.isEmpty()) {
        record.insert(QStringLiteral("futureField"), true);
    } else {
        QJsonObject nested = record.value(nestedObjectField).toObject();
        nested.insert(QStringLiteral("futureField"), true);
        record.insert(nestedObjectField, nested);
    }
    records[0] = record;
    document.insert(arrayField, records);
    return document;
}

NocDesign versionOneDesign() {
    NocDesign design;
    design.formatVersion = 1;
    design.id = QStringLiteral("version_one");
    design.name = QStringLiteral("Version one");
    design.package = PackageReference{
        QStringLiteral("test.json-version"),
        QStringLiteral("1.0.0")
    };
    design.topology = TopologySpec{QStringLiteral("mesh"), 2, 2};
    return design;
}

NocDesign versionTwoDesign() {
    NocDesign design = versionOneDesign();
    design.formatVersion = 2;
    design.id = QStringLiteral("version_two");
    design.name = QStringLiteral("Version two");
    design.packageData = QJsonObject{{QStringLiteral("opaque"), true}};
    design.endpoints.append(EndpointInstance{
        QStringLiteral("ep0"),
        QStringLiteral("client"),
        EndpointAttachment{RouterPosition{0, 0}, QStringLiteral("local0")},
        QJsonObject{{QStringLiteral("dataWidth"), 64}}
    });
    design.domains = {
        DomainDefinition{
            QStringLiteral("clk_a"),
            QStringLiteral("clock"),
            QStringLiteral("Clock A"),
            QJsonObject{{QStringLiteral("frequencyMHz"), 800}}
        },
        DomainDefinition{
            QStringLiteral("clk_b"),
            QStringLiteral("clock"),
            QStringLiteral("Clock B"),
            QJsonObject{{QStringLiteral("frequencyMHz"), 400}}
        },
        DomainDefinition{
            QStringLiteral("pd_main"),
            QStringLiteral("power"),
            QStringLiteral("Main power"),
            QJsonObject{{QStringLiteral("voltageMv"), 900}}
        }
    };
    design.domainMemberships = {
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clk_a")}},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        },
        DomainMembership{
            ElementRef{ElementKind::Router, QStringLiteral("r-1-0")},
            QHash<QString, QStringList>{
                {QStringLiteral("clock"), QStringList{QStringLiteral("clk_b")}},
                {QStringLiteral("power"), QStringList{QStringLiteral("pd_main")}}
            }
        }
    };
    design.domainRelations.append(DomainRelation{
        QStringLiteral("synchronous-peer"),
        QStringLiteral("clk_a"),
        QStringLiteral("clk_b"),
        QJsonObject{{QStringLiteral("latency"), 1}}
    });
    design.crossingPolicies.append(DomainCrossingPolicy{
        QStringLiteral("clock_async"),
        QStringLiteral("clock"),
        QStringLiteral("clk_a"),
        QStringLiteral("clk_b"),
        QJsonObject{{QStringLiteral("synchronizerStages"), 2}}
    });
    design.edgeOverrides.append(DomainEdgeOverride{
        ElementRef{
            ElementKind::RouterLink,
            linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
        },
        QStringLiteral("clock"),
        QStringLiteral("clock_async"),
        QJsonObject{{QStringLiteral("implementation"), QStringLiteral("async-fifo")}}
    });
    return design;
}

NocDesign versionThreeDesign() {
    NocDesign design = versionTwoDesign();
    design.formatVersion = 3;
    design.id = QStringLiteral("version_three");
    design.name = QStringLiteral("Version three");
    design.elementConfigurations = {
        ElementConfiguration{
            ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
            QStringLiteral("router.microarchitecture"),
            QJsonObject{
                {QStringLiteral("inputBufferDepth"), 4},
                {QStringLiteral("virtualChannels"), 2}
            }
        },
        ElementConfiguration{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            },
            QStringLiteral("link.pipeline"),
            QJsonObject{{QStringLiteral("stages"), 2}}
        },
        ElementConfiguration{
            ElementRef{ElementKind::EndpointAttachment, QStringLiteral("ep0")},
            QStringLiteral("attachment.buffering"),
            QJsonObject{{QStringLiteral("requestDepth"), 8}}
        }
    };
    return design;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    const NocDesign v1Design = versionOneDesign();
    const QJsonObject v1Json = designToJson(v1Design);
    for (const QString& field : kDomainArrayFields) {
        check(!v1Json.contains(field),
              QStringLiteral("V1 serialization omits %1").arg(field));
    }
    check(!v1Json.contains(kElementConfigurationsField),
          QStringLiteral("V1 serialization omits elementConfigurations"));
    const DesignLoadResult v1Reloaded = designFromJson(v1Json);
    check(v1Reloaded.success && v1Reloaded.design.formatVersion == 1,
          QStringLiteral("a normal V1 document loads successfully"));

    for (const QString& field : kDomainArrayFields) {
        QJsonObject invalidV1 = v1Json;
        invalidV1.insert(field, QJsonArray{});
        const DesignLoadResult result = designFromJson(invalidV1);
        check(!result.success
                  && hasDiagnostic(result.diagnostics,
                                   QStringLiteral("design.domains_require_v2"),
                                   QLatin1Char('/') + field),
              QStringLiteral("V1 rejects an explicitly empty %1 field").arg(field));
    }
    QJsonObject invalidV1ElementConfigurations = v1Json;
    invalidV1ElementConfigurations.insert(kElementConfigurationsField,
                                          QJsonArray{});
    const DesignLoadResult invalidV1ElementConfigurationsResult =
        designFromJson(invalidV1ElementConfigurations);
    check(!invalidV1ElementConfigurationsResult.success
              && hasDiagnostic(
                  invalidV1ElementConfigurationsResult.diagnostics,
                  QStringLiteral("design.element_configurations_require_v3"),
                  QStringLiteral("/elementConfigurations")),
          QStringLiteral("V1 rejects an explicitly empty elementConfigurations field"));

    const NocDesign v2Design = versionTwoDesign();
    const QJsonObject v2Json = designToJson(v2Design);
    for (const QString& field : kDomainArrayFields) {
        check(v2Json.value(field).isArray(),
              QStringLiteral("V2 serialization emits the required %1 array").arg(field));
    }
    check(!v2Json.contains(kElementConfigurationsField),
          QStringLiteral("V2 serialization omits elementConfigurations"));

    const DesignLoadResult v2Reloaded = designFromJson(v2Json);
    check(v2Reloaded.success, QStringLiteral("a complete V2 document loads successfully"));
    if (v2Reloaded.success) {
        check(v2Reloaded.design.domains == v2Design.domains,
              QStringLiteral("V2 round-trip preserves Domain definitions"));
        check(v2Reloaded.design.domainMemberships == v2Design.domainMemberships,
              QStringLiteral("V2 round-trip preserves Domain memberships"));
        check(v2Reloaded.design.domainRelations == v2Design.domainRelations,
              QStringLiteral("V2 round-trip preserves Domain relations"));
        check(v2Reloaded.design.crossingPolicies == v2Design.crossingPolicies,
              QStringLiteral("V2 round-trip preserves crossing policies"));
        check(v2Reloaded.design.edgeOverrides == v2Design.edgeOverrides,
              QStringLiteral("V2 round-trip preserves edge overrides"));
        check(designToJson(v2Reloaded.design) == v2Json,
              QStringLiteral("V2 serialization is stable after a full round-trip"));
    }

    for (const QString& field : QStringList{
             QStringLiteral("domainRelations"),
             QStringLiteral("crossingPolicies"),
             QStringLiteral("edgeOverrides")}) {
        const DesignLoadResult unknownRecord = designFromJson(
            withUnknownRecordField(v2Json, field));
        check(!unknownRecord.success
                  && hasDiagnostic(
                      unknownRecord.diagnostics,
                      QStringLiteral("json.unknown_field"),
                      QLatin1Char('/') + field
                          + QStringLiteral("/0/futureField")),
              QStringLiteral(
                  "V2 rejects future fields in %1 records").arg(field));
    }
    const DesignLoadResult unknownOverrideElement = designFromJson(
        withUnknownRecordField(
            v2Json,
            QStringLiteral("edgeOverrides"),
            QStringLiteral("edge")));
    check(!unknownOverrideElement.success
              && hasDiagnostic(
                  unknownOverrideElement.diagnostics,
                  QStringLiteral("json.unknown_field"),
                  QStringLiteral(
                      "/edgeOverrides/0/edge/futureField")),
          QStringLiteral(
              "V2 rejects future fields in nested ElementRef records"));

    for (const QString& field : kDomainArrayFields) {
        QJsonObject incompleteV2 = v2Json;
        incompleteV2.remove(field);
        const DesignLoadResult result = designFromJson(incompleteV2);
        check(!result.success
                  && hasDiagnostic(result.diagnostics,
                                   QStringLiteral("json.expected_array"),
                                   QLatin1Char('/') + field),
              QStringLiteral("V2 rejects a missing %1 array").arg(field));
    }

    QJsonObject invalidV2ElementConfigurations = v2Json;
    invalidV2ElementConfigurations.insert(kElementConfigurationsField,
                                          QJsonArray{});
    const DesignLoadResult invalidV2ElementConfigurationsResult =
        designFromJson(invalidV2ElementConfigurations);
    check(!invalidV2ElementConfigurationsResult.success
              && hasDiagnostic(
                  invalidV2ElementConfigurationsResult.diagnostics,
                  QStringLiteral("design.element_configurations_require_v3"),
                  QStringLiteral("/elementConfigurations")),
          QStringLiteral("V2 rejects an explicitly empty elementConfigurations field"));

    const NocDesign v3Design = versionThreeDesign();
    const QJsonObject v3Json = designToJson(v3Design);
    for (const QString& field : kDomainArrayFields) {
        check(v3Json.value(field).isArray(),
              QStringLiteral("V3 serialization retains the required %1 array").arg(field));
    }
    check(v3Json.value(kElementConfigurationsField).isArray(),
          QStringLiteral("V3 serialization emits the required elementConfigurations array"));

    const DesignLoadResult v3Reloaded = designFromJson(v3Json);
    check(v3Reloaded.success,
          QStringLiteral("a complete V3 document loads successfully"));
    if (v3Reloaded.success) {
        check(v3Reloaded.design.elementConfigurations
                  == v3Design.elementConfigurations,
              QStringLiteral("V3 round-trip preserves sparse element configurations"));
        check(designToJson(v3Reloaded.design) == v3Json,
              QStringLiteral("V3 serialization is stable after a full round-trip"));
    }
    const DesignLoadResult unknownElementConfiguration = designFromJson(
        withUnknownRecordField(
            v3Json, QStringLiteral("elementConfigurations")));
    check(!unknownElementConfiguration.success
              && hasDiagnostic(
                  unknownElementConfiguration.diagnostics,
                  QStringLiteral("json.unknown_field"),
                  QStringLiteral(
                      "/elementConfigurations/0/futureField")),
          QStringLiteral(
              "V3 rejects future fields in element configuration records"));

    QJsonObject missingV3ElementConfigurations = v3Json;
    missingV3ElementConfigurations.remove(kElementConfigurationsField);
    const DesignLoadResult missingV3ElementConfigurationsResult =
        designFromJson(missingV3ElementConfigurations);
    check(!missingV3ElementConfigurationsResult.success
              && hasDiagnostic(
                  missingV3ElementConfigurationsResult.diagnostics,
                  QStringLiteral("json.expected_array"),
                  QStringLiteral("/elementConfigurations")),
          QStringLiteral("V3 rejects a missing elementConfigurations array"));

    for (const QString& field : kDomainArrayFields) {
        QJsonObject incompleteV3 = v3Json;
        incompleteV3.remove(field);
        const DesignLoadResult result = designFromJson(incompleteV3);
        check(!result.success
                  && hasDiagnostic(result.diagnostics,
                                   QStringLiteral("json.expected_array"),
                                   QLatin1Char('/') + field),
              QStringLiteral("V3 rejects a missing %1 array").arg(field));
    }

    QJsonObject v4Json = v3Json;
    v4Json.insert(QStringLiteral("formatVersion"), 4);
    const DesignLoadResult v4Result = designFromJson(v4Json);
    check(!v4Result.success
              && hasDiagnostic(v4Result.diagnostics,
                               QStringLiteral("design.unsupported_version"),
                               QStringLiteral("/formatVersion")),
          QStringLiteral("formatVersion 4 is rejected"));

    QTemporaryDir resourceFixture(
        QStringLiteral("/tmp/finepaper-design-json-budget-XXXXXX"));
    check(resourceFixture.isValid(),
          QStringLiteral("Design JSON resource fixture is available"));
    if (resourceFixture.isValid()) {
        const QString oversizedPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("oversized.fpnoc"));
        check(writeBytes(
                  oversizedPath,
                  QByteArray(kMaximumDesignJsonBytes + 1, 'x')),
              QStringLiteral("oversized Design JSON fixture is writable"));
        const DesignLoadResult oversized = loadDesign(oversizedPath);
        check(!oversized.success
                  && hasDiagnostic(
                      oversized.diagnostics,
                      QStringLiteral("json.document_too_large"),
                      oversizedPath),
              QStringLiteral(
                  "Design JSON byte budget is enforced before parsing"));

        QJsonArray excessiveItems;
        for (int index = 0; index <= 65'536; ++index) {
            excessiveItems.append(index);
        }
        const QString arrayPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("array.fpnoc"));
        check(writeBytes(
                  arrayPath,
                  QJsonDocument(QJsonObject{
                      {QStringLiteral("value"), excessiveItems}})
                      .toJson(QJsonDocument::Compact)),
              QStringLiteral("oversized Design JSON array is writable"));
        const DesignLoadResult excessiveArray = loadDesign(arrayPath);
        check(!excessiveArray.success
                  && hasDiagnostic(
                      excessiveArray.diagnostics,
                      QStringLiteral("json.array_too_large"),
                      arrayPath + QStringLiteral("/value")),
              QStringLiteral("Design JSON array budget is enforced"));

        const QString stringPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("string.fpnoc"));
        check(writeBytes(
                  stringPath,
                  QJsonDocument(QJsonObject{
                      {QStringLiteral("value"),
                       QString(65'537, QLatin1Char('s'))}})
                      .toJson(QJsonDocument::Compact)),
              QStringLiteral("oversized Design JSON string is writable"));
        const DesignLoadResult excessiveString = loadDesign(stringPath);
        check(!excessiveString.success
                  && hasDiagnostic(
                      excessiveString.diagnostics,
                      QStringLiteral("json.string_too_long"),
                      stringPath + QStringLiteral("/value")),
              QStringLiteral("Design JSON string budget is enforced"));

        QByteArray deepJson("{\"value\":");
        for (int index = 0; index < 65; ++index) {
            deepJson.append('[');
        }
        deepJson.append("null");
        for (int index = 0; index < 65; ++index) {
            deepJson.append(']');
        }
        deepJson.append('}');
        const QString depthPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("depth.fpnoc"));
        check(writeBytes(depthPath, deepJson),
              QStringLiteral("deep Design JSON fixture is writable"));
        const DesignLoadResult excessiveDepth = loadDesign(depthPath);
        const QString depthPointer = QStringLiteral("/value")
            + QStringLiteral("/0").repeated(64);
        check(!excessiveDepth.success
                  && hasDiagnostic(
                      excessiveDepth.diagnostics,
                      QStringLiteral("json.depth_exceeded"),
                      depthPath + depthPointer),
              QStringLiteral("Design JSON nesting budget is enforced"));

        QJsonObject excessiveMembers;
        for (int index = 0; index <= 65'536; ++index) {
            excessiveMembers.insert(QStringLiteral("member%1").arg(index),
                                    QJsonValue::Null);
        }
        const QString membersPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("object-members.fpnoc"));
        check(writeBytes(
                  membersPath,
                  QJsonDocument(QJsonObject{
                      {QStringLiteral("value"), excessiveMembers}})
                      .toJson(QJsonDocument::Compact)),
              QStringLiteral("oversized Design JSON object fixture is writable"));
        const DesignLoadResult excessiveMembersResult = loadDesign(membersPath);
        check(!excessiveMembersResult.success
                  && hasDiagnostic(excessiveMembersResult.diagnostics,
                                   QStringLiteral("json.object_too_large"),
                                   membersPath + QStringLiteral("/value")),
              QStringLiteral("Design JSON object member budget is enforced"));

        const QString longKey(4'097, QLatin1Char('k'));
        const QString keyPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("object-key.fpnoc"));
        check(writeBytes(
                  keyPath,
                  QJsonDocument(QJsonObject{
                      {QStringLiteral("value"),
                       QJsonObject{{longKey, QJsonValue::Null}}}})
                      .toJson(QJsonDocument::Compact)),
              QStringLiteral("oversized Design JSON object key fixture is writable"));
        const DesignLoadResult excessiveKey = loadDesign(keyPath);
        check(!excessiveKey.success
                  && hasDiagnostic(excessiveKey.diagnostics,
                                   QStringLiteral("json.object_key_too_long"),
                                   keyPath + QStringLiteral("/value/") + longKey),
              QStringLiteral("Design JSON object key budget is enforced"));

        QJsonObject valueBuckets;
        for (int bucket = 0; bucket < 16; ++bucket) {
            QJsonArray values;
            for (int index = 0; index < 65'536; ++index) {
                values.append(QJsonValue::Null);
            }
            valueBuckets.insert(
                QStringLiteral("bucket%1")
                    .arg(bucket, 2, 10, QLatin1Char('0')),
                values);
        }
        const QString valuesPath = QDir(resourceFixture.path()).filePath(
            QStringLiteral("value-budget.fpnoc"));
        check(writeBytes(
                  valuesPath,
                  QJsonDocument(QJsonObject{
                      {QStringLiteral("value"), valueBuckets}})
                      .toJson(QJsonDocument::Compact)),
              QStringLiteral("Design JSON value budget fixture is writable"));
        const DesignLoadResult excessiveValues = loadDesign(valuesPath);
        check(!excessiveValues.success
                  && hasDiagnostic(excessiveValues.diagnostics,
                                   QStringLiteral("json.value_budget_exceeded"),
                                   valuesPath + QStringLiteral(
                                       "/value/bucket15/16942")),
              QStringLiteral("Design JSON total value budget is enforced"));
    }

    if (failures == 0) {
        QTextStream(stdout) << "JSON version tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " JSON version test(s) failed" << Qt::endl;
    return 1;
}
