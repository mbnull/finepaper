#include "application/application.h"
#include "application/domain_service.h"
#include "execution/cancellation.h"
#include "execution/process.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

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
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

bool createEmptyFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool writeExecutable(const QString& path, const QByteArray& contents) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(contents) != contents.size()) {
        return false;
    }
    file.close();
    return QFile::setPermissions(
        path,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner | QFileDevice::ReadGroup
            | QFileDevice::ExeGroup | QFileDevice::ReadOther
            | QFileDevice::ExeOther);
}

QJsonObject cancellationPackageManifest() {
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-package")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("test.cancellation")},
        {QStringLiteral("name"), QStringLiteral("Cancellation Test Package")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("mesh"), QJsonObject{
            {QStringLiteral("rows"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 2},
                {QStringLiteral("default"), 1}
            }},
            {QStringLiteral("columns"), QJsonObject{
                {QStringLiteral("min"), 1},
                {QStringLiteral("max"), 2},
                {QStringLiteral("default"), 1}
            }}
        }},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("endpointTypes"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("device")},
                {QStringLiteral("label"), QStringLiteral("Device")},
                {QStringLiteral("parameters"), QJsonArray{}}
            }
        }},
        {QStringLiteral("attachment"), QJsonObject{
            {QStringLiteral("maxPerRouter"), 2},
            {QStringLiteral("slotMode"), QStringLiteral("automatic")}
        }},
        {QStringLiteral("generator"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("cancellation-test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("executable"), QStringLiteral("runtime/bin/generate")},
            {QStringLiteral("supportsValidate"), true},
            {QStringLiteral("timeoutSeconds"), 10}
        }}
    };
}

bool prepareCancellationPackage(const QString& packageRoot) {
    const QByteArray script = R"SCRIPT(#!/bin/sh
operation="$1"
shift
result=""
output=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --result) result="$2"; shift 2 ;;
        --output) output="$2"; shift 2 ;;
        *) shift ;;
    esac
done
package_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "$operation" = "validate" ]; then
    : > "$package_root/validate.started"
    if [ -f "$package_root/slow-validate" ]; then
        trap '' TERM
        (trap '' TERM; sleep 1; : > "$package_root/validate-child-survived") &
        wait
    fi
    printf '%s\n' '{"success":true,"diagnostics":[]}' > "$result"
    if [ -f "$package_root/force-validation-run-cleanup-failure" ]; then
        mkdir protected-runtime
        printf '%s\n' 'retained validation evidence' > protected-runtime/evidence.txt
        chmod a-w protected-runtime
    fi
    exit 0
fi

: > "$package_root/generate.started"
printf '%s\n' 'generator stdout before completion'
printf '%s\n' 'generator stderr before completion' >&2
if [ -f "$package_root/force-log-archive-failure" ]; then
    mkdir stdout.log
fi
if [ -f "$package_root/slow-generate" ]; then
    trap '' TERM
    (trap '' TERM; sleep 1; : > "$package_root/generate-child-survived") &
    wait
fi
mkdir -p "$output"
printf '%s\n' '// cancellation test artifact' > "$output/generated.sv"
printf '%s\n' '{"success":true,"diagnostics":[],"artifacts":[{"id":"rtl","type":"rtl","path":"generated.sv","primary":true}]}' > "$result"
if [ -f "$package_root/force-capture-cleanup-failure" ]; then
    chmod a-w .finepaper-process-output-*
fi
)SCRIPT";
    const QString executable = QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    return writeExecutable(executable, script)
        && saveJsonObject(
            QDir(packageRoot).filePath(QStringLiteral("package.json")),
            cancellationPackageManifest());
}

QJsonObject cancellationDesignRequest() {
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("cancellation_test")},
        {QStringLiteral("name"), QStringLiteral("Cancellation Test")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.cancellation")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 1},
            {QStringLiteral("columns"), 1}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("device_0")},
                {QStringLiteral("type"), QStringLiteral("device")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            }
        }}
    };
}

std::thread cancelWhenFileExists(const QString& marker,
                                 CancellationSource source,
                                 std::atomic_bool& observed) {
    return std::thread([marker, source = std::move(source), &observed]() mutable {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3000) {
            if (QFileInfo::exists(marker)) {
                observed.store(true, std::memory_order_release);
                source.requestCancellation();
                return;
            }
            QThread::msleep(5);
        }
        source.requestCancellation();
    });
}

bool waitForFile(const QString& path, qint64 timeoutMilliseconds = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMilliseconds) {
        if (QFileInfo::exists(path)) {
            return true;
        }
        QThread::msleep(5);
    }
    return QFileInfo::exists(path);
}

bool prepareSnapshotPackage(const QString& packageRoot,
                            const QByteArray& variant,
                            bool holdValidation) {
    QJsonObject manifest = cancellationPackageManifest();
    manifest.insert(
        QStringLiteral("id"), QStringLiteral("test.package-snapshot"));
    manifest.insert(
        QStringLiteral("name"), QStringLiteral("Package Snapshot Test"));
    QJsonObject generator = manifest.value(
        QStringLiteral("generator")).toObject();
    generator.insert(
        QStringLiteral("name"),
        QStringLiteral("snapshot-%1").arg(QString::fromLatin1(variant)));
    manifest.insert(QStringLiteral("generator"), generator);

    QByteArray script = R"SCRIPT(#!/bin/sh
operation="$1"
shift
result=""
output=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --result) result="$2"; shift 2 ;;
        --output) output="$2"; shift 2 ;;
        *) shift ;;
    esac
done
package_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "$operation" = "validate" ]; then
    : > "$package_root/validate.started"
    if [ -f "$package_root/hold-validation" ]; then
        while [ ! -f "$package_root/release-validation" ]; do
            sleep 0.01
        done
    fi
    printf '%s\n' '{"success":true,"diagnostics":[]}' > "$result"
    exit 0
fi
mkdir -p "$output"
printf '%s\n' '// snapshot __VARIANT__' > "$output/__VARIANT__.sv"
printf '%s\n' '{"success":true,"diagnostics":[],"artifacts":[{"id":"rtl-__VARIANT__","type":"rtl","path":"__VARIANT__.sv","primary":true}]}' > "$result"
)SCRIPT";
    script.replace("__VARIANT__", variant);
    const QString executable = QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    return writeExecutable(executable, script)
        && saveJsonObject(
            QDir(packageRoot).filePath(QStringLiteral("package.json")),
            manifest)
        && (!holdValidation
            || createEmptyFile(QDir(packageRoot).filePath(
                QStringLiteral("hold-validation"))));
}

void verifyProcessCancellation() {
    const CancellationToken inertToken;
    check(!inertToken.isCancellationRequested(),
          QStringLiteral("a default cancellation token is inert"));

    CancellationSource alreadyCancelled;
    const CancellationToken alreadyCancelledToken = alreadyCancelled.token();
    alreadyCancelled.requestCancellation();
    const ProcessResult skipped = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{QStringLiteral("-c"), QStringLiteral("exit 0")},
        QStringLiteral("/tmp"),
        std::chrono::seconds(1),
        QProcessEnvironment::systemEnvironment(),
        alreadyCancelledToken);
    check(skipped.cancelled && !skipped.started && !skipped.timedOut,
          QStringLiteral("a pre-cancelled process is never started"));

    const ProcessResult completed = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{
            QStringLiteral("-c"),
            QStringLiteral("sleep 0.1; printf 'completed'")
        },
        QStringLiteral("/tmp"),
        1000);
    check(completed.started && !completed.cancelled && !completed.timedOut
              && !completed.crashed && completed.exitCode == 0
              && completed.standardOutput == QStringLiteral("completed")
              && completed.error.isEmpty(),
          QStringLiteral("the non-cancelled process path preserves normal results"));

    const ProcessResult lateHelperOutput = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{
            QStringLiteral("-c"),
            QStringLiteral(
                "(sleep 0.1; printf 'late-out'; printf 'late-err' >&2) & exit 0")
        },
        QStringLiteral("/tmp"),
        1000);
    check(lateHelperOutput.started && !lateHelperOutput.cancelled
              && !lateHelperOutput.timedOut && !lateHelperOutput.crashed
              && lateHelperOutput.exitCode == 0
              && lateHelperOutput.standardOutput == QStringLiteral("late-out")
              && lateHelperOutput.standardError == QStringLiteral("late-err"),
          QStringLiteral(
              "output capture spans helpers that outlive their launcher"));

    const ProcessResult excessiveOutput = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{
            QStringLiteral("-c"),
            QStringLiteral("head -c 9437184 /dev/zero")
        },
        QStringLiteral("/tmp"),
        std::chrono::seconds(5),
        QProcessEnvironment::systemEnvironment(),
        CancellationToken{});
    check(excessiveOutput.started && !excessiveOutput.cancelled
              && !excessiveOutput.timedOut
              && excessiveOutput.standardOutputTruncated
              && QFileInfo(excessiveOutput.standardOutputCapturePath).size()
                    == 9437184
              && excessiveOutput.standardOutput.size()
                    < 1024 * 1024 + 256,
          QStringLiteral(
              "large Package logs stay complete on disk and bounded in memory"));

    ProcessResult finalizedCapture = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{
            QStringLiteral("-c"),
            QStringLiteral("printf 'lease-output'")
        },
        QStringLiteral("/tmp"),
        1000);
    const QString finalizedCapturePath =
        finalizedCapture.outputCapture.directoryPath();
    TemporaryDirectoryLease copiedLease = finalizedCapture.outputCapture;
    TemporaryDirectoryLease movedLease = std::move(copiedLease);
    const TemporaryDirectoryFinalizationResult removedCapture =
        movedLease.finalize();
    const TemporaryDirectoryFinalizationResult repeatedFinalization =
        finalizedCapture.outputCapture.finalize(
            TemporaryDirectoryFinalizationMode::Retain);
    check(!removedCapture.cleanupUnresolved
              && repeatedFinalization.retainedPath.isEmpty()
              && !QFileInfo::exists(finalizedCapturePath),
          QStringLiteral(
              "a copied and moved capture lease removes its directory exactly once"));

    ProcessResult retainedCapture = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{
            QStringLiteral("-c"),
            QStringLiteral("printf 'retained-output'")
        },
        QStringLiteral("/tmp"),
        1000);
    const QString retainedCapturePath =
        retainedCapture.outputCapture.directoryPath();
    check(QFile::setPermissions(
              retainedCapturePath,
              QFileDevice::ReadOwner | QFileDevice::ExeOwner),
          QStringLiteral("capture cleanup failure fixture is made read-only"));
    const TemporaryDirectoryFinalizationResult failedCaptureRemoval =
        retainedCapture.outputCapture.finalize();
    check(failedCaptureRemoval.cleanupUnresolved
              && failedCaptureRemoval.retainedPath == retainedCapturePath
              && !failedCaptureRemoval.error.isEmpty()
              && QFileInfo::exists(retainedCapturePath),
          QStringLiteral(
              "capture deletion failure is observable and retains its path"));
    (void)QFile::setPermissions(
        retainedCapturePath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner);
    (void)QDir(retainedCapturePath).removeRecursively();

    const ProcessResult failedStart = runProcess(
        QStringLiteral("/finepaper-test/no-such-executable"),
        {},
        QStringLiteral("/tmp"),
        1000);
    check(!failedStart.started && !failedStart.cancelled
              && !failedStart.timedOut && !failedStart.crashed
              && !failedStart.error.isEmpty(),
          QStringLiteral("failed process startup remains distinct from cancellation"));

    const ProcessResult crashed = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{
            QStringLiteral("-c"),
            QStringLiteral("kill -KILL $$")
        },
        QStringLiteral("/tmp"),
        1000);
    check(crashed.started && crashed.crashed && !crashed.cancelled
              && !crashed.timedOut,
          QStringLiteral("a genuine process crash remains distinct from cancellation"));

    QTemporaryDir fixture(QStringLiteral("/tmp/finepaper-cancel-process-XXXXXX"));
    check(fixture.isValid(),
          QStringLiteral("the process cancellation fixture is available"));
    if (!fixture.isValid()) {
        return;
    }

    const QString ready = QDir(fixture.path()).filePath(QStringLiteral("ready"));
    const QString childSurvived = QDir(fixture.path()).filePath(
        QStringLiteral("child-survived"));
    const QString parentSurvived = QDir(fixture.path()).filePath(
        QStringLiteral("parent-survived"));
    const QString command = QStringLiteral(
        "trap '' TERM; (trap '' TERM; sleep 1; : > '%1') & "
        ": > '%2'; wait; : > '%3'")
        .arg(childSurvived, ready, parentSurvived);

    CancellationSource source;
    std::atomic_bool observed = false;
    std::thread canceller = cancelWhenFileExists(ready, source, observed);
    QElapsedTimer latency;
    latency.start();
    const ProcessResult cancelled = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{QStringLiteral("-c"), command},
        fixture.path(),
        std::chrono::seconds(10),
        QProcessEnvironment::systemEnvironment(),
        source.token());
    const qint64 elapsed = latency.elapsed();
    canceller.join();

    check(observed.load(std::memory_order_acquire),
          QStringLiteral("cancellation begins only after the slow process is running"));
    check(cancelled.started && cancelled.cancelled && !cancelled.timedOut
              && !cancelled.crashed && cancelled.exitCode == -1
              && !cancelled.cleanupFailed,
          QStringLiteral("process cancellation has a distinct terminal state"));
    check(elapsed < 2000,
          QStringLiteral("process cancellation completes within two seconds"));
    QThread::msleep(1200);
    check(!QFileInfo::exists(childSurvived)
              && !QFileInfo::exists(parentSurvived),
          QStringLiteral("cancellation removes the parent and stubborn descendants"));

    const QString detachedReady = QDir(fixture.path()).filePath(
        QStringLiteral("detached-ready"));
    const QString detachedChildSurvived = QDir(fixture.path()).filePath(
        QStringLiteral("detached-child-survived"));
    const QString detachedCommand = QStringLiteral(
        "(trap '' TERM; sleep 0.1; : > '%1'; sleep 1; : > '%2') & exit 0")
        .arg(detachedReady, detachedChildSurvived);
    CancellationSource detachedSource;
    std::atomic_bool detachedObserved = false;
    std::thread detachedCanceller = cancelWhenFileExists(
        detachedReady, detachedSource, detachedObserved);
    const ProcessResult detachedCancellation = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{QStringLiteral("-c"), detachedCommand},
        fixture.path(),
        std::chrono::seconds(10),
        QProcessEnvironment::systemEnvironment(),
        detachedSource.token());
    detachedCanceller.join();
    check(detachedObserved.load(std::memory_order_acquire)
              && detachedCancellation.cancelled
              && detachedCancellation.exitCode == -1
              && !detachedCancellation.cleanupFailed,
          QStringLiteral(
              "cancellation remains available after the launcher exits"));
    QThread::msleep(1200);
    check(!QFileInfo::exists(detachedChildSurvived),
          QStringLiteral(
              "cancellation removes descendants after the launcher has exited"));

    const QString gracefulReady = QDir(fixture.path()).filePath(
        QStringLiteral("graceful-ready"));
    const QString gracefulCleanup = QDir(fixture.path()).filePath(
        QStringLiteral("graceful-child-cleanup"));
    const QString gracefulCommand = QStringLiteral(
        "(trap \"sleep 0.1; : > '%1'; exit 0\" TERM; "
        "while :; do sleep 10; done) & "
        "trap 'exit 0' TERM; : > '%2'; wait")
        .arg(gracefulCleanup, gracefulReady);
    CancellationSource gracefulSource;
    std::atomic_bool gracefulObserved = false;
    std::thread gracefulCanceller = cancelWhenFileExists(
        gracefulReady, gracefulSource, gracefulObserved);
    const ProcessResult gracefulCancellation = runProcess(
        QStringLiteral("/bin/sh"),
        QStringList{QStringLiteral("-c"), gracefulCommand},
        fixture.path(),
        std::chrono::seconds(10),
        QProcessEnvironment::systemEnvironment(),
        gracefulSource.token());
    gracefulCanceller.join();
    check(gracefulObserved.load(std::memory_order_acquire)
              && gracefulCancellation.cancelled,
          QStringLiteral("graceful descendant cancellation is requested while running"));
    check(QFileInfo::exists(gracefulCleanup),
          QStringLiteral("SIGTERM gives descendants time to finish cleanup before SIGKILL"));
}

void verifyInProcessCancellationPolling() {
    NocDesign largeTopology;
    largeTopology.topology = TopologySpec{
        QStringLiteral("mesh"), 1000, 1000};
    int topologyChecks = 0;
    const TopologyProjection partialTopology = projectTopology(
        largeTopology,
        [&topologyChecks] {
            ++topologyChecks;
            return topologyChecks > 20;
        });
    check(topologyChecks > 20
              && partialTopology.routers.size() < 1'000'000,
          QStringLiteral(
              "topology projection polls cancellation inside the Router loops"));

    PackageDefinition domainPackage;
    domainPackage.formatVersion = largeTopology.formatVersion;
    int domainChecks = 0;
    (void)domain_service::validateAgainstPackage(
        largeTopology,
        domainPackage,
        [&domainChecks] {
            ++domainChecks;
            return domainChecks > 20;
        });
    check(domainChecks > 20 && domainChecks < 100,
          QStringLiteral(
              "Package Domain validation propagates cancellation into projection"));

    NocDesign multiValueDomainDesign;
    multiValueDomainDesign.formatVersion = 3;
    multiValueDomainDesign.id = QStringLiteral("multi_value_domain");
    multiValueDomainDesign.name = QStringLiteral("Multi-value Domain");
    multiValueDomainDesign.package = PackageReference{
        QStringLiteral("test.domain"), QStringLiteral("1.0.0")};
    QJsonArray propertyItems;
    for (int index = 0; index < 1000; ++index) {
        propertyItems.append(index);
    }
    multiValueDomainDesign.domains.append(DomainDefinition{
        QStringLiteral("power_0"),
        QStringLiteral("power"),
        QStringLiteral("Power 0"),
        QJsonObject{{QStringLiteral("levels"), propertyItems}}});
    DomainPropertyDefinition levels;
    levels.id = QStringLiteral("levels");
    levels.type = ParameterType::Integer;
    levels.multiple = true;
    DomainTypeDefinition powerType;
    powerType.id = QStringLiteral("power");
    powerType.properties.append(levels);
    PackageDefinition multiValueDomainPackage;
    multiValueDomainPackage.formatVersion = 3;
    multiValueDomainPackage.domainTypes.append(powerType);
    int propertyChecks = 0;
    (void)domain_service::validateAgainstPackage(
        multiValueDomainDesign,
        multiValueDomainPackage,
        [&propertyChecks] {
            ++propertyChecks;
            return propertyChecks > 50;
        });
    check(propertyChecks > 50 && propertyChecks < 200,
          QStringLiteral(
              "multi-value Domain properties poll cancellation between items"));

    NocDesign largeStructure;
    largeStructure.formatVersion = 3;
    largeStructure.id = QStringLiteral("large_structure");
    largeStructure.name = QStringLiteral("Large Structure");
    largeStructure.package = PackageReference{
        QStringLiteral("test.package"), QStringLiteral("1.0.0")};
    largeStructure.endpoints.reserve(1000);
    for (int index = 0; index < 1000; ++index) {
        largeStructure.endpoints.append(EndpointInstance{
            QStringLiteral("endpoint_%1").arg(index),
            QStringLiteral("device"),
            EndpointAttachment{RouterPosition{0, 0}, std::nullopt},
            {}});
    }
    int structureChecks = 0;
    (void)validateDesignStructure(
        largeStructure,
        [&structureChecks] {
            ++structureChecks;
            return structureChecks > 8;
        });
    check(structureChecks > 8 && structureChecks < 100,
          QStringLiteral(
              "structural validation polls cancellation between records"));

    NocDesign configuredDesign = largeStructure;
    configuredDesign.elementConfigurations.reserve(1000);
    for (int index = 0; index < 1000; ++index) {
        configuredDesign.elementConfigurations.append(ElementConfiguration{
            ElementRef{ElementKind::Invalid,
                       QStringLiteral("element_%1").arg(index)},
            QStringLiteral("missing"),
            QJsonObject{{QStringLiteral("value"), index}}});
    }
    PackageDefinition emptyPackage;
    int configurationChecks = 0;
    const QVector<Diagnostic> partialConfigurationDiagnostics =
        validateElementConfigurations(
            configuredDesign,
            emptyPackage,
            [&configurationChecks] {
                ++configurationChecks;
                return configurationChecks > 8;
            });
    check(configurationChecks > 8
              && partialConfigurationDiagnostics.size() < 1000,
          QStringLiteral(
              "element configuration validation polls cancellation between records"));

    QJsonArray diagnosticValues;
    for (int index = 0; index < 1000; ++index) {
        diagnosticValues.append(QJsonObject{
            {QStringLiteral("severity"), QStringLiteral("warning")},
            {QStringLiteral("code"),
             QStringLiteral("test.%1").arg(index)},
            {QStringLiteral("message"), QStringLiteral("test diagnostic")}
        });
    }
    int protocolChecks = 0;
    const PackageOperationResult partialProtocol =
        parsePackageOperationResult(
            QJsonObject{
                {QStringLiteral("success"), true},
                {QStringLiteral("diagnostics"), diagnosticValues}},
            QStringLiteral("/tmp/result.json"),
            QStringLiteral("test"),
            ArtifactResultPolicy::Optional,
            [&protocolChecks] {
                ++protocolChecks;
                return protocolChecks > 8;
            });
    check(protocolChecks > 8 && !partialProtocol.protocolValid
              && partialProtocol.diagnostics.size() < 1000,
          QStringLiteral(
              "Package result parsing polls cancellation between records"));

    QTemporaryDir serializationFixture(
        QStringLiteral("/tmp/finepaper-cancel-serialization-XXXXXX"));
    NocDesign serializationDesign = largeStructure;
    serializationDesign.endpoints.resize(1);
    QJsonArray largeParameterArray;
    for (int index = 0; index < 10'000; ++index) {
        largeParameterArray.append(index);
    }
    serializationDesign.endpoints[0].parameters = QJsonObject{
        {QStringLiteral("largeArray"), largeParameterArray}};
    int serializationChecks = 0;
    QVector<Diagnostic> serializationDiagnostics;
    const QString cancelledDesignPath = QDir(serializationFixture.path())
        .filePath(QStringLiteral("cancelled-design.json"));
    const QByteArray originalDesignContents("original-design-sentinel\n");
    {
        QFile originalDesign(cancelledDesignPath);
        check(originalDesign.open(
                  QIODevice::WriteOnly | QIODevice::Truncate)
                  && originalDesign.write(originalDesignContents)
                        == originalDesignContents.size(),
              QStringLiteral(
                  "cancelled serialization fixture starts with an existing target"));
    }
    const bool serialized = saveDesign(
        cancelledDesignPath,
        serializationDesign,
        &serializationDiagnostics,
        [&serializationChecks] {
            ++serializationChecks;
            return serializationChecks > 100;
        });
    check(serializationFixture.isValid() && !serialized
              && serializationChecks > 100
              && readFile(cancelledDesignPath) == originalDesignContents
              && serializationDiagnostics.isEmpty(),
          QStringLiteral(
              "runtime Design serialization cancellation preserves the previous file"));

    serializationDesign.name = QString::fromUtf8(
        "Quoted \"NoC\"\nUnicode ") + QString::fromUtf8("\xF0\x9F\x8C\x90");
    const QString completedDesignPath = QDir(serializationFixture.path())
        .filePath(QStringLiteral("completed-design.json"));
    int completedSerializationChecks = 0;
    const bool completedSerialization = saveDesign(
        completedDesignPath,
        serializationDesign,
        &serializationDiagnostics,
        [&completedSerializationChecks] {
            ++completedSerializationChecks;
            return false;
        });
    const DesignLoadResult reloadedSerialization = loadDesign(
        completedDesignPath);
    check(completedSerialization && completedSerializationChecks > 100
              && reloadedSerialization.success
              && reloadedSerialization.design.name
                    == serializationDesign.name
              && reloadedSerialization.design.endpoints.size() == 1
              && reloadedSerialization.design.endpoints.constFirst().parameters
                    == serializationDesign.endpoints.constFirst().parameters,
          QStringLiteral(
              "cancellable runtime serialization preserves nested JSON and Unicode"));
}

void verifyApplicationCancellation() {
    QTemporaryDir fixture(QStringLiteral("/tmp/finepaper-cancel-package-XXXXXX"));
    QTemporaryDir output(QStringLiteral("/tmp/finepaper-cancel-output-XXXXXX"));
    check(fixture.isValid() && output.isValid()
              && prepareCancellationPackage(fixture.path()),
          QStringLiteral("the cancellable Package fixture is available"));
    if (!fixture.isValid() || !output.isValid()) {
        return;
    }

    FinepaperApplication application;
    const PackageCatalogReloadResult reload = application.reloadPackages(
        QStringList{fixture.path()});
    check(reload.committed() && !hasErrors(reload.diagnostics),
          QStringLiteral("the cancellable Package fixture loads"));
    const DesignResult created = application.createDesign(
        cancellationDesignRequest());
    check(created.success,
          QStringLiteral("the cancellation test design is valid"));
    if (!reload.committed() || !created.success) {
        return;
    }

    const QString validateStarted = QDir(fixture.path()).filePath(
        QStringLiteral("validate.started"));
    const QString validateSurvived = QDir(fixture.path()).filePath(
        QStringLiteral("validate-child-survived"));
    check(createEmptyFile(QDir(fixture.path()).filePath(
              QStringLiteral("slow-validate"))),
          QStringLiteral("slow validation mode is enabled"));
    CancellationSource validationSource;
    std::atomic_bool validationObserved = false;
    std::thread validationCanceller = cancelWhenFileExists(
        validateStarted, validationSource, validationObserved);
    QElapsedTimer validationLatency;
    validationLatency.start();
    const ValidationResult validation = application.validate(
        created.design, true, validationSource.token());
    const qint64 validationElapsed = validationLatency.elapsed();
    validationCanceller.join();
    check(validationObserved.load(std::memory_order_acquire)
              && validation.cancelled && !validation.success
              && hasDiagnosticCode(
                  validation.diagnostics, QStringLiteral("operation.cancelled")),
          QStringLiteral("application validation propagates process cancellation"));
    check(validationElapsed < 2000,
          QStringLiteral("validation cancellation completes within two seconds"));
    QThread::msleep(1200);
    check(!QFileInfo::exists(validateSurvived),
          QStringLiteral("validation cancellation removes runtime descendants"));

    QFile::remove(validateStarted);
    QFile::remove(validateSurvived);
    const QString generateStarted = QDir(fixture.path()).filePath(
        QStringLiteral("generate.started"));
    QFile::remove(generateStarted);
    CancellationSource validationStageSource;
    std::atomic_bool validationStageObserved = false;
    std::thread validationStageCanceller = cancelWhenFileExists(
        validateStarted, validationStageSource, validationStageObserved);
    const GenerationResult validationStageGeneration = application.generate(
        created.design,
        GenerationOptions{output.path()},
        validationStageSource.token());
    validationStageCanceller.join();
    check(validationStageObserved.load(std::memory_order_acquire)
              && validationStageGeneration.cancelled
              && !validationStageGeneration.success
              && !QFileInfo::exists(generateStarted),
          QStringLiteral(
              "generation cancellation during validation never starts the generator"));

    QFile::remove(QDir(fixture.path()).filePath(QStringLiteral("slow-validate")));
    QFile::remove(validateStarted);
    const QString generateSurvived = QDir(fixture.path()).filePath(
        QStringLiteral("generate-child-survived"));
    check(createEmptyFile(QDir(fixture.path()).filePath(
              QStringLiteral("slow-generate"))),
          QStringLiteral("slow generation mode is enabled"));
    CancellationSource generationSource;
    std::atomic_bool generationObserved = false;
    std::thread generationCanceller = cancelWhenFileExists(
        generateStarted, generationSource, generationObserved);
    QElapsedTimer generationLatency;
    generationLatency.start();
    const GenerationResult generation = application.generate(
        created.design,
        GenerationOptions{output.path()},
        generationSource.token());
    const qint64 generationElapsed = generationLatency.elapsed();
    generationCanceller.join();
    check(generationObserved.load(std::memory_order_acquire)
              && generation.cancelled && !generation.success
              && generation.artifacts.isEmpty()
              && hasDiagnosticCode(
                  generation.diagnostics, QStringLiteral("operation.cancelled")),
          QStringLiteral("application generation propagates process cancellation"));
    check(generationElapsed < 2000,
          QStringLiteral("generation cancellation completes within two seconds"));
    check(QFileInfo(generation.stdoutLog).isFile()
              && QFileInfo(generation.stderrLog).isFile()
              && readFile(generation.stdoutLog)
                    == QByteArray("generator stdout before completion\n")
              && readFile(generation.stderrLog)
                    == QByteArray("generator stderr before completion\n"),
          QStringLiteral(
              "cancelled generation archives both complete process logs before returning"));
    QThread::msleep(1200);
    check(!QFileInfo::exists(generateSurvived),
          QStringLiteral("generation cancellation removes runtime descendants"));

    QFile::remove(QDir(fixture.path()).filePath(QStringLiteral("slow-generate")));
    QFile::remove(validateStarted);
    QFile::remove(generateStarted);
    const ValidationResult completedValidation = application.validate(
        created.design, true);
    const GenerationResult completedGeneration = application.generate(
        created.design, GenerationOptions{output.path()});
    check(completedValidation.success && !completedValidation.cancelled,
          QStringLiteral("non-cancelled application validation still succeeds"));
    check(completedGeneration.success && !completedGeneration.cancelled
              && completedGeneration.artifacts.size() == 1,
          QStringLiteral("non-cancelled application generation still succeeds"));

    const QString forceValidationRunCleanupFailure =
        QDir(fixture.path()).filePath(
            QStringLiteral("force-validation-run-cleanup-failure"));
    check(createEmptyFile(forceValidationRunCleanupFailure),
          QStringLiteral(
              "validation runtime-directory cleanup failure mode is enabled"));
    const ValidationResult validationRunCleanupFailure =
        application.validate(created.design, true);
    bool retainedValidationEvidenceFound = false;
    for (const QString& retainedPath
         : validationRunCleanupFailure.retainedRuntimePaths) {
        if (QFileInfo::exists(QDir(retainedPath).filePath(
                QStringLiteral("protected-runtime/evidence.txt")))) {
            retainedValidationEvidenceFound = true;
            break;
        }
    }
    check(validationRunCleanupFailure.cleanupUnresolved
              && !validationRunCleanupFailure.processCleanupUnresolved
              && !validationRunCleanupFailure.retainedRuntimePaths.isEmpty()
              && retainedValidationEvidenceFound
              && hasDiagnosticCode(
                  validationRunCleanupFailure.diagnostics,
                  QStringLiteral("operation.cleanup_failed")),
          QStringLiteral(
              "validation surfaces outer runtime-directory deletion failure without process quarantine"));
    for (const QString& retainedPath
         : validationRunCleanupFailure.retainedRuntimePaths) {
        const QString protectedRuntime = QDir(retainedPath).filePath(
            QStringLiteral("protected-runtime"));
        (void)QFile::setPermissions(
            protectedRuntime,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner);
        (void)QDir(retainedPath).removeRecursively();
    }
    QFile::remove(forceValidationRunCleanupFailure);

    const QString forceCaptureCleanupFailure = QDir(fixture.path()).filePath(
        QStringLiteral("force-capture-cleanup-failure"));
    check(createEmptyFile(forceCaptureCleanupFailure),
          QStringLiteral("capture cleanup failure mode is enabled"));
    const GenerationResult cleanupFailureGeneration = application.generate(
        created.design, GenerationOptions{output.path()});
    check(cleanupFailureGeneration.cleanupUnresolved
              && !cleanupFailureGeneration.processCleanupUnresolved
              && !cleanupFailureGeneration.retainedRuntimePaths.isEmpty()
              && hasDiagnosticCode(
                  cleanupFailureGeneration.diagnostics,
                  QStringLiteral("operation.cleanup_failed")),
          QStringLiteral(
              "application generation propagates capture deletion failure as typed cleanup state"));
    for (const QString& retainedPath
         : cleanupFailureGeneration.retainedRuntimePaths) {
        (void)QFile::setPermissions(
            retainedPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ExeOwner);
        (void)QDir(retainedPath).removeRecursively();
    }
    QFile::remove(forceCaptureCleanupFailure);

    const QString forceLogArchiveFailure = QDir(fixture.path()).filePath(
        QStringLiteral("force-log-archive-failure"));
    check(createEmptyFile(forceLogArchiveFailure),
          QStringLiteral("log archive failure mode is enabled"));
    const GenerationResult logArchiveFailureGeneration = application.generate(
        created.design, GenerationOptions{output.path()});
    bool completeCaptureRetained = false;
    for (const QString& retainedPath
         : logArchiveFailureGeneration.retainedRuntimePaths) {
        if (readFile(QDir(retainedPath).filePath(QStringLiteral("stdout.log")))
            == QByteArray("generator stdout before completion\n")) {
            completeCaptureRetained = true;
            break;
        }
    }
    check(logArchiveFailureGeneration.cleanupUnresolved
              && !logArchiveFailureGeneration.processCleanupUnresolved
              && completeCaptureRetained
              && hasDiagnosticCode(
                  logArchiveFailureGeneration.diagnostics,
                  QStringLiteral("run.log_write_failed")),
          QStringLiteral(
              "failed final-log archival retains the complete capture and typed cleanup state"));
    for (const QString& retainedPath
         : logArchiveFailureGeneration.retainedRuntimePaths) {
        (void)QDir(retainedPath).removeRecursively();
    }
    QFile::remove(forceLogArchiveFailure);

    CancellationSource preCancelled;
    preCancelled.requestCancellation();
    QFile::remove(validateStarted);
    const ValidationResult skippedValidation = application.validate(
        created.design, true, preCancelled.token());
    check(skippedValidation.cancelled && !skippedValidation.success
              && !QFileInfo::exists(validateStarted),
          QStringLiteral("pre-cancelled validation does not enter the Package runtime"));
    check(validationResultToJson(skippedValidation)
                  .value(QStringLiteral("cancelled")).toBool(),
          QStringLiteral("validation JSON exposes cancellation explicitly"));
    check(!validationResultToJson(skippedValidation)
                   .value(QStringLiteral("processCleanupUnresolved")).toBool(),
          QStringLiteral(
              "validation JSON distinguishes process cleanup from retained files"));
    ValidationResult unresolvedValidationCleanup;
    unresolvedValidationCleanup.processCleanupUnresolved = true;
    const QJsonObject unresolvedValidationJson =
        validationResultToJson(unresolvedValidationCleanup);
    check(unresolvedValidationJson.contains(
              QStringLiteral("processCleanupUnresolved"))
              && unresolvedValidationJson.value(
                     QStringLiteral("processCleanupUnresolved")).toBool(),
          QStringLiteral(
              "validation JSON serializes an unresolved process cleanup explicitly"));
    check(generationResultToJson(generation)
                  .value(QStringLiteral("cancelled")).toBool(),
          QStringLiteral("generation JSON exposes cancellation explicitly"));
    check(!generationResultToJson(generation)
                   .value(QStringLiteral("processCleanupUnresolved")).toBool(),
          QStringLiteral(
              "generation JSON distinguishes process cleanup from retained files"));
    GenerationResult unresolvedGenerationCleanup;
    unresolvedGenerationCleanup.processCleanupUnresolved = true;
    const QJsonObject unresolvedGenerationJson =
        generationResultToJson(unresolvedGenerationCleanup);
    check(unresolvedGenerationJson.contains(
              QStringLiteral("processCleanupUnresolved"))
              && unresolvedGenerationJson.value(
                     QStringLiteral("processCleanupUnresolved")).toBool(),
          QStringLiteral(
              "generation JSON serializes an unresolved process cleanup explicitly"));
}

void verifyOperationPackageSnapshot() {
    QTemporaryDir oldPackage(
        QStringLiteral("/tmp/finepaper-snapshot-old-XXXXXX"));
    QTemporaryDir newPackage(
        QStringLiteral("/tmp/finepaper-snapshot-new-XXXXXX"));
    QTemporaryDir output(
        QStringLiteral("/tmp/finepaper-snapshot-output-XXXXXX"));
    check(oldPackage.isValid() && newPackage.isValid() && output.isValid()
              && prepareSnapshotPackage(
                  oldPackage.path(), QByteArray("old"), true)
              && prepareSnapshotPackage(
                  newPackage.path(), QByteArray("new"), false),
          QStringLiteral("old and new Package snapshot fixtures are available"));
    if (!oldPackage.isValid() || !newPackage.isValid()
        || !output.isValid()) {
        return;
    }

    FinepaperApplication application;
    const PackageCatalogReloadResult oldReload = application.reloadPackages(
        QStringList{oldPackage.path()});
    QJsonObject request = cancellationDesignRequest();
    request.insert(
        QStringLiteral("package"),
        QJsonObject{
            {QStringLiteral("id"),
             QStringLiteral("test.package-snapshot")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        });
    const DesignResult created = application.createDesign(request);
    check(oldReload.committed() && created.success,
          QStringLiteral("the old Package snapshot creates a design"));
    if (!oldReload.committed() || !created.success) {
        return;
    }

    std::optional<GenerationResult> generation;
    std::thread generationThread([&] {
        generation = application.generate(
            created.design, GenerationOptions{output.path()});
    });

    const QString validationStarted = QDir(oldPackage.path()).filePath(
        QStringLiteral("validate.started"));
    const bool validationObserved = waitForFile(validationStarted);
    PackageCatalogReloadResult newReload;
    if (validationObserved) {
        // The operation has already resolved and entered the old validator.
        // Reload completes before validation is released, so any later catalog
        // resolve would deterministically observe the new Package.
        newReload = application.reloadPackages(
            QStringList{newPackage.path()});
    }
    const bool validationReleased = createEmptyFile(
        QDir(oldPackage.path()).filePath(
            QStringLiteral("release-validation")));
    generationThread.join();

    check(validationObserved && validationReleased
              && newReload.committed(),
          QStringLiteral(
              "catalog reload commits while old Package validation is paused"));
    check(generation && generation->success && generation->tool
              && generation->tool->name == QStringLiteral("snapshot-old")
              && generation->artifacts.size() == 1
              && generation->artifacts.constFirst().path
                     == QStringLiteral("old.sv")
              && readFile(QDir(generation->outputDirectory).filePath(
                     QStringLiteral("old.sv")))
                     == QByteArray("// snapshot old\n"),
          QStringLiteral(
              "one generate operation validates and generates with the same LoadedPackage snapshot"));
    check(application.packages().size() == 1
              && application.packages().constFirst().generator.name
                     == QStringLiteral("snapshot-new"),
          QStringLiteral(
              "the committed reload is visible to operations started afterwards"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    verifyInProcessCancellationPolling();
#ifdef Q_OS_UNIX
    verifyProcessCancellation();
    verifyApplicationCancellation();
    verifyOperationPackageSnapshot();
#else
    QTextStream(stdout)
        << "finepaper-cancellation-tests: process-tree checks require Unix"
        << Qt::endl;
#endif

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-cancellation-tests passed" << Qt::endl;
    }
    return failures == 0 ? 0 : 1;
}
