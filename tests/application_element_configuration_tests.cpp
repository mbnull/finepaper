#include "application/application.h"
#include "application/mesh_resize_plan.h"
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

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics,
                       const QString& code) {
    return std::any_of(
        diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

bool sameDesign(const NocDesign& lhs, const NocDesign& rhs) {
    return designToJson(lhs) == designToJson(rhs);
}

void checkAtomicFailure(const DesignResult& result,
                        const NocDesign& original,
                        const QString& code,
                        const QString& message) {
    check(!result.success && sameDesign(result.design, original)
              && hasDiagnosticCode(result.diagnostics, code),
          message);
}

const ElementConfiguration* findConfiguration(
    const NocDesign& design,
    const ElementRef& element,
    const QString& propertySet) {
    const auto configuration = std::find_if(
        design.elementConfigurations.cbegin(),
        design.elementConfigurations.cend(),
        [&](const ElementConfiguration& value) {
            return value.element == element && value.propertySet == propertySet;
        });
    return configuration == design.elementConfigurations.cend()
        ? nullptr : &(*configuration);
}

QJsonObject property(const QString& id,
                     const QString& type,
                     const QJsonValue& defaultValue,
                     std::optional<double> minimum = std::nullopt,
                     std::optional<double> maximum = std::nullopt,
                     const QJsonArray& values = {},
                     bool multiple = false) {
    QJsonObject object{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), type},
        {QStringLiteral("default"), defaultValue}
    };
    if (minimum) {
        object.insert(QStringLiteral("minimum"), *minimum);
    }
    if (maximum) {
        object.insert(QStringLiteral("maximum"), *maximum);
    }
    if (!values.isEmpty()) {
        object.insert(QStringLiteral("values"), values);
    }
    if (multiple) {
        object.insert(QStringLiteral("multiple"), true);
    }
    return object;
}

QJsonObject packageManifest() {
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("id"), QStringLiteral("test.element-configuration")},
        {QStringLiteral("name"), QStringLiteral("Element configuration test")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 3},
                {QStringLiteral("default"), 2}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 3},
                {QStringLiteral("default"), 2}
            }}
        }},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("endpointTypes"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("client")},
                {QStringLiteral("label"), QStringLiteral("Client")},
                {QStringLiteral("parameters"), QJsonArray{}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("memory")},
                {QStringLiteral("label"), QStringLiteral("Memory")},
                {QStringLiteral("parameters"), QJsonArray{}}
            }
        }},
        {QStringLiteral("domainTypes"), QJsonArray{}},
        {QStringLiteral("runtimeCapabilities"), QJsonObject{
            {QStringLiteral("domainConfiguration"), QJsonObject{
                {QStringLiteral("domains"), false},
                {QStringLiteral("memberships"), false},
                {QStringLiteral("relations"), false},
                {QStringLiteral("crossingPolicies"), false},
                {QStringLiteral("edgeOverrides"), false}
            }}
        }},
        {QStringLiteral("elementPropertySets"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("router-impl")},
                {QStringLiteral("label"), QStringLiteral("Router implementation")},
                {QStringLiteral("appliesTo"), QJsonArray{
                    QStringLiteral("router")
                }},
                {QStringLiteral("properties"), QJsonArray{
                    property(QStringLiteral("depth"),
                             QStringLiteral("integer"),
                             4,
                             1,
                             16),
                    property(QStringLiteral("mode"),
                             QStringLiteral("enum"),
                             QStringLiteral("safe"),
                             std::nullopt,
                             std::nullopt,
                             QJsonArray{QStringLiteral("safe"),
                                        QStringLiteral("fast")}),
                    property(QStringLiteral("weights"),
                             QStringLiteral("integer"),
                             QJsonArray{1},
                             0,
                             8,
                             {},
                             true),
                    property(QStringLiteral("enabled"),
                             QStringLiteral("boolean"),
                             true)
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("fabric-link")},
                {QStringLiteral("label"), QStringLiteral("Fabric link")},
                {QStringLiteral("appliesTo"), QJsonArray{
                    QStringLiteral("router-link")
                }},
                {QStringLiteral("properties"), QJsonArray{
                    property(QStringLiteral("latency"),
                             QStringLiteral("number"),
                             1.5,
                             0.0,
                             10.0)
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("client-attachment")},
                {QStringLiteral("label"), QStringLiteral("Client attachment")},
                {QStringLiteral("appliesTo"), QJsonArray{
                    QStringLiteral("endpoint-attachment")
                }},
                {QStringLiteral("endpointTypes"), QJsonArray{
                    QStringLiteral("client")
                }},
                {QStringLiteral("properties"), QJsonArray{
                    property(QStringLiteral("width"),
                             QStringLiteral("integer"),
                             32,
                             8,
                             256)
                }}
            }
        }},
        {QStringLiteral("attachment"), QJsonObject{
            {QStringLiteral("maxPerRouter"), 8},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")}
        }},
        {QStringLiteral("generator"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("element-config-test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/tool")},
            {QStringLiteral("supportsValidate"), false},
            {QStringLiteral("timeoutSeconds"), 10}
        }},
        {QStringLiteral("engine"), QJsonObject{
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/tool")},
            {QStringLiteral("providesValidation"), true},
            {QStringLiteral("timeoutSeconds"), 10}
        }}
    };
}

bool prepareFixture(const QString& packageRoot) {
    const QString executable = QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/tool"));
    if (!QDir().mkpath(QFileInfo(executable).absolutePath())) {
        return false;
    }
    QFile tool(executable);
    if (!tool.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray script = R"SCRIPT(#!/bin/sh
operation="$1"
shift
design=""
result=""
output=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --design) design="$2"; shift 2 ;;
        --result) result="$2"; shift 2 ;;
        --output) output="$2"; shift 2 ;;
        *) shift ;;
    esac
done
if ! grep -Eq '"formatVersion"[[:space:]]*:[[:space:]]*3' "$design" \
   || ! grep -q '"elementConfigurations"' "$design" \
   || ! grep -Eq '"depth"[[:space:]]*:[[:space:]]*8' "$design"; then
    printf '%s\n' '{"success":false,"diagnostics":[{"severity":"error","code":"mock.v3_input_missing","message":"V3 element configuration missing"}],"artifacts":[]}' > "$result"
    exit 1
fi
if [ "$operation" = "validate" ]; then
    printf '%s\n' '{"success":true,"diagnostics":[{"severity":"info","code":"mock.v3_input","message":"V3 input received"}]}' > "$result"
    exit 0
fi
mkdir -p "$output"
cp "$design" "$output/captured-design.json"
printf '%s\n' '{"success":true,"diagnostics":[],"artifacts":[{"id":"captured","type":"design","path":"captured-design.json","primary":true}]}' > "$result"
)SCRIPT";
    if (tool.write(script) != script.size()) {
        return false;
    }
    tool.close();
    if (!QFile::setPermissions(
            executable,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                | QFileDevice::ExeOther)) {
        return false;
    }
    return saveJsonObject(
        QDir(packageRoot).filePath(QStringLiteral("package.json")),
        packageManifest());
}

QJsonObject createRequest(bool includeConfigurations = false) {
    QJsonObject request{
        {QStringLiteral("id"), QStringLiteral("element_configuration")},
        {QStringLiteral("name"), QStringLiteral("Element Configuration")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.element-configuration")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("ep0")},
                {QStringLiteral("type"), QStringLiteral("client")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("ep1")},
                {QStringLiteral("type"), QStringLiteral("memory")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            }
        }}
    };
    if (includeConfigurations) {
        request.insert(
            QStringLiteral("elementConfigurations"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("element"), QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("router")},
                        {QStringLiteral("id"), QStringLiteral("r-0-0")}
                    }},
                    {QStringLiteral("propertySet"), QStringLiteral("router-impl")},
                    {QStringLiteral("properties"), QJsonObject{
                        {QStringLiteral("depth"), 8}
                    }}
                },
                QJsonObject{
                    {QStringLiteral("element"), QJsonObject{
                        {QStringLiteral("kind"),
                         QStringLiteral("endpoint-attachment")},
                        {QStringLiteral("id"), QStringLiteral("ep0")}
                    }},
                    {QStringLiteral("propertySet"),
                     QStringLiteral("client-attachment")},
                    {QStringLiteral("properties"), QJsonObject{
                        {QStringLiteral("width"), 64}
                    }}
                }
            });
    }
    return request;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication qtApplication(argc, argv);
    QTemporaryDir fixture(QStringLiteral(
        "/tmp/finepaper-element-configuration-test-XXXXXX"));
    check(fixture.isValid() && prepareFixture(fixture.path()),
          QStringLiteral("V3 Package fixture is available"));
    if (!fixture.isValid()) {
        return 1;
    }

    FinepaperApplication application;
    const PackageCatalogReloadResult packageReload =
        application.reloadPackages(QStringList{fixture.path()});
    check(packageReload.committed() && !hasErrors(packageReload.diagnostics)
              && application.packages().size() == 1,
          QStringLiteral("V3 Package with element property sets loads"));
    if (application.packages().isEmpty()) {
        return 1;
    }
    const PackageDefinition& package = application.packages().constFirst();

    const DesignResult created = application.createDesign(createRequest());
    check(created.success && created.design.formatVersion == 3
              && created.design.elementConfigurations.isEmpty(),
          QStringLiteral("Package V3 creates a Design V3 without materialized defaults"));
    if (!created.success) {
        for (const Diagnostic& diagnostic : created.diagnostics) {
            QTextStream(stderr) << diagnostic.code << ' ' << diagnostic.path
                                << ' ' << diagnostic.message << Qt::endl;
        }
        return 1;
    }
    const NocDesign base = created.design;

    const DesignResult createdWithConfigurations =
        application.createDesign(createRequest(true));
    check(createdWithConfigurations.success
              && createdWithConfigurations.design.elementConfigurations.size() == 2
              && findConfiguration(
                  createdWithConfigurations.design,
                  ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
                  QStringLiteral("router-impl")),
          QStringLiteral("createDesign parses and validates explicit V3 elementConfigurations"));

    QJsonObject malformedRequest = createRequest();
    malformedRequest.insert(QStringLiteral("elementConfigurations"), QJsonObject{});
    check(!application.createDesign(malformedRequest).success
              && hasDiagnosticCode(
                  application.createDesign(malformedRequest).diagnostics,
                  QStringLiteral("json.expected_array")),
          QStringLiteral("createDesign never silently ignores a malformed elementConfigurations field"));

    QJsonObject nonSparseRequest = createRequest();
    nonSparseRequest.insert(
        QStringLiteral("elementConfigurations"),
        QJsonArray{QJsonObject{
            {QStringLiteral("element"), QJsonObject{
                {QStringLiteral("kind"), QStringLiteral("router")},
                {QStringLiteral("id"), QStringLiteral("r-0-0")}
            }},
            {QStringLiteral("propertySet"), QStringLiteral("router-impl")},
            {QStringLiteral("properties"), QJsonObject{
                {QStringLiteral("depth"), 4}
            }}
        }});
    const DesignResult nonSparseCreate = application.createDesign(nonSparseRequest);
    check(!nonSparseCreate.success
              && hasDiagnosticCode(
                  nonSparseCreate.diagnostics,
                  QStringLiteral("element_configuration.non_sparse_property")),
          QStringLiteral("createDesign rejects default-valued persisted overrides"));

    const ElementRef router{
        ElementKind::Router, QStringLiteral("r-0-0")};
    const ResolvedElementConfiguration defaults = resolveElementConfiguration(
        base, package, router, QStringLiteral("router-impl"));
    check(defaults.success() && defaults.overrideProperties.isEmpty()
              && defaults.defaultProperties.value(QStringLiteral("depth")).toInt() == 4
              && defaults.properties == defaults.defaultProperties,
          QStringLiteral("resolver exposes Package defaults separately from sparse overrides"));

    const DesignResult configured = application.setElementConfiguration(
        base,
        router,
        QStringLiteral("router-impl"),
        QJsonObject{
            {QStringLiteral("depth"), 8},
            {QStringLiteral("mode"), QStringLiteral("safe")},
            {QStringLiteral("weights"), QJsonArray{1}},
            {QStringLiteral("enabled"), true}
        });
    const ElementConfiguration* routerConfiguration = configured.success
        ? findConfiguration(
              configured.design, router, QStringLiteral("router-impl"))
        : nullptr;
    check(configured.success && routerConfiguration
              && routerConfiguration->properties
                  == QJsonObject{{QStringLiteral("depth"), 8}},
          QStringLiteral("setElementConfiguration persists only values different from defaults"));
    const ResolvedElementConfiguration resolved = configured.success
        ? resolveElementConfiguration(
              configured.design,
              package,
              router,
              QStringLiteral("router-impl"))
        : ResolvedElementConfiguration{};
    check(resolved.success()
              && resolved.overrideProperties
                  == QJsonObject{{QStringLiteral("depth"), 8}}
              && resolved.properties.value(QStringLiteral("mode")).toString()
                  == QStringLiteral("safe")
              && resolved.properties.value(QStringLiteral("depth")).toInt() == 8,
          QStringLiteral("resolver overlays sparse values on complete defaults"));

    if (configured.success) {
        const DesignResult resetByDefaults = application.setElementConfiguration(
            configured.design,
            router,
            QStringLiteral("router-impl"),
            QJsonObject{});
        check(resetByDefaults.success
                  && !findConfiguration(
                      resetByDefaults.design,
                      router,
                      QStringLiteral("router-impl")),
              QStringLiteral("setting only defaults removes the persisted record"));

        const DesignResult cleared = application.clearElementConfiguration(
            configured.design, router, QStringLiteral("router-impl"));
        check(cleared.success
                  && !findConfiguration(
                      cleared.design, router, QStringLiteral("router-impl")),
              QStringLiteral("clearElementConfiguration removes the complete sparse record"));
    }

    checkAtomicFailure(
        application.setElementConfiguration(
            base,
            router,
            QStringLiteral("router-impl"),
            QJsonObject{{QStringLiteral("unknown"), 1}}),
        base,
        QStringLiteral("element_configuration.unknown_property"),
        QStringLiteral("unknown properties are rejected atomically"));
    checkAtomicFailure(
        application.setElementConfiguration(
            base,
            router,
            QStringLiteral("router-impl"),
            QJsonObject{{QStringLiteral("weights"), 3}}),
        base,
        QStringLiteral("element_configuration.invalid_type"),
        QStringLiteral("multiple properties require an array atomically"));
    checkAtomicFailure(
        application.setElementConfiguration(
            base,
            router,
            QStringLiteral("router-impl"),
            QJsonObject{{QStringLiteral("weights"), QJsonArray{9}}}),
        base,
        QStringLiteral("element_configuration.above_maximum"),
        QStringLiteral("multiple property items enforce numeric ranges atomically"));
    checkAtomicFailure(
        application.setElementConfiguration(
            base,
            router,
            QStringLiteral("router-impl"),
            QJsonObject{{QStringLiteral("mode"), QStringLiteral("turbo")}}),
        base,
        QStringLiteral("element_configuration.invalid_enum"),
        QStringLiteral("enum properties reject undeclared values atomically"));
    checkAtomicFailure(
        application.setElementConfiguration(
            base,
            ElementRef{ElementKind::EndpointAttachment,
                       QStringLiteral("ep1")},
            QStringLiteral("client-attachment"),
            QJsonObject{{QStringLiteral("width"), 64}}),
        base,
        QStringLiteral("element_configuration.endpoint_type_not_applicable"),
        QStringLiteral("Endpoint attachment property sets enforce Endpoint type filters"));
    checkAtomicFailure(
        application.setElementConfiguration(
            base,
            ElementRef{ElementKind::RouterLink,
                       linkId(QStringLiteral("r-0-0"),
                              QStringLiteral("r-1-0"))},
            QStringLiteral("router-impl"),
            QJsonObject{{QStringLiteral("depth"), 8}}),
        base,
        QStringLiteral("element_configuration.not_applicable"),
        QStringLiteral("property sets enforce their Package-declared element kinds"));

    const ElementRef attachment{
        ElementKind::EndpointAttachment, QStringLiteral("ep0")};
    const DesignResult attachmentConfigured =
        application.setElementConfiguration(
            base,
            attachment,
            QStringLiteral("client-attachment"),
            QJsonObject{{QStringLiteral("width"), 64}});
    check(attachmentConfigured.success,
          QStringLiteral("Endpoint attachment configuration can be created"));
    if (attachmentConfigured.success) {
        const DesignResult moved = application.moveEndpoint(
            attachmentConfigured.design,
            QStringLiteral("ep0"),
            RouterPosition{1, 1});
        check(moved.success
                  && findConfiguration(
                      moved.design,
                      attachment,
                      QStringLiteral("client-attachment")),
              QStringLiteral("moving an Endpoint retains its attachment configuration"));

        const auto endpoint = std::find_if(
            attachmentConfigured.design.endpoints.cbegin(),
            attachmentConfigured.design.endpoints.cend(),
            [](const EndpointInstance& value) {
                return value.id == QStringLiteral("ep0");
            });
        const ElementConfiguration* snapshot = findConfiguration(
            attachmentConfigured.design,
            attachment,
            QStringLiteral("client-attachment"));
        const DesignResult removed = application.removeEndpoint(
            attachmentConfigured.design, QStringLiteral("ep0"));
        check(removed.success
                  && !findConfiguration(
                      removed.design,
                      attachment,
                      QStringLiteral("client-attachment")),
              QStringLiteral("removing an Endpoint deletes its attachment configuration"));
        if (removed.success
            && endpoint != attachmentConfigured.design.endpoints.cend()
            && snapshot) {
            const DesignResult restored = application.addEndpoint(
                removed.design,
                *endpoint,
                {},
                {},
                QVector<ElementConfiguration>{*snapshot});
            check(restored.success
                      && findConfiguration(
                          restored.design,
                          attachment,
                          QStringLiteral("client-attachment")),
                  QStringLiteral("addEndpoint atomically restores attachment configuration snapshots"));

            ElementConfiguration mismatched = *snapshot;
            mismatched.element = router;
            checkAtomicFailure(
                application.addEndpoint(
                    removed.design,
                    *endpoint,
                    {},
                    {},
                    QVector<ElementConfiguration>{mismatched}),
                removed.design,
                QStringLiteral("endpoint.attachment_configuration_mismatch"),
                QStringLiteral("addEndpoint rejects mismatched attachment configuration snapshots atomically"));

            ElementConfiguration invalid = *snapshot;
            invalid.properties.insert(
                QStringLiteral("width"), QStringLiteral("sixty-four"));
            checkAtomicFailure(
                application.addEndpoint(
                    removed.design,
                    *endpoint,
                    {},
                    {},
                    QVector<ElementConfiguration>{invalid}),
                removed.design,
                QStringLiteral("element_configuration.invalid_type"),
                QStringLiteral("addEndpoint validates restored attachment configuration values atomically"));
        }
    }

    if (configured.success) {
        const DesignResult replacedDomains =
            application.replaceDomainConfiguration(
                configured.design, DomainConfiguration{});
        check(replacedDomains.success
                  && replacedDomains.design.elementConfigurations
                      == configured.design.elementConfigurations,
              QStringLiteral("replacing V3 Domain configuration preserves element configurations"));
    }

    const ElementRef removedRouter{
        ElementKind::Router, QStringLiteral("r-1-1")};
    const ElementRef removedLink{
        ElementKind::RouterLink,
        linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))};
    const DesignResult routerImpact = application.setElementConfiguration(
        base,
        removedRouter,
        QStringLiteral("router-impl"),
        QJsonObject{{QStringLiteral("depth"), 8}});
    const DesignResult linkImpact = routerImpact.success
        ? application.setElementConfiguration(
              routerImpact.design,
              removedLink,
              QStringLiteral("fabric-link"),
              QJsonObject{{QStringLiteral("latency"), 2.0}})
        : DesignResult{};
    check(linkImpact.success,
          QStringLiteral("Mesh shrink impact fixture is valid"));
    if (linkImpact.success) {
        const MeshResizePlan plan = buildMeshResizePlan(
            linkImpact.design, package, 1, 1);
        check(plan.removedElementConfigurations.size() == 2
                  && plan.requiresImpactConfirmation()
                  && hasDiagnosticCode(
                      plan.diagnostics,
                      QStringLiteral(
                          "mesh.resize_would_remove_element_configuration")),
              QStringLiteral("Mesh shrink previews exact Router and RouterLink configuration loss"));
        checkAtomicFailure(
            application.resizeMesh(linkImpact.design, 1, 1),
            linkImpact.design,
            QStringLiteral(
                "mesh.resize_missing_element_configuration_confirmation"),
            QStringLiteral("Mesh shrink rejects unconfirmed element configuration loss"));

        MeshResizeImpactConfirmation exact;
        exact.removedElementConfigurations =
            plan.removedElementConfigurations;
        MeshResizeImpactConfirmation stale = exact;
        stale.removedElementConfigurations[0].properties.insert(
            QStringLiteral("depth"), 12);
        checkAtomicFailure(
            application.resizeMesh(linkImpact.design, 1, 1, {}, stale),
            linkImpact.design,
            QStringLiteral(
                "mesh.resize_stale_element_configuration_confirmation"),
            QStringLiteral("Mesh shrink rejects stale configuration confirmations"));

        const DesignResult shrunk = application.resizeMesh(
            linkImpact.design, 1, 1, {}, exact);
        check(shrunk.success && shrunk.design.elementConfigurations.isEmpty(),
              QStringLiteral("confirmed Mesh shrink removes only the previewed configurations"));
        if (shrunk.success) {
            const DesignResult expanded = application.resizeMesh(
                shrunk.design, 2, 2);
            check(expanded.success
                      && expanded.design.elementConfigurations.isEmpty(),
                  QStringLiteral("Mesh expansion inherits defaults without materializing records"));
        }
    }

    if (configured.success) {
        const ValidationResult validation = application.validate(
            configured.design, true);
        check(validation.success
                  && hasDiagnosticCode(
                      validation.diagnostics,
                      QStringLiteral("mock.v3_input")),
              QStringLiteral("Engine validation receives Design V3 element configurations"));
        QTemporaryDir output(QStringLiteral(
            "/tmp/finepaper-element-configuration-output-XXXXXX"));
        if (output.isValid()) {
            const GenerationResult generation = application.generate(
                configured.design, GenerationOptions{output.path()});
            const DesignLoadResult captured = loadDesign(
                QDir(generation.outputDirectory).filePath(
                    QStringLiteral("captured-design.json")));
            check(generation.success && captured.success
                      && captured.design.formatVersion == 3
                      && captured.design.elementConfigurations
                          == configured.design.elementConfigurations,
                  QStringLiteral("Generator input preserves Design V3 and sparse element configurations"));
        }
    }

    if (failures == 0) {
        QTextStream(stdout)
            << "application element configuration tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
