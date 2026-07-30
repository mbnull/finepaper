#include "noc/model.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QJsonArray>
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

    if (failures == 0) {
        QTextStream(stdout) << "JSON version tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures << " JSON version test(s) failed" << Qt::endl;
    return 1;
}
