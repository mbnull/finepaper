#include "application/application.h"
#include "application/runtime_settings.h"
#include "execution/process.h"
#include "noc/model.h"
#include "storage/json.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QVector>

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

bool hasDiagnosticCode(const QVector<Diagnostic>& diagnostics, const QString& code) {
    return std::any_of(diagnostics.cbegin(), diagnostics.cend(), [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

bool copyDirectoryTree(const QString& sourcePath, const QString& destinationPath) {
    const QDir source(sourcePath);
    if (!source.exists() || !QDir().mkpath(destinationPath)) {
        return false;
    }

    QDirIterator iterator(
        sourcePath,
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceEntry = iterator.next();
        const QFileInfo sourceInfo(sourceEntry);
        const QString relative = source.relativeFilePath(sourceEntry);
        const QString destinationEntry = QDir(destinationPath).filePath(relative);
        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(destinationEntry)) {
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(QFileInfo(destinationEntry).absolutePath())
            || !QFile::copy(sourceEntry, destinationEntry)
            || !QFile::setPermissions(destinationEntry, sourceInfo.permissions())) {
            return false;
        }
    }
    return true;
}

QJsonObject objectWithStringField(const QJsonArray& values,
                                  const QString& field,
                                  const QString& expected) {
    const auto value = std::find_if(
        values.cbegin(), values.cend(), [&](const QJsonValue& candidate) {
            return candidate.isObject()
                && candidate.toObject().value(field).toString() == expected;
        });
    return value == values.cend() ? QJsonObject{} : value->toObject();
}

const Artifact* artifactWithType(const GenerationResult& result,
                                 const QString& type) {
    const auto artifact = std::find_if(
        result.artifacts.cbegin(),
        result.artifacts.cend(),
        [&type](const Artifact& candidate) { return candidate.type == type; });
    return artifact == result.artifacts.cend() ? nullptr : &*artifact;
}

const Artifact* primaryArtifact(const GenerationResult& result) {
    const auto artifact = std::find_if(
        result.artifacts.cbegin(),
        result.artifacts.cend(),
        [](const Artifact& candidate) { return candidate.primary; });
    return artifact == result.artifacts.cend() ? nullptr : &*artifact;
}

QString readArtifact(const GenerationResult& result, const Artifact* artifact) {
    if (!artifact) {
        return {};
    }
    QFile file(QDir(result.outputDirectory).filePath(artifact->path));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                          : QString{};
}

JsonObjectLoadResult loadArtifactObject(const GenerationResult& result,
                                        const Artifact* artifact) {
    if (!artifact) {
        return {};
    }
    const JsonObjectLoadResult loaded = loadJsonObject(
        QDir(result.outputDirectory).filePath(artifact->path));
    if (!loaded.success) {
        const QString detail = loaded.diagnostics.isEmpty()
            ? QStringLiteral("no diagnostic")
            : QStringLiteral("%1: %2")
                  .arg(loaded.diagnostics.first().code,
                       loaded.diagnostics.first().message);
        check(false,
              QStringLiteral("artifact %1 must parse as JSON object (%2)")
                  .arg(artifact->path, detail));
    }
    return loaded;
}

QJsonObject powerSupply(const QString& id,
                        const QString& net,
                        const QString& exposure,
                        int voltageMv,
                        bool switchable = false) {
    QJsonArray states = {
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("on")},
            {QStringLiteral("condition"), QStringLiteral("full-on")},
            {QStringLiteral("voltageMv"), voltageMv}
        }
    };
    if (switchable) {
        states.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("off")},
            {QStringLiteral("condition"), QStringLiteral("off")}
        });
    }
    QJsonObject supply = {
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), QStringLiteral("power")},
        {QStringLiteral("exposure"), exposure},
        {QStringLiteral("net"), net},
        {QStringLiteral("states"), states}
    };
    if (exposure == QStringLiteral("external-port")) {
        supply.insert(QStringLiteral("port"), net);
    }
    return supply;
}

QJsonObject powerControl(const QString& id, const QString& signal) {
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("signal"), signal},
        {QStringLiteral("source"), QStringLiteral("top-port")},
        {QStringLiteral("activeSense"), QStringLiteral("high")},
        {QStringLiteral("ownerDomain"), QStringLiteral("power-main")}
    };
}

QJsonObject technologyCell(const QString& id,
                           const QString& kind,
                           const QString& cell,
                           const QString& direction = {}) {
    QJsonObject mapping = {
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("cells"), QJsonArray{cell}}
    };
    if (!direction.isEmpty()) {
        mapping.insert(QStringLiteral("direction"), direction);
    }
    return mapping;
}

QJsonObject completePowerIntent() {
    return QJsonObject{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-power-intent")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("supplies"), QJsonArray{
            powerSupply(QStringLiteral("vdd-main"),
                        QStringLiteral("VDD_MAIN"),
                        QStringLiteral("external-port"), 900),
            powerSupply(QStringLiteral("vdd-low-in"),
                        QStringLiteral("VDD_LOW_IN"),
                        QStringLiteral("external-port"), 750),
            powerSupply(QStringLiteral("vdd-low-sw"),
                        QStringLiteral("VDD_LOW_SW"),
                        QStringLiteral("internal-switched"), 750, true),
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("vss")},
                {QStringLiteral("kind"), QStringLiteral("ground")},
                {QStringLiteral("exposure"), QStringLiteral("external-port")},
                {QStringLiteral("port"), QStringLiteral("VSS")},
                {QStringLiteral("net"), QStringLiteral("VSS")},
                {QStringLiteral("states"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("on")},
                        {QStringLiteral("condition"), QStringLiteral("full-on")},
                        {QStringLiteral("voltageMv"), 0}
                    }
                }}
            }
        }},
        {QStringLiteral("controls"), QJsonArray{
            powerControl(QStringLiteral("low-enable"),
                         QStringLiteral("low_enable")),
            powerControl(QStringLiteral("low-isolate"),
                         QStringLiteral("low_isolate")),
            powerControl(QStringLiteral("low-save"),
                         QStringLiteral("low_save")),
            powerControl(QStringLiteral("low-restore"),
                         QStringLiteral("low_restore"))
        }},
        {QStringLiteral("domains"), QJsonArray{
            QJsonObject{
                {QStringLiteral("domain"), QStringLiteral("power-main")},
                {QStringLiteral("primaryPower"), QStringLiteral("vdd-main")},
                {QStringLiteral("primaryGround"), QStringLiteral("vss")},
                {QStringLiteral("mode"), QStringLiteral("always-on")},
                {QStringLiteral("defaultState"), QStringLiteral("on")},
                {QStringLiteral("states"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("on")},
                        {QStringLiteral("powerState"), QStringLiteral("on")},
                        {QStringLiteral("groundState"), QStringLiteral("on")},
                        {QStringLiteral("behavior"), QStringLiteral("operational")}
                    }
                }},
                {QStringLiteral("levelShifter"), QJsonObject{
                    {QStringLiteral("location"), QStringLiteral("automatic")}
                }}
            },
            QJsonObject{
                {QStringLiteral("domain"), QStringLiteral("power-low")},
                {QStringLiteral("primaryPower"), QStringLiteral("vdd-low-sw")},
                {QStringLiteral("primaryGround"), QStringLiteral("vss")},
                {QStringLiteral("mode"), QStringLiteral("switchable")},
                {QStringLiteral("defaultState"), QStringLiteral("on")},
                {QStringLiteral("states"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("on")},
                        {QStringLiteral("powerState"), QStringLiteral("on")},
                        {QStringLiteral("groundState"), QStringLiteral("on")},
                        {QStringLiteral("behavior"), QStringLiteral("operational")}
                    },
                    QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("sleep")},
                        {QStringLiteral("powerState"), QStringLiteral("off")},
                        {QStringLiteral("groundState"), QStringLiteral("on")},
                        {QStringLiteral("behavior"), QStringLiteral("retained")}
                    }
                }},
                {QStringLiteral("powerSwitch"), QJsonObject{
                    {QStringLiteral("inputSupply"), QStringLiteral("vdd-low-in")},
                    {QStringLiteral("outputSupply"), QStringLiteral("vdd-low-sw")},
                    {QStringLiteral("control"), QStringLiteral("low-enable")},
                    {QStringLiteral("onSense"), QStringLiteral("high")}
                }},
                {QStringLiteral("retention"), QJsonObject{
                    {QStringLiteral("supply"), QStringLiteral("vdd-main")},
                    {QStringLiteral("saveControl"), QStringLiteral("low-save")},
                    {QStringLiteral("restoreControl"), QStringLiteral("low-restore")},
                    {QStringLiteral("saveEdge"), QStringLiteral("posedge")},
                    {QStringLiteral("restoreEdge"), QStringLiteral("negedge")},
                    {QStringLiteral("location"), QStringLiteral("self")}
                }},
                {QStringLiteral("isolation"), QJsonObject{
                    {QStringLiteral("control"), QStringLiteral("low-isolate")},
                    {QStringLiteral("supply"), QStringLiteral("vdd-main")},
                    {QStringLiteral("clampValue"), 0},
                    {QStringLiteral("location"), QStringLiteral("parent")}
                }},
                {QStringLiteral("levelShifter"), QJsonObject{
                    {QStringLiteral("location"), QStringLiteral("automatic")}
                }}
            }
        }},
        {QStringLiteral("defaultSystemState"), QStringLiteral("run")},
        {QStringLiteral("systemStates"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("run")},
                {QStringLiteral("domainStates"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("domain"), QStringLiteral("power-main")},
                        {QStringLiteral("state"), QStringLiteral("on")}
                    },
                    QJsonObject{
                        {QStringLiteral("domain"), QStringLiteral("power-low")},
                        {QStringLiteral("state"), QStringLiteral("on")}
                    }
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("sleep")},
                {QStringLiteral("domainStates"), QJsonArray{
                    QJsonObject{
                        {QStringLiteral("domain"), QStringLiteral("power-main")},
                        {QStringLiteral("state"), QStringLiteral("on")}
                    },
                    QJsonObject{
                        {QStringLiteral("domain"), QStringLiteral("power-low")},
                        {QStringLiteral("state"), QStringLiteral("sleep")}
                    }
                }}
            }
        }},
        {QStringLiteral("technology"), QJsonObject{
            {QStringLiteral("profile"), QStringLiteral("upf-interface-cells")},
            {QStringLiteral("interfaceCells"), QJsonArray{
                technologyCell(QStringLiteral("iso"),
                               QStringLiteral("isolation"),
                               QStringLiteral("LIB.ISO")),
                technologyCell(QStringLiteral("ls-up"),
                               QStringLiteral("level-shifter"),
                               QStringLiteral("LIB.LS_UP"),
                               QStringLiteral("up")),
                technologyCell(QStringLiteral("ls-down"),
                               QStringLiteral("level-shifter"),
                               QStringLiteral("LIB.LS_DOWN"),
                               QStringLiteral("down")),
                technologyCell(QStringLiteral("ret"),
                               QStringLiteral("retention"),
                               QStringLiteral("LIB.RET")),
                technologyCell(QStringLiteral("switch"),
                               QStringLiteral("power-switch"),
                               QStringLiteral("LIB.SWITCH"))
            }}
        }}
    };
}

void checkCompletePowerGeneration(const GenerationResult& generation) {
    const Artifact* intentArtifact = artifactWithType(
        generation, QStringLiteral("power-intent-plan"));
    const Artifact* implementationArtifact = artifactWithType(
        generation, QStringLiteral("power-implementation-plan"));
    const Artifact* upfArtifact = artifactWithType(
        generation, QStringLiteral("power-intent-upf"));
    const Artifact* evidenceArtifact = artifactWithType(
        generation, QStringLiteral("power-intent-evidence"));
    const Artifact* hierarchyArtifact = artifactWithType(
        generation, QStringLiteral("rtl-hierarchy"));
    const JsonObjectLoadResult loadedIntent = loadArtifactObject(
        generation, intentArtifact);
    const JsonObjectLoadResult loadedImplementation = loadArtifactObject(
        generation, implementationArtifact);
    const JsonObjectLoadResult loadedEvidence = loadArtifactObject(
        generation, evidenceArtifact);
    const JsonObjectLoadResult loadedHierarchy = loadArtifactObject(
        generation, hierarchyArtifact);
    const QJsonObject intent = loadedIntent.object;
    const QJsonObject implementation = loadedImplementation.object;
    const QJsonObject evidence = loadedEvidence.object;
    const QJsonObject hierarchy = loadedHierarchy.object;
    const QString upf = readArtifact(generation, upfArtifact);
    const QString top = readArtifact(generation, primaryArtifact(generation));

    check(generation.success
              && intentArtifact
              && implementationArtifact
              && upfArtifact
              && evidenceArtifact
              && hierarchyArtifact
              && loadedIntent.success
              && loadedImplementation.success
              && loadedEvidence.success
              && loadedHierarchy.success
              && QSet<QString>{
                     intentArtifact ? intentArtifact->path : QString{},
                     implementationArtifact ? implementationArtifact->path
                                            : QString{},
                     upfArtifact ? upfArtifact->path : QString{},
                     evidenceArtifact ? evidenceArtifact->path : QString{}
                 }.size() == 4
              && upfArtifact->path.endsWith(QStringLiteral(".upf")),
          QStringLiteral(
              "Core exposes every Package-generated Power intent artifact type"));
    check(intent.value(QStringLiteral("format")).toString()
                  == QStringLiteral("finepaper.noc-power-intent-plan")
              && intent.value(QStringLiteral("formatVersion")).toInt() == 1
              && implementation.value(QStringLiteral("format")).toString()
                  == QStringLiteral("finepaper.noc-power-implementation-plan")
              && implementation.value(QStringLiteral("formatVersion")).toInt()
                  == 1
              && evidence.value(QStringLiteral("format")).toString()
                  == QStringLiteral(
                      "finepaper.noc-power-intent-render-receipt")
              && evidence.value(QStringLiteral("formatVersion")).toInt() == 1,
          QStringLiteral(
              "Power artifacts preserve their versioned JSON contracts through Core"));

    const auto hasExactStringIds = [](const QJsonArray& values,
                                      const QString& field,
                                      const QSet<QString>& expected) {
        if (values.size() != expected.size()) {
            return false;
        }
        QSet<QString> actual;
        for (const QJsonValue& value : values) {
            if (!value.isObject()) {
                return false;
            }
            const QJsonValue id = value.toObject().value(field);
            if (!id.isString() || id.toString().isEmpty()
                || actual.contains(id.toString())) {
                return false;
            }
            actual.insert(id.toString());
        }
        return actual == expected;
    };
    check(hasExactStringIds(
              intent.value(QStringLiteral("supplies")).toArray(),
              QStringLiteral("id"),
              QSet<QString>{
                      QStringLiteral("vdd-low-in"),
                      QStringLiteral("vdd-low-sw"),
                      QStringLiteral("vdd-main"),
                      QStringLiteral("vss")
                  })
              && hasExactStringIds(
                  intent.value(QStringLiteral("controls")).toArray(),
                  QStringLiteral("id"),
                  QSet<QString>{
                      QStringLiteral("low-enable"),
                      QStringLiteral("low-isolate"),
                      QStringLiteral("low-restore"),
                      QStringLiteral("low-save")
                  })
              && hasExactStringIds(
                  intent.value(QStringLiteral("domains")).toArray(),
                  QStringLiteral("domain"),
                  QSet<QString>{
                      QStringLiteral("power-low"),
                      QStringLiteral("power-main")
                  })
              && hasExactStringIds(
                  intent.value(QStringLiteral("systemStates")).toArray(),
                  QStringLiteral("id"),
                  QSet<QString>{
                      QStringLiteral("run"), QStringLiteral("sleep")
                  })
              && hasExactStringIds(
                  intent.value(QStringLiteral("technology")).toObject()
                      .value(QStringLiteral("interfaceCells")).toArray(),
                  QStringLiteral("id"),
                  QSet<QString>{
                      QStringLiteral("iso"),
                      QStringLiteral("ls-down"),
                      QStringLiteral("ls-up"),
                      QStringLiteral("ret"),
                      QStringLiteral("switch")
                  })
              && intent.value(QStringLiteral("defaultSystemState")).toString()
                  == QStringLiteral("run")
              && intent.value(QStringLiteral("implementationPlan")).toObject()
                     .value(QStringLiteral("format")).toString()
                  == QStringLiteral(
                      "finepaper.noc-domain-implementation-plan"),
          QStringLiteral(
              "Power intent plan preserves every Package-owned fixture record"));

    const QJsonObject mainSupply = objectWithStringField(
        intent.value(QStringLiteral("supplies")).toArray(),
        QStringLiteral("id"), QStringLiteral("vdd-main"));
    const QJsonObject switchedSupply = objectWithStringField(
        intent.value(QStringLiteral("supplies")).toArray(),
        QStringLiteral("id"), QStringLiteral("vdd-low-sw"));
    const QJsonObject mainOnState = objectWithStringField(
        mainSupply.value(QStringLiteral("states")).toArray(),
        QStringLiteral("id"), QStringLiteral("on"));
    const QJsonObject switchedOnState = objectWithStringField(
        switchedSupply.value(QStringLiteral("states")).toArray(),
        QStringLiteral("id"), QStringLiteral("on"));
    const QJsonObject lowEnable = objectWithStringField(
        intent.value(QStringLiteral("controls")).toArray(),
        QStringLiteral("id"), QStringLiteral("low-enable"));
    const QJsonObject lowDomain = objectWithStringField(
        intent.value(QStringLiteral("domains")).toArray(),
        QStringLiteral("domain"), QStringLiteral("power-low"));
    const QJsonObject sleepSystemState = objectWithStringField(
        intent.value(QStringLiteral("systemStates")).toArray(),
        QStringLiteral("id"), QStringLiteral("sleep"));
    const QJsonObject lowSleepState = objectWithStringField(
        sleepSystemState.value(QStringLiteral("domainStates")).toArray(),
        QStringLiteral("domain"), QStringLiteral("power-low"));
    const QJsonObject isolationCell = objectWithStringField(
        intent.value(QStringLiteral("technology")).toObject()
            .value(QStringLiteral("interfaceCells")).toArray(),
        QStringLiteral("id"), QStringLiteral("iso"));
    const QJsonObject upShifterCell = objectWithStringField(
        intent.value(QStringLiteral("technology")).toObject()
            .value(QStringLiteral("interfaceCells")).toArray(),
        QStringLiteral("id"), QStringLiteral("ls-up"));
    check(mainSupply.value(QStringLiteral("exposure")).toString()
                  == QStringLiteral("external-port")
              && mainSupply.value(QStringLiteral("port")).toString()
                  == QStringLiteral("VDD_MAIN")
              && mainOnState.value(QStringLiteral("voltageMv")).toInt(-1)
                  == 900
              && switchedSupply.value(QStringLiteral("exposure")).toString()
                  == QStringLiteral("internal-switched")
              && !switchedSupply.contains(QStringLiteral("port"))
              && switchedOnState.value(QStringLiteral("voltageMv")).toInt(-1)
                  == 750
              && lowEnable.value(QStringLiteral("signal")).toString()
                  == QStringLiteral("low_enable")
              && lowEnable.value(QStringLiteral("source")).toString()
                  == QStringLiteral("top-port")
              && lowDomain.value(QStringLiteral("mode")).toString()
                  == QStringLiteral("switchable")
              && lowDomain.value(QStringLiteral("powerSwitch")).toObject()
                     .value(QStringLiteral("outputSupply")).toString()
                  == QStringLiteral("vdd-low-sw")
              && lowDomain.value(QStringLiteral("retention")).toObject()
                     .value(QStringLiteral("saveControl")).toString()
                  == QStringLiteral("low-save")
              && lowDomain.value(QStringLiteral("isolation")).toObject()
                     .value(QStringLiteral("control")).toString()
                  == QStringLiteral("low-isolate")
              && lowSleepState.value(QStringLiteral("state")).toString()
                  == QStringLiteral("sleep")
              && isolationCell.value(QStringLiteral("cells")).toArray()
                  == QJsonArray{QStringLiteral("LIB.ISO")}
              && upShifterCell.value(QStringLiteral("direction")).toString()
                  == QStringLiteral("up")
              && upShifterCell.value(QStringLiteral("cells")).toArray()
                  == QJsonArray{QStringLiteral("LIB.LS_UP")},
          QStringLiteral(
              "Power intent plan preserves critical supply, control, state, and technology values"));

    const QSet<QString> expectedCdcPowerDirections = {
        QStringLiteral("router-link/")
            + linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            + QStringLiteral("/from-to"),
        QStringLiteral("router-link/")
            + linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            + QStringLiteral("/to-from"),
        QStringLiteral("router-link/")
            + linkId(QStringLiteral("r-0-1"), QStringLiteral("r-1-1"))
            + QStringLiteral("/from-to"),
        QStringLiteral("router-link/")
            + linkId(QStringLiteral("r-0-1"), QStringLiteral("r-1-1"))
            + QStringLiteral("/to-from")
    };
    QSet<QString> hierarchyCdcPowerDirections;
    int hierarchyCdcPowerDirectionCount = 0;
    for (const QJsonValue& value : hierarchy.value(
             QStringLiteral("edgeDirections")).toArray()) {
        const QJsonObject direction = value.toObject();
        const QJsonObject edge = direction.value(
            QStringLiteral("edge")).toObject();
        const QJsonObject boundary = direction.value(
            QStringLiteral("powerBoundary")).toObject();
        const QJsonObject bridge = direction.value(
            QStringLiteral("bridge")).toObject();
        if (edge.value(QStringLiteral("kind")).toString()
                == QStringLiteral("router-link")
            && direction.value(QStringLiteral("sourceClockDomain")).toString()
                != direction.value(
                    QStringLiteral("destinationClockDomain")).toString()
            && bridge.value(QStringLiteral("module")).toString()
                == QStringLiteral("fp_async_ready_valid_fifo")
            && bridge.value(QStringLiteral("placement")).toString()
                == QStringLiteral("infrastructure")
            && boundary.value(QStringLiteral("status")).toString()
                == QStringLiteral("deferred")
            && boundary.value(QStringLiteral("reasonCode")).toString()
                == QStringLiteral(
                    "rtl_hierarchy.infrastructure_bridge_supply_unowned")) {
            ++hierarchyCdcPowerDirectionCount;
            hierarchyCdcPowerDirections.insert(
                edge.value(QStringLiteral("kind")).toString()
                + QLatin1Char('/')
                + edge.value(QStringLiteral("id")).toString()
                + QLatin1Char('/')
                + direction.value(QStringLiteral("orientation")).toString());
        }
    }

    int directOrientations = 0;
    int combinedOrientations = 0;
    int completeDirectOrientations = 0;
    int completeCombinedOrientations = 0;
    QSet<QString> implementationCdcPowerDirections;
    for (const QJsonValue& value : implementation.value(
             QStringLiteral("edgeOrientations")).toArray()) {
        const QJsonObject orientation = value.toObject();
        const QJsonObject edge = orientation.value(
            QStringLiteral("edge")).toObject();
        const QString boundaryStatus = orientation.value(
            QStringLiteral("powerBoundary")).toObject().value(
            QStringLiteral("status")).toString();
        const QString boundaryReason = orientation.value(
            QStringLiteral("powerBoundary")).toObject().value(
            QStringLiteral("reasonCode")).toString();
        const QString directionKey =
            edge.value(QStringLiteral("kind")).toString()
            + QLatin1Char('/')
            + edge.value(QStringLiteral("id")).toString()
            + QLatin1Char('/')
            + orientation.value(QStringLiteral("orientation")).toString();
        const bool direct = edge.value(QStringLiteral("kind")).toString()
                == QStringLiteral("endpoint-attachment")
            && edge.value(QStringLiteral("id")).toString()
                == QStringLiteral("z_cpu");
        const bool combined = edge.value(QStringLiteral("kind")).toString()
                == QStringLiteral("router-link")
            && boundaryStatus == QStringLiteral("deferred");
        if (direct) {
            ++directOrientations;
        } else if (combined) {
            ++combinedOrientations;
            implementationCdcPowerDirections.insert(directionKey);
        } else {
            continue;
        }

        const QJsonArray flows = orientation.value(
            QStringLiteral("signalFlows")).toArray();
        const int expectedFlowCount = direct ? 3 : 6;
        int strategyCount = 0;
        int expectedStrategies = 0;
        bool complete = flows.size() == expectedFlowCount;
        for (const QJsonValue& flowValue : flows) {
            const QJsonObject flow = flowValue.toObject();
            for (const QString& strategyName : {
                     QStringLiteral("isolation"),
                     QStringLiteral("levelShifter")}) {
                const QJsonObject strategy = flow.value(strategyName).toObject();
                const QString status = strategy.value(
                    QStringLiteral("status")).toString();
                const QString token = strategy.value(
                    QStringLiteral("token")).toString();
                ++strategyCount;
                if (direct) {
                    const bool emitted = status == QStringLiteral("expected");
                    const bool notRequired =
                        status == QStringLiteral("not-required");
                    expectedStrategies += emitted ? 1 : 0;
                    complete = complete
                        && (emitted || notRequired)
                        && !token.isEmpty()
                        && (upf.contains(token) == emitted);
                } else {
                    complete = complete
                        && status == QStringLiteral("deferred")
                        && !token.isEmpty()
                        && !upf.contains(token);
                }
            }
        }
        complete = complete && strategyCount == expectedFlowCount * 2;
        if (direct && boundaryStatus == QStringLiteral("resolvable")
            && expectedStrategies > 0 && complete) {
            ++completeDirectOrientations;
        } else if (combined
                   && boundaryReason
                       == QStringLiteral(
                           "rtl_hierarchy.infrastructure_bridge_supply_unowned")
                   && hierarchyCdcPowerDirections.contains(directionKey)
                   && complete) {
            ++completeCombinedOrientations;
        }
    }
    check(directOrientations == 2
              && completeDirectOrientations == directOrientations,
          QStringLiteral(
              "direct Endpoint Power crossings produce traceable UPF strategies"));
    check(hierarchyCdcPowerDirectionCount
                  == expectedCdcPowerDirections.size()
              && hierarchyCdcPowerDirections == expectedCdcPowerDirections
              && implementationCdcPowerDirections
                  == expectedCdcPowerDirections
              && combinedOrientations == expectedCdcPowerDirections.size()
              && completeCombinedOrientations == combinedOrientations,
          QStringLiteral(
              "combined CDC and Power Router crossings remain explicit and absent from UPF"));

    QSet<QString> commandKinds;
    const QJsonArray commands = evidence.value(
        QStringLiteral("commands")).toArray();
    bool commandLedgerIsComplete =
        evidence.value(QStringLiteral("commands")).isArray()
        && evidence.value(QStringLiteral("commandCount")).toInt(-1)
        == commands.size();
    QStringList ledgerCommandLines;
    for (qsizetype index = 0; index < commands.size(); ++index) {
        const QJsonValue value = commands.at(index);
        const QJsonObject command = value.toObject();
        const QJsonValue sequence = command.value(QStringLiteral("sequence"));
        const QJsonValue emitted = command.value(QStringLiteral("emitted"));
        const QString kind = command.value(QStringLiteral("kind")).toString();
        const QJsonArray arguments = command.value(
            QStringLiteral("arguments")).toArray();
        commandLedgerIsComplete = commandLedgerIsComplete
            && value.isObject()
            && sequence.isDouble()
            && sequence.toInteger(-1) == index
            && emitted.isBool()
            && emitted.toBool()
            && !kind.isEmpty()
            && command.value(QStringLiteral("arguments")).isArray();
        QStringList commandWords = {kind};
        for (const QJsonValue& argument : arguments) {
            commandLedgerIsComplete = commandLedgerIsComplete
                && argument.isString();
            commandWords.append(argument.toString());
        }
        ledgerCommandLines.append(commandWords.join(QLatin1Char(' ')));
        commandKinds.insert(kind);
    }
    const bool isUpfDocument =
        upf.startsWith(QStringLiteral("# Generated power intent:"))
        && upf.contains(QStringLiteral("\nupf_version 2.1\n"))
        && !upf.trimmed().startsWith(QLatin1Char('{'));
    QStringList upfCommandLines;
    for (const QString& line : upf.split(QLatin1Char('\n'))) {
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
            upfCommandLines.append(line);
        }
    }
    const QSet<QString> requiredCommands{
        QStringLiteral("create_power_switch"),
        QStringLiteral("set_retention"),
        QStringLiteral("set_isolation"),
        QStringLiteral("set_level_shifter")
    };
    check(isUpfDocument
              && upf.contains(QStringLiteral("create_power_switch"))
              && upf.contains(QStringLiteral("set_retention"))
              && upf.contains(QStringLiteral("set_isolation"))
              && upf.contains(QStringLiteral("set_level_shifter"))
              && commandLedgerIsComplete
              && ledgerCommandLines == upfCommandLines
              && commandKinds.contains(requiredCommands),
          QStringLiteral(
              "generated UPF and its evidence ledger agree on every Power command class"));
    const QJsonValue coverageComplete = evidence.value(
        QStringLiteral("implementationCoverage")).toObject().value(
        QStringLiteral("complete"));
    check(coverageComplete.isBool()
              && !coverageComplete.toBool()
              && evidence.value(QStringLiteral("implementationCoverage"))
                     .toObject().value(QStringLiteral("summary")).toObject()
                     .value(QStringLiteral("deferred")).toInt() > 0
              && evidence.value(QStringLiteral("validation")).toObject().value(
                     QStringLiteral("commercialSemanticValidation")).toString()
                  == QStringLiteral("not-performed"),
          QStringLiteral(
              "Power evidence does not overclaim deferred or commercial validation"));

    static const QRegularExpression powerControlPortPattern(
        QStringLiteral(
            R"(\binput\s+logic\s+(low_enable|low_isolate|low_save|low_restore)\b)"));
    QSet<QString> materializedControlSignals;
    QRegularExpressionMatchIterator controlMatches =
        powerControlPortPattern.globalMatch(top);
    while (controlMatches.hasNext()) {
        materializedControlSignals.insert(controlMatches.next().captured(1));
    }
    check(materializedControlSignals
              == QSet<QString>{
                  QStringLiteral("low_enable"),
                  QStringLiteral("low_isolate"),
                  QStringLiteral("low_save"),
                  QStringLiteral("low_restore")
              },
          QStringLiteral(
              "every declared top-port Power control is materialized in top RTL"));
}

struct RtlInstanceRecord {
    QString module;
    QString block;
};

QHash<QString, QVector<RtlInstanceRecord>> indexRtlInstances(
    const QString& rtl) {
    static const QRegularExpression expression(
        QStringLiteral(
            R"((?<![A-Za-z0-9_$])([A-Za-z_][A-Za-z0-9_$]*)\s*(?:#\s*\([^;]*?\)\s*)?([A-Za-z_][A-Za-z0-9_$]*)\s*\([^;]*?\)\s*;)"),
        QRegularExpression::DotMatchesEverythingOption);
    QHash<QString, QVector<RtlInstanceRecord>> instances;
    QRegularExpressionMatchIterator matches = expression.globalMatch(rtl);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        instances[match.captured(2)].append(
            RtlInstanceRecord{match.captured(1), match.captured(0)});
    }
    return instances;
}

bool preparePackageFixture(const QString& packageRoot,
                           const QString& generatorSource,
                           const QJsonObject& manifest) {
    const QString executable = QDir(packageRoot).filePath(QStringLiteral("runtime/bin/generate"));
    if (!QDir().mkpath(QFileInfo(executable).absolutePath())) {
        return false;
    }
    if (!QFileInfo::exists(executable)) {
        if (!QFile::copy(generatorSource, executable)) {
            return false;
        }
        if (!QFile::setPermissions(
                executable,
                QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                    QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                    QFileDevice::ReadOther | QFileDevice::ExeOther)) {
            return false;
        }
    }
    return saveJsonObject(QDir(packageRoot).filePath(QStringLiteral("package.json")), manifest);
}

QJsonObject request() {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("test_mesh")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("finepaper.noc")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("z_cpu")},
                {QStringLiteral("type"), QStringLiteral("master")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("a_cpu")},
                {QStringLiteral("type"), QStringLiteral("master")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("memory")},
                {QStringLiteral("type"), QStringLiteral("slave")},
                {QStringLiteral("router"), QJsonArray{1, 1}},
                {QStringLiteral("parameters"), QJsonObject{
                    {QStringLiteral("dataWidth"), 128}
                }}
            }
        }}
    };
}

QJsonObject configurableRequest() {
    QJsonObject configurable = request();
    configurable.insert(QStringLiteral("name"),
                        QStringLiteral("configurable_mesh"));
    QJsonObject package = configurable.value(
        QStringLiteral("package")).toObject();
    package.insert(QStringLiteral("version"), QStringLiteral("3.1.0"));
    configurable.insert(QStringLiteral("package"), package);
    QJsonArray endpoints = configurable.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject memory = endpoints[2].toObject();
    QJsonObject memoryParameters = memory.value(
        QStringLiteral("parameters")).toObject();
    memoryParameters.insert(QStringLiteral("bufferDepth"), 37);
    memoryParameters.insert(QStringLiteral("qosEnabled"), true);
    memory.insert(QStringLiteral("parameters"), memoryParameters);
    endpoints[2] = memory;
    configurable.insert(QStringLiteral("endpoints"), endpoints);
    return configurable;
}

QJsonObject complexRequest() {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("complex_demo")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.complex-engine")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("host")},
                {QStringLiteral("type"), QStringLiteral("client")},
                {QStringLiteral("router"), QJsonArray{0, 0}}
            }
        }},
        {QStringLiteral("packageData"), QJsonObject{
            {QStringLiteral("vendorTopology"), QStringLiteral("opaque-to-core")}
        }}
    };
}

QJsonObject explicitSlotRequest() {
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("explicit_slot_demo")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.explicit-slots")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("device_0")},
                {QStringLiteral("type"), QStringLiteral("device")},
                {QStringLiteral("router"), QJsonArray{0, 0}},
                {QStringLiteral("slot"), QStringLiteral("local1")}
            }
        }}
    };
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);

    const PackageOperationResult invalidOperationResult = parsePackageOperationResult(
        QJsonObject{
            {QStringLiteral("success"), QStringLiteral("yes")},
            {QStringLiteral("diagnostics"), QJsonObject{}},
            {QStringLiteral("artifacts"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("a")},
                            {QStringLiteral("type"), QStringLiteral("rtl")},
                            {QStringLiteral("path"), QStringLiteral("a.sv")},
                            {QStringLiteral("primary"), QStringLiteral("true")}}
            }}},
        QStringLiteral("/tmp/result.json"),
        QStringLiteral("test"),
        ArtifactResultPolicy::Required);
    check(!invalidOperationResult.protocolValid
              && hasDiagnosticCode(invalidOperationResult.diagnostics,
                                   QStringLiteral("operation.invalid_result")),
          QStringLiteral("Package operation results are parsed with a strict typed contract"));

    const JsonObjectLoadResult referenceManifest = loadJsonObject(
        QDir(projectRoot).filePath(QStringLiteral("packages/finepaper-noc/package.json")));
    check(referenceManifest.success, QStringLiteral("reference Package manifest is readable"));
    QTemporaryDir manifestFixture(QStringLiteral("/tmp/finepaper-manifest-test-XXXXXX"));
    check(manifestFixture.isValid(), QStringLiteral("temporary Package fixture is available"));
    const QString generatorSource = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc/runtime/bin/generate"));
    if (referenceManifest.success && manifestFixture.isValid()) {
        QJsonObject invalidManifest = referenceManifest.object;
        invalidManifest.insert(QStringLiteral("endpointTypes"), QJsonObject{});
        invalidManifest.insert(QStringLiteral("engine"), QStringLiteral("invalid"));
        QJsonObject attachment = invalidManifest.value(QStringLiteral("attachment")).toObject();
        attachment.insert(QStringLiteral("maxPerRouter"), 0);
        attachment.insert(QStringLiteral("slotMode"), 7);
        invalidManifest.insert(QStringLiteral("attachment"), attachment);
        QJsonObject generator = invalidManifest.value(QStringLiteral("generator")).toObject();
        generator.insert(QStringLiteral("supportsValidate"), QStringLiteral("true"));
        generator.insert(QStringLiteral("timeoutSeconds"),
                         kMaximumPackageTimeoutSeconds + 1);
        invalidManifest.insert(QStringLiteral("generator"), generator);
        QJsonObject mesh = invalidManifest.value(QStringLiteral("mesh")).toObject();
        QJsonObject rows = mesh.value(QStringLiteral("rows")).toObject();
        rows.insert(QStringLiteral("min"), 4);
        rows.insert(QStringLiteral("default"), 2);
        rows.insert(QStringLiteral("max"), kMaximumMeshDimension + 1);
        mesh.insert(QStringLiteral("rows"), rows);
        invalidManifest.insert(QStringLiteral("mesh"), mesh);
        invalidManifest.insert(QStringLiteral("parameters"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("badInteger")},
                        {QStringLiteral("type"), QStringLiteral("integer")},
                        {QStringLiteral("default"), 1.5},
                        {QStringLiteral("minimum"), 10},
                        {QStringLiteral("maximum"), 1}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("badEnum")},
                        {QStringLiteral("type"), QStringLiteral("enum")},
                        {QStringLiteral("default"), QStringLiteral("missing")},
                        {QStringLiteral("values"), QJsonArray{QStringLiteral("a"),
                                                              QStringLiteral("a"), 3}}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("badType")},
                        {QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("default"), QJsonObject{}}}
        });
        check(preparePackageFixture(manifestFixture.path(), generatorSource, invalidManifest),
              QStringLiteral("strict Package fixture is prepared"));
        const PackageLoadResult invalidPackage = loadPackage(manifestFixture.path());
        check(!invalidPackage.success &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_endpoint_types")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_engine")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_attachment_capacity")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_timeout")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_mesh_rows")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.mesh_projection_too_large")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_parameter_type")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.invalid_parameter_default")) &&
                  hasDiagnosticCode(invalidPackage.diagnostics,
                                    QStringLiteral("package.duplicate_parameter_value")),
              QStringLiteral("Package manifest validation fails closed across typed and semantic fields"));
    }

#ifdef Q_OS_UNIX
    QTemporaryDir executableFixture(QStringLiteral("/tmp/finepaper-executable-test-XXXXXX"));
    if (referenceManifest.success && executableFixture.isValid()) {
        QJsonObject escapedManifest = referenceManifest.object;
        const QString linkedExecutable = QDir(executableFixture.path()).filePath(
            QStringLiteral("runtime/bin/generate"));
        check(QDir().mkpath(QFileInfo(linkedExecutable).absolutePath()) &&
                  QFile(QStringLiteral("/bin/true")).link(linkedExecutable) &&
                  saveJsonObject(QDir(executableFixture.path()).filePath(
                                     QStringLiteral("package.json")),
                                 escapedManifest),
              QStringLiteral("escaped executable fixture is prepared"));
        const PackageLoadResult escapedPackage = loadPackage(executableFixture.path());
        check(!escapedPackage.success &&
                  hasDiagnosticCode(escapedPackage.diagnostics,
                                    QStringLiteral("package.executable_escape")),
              QStringLiteral("Package executable symlinks cannot escape the Package root"));
    }
#endif

    QTemporaryDir runtimeRoots(QStringLiteral("/tmp/finepaper-runtime-roots-XXXXXX"));
    const QByteArray previousPackagePath = qgetenv("FINEPAPER_PACKAGE_PATH");
    if (runtimeRoots.isValid()) {
        const QDir rootsDirectory(runtimeRoots.path());
        rootsDirectory.mkpath(QStringLiteral("explicit"));
        rootsDirectory.mkpath(QStringLiteral("configured"));
        rootsDirectory.mkpath(QStringLiteral("environment"));
        rootsDirectory.mkpath(QStringLiteral("fallback"));
        qputenv("FINEPAPER_PACKAGE_PATH",
                 rootsDirectory.filePath(QStringLiteral("environment")).toUtf8());
        const QString explicitRoot = rootsDirectory.filePath(QStringLiteral("explicit"));
        const QString configuredRoot = rootsDirectory.filePath(QStringLiteral("configured"));
        const RuntimeLocations locations = resolveRuntimeLocations(
            QStringList{explicitRoot, explicitRoot + QStringLiteral("/.")},
            QStringList{configuredRoot}, runtimeRoots.path());
        const QStringList roots = locations.packageRoots;
        check(roots.size() >= 6 && roots.at(0) == QFileInfo(explicitRoot).canonicalFilePath()
                  && roots.at(1) == QFileInfo(configuredRoot).canonicalFilePath()
                  && roots.at(2) == QFileInfo(rootsDirectory.filePath(
                      QStringLiteral("environment"))).canonicalFilePath(),
              QStringLiteral("runtime Package roots merge in source order with canonical deduplication"));
        check(roots.contains(QDir(QCoreApplication::applicationDirPath()).filePath(
                  QStringLiteral("../share/finepaper/packages")))
                  || roots.contains(QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath(
                      QStringLiteral("../share/finepaper/packages"))).absoluteFilePath()),
              QStringLiteral("runtime resolver includes application share Package fallback"));
        RuntimeLocations installedLocations = locations;
        const QString fixtureRoot = QDir(projectRoot).filePath(QStringLiteral("tests/fixtures"));
        appendPackageRoots(installedLocations, QStringList{fixtureRoot, fixtureRoot}, projectRoot);
        check(installedLocations.packageRoots.size() == locations.packageRoots.size() + 1,
              QStringLiteral("manually added Package roots merge centrally without duplicates"));
    }
    if (previousPackagePath.isNull()) {
        qunsetenv("FINEPAPER_PACKAGE_PATH");
    } else {
        qputenv("FINEPAPER_PACKAGE_PATH", previousPackagePath);
    }
    const RuntimeLocations locations = resolveRuntimeLocations(
        QStringList{QDir(projectRoot).filePath(QStringLiteral("packages"))}, projectRoot);
    check(locations.packageRoots.first() == QFileInfo(QDir(projectRoot).filePath(
              QStringLiteral("packages"))).canonicalFilePath(),
          QStringLiteral("explicit Package roots use the shared runtime resolver"));
    check(locations.defaultOutputRoot == QDir(projectRoot).filePath(QStringLiteral("output")),
          QStringLiteral("default output root comes from the shared runtime resolver"));
    FinepaperApplication finepaper;
    const QString bundledPackageRoot = QDir(projectRoot).filePath(
        QStringLiteral("packages"));
    const QString bundledPackagePath = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc"));
    const PackageCatalogReloadResult packageReload = finepaper.reloadPackages(
        QStringList{bundledPackageRoot});
    check(packageReload.committed() && !hasErrors(packageReload.diagnostics),
          QStringLiteral("reference Packages load"));
    check(finepaper.packages().size() == 2
              && finepaper.packages().at(0).key()
                  == QStringLiteral("finepaper.noc@1.0.0")
              && finepaper.packages().at(1).key()
                  == QStringLiteral("finepaper.noc@3.1.0"),
          QStringLiteral("V1 compatibility and V3 configurable Packages load together"));
    const auto configurablePackage = std::find_if(
        finepaper.packages().cbegin(), finepaper.packages().cend(),
        [](const PackageDefinition& package) {
            return package.key() == QStringLiteral("finepaper.noc@3.1.0");
        });
    const EndpointTypeDefinition* masterType =
        configurablePackage != finepaper.packages().cend()
        ? configurablePackage->endpointType(QStringLiteral("master"))
        : nullptr;
    const ParameterDefinition* qosParameter = nullptr;
    if (masterType) {
        const auto parameter = std::find_if(
            masterType->parameters.cbegin(), masterType->parameters.cend(),
            [](const ParameterDefinition& value) {
                return value.id == QStringLiteral("qosEnabled");
            });
        if (parameter != masterType->parameters.cend()) {
            qosParameter = &(*parameter);
        }
    }
    check(qosParameter
              && qosParameter->category == QStringLiteral("Traffic")
              && qosParameter->advanced
              && !qosParameter->description.isEmpty(),
          QStringLiteral(
              "bundled V3 Endpoint schemas expose Package-owned presentation metadata"));

    QTemporaryDir duplicatePackageFixture(
        QStringLiteral("/tmp/finepaper-duplicate-package-XXXXXX"));
    check(referenceManifest.success && duplicatePackageFixture.isValid()
              && preparePackageFixture(duplicatePackageFixture.path(),
                                       generatorSource,
                                       referenceManifest.object),
          QStringLiteral("duplicate Package fixture is prepared"));
    if (duplicatePackageFixture.isValid()) {
        FinepaperApplication resilientCatalog;
        const QString missingPackageRoot = QDir(duplicatePackageFixture.path()).filePath(
            QStringLiteral("missing-root"));
        const PackageCatalogReloadResult discoveryReload = resilientCatalog.reloadPackages(
            QStringList{bundledPackageRoot,
                        bundledPackagePath,
                        duplicatePackageFixture.path(),
                        missingPackageRoot});
        check(discoveryReload.committed()
                  && resilientCatalog.packages().size() == 1
                  && resilientCatalog.packages().at(0).id == QStringLiteral("finepaper.noc")
                  && resilientCatalog.packages().at(0).version == QStringLiteral("3.1.0")
                  && discoveryReload.acceptedCount == 1
                  && discoveryReload.rejectedCount == 2
                  && hasDiagnosticCode(discoveryReload.diagnostics,
                                       QStringLiteral("package.duplicate_conflict"))
                  && hasDiagnosticCode(discoveryReload.diagnostics,
                                       QStringLiteral("package.root_missing")),
              QStringLiteral("conflicting duplicates are both isolated without hiding unrelated Packages"));
    }
    if (manifestFixture.isValid()) {
        const PackageCatalogReloadResult failedReload = finepaper.reloadPackages(
            QStringList{manifestFixture.path()});
        check(!failedReload.committed()
                  && !failedReload.catalogFatal()
                  && hasErrors(failedReload.diagnostics)
                  && finepaper.packages().size() == 2 &&
                  finepaper.packages().at(0).id == QStringLiteral("finepaper.noc"),
              QStringLiteral("an all-rejected Package reload preserves the previous catalog snapshot"));
    }

    const DesignResult created = finepaper.createDesign(request());
    check(created.success, QStringLiteral("request creates a valid NocDesign"));
    check(created.design.parameters.value(QStringLiteral("dataWidth")).toInt() == 64,
          QStringLiteral("Package defaults are materialized at creation"));
    check(created.design.endpoints.at(0).parameters.value(QStringLiteral("bufferDepth")).toInt() == 16,
          QStringLiteral("endpoint defaults are materialized at creation"));

    const DesignCreationRequest interactiveRequest{
        QStringLiteral("  Typed Interactive NoC  "),
        PackageReference{QStringLiteral("finepaper.noc"),
                         QStringLiteral("1.0.0")},
        TopologySpec{QStringLiteral("mesh"), 3, 4},
        std::nullopt};
    const DesignResult interactiveCreated = finepaper.createDesign(
        interactiveRequest);
    check(interactiveCreated.success
              && interactiveCreated.design.name
                     == QStringLiteral("Typed Interactive NoC")
              && interactiveCreated.design.package == interactiveRequest.package
              && interactiveCreated.design.package.key()
                     == QStringLiteral("finepaper.noc@1.0.0")
              && interactiveCreated.design.topology.rows == 3
              && interactiveCreated.design.topology.columns == 4
              && interactiveCreated.design.parameters.value(
                     QStringLiteral("dataWidth")).toInt() == 64,
          QStringLiteral(
              "typed interactive creation crosses the Application boundary and preserves normal defaults and validation"));

    QJsonObject numericNameRequest = request();
    numericNameRequest.insert(QStringLiteral("name"), QStringLiteral("123 Demo-NoC"));
    numericNameRequest.remove(QStringLiteral("id"));
    const DesignResult numericNameDesign = finepaper.createDesign(numericNameRequest);
    check(numericNameDesign.success &&
              numericNameDesign.design.id == QStringLiteral("noc_123_demo_noc"),
          QStringLiteral("generated design IDs are safe when display names start with digits"));

    QJsonObject fractionalTopologyRequest = request();
    QJsonObject fractionalTopology = fractionalTopologyRequest.value(
        QStringLiteral("topology")).toObject();
    fractionalTopology.insert(QStringLiteral("rows"), 1.5);
    fractionalTopologyRequest.insert(QStringLiteral("topology"), fractionalTopology);
    const DesignResult fractionalTopologyDesign = finepaper.createDesign(
        fractionalTopologyRequest);
    check(!fractionalTopologyDesign.success &&
              hasDiagnosticCode(fractionalTopologyDesign.diagnostics,
                                QStringLiteral("create.expected_integer")),
          QStringLiteral("createDesign rejects fractional topology dimensions"));

    QJsonObject invalidEndpointsRequest = request();
    invalidEndpointsRequest.insert(QStringLiteral("endpoints"), QJsonObject{});
    const DesignResult invalidEndpointsDesign = finepaper.createDesign(invalidEndpointsRequest);
    check(!invalidEndpointsDesign.success &&
              hasDiagnosticCode(invalidEndpointsDesign.diagnostics,
                                QStringLiteral("create.expected_array")),
          QStringLiteral("createDesign rejects a non-array endpoints field"));

    QJsonObject automaticSlotRequest = request();
    QJsonArray automaticSlotEndpoints = automaticSlotRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject automaticSlotEndpoint = automaticSlotEndpoints.at(0).toObject();
    automaticSlotEndpoint.insert(QStringLiteral("slot"), QStringLiteral("0"));
    automaticSlotEndpoints[0] = automaticSlotEndpoint;
    automaticSlotRequest.insert(QStringLiteral("endpoints"), automaticSlotEndpoints);
    const DesignResult automaticSlotDesign = finepaper.createDesign(automaticSlotRequest);
    check(!automaticSlotDesign.success &&
              hasDiagnosticCode(automaticSlotDesign.diagnostics,
                                QStringLiteral("endpoint.automatic_slot_persisted")),
          QStringLiteral("automatic Package source designs cannot persist derived slots"));

    NocDesign duplicateAutomaticSlots = created.design;
    duplicateAutomaticSlots.endpoints[0].attachment.slot = QStringLiteral("same");
    duplicateAutomaticSlots.endpoints[1].attachment.slot = QStringLiteral("same");
    const ValidationResult duplicateAutomaticValidation = finepaper.validate(
        duplicateAutomaticSlots, false);
    check(!duplicateAutomaticValidation.success &&
              hasDiagnosticCode(duplicateAutomaticValidation.diagnostics,
                                QStringLiteral("endpoint.duplicate_slot")),
          QStringLiteral("duplicate persisted slots are rejected in automatic mode"));

    NocDesign staleSlotDesign = created.design;
    staleSlotDesign.endpoints[2].attachment.slot = QStringLiteral("stale");
    const DesignResult clearedSlotMove = finepaper.moveEndpoint(
        staleSlotDesign, QStringLiteral("memory"), RouterPosition{0, 1}, std::nullopt);
    check(clearedSlotMove.success &&
              !clearedSlotMove.design.endpoints.at(2).attachment.slot,
          QStringLiteral("moving with no slot clears stale automatic slot state"));

    NocDesign oversizedDesign = created.design;
    oversizedDesign.topology.rows = kMaximumMeshDimension;
    oversizedDesign.topology.columns = kMaximumMeshDimension;
    const QVector<Diagnostic> oversizedDiagnostics = validateDesignStructure(oversizedDesign);
    check(hasDiagnosticCode(oversizedDiagnostics,
                            QStringLiteral("topology.projection_too_large")) &&
              projectTopology(oversizedDesign).routers.isEmpty(),
          QStringLiteral("oversized topology is rejected and never projected"));

    NocDesign unsafeIdentifierDesign = created.design;
    unsafeIdentifierDesign.id = QStringLiteral("unsafe-id");
    const ValidationResult unsafeIdentifierValidation = finepaper.validate(
        unsafeIdentifierDesign, true);
    check(!unsafeIdentifierValidation.success,
          QStringLiteral("Package process validation rejects unsafe HDL design identifiers"));

    const NocDesign resolved = withResolvedAutomaticSlots(created.design);
    check(resolved.endpoints.at(0).attachment.slot == QStringLiteral("1"),
          QStringLiteral("automatic slot assignment is stable by endpoint id"));
    check(resolved.endpoints.at(1).attachment.slot == QStringLiteral("0"),
          QStringLiteral("automatic slot assignment is stable by endpoint id"));

    const TopologyProjection projection = projectTopology(resolved);
    check(projection.routers.size() == 4, QStringLiteral("2x2 Mesh projects four Routers"));
    check(projection.links.size() == 4, QStringLiteral("2x2 Mesh projects four Links"));
    check(projection.routers.at(0).id == QStringLiteral("r-0-0"),
          QStringLiteral("Mesh Router IDs are deterministic"));

    const DesignLoadResult reloaded = designFromJson(designToJson(created.design));
    check(reloaded.success, QStringLiteral("NocDesign JSON round-trips"));
    check(reloaded.design.endpoints.size() == created.design.endpoints.size(),
          QStringLiteral("NocDesign JSON preserves Endpoint attachments"));

    const DesignResult rejectedResize = finepaper.resizeMesh(created.design, 1, 1);
    check(!rejectedResize.success, QStringLiteral("Mesh resize does not silently detach Endpoints"));
    check(hasDiagnosticCode(rejectedResize.diagnostics,
                            QStringLiteral("mesh.resize_would_detach_endpoint")),
          QStringLiteral("unsafe Mesh resize reports its reason"));

    const DesignResult moved = finepaper.moveEndpoint(
        created.design, QStringLiteral("memory"), RouterPosition{0, 1});
    check(moved.success && moved.design.endpoints.at(2).attachment.router == RouterPosition{0, 1},
          QStringLiteral("Endpoint movement uses the shared application operation"));

    const DesignResult rejectedMove = finepaper.moveEndpoint(
        created.design, QStringLiteral("memory"), RouterPosition{99, 99});
    check(!rejectedMove.success
              && designToJson(rejectedMove.design) == designToJson(created.design),
          QStringLiteral("failed Endpoint movement returns the unchanged Design"));

    QJsonObject invalidParameters = created.design.parameters;
    invalidParameters.insert(QStringLiteral("dataWidth"), QStringLiteral("wide"));
    const DesignResult rejectedParameterUpdate = finepaper.updateParameters(
        created.design, invalidParameters);
    check(!rejectedParameterUpdate.success
              && designToJson(rejectedParameterUpdate.design) == designToJson(created.design),
          QStringLiteral("failed parameter updates return the unchanged Design"));

    const ValidationResult validation = finepaper.validate(created.design, true);
    check(validation.success, QStringLiteral("reference Package validates through its process boundary"));

    QTemporaryDir outputRoot(QStringLiteral("/tmp/finepaper-core-test-XXXXXX"));
    check(outputRoot.isValid(), QStringLiteral("temporary output directory is available"));
    if (outputRoot.isValid()) {
        const GenerationResult generation = finepaper.generate(
            created.design, GenerationOptions{outputRoot.path()});
        check(generation.success, QStringLiteral("reference Package generates RTL"));
        check(!generation.artifacts.isEmpty(), QStringLiteral("generation reports artifacts"));
        check(std::none_of(
                  generation.artifacts.cbegin(),
                  generation.artifacts.cend(),
                  [](const Artifact& artifact) {
                      return artifact.type == QStringLiteral("constraints")
                          || artifact.path.endsWith(
                              QStringLiteral("_domain_constraints.json"));
                  }),
              QStringLiteral(
                  "V1 compatibility generation does not invent Domain constraints"));
        const auto primary = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(), [](const Artifact& artifact) {
                return artifact.primary;
            });
        check(primary != generation.artifacts.cend(), QStringLiteral("generation reports a primary artifact"));
        if (primary != generation.artifacts.cend()) {
            check(QFileInfo(QDir(generation.outputDirectory).filePath(primary->path)).isFile(),
                  QStringLiteral("primary artifact exists below the run output directory"));
        }
    }

    const DesignResult configurableCreated = finepaper.createDesign(
        configurableRequest());
    check(configurableCreated.success
              && configurableCreated.design.formatVersion == 3
              && configurableCreated.design.domains.size() == 2
              && configurableCreated.design.domainMemberships.size() == 7,
          QStringLiteral(
              "bundled V3 Package materializes Package-declared clock/power defaults and assignments"));
    const DesignResult routerConfigured = configurableCreated.success
        ? finepaper.setElementConfiguration(
              configurableCreated.design,
              ElementRef{ElementKind::Router, QStringLiteral("r-0-0")},
              QStringLiteral("router.microarchitecture"),
              QJsonObject{
                  {QStringLiteral("routingAlgorithm"), QStringLiteral("yx")},
                  {QStringLiteral("virtualChannels"), 4},
                  {QStringLiteral("bufferDepth"), 16}})
        : DesignResult{};
    check(routerConfigured.success
              && routerConfigured.design.elementConfigurations.size() == 1,
          QStringLiteral(
              "bundled V3 Package accepts sparse Router microarchitecture overrides"));
    QTemporaryDir configurableOutput(
        QStringLiteral("/tmp/finepaper-configurable-core-test-XXXXXX"));
    if (routerConfigured.success && configurableOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            routerConfigured.design,
            GenerationOptions{configurableOutput.path()});
        const auto primary = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) { return artifact.primary; });
        const auto intent = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.path.endsWith(
                    QStringLiteral("_design_intent.json"));
            });
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints")
                    && artifact.path.endsWith(
                        QStringLiteral("_domain_constraints.json"));
            });
        const auto implementation = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("implementation-plan")
                    && artifact.path.endsWith(
                        QStringLiteral("_domain_implementation.json"));
            });
        QString primaryText;
        if (primary != generation.artifacts.cend()) {
            QFile primaryFile(QDir(generation.outputDirectory).filePath(
                primary->path));
            if (primaryFile.open(QIODevice::ReadOnly)) {
                primaryText = QString::fromUtf8(primaryFile.readAll());
            }
        }
        DesignLoadResult intentDesign;
        if (intent != generation.artifacts.cend()) {
            intentDesign = loadDesign(
                QDir(generation.outputDirectory).filePath(intent->path));
        }
        JsonObjectLoadResult defaultConstraints;
        if (constraints != generation.artifacts.cend()) {
            defaultConstraints = loadJsonObject(
                QDir(generation.outputDirectory).filePath(constraints->path));
        }
        JsonObjectLoadResult defaultImplementation;
        if (implementation != generation.artifacts.cend()) {
            defaultImplementation = loadJsonObject(
                QDir(generation.outputDirectory).filePath(implementation->path));
        }
        const auto intentMemory = intentDesign.success
            ? std::find_if(
                  intentDesign.design.endpoints.cbegin(),
                  intentDesign.design.endpoints.cend(),
                  [](const EndpointInstance& endpoint) {
                      return endpoint.id == QStringLiteral("memory");
                  })
            : intentDesign.design.endpoints.cend();
        check(generation.success
                  && primaryText.contains(
                      QStringLiteral("route=yx vc=4 depth=16"))
                  && primaryText.contains(
                      QStringLiteral(
                          "data_width=128 buffer_depth=37 qos=enabled"))
                  && intentDesign.success
                  && intentDesign.design.elementConfigurations
                      == routerConfigured.design.elementConfigurations
                  && intentMemory != intentDesign.design.endpoints.cend()
                  && intentMemory->parameters.value(
                         QStringLiteral("bufferDepth")).toInt() == 37
                  && intentMemory->parameters.value(
                         QStringLiteral("qosEnabled")).toBool()
                  && defaultConstraints.success
                  && defaultConstraints.object.value(
                         QStringLiteral("format")).toString()
                      == QStringLiteral("finepaper.noc-domain-constraints")
                  && defaultConstraints.object.value(
                         QStringLiteral("boundarySemantics")).toObject().value(
                         QStringLiteral("scope")).toString()
                      == QStringLiteral("bidirectional-physical-edge")
                  && defaultConstraints.object.value(
                         QStringLiteral("instances")).toArray().size() == 2
                  && defaultConstraints.object.value(
                         QStringLiteral("meshCrossings")).toArray().isEmpty()
                  && defaultImplementation.success
                  && defaultImplementation.object.value(
                         QStringLiteral("format")).toString()
                      == QStringLiteral(
                          "finepaper.noc-domain-implementation-plan")
                  && defaultImplementation.object.value(
                         QStringLiteral("domainBindings")).toArray().size() == 2,
              QStringLiteral(
                  "bundled V3 Generator consumes intent and emits a typed Domain implementation plan"));
    }

    QTemporaryDir hermeticV3Run(
        QStringLiteral("/tmp/finepaper-hermetic-v3-test-XXXXXX"));
    if (configurableCreated.success && hermeticV3Run.isValid()) {
        const QString isolatedPackage = QDir(hermeticV3Run.path()).filePath(
            QStringLiteral("finepaper-noc-v3"));
        const QString designPath = QDir(hermeticV3Run.path()).filePath(
            QStringLiteral("design.json"));
        const QString resultPath = QDir(hermeticV3Run.path()).filePath(
            QStringLiteral("result.json"));
        const QString outputPath = QDir(hermeticV3Run.path()).filePath(
            QStringLiteral("output"));
        const bool copied = copyDirectoryTree(
            QDir(projectRoot).filePath(
                QStringLiteral("packages/finepaper-noc-v3")),
            isolatedPackage);
        const bool saved = saveDesign(designPath, configurableCreated.design);
        const ProcessResult process = copied && saved
            ? runProcess(
                  QDir(isolatedPackage).filePath(
                      QStringLiteral("runtime/bin/generate")),
                  QStringList{
                      QStringLiteral("generate"),
                      QStringLiteral("--design"),
                      designPath,
                      QStringLiteral("--output"),
                      outputPath,
                      QStringLiteral("--result"),
                      resultPath
                  },
                  hermeticV3Run.path(),
                  30'000)
            : ProcessResult{};
        const JsonObjectLoadResult result = loadJsonObject(resultPath);
        check(copied
                  && saved
                  && process.started
                  && process.exitCode == 0
                  && result.success
                  && result.object.value(QStringLiteral("success")).toBool()
                  && QFileInfo::exists(
                      QDir(outputPath).filePath(
                          QStringLiteral(
                              "configurable_mesh_domain_constraints.json")))
                  && QFileInfo::exists(
                      QDir(outputPath).filePath(
                          QStringLiteral(
                              "configurable_mesh_domain_implementation.json"))),
              QStringLiteral(
                  "bundled V3 Package validates and generates when installed without its V1 sibling"));

        const QString v1DesignPath = QDir(hermeticV3Run.path()).filePath(
            QStringLiteral("v1-design.json"));
        const QString v1ResultPath = QDir(hermeticV3Run.path()).filePath(
            QStringLiteral("v1-result.json"));
        const bool savedV1 = saveDesign(v1DesignPath, created.design);
        const ProcessResult v1Process = copied && savedV1
            ? runProcess(
                  QDir(isolatedPackage).filePath(
                      QStringLiteral("runtime/bin/generate")),
                  QStringList{
                      QStringLiteral("validate"),
                      QStringLiteral("--design"),
                      v1DesignPath,
                      QStringLiteral("--result"),
                      v1ResultPath
                  },
                  hermeticV3Run.path(),
                  30'000)
            : ProcessResult{};
        const JsonObjectLoadResult v1Result = loadJsonObject(v1ResultPath);
        const QJsonArray v1Diagnostics = v1Result.object.value(
            QStringLiteral("diagnostics")).toArray();
        const QJsonObject v1Diagnostic = v1Diagnostics.isEmpty()
            ? QJsonObject{}
            : v1Diagnostics.at(0).toObject();
        check(savedV1
                  && v1Process.started
                  && v1Process.exitCode != 0
                  && v1Result.success
                  && v1Diagnostic.value(QStringLiteral("code")).toString()
                      == QStringLiteral("realization.invalid_design_version")
                  && v1Diagnostic.value(QStringLiteral("path")).toString()
                      == QStringLiteral("/design/formatVersion"),
              QStringLiteral(
                  "standalone V3 Package rejects a V1 Design before the legacy generator"));
    }

    NocDesign crossingDesign;
    if (routerConfigured.success) {
        crossingDesign = routerConfigured.design;
        crossingDesign.domains.append(DomainDefinition{
            QStringLiteral("clock-io"),
            QStringLiteral("clock"),
            QStringLiteral("I/O clock"),
            QJsonObject{{QStringLiteral("frequencyMHz"), 500}}
        });
        crossingDesign.domains.append(DomainDefinition{
            QStringLiteral("power-low"),
            QStringLiteral("power"),
            QStringLiteral("Low-voltage island"),
            QJsonObject{
                {QStringLiteral("voltageMv"), 750},
                {QStringLiteral("retention"), true}
            }
        });
        for (DomainMembership& membership : crossingDesign.domainMemberships) {
            const bool alternateRouter = membership.element.kind == ElementKind::Router
                && (membership.element.id == QStringLiteral("r-1-0")
                    || membership.element.id == QStringLiteral("r-1-1"));
            const bool alternateEndpoint = membership.element.kind == ElementKind::Endpoint
                && membership.element.id == QStringLiteral("memory");
            if (alternateRouter || alternateEndpoint) {
                membership.assignments.insert(
                    QStringLiteral("clock"),
                    QStringList{QStringLiteral("clock-io")});
                membership.assignments.insert(
                    QStringLiteral("power"),
                    QStringList{QStringLiteral("power-low")});
            }
        }
        crossingDesign.domainRelations.append(DomainRelation{
            QStringLiteral("derived-from"),
            QStringLiteral("clock-io"),
            QStringLiteral("clock-main"),
            QJsonObject{{QStringLiteral("divider"), 2}}
        });
        crossingDesign.crossingPolicies = {
            DomainCrossingPolicy{
                QStringLiteral("clock-main-to-io"),
                QStringLiteral("clock"),
                QStringLiteral("clock-main"),
                QStringLiteral("clock-io"),
                QJsonObject{
                    {QStringLiteral("implementation"), QStringLiteral("async-fifo")},
                    {QStringLiteral("synchronizerStages"), 3},
                    {QStringLiteral("fifoDepth"), 4}
                }
            },
            DomainCrossingPolicy{
                QStringLiteral("power-main-to-low"),
                QStringLiteral("power"),
                QStringLiteral("power-main"),
                QStringLiteral("power-low"),
                QJsonObject{
                    {QStringLiteral("isolation"), true},
                    {QStringLiteral("levelShift"), QStringLiteral("auto")}
                }
            }
        };
        crossingDesign.edgeOverrides.append(DomainEdgeOverride{
            ElementRef{
                ElementKind::RouterLink,
                linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
            },
            QStringLiteral("power"),
            QStringLiteral("power-main-to-low"),
            QJsonObject{{QStringLiteral("isolation"), false}}
        });
    }

    const ValidationResult crossingValidation = routerConfigured.success
        ? finepaper.validate(crossingDesign, true)
        : ValidationResult{};
    check(crossingValidation.success,
          QStringLiteral(
              "bundled V3 runtime validates Domain relations and resolvable Mesh crossings"));

    NocDesign defaultedFifoDepthDesign = crossingDesign;
    for (DomainCrossingPolicy& policy :
         defaultedFifoDepthDesign.crossingPolicies) {
        if (policy.domainType == QStringLiteral("clock")) {
            policy.properties.remove(QStringLiteral("fifoDepth"));
        }
    }
    const ValidationResult defaultedFifoDepthValidation =
        routerConfigured.success
        ? finepaper.validate(defaultedFifoDepthDesign, true)
        : ValidationResult{};
    check(defaultedFifoDepthValidation.success,
          QStringLiteral(
              "V3 runtime preserves designs that predate the optional FIFO depth property"));

    NocDesign invalidFifoDepthDesign = crossingDesign;
    for (DomainCrossingPolicy& policy : invalidFifoDepthDesign.crossingPolicies) {
        if (policy.domainType == QStringLiteral("clock")) {
            policy.properties.insert(QStringLiteral("fifoDepth"), 3);
        }
    }
    const ValidationResult invalidFifoDepthValidation = routerConfigured.success
        ? finepaper.validate(invalidFifoDepthDesign, true)
        : ValidationResult{};
    const auto invalidFifoDepthDiagnostic = std::find_if(
        invalidFifoDepthValidation.diagnostics.cbegin(),
        invalidFifoDepthValidation.diagnostics.cend(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code
                    == QStringLiteral("realization.value_not_power_of_two")
                && diagnostic.path.endsWith(QStringLiteral("/fifoDepth"));
        });
    check(!invalidFifoDepthValidation.success
              && invalidFifoDepthDiagnostic
                  != invalidFifoDepthValidation.diagnostics.cend(),
          QStringLiteral(
              "V3 runtime reports a structured path when FIFO depth is not a power of two"));

    QTemporaryDir crossingOutput(
        QStringLiteral("/tmp/finepaper-domain-crossing-test-XXXXXX"));
    check(crossingOutput.isValid(),
          QStringLiteral("Domain crossing output directory is available"));
    QJsonObject crossingConstraints;
    QString crossingConstraintsText;
    QJsonObject crossingImplementation;
    QString crossingImplementationText;
    QJsonObject crossingHierarchy;
    QString crossingTopText;
    QString crossingEvidenceText;
    if (crossingValidation.success && crossingOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            crossingDesign, GenerationOptions{crossingOutput.path()});
        const Artifact* topArtifact = primaryArtifact(generation);
        const Artifact* evidenceArtifact = artifactWithType(
            generation, QStringLiteral("implementation-evidence"));
        const Artifact* hierarchyArtifact = artifactWithType(
            generation, QStringLiteral("rtl-hierarchy"));
        crossingTopText = readArtifact(generation, topArtifact);
        crossingEvidenceText = readArtifact(generation, evidenceArtifact);
        QJsonObject crossingEvidence;
        if (evidenceArtifact) {
            crossingEvidence = loadJsonObject(
                QDir(generation.outputDirectory).filePath(
                    evidenceArtifact->path)).object;
        }
        if (hierarchyArtifact) {
            crossingHierarchy = loadJsonObject(
                QDir(generation.outputDirectory).filePath(
                    hierarchyArtifact->path)).object;
        }
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints")
                    && artifact.path.endsWith(
                        QStringLiteral("_domain_constraints.json"));
            });
        const auto implementation = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("implementation-plan")
                    && artifact.path.endsWith(
                        QStringLiteral("_domain_implementation.json"));
            });
        if (constraints != generation.artifacts.cend()) {
            const QString path = QDir(generation.outputDirectory).filePath(
                constraints->path);
            const JsonObjectLoadResult loaded = loadJsonObject(path);
            crossingConstraints = loaded.object;
            QFile constraintsFile(path);
            if (constraintsFile.open(QIODevice::ReadOnly)) {
                crossingConstraintsText = QString::fromUtf8(
                    constraintsFile.readAll());
            }
        }
        if (implementation != generation.artifacts.cend()) {
            const QString path = QDir(generation.outputDirectory).filePath(
                implementation->path);
            const JsonObjectLoadResult loaded = loadJsonObject(path);
            crossingImplementation = loaded.object;
            QFile implementationFile(path);
            if (implementationFile.open(QIODevice::ReadOnly)) {
                crossingImplementationText = QString::fromUtf8(
                    implementationFile.readAll());
            }
        }
        const QJsonObject clockInstance = objectWithStringField(
            crossingConstraints.value(QStringLiteral("instances")).toArray(),
            QStringLiteral("id"),
            QStringLiteral("clock-io"));
        QJsonObject overriddenCrossing;
        const QString overriddenLink = linkId(
            QStringLiteral("r-0-0"), QStringLiteral("r-1-0"));
        for (const QJsonValue& value : crossingConstraints.value(
                 QStringLiteral("meshCrossings")).toArray()) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("domainType")).toString()
                    == QStringLiteral("power")
                && candidate.value(QStringLiteral("edge")).toObject().value(
                       QStringLiteral("id")).toString() == overriddenLink) {
                overriddenCrossing = candidate;
                break;
            }
        }
        const QJsonObject resolution = overriddenCrossing.value(
            QStringLiteral("resolution")).toObject();
        const auto implementationEdge = [&crossingImplementation](
                                            const QString& id) {
            for (const QJsonValue& value : crossingImplementation.value(
                     QStringLiteral("edgeBindings")).toArray()) {
                const QJsonObject candidate = value.toObject();
                if (candidate.value(QStringLiteral("edge")).toObject().value(
                        QStringLiteral("id")).toString() == id) {
                    return candidate;
                }
            }
            return QJsonObject{};
        };
        const auto stageRoles = [](const QJsonArray& stages) {
            QStringList roles;
            for (const QJsonValue& value : stages) {
                roles.append(value.toObject().value(
                    QStringLiteral("role")).toString());
            }
            return roles;
        };
        const auto stageWithRole = [](const QJsonArray& stages,
                                      const QString& role) {
            for (const QJsonValue& value : stages) {
                const QJsonObject stage = value.toObject();
                if (stage.value(QStringLiteral("role")).toString() == role) {
                    return stage;
                }
            }
            return QJsonObject{};
        };
        const QJsonArray overriddenPlanStages = implementationEdge(
            overriddenLink).value(QStringLiteral("stages")).toArray();
        const QJsonArray regularPlanStages = implementationEdge(
            linkId(QStringLiteral("r-0-1"), QStringLiteral("r-1-1")))
            .value(QStringLiteral("stages")).toArray();
        const QJsonObject translationStage = stageWithRole(
            overriddenPlanStages,
            QStringLiteral("voltage-translation-boundary"));
        QStringList translationDirections;
        for (const QJsonValue& value : translationStage.value(
                 QStringLiteral("directions")).toArray()) {
            translationDirections.append(
                value.toObject().value(QStringLiteral("parameters")).toObject()
                    .value(QStringLiteral("translation-direction")).toObject()
                    .value(QStringLiteral("value")).toString());
        }
        check(generation.success
                  && !crossingConstraints.isEmpty()
                  && crossingImplementation.value(
                         QStringLiteral("format")).toString()
                      == QStringLiteral(
                          "finepaper.noc-domain-implementation-plan")
                  && clockInstance.value(QStringLiteral("members"))
                         .toArray().size() == 3
                  && crossingConstraints.value(QStringLiteral("relations"))
                         .toArray().size() == 1
                  && crossingConstraints.value(QStringLiteral("policies"))
                         .toArray().size() == 2
                  && crossingConstraints.value(QStringLiteral("overrides"))
                         .toArray().size() == 1
                  && crossingConstraints.value(
                         QStringLiteral("boundarySemantics")).toObject().value(
                         QStringLiteral("policyOrientation")).toString()
                      == QStringLiteral("canonical-edge-endpoints")
                  && crossingConstraints.value(QStringLiteral("meshCrossings"))
                         .toArray().size() == 4
                  && resolution.value(QStringLiteral("source")).toString()
                      == QStringLiteral("override")
                  && resolution.value(QStringLiteral("policy")).toString()
                      == QStringLiteral("power-main-to-low")
                  && resolution.value(QStringLiteral("overrideProperties"))
                         .toObject().contains(QStringLiteral("isolation"))
                  && !resolution.value(QStringLiteral("overrideProperties"))
                          .toObject().value(QStringLiteral("isolation")).toBool()
                  && resolution.value(QStringLiteral("effectiveProperties"))
                         .toObject().value(QStringLiteral("levelShift")).toString()
                      == QStringLiteral("auto")
                  && !resolution.value(QStringLiteral("effectiveProperties"))
                          .toObject().value(QStringLiteral("isolation")).toBool()
                  && stageRoles(overriddenPlanStages)
                      == QStringList{
                          QStringLiteral("timing-boundary"),
                          QStringLiteral("voltage-translation-boundary")}
                  && stageRoles(regularPlanStages)
                      == QStringList{
                          QStringLiteral("timing-boundary"),
                          QStringLiteral("power-isolation-boundary"),
                          QStringLiteral("voltage-translation-boundary")}
                  && translationDirections
                      == QStringList{
                          QStringLiteral("down"), QStringLiteral("up")},
              QStringLiteral(
                  "Domain lowering resolves typed stages, automatic voltage direction, and edge overrides"));

        const QJsonArray domainInfrastructure = crossingEvidence.value(
            QStringLiteral("domainInfrastructure")).toArray();
        bool clockPortsMatchEvidence = domainInfrastructure.size() == 2;
        for (const QJsonValue& value : domainInfrastructure) {
            const QString clockPort = value.toObject().value(
                QStringLiteral("clockPort")).toString();
            clockPortsMatchEvidence = clockPortsMatchEvidence
                && !clockPort.isEmpty()
                && crossingTopText.contains(
                    QStringLiteral("input  logic %1").arg(clockPort));
        }
        check(topArtifact
                  && topArtifact->type == QStringLiteral("rtl")
                  && topArtifact->path.endsWith(QStringLiteral("_top.v"))
                  && evidenceArtifact
                  && crossingEvidence.value(QStringLiteral("format")).toString()
                      == QStringLiteral(
                          "finepaper.noc-domain-implementation-evidence")
                  && crossingEvidence.value(QStringLiteral("formatVersion"))
                         .toInt() == 1
                  && clockPortsMatchEvidence
                  && crossingTopText.count(
                         QStringLiteral("fp_reset_synchronizer #(")) == 2,
              QStringLiteral(
                  "Domain RTL evidence maps two active clock ports and reset synchronizers to the generated top"));

        bool hierarchyMatchesTop = true;
        int deferredCombinedBoundaries = 0;
        const auto rtlInstances = indexRtlInstances(crossingTopText);
        QHash<QString, QString> hierarchyInstanceBlocks;
        const QJsonArray hierarchyElements = crossingHierarchy.value(
            QStringLiteral("elements")).toArray();
        for (const QJsonValue& value : hierarchyElements) {
            const QJsonObject element = value.toObject();
            const QString module = element.value(
                QStringLiteral("module")).toString();
            const QString instance = element.value(
                QStringLiteral("instance")).toString();
            const QVector<RtlInstanceRecord> matches = rtlInstances.value(
                instance);
            const QString block = matches.size() == 1
                ? matches.first().block
                : QString{};
            hierarchyMatchesTop = hierarchyMatchesTop
                && !module.isEmpty()
                && !instance.isEmpty()
                && matches.size() == 1
                && matches.first().module == module
                && !block.isEmpty()
                && !hierarchyInstanceBlocks.contains(instance);
            hierarchyInstanceBlocks.insert(instance, block);
        }
        const QJsonArray hierarchyDirections = crossingHierarchy.value(
            QStringLiteral("edgeDirections")).toArray();
        for (const QJsonValue& value : hierarchyDirections) {
            const QJsonObject direction = value.toObject();
            const QJsonObject bridge = direction.value(
                QStringLiteral("bridge")).toObject();
            const bool bridged = !bridge.isEmpty();
            if (bridged) {
                const QString module = bridge.value(
                    QStringLiteral("module")).toString();
                const QString instance = bridge.value(
                    QStringLiteral("instance")).toString();
                const QVector<RtlInstanceRecord> matches = rtlInstances.value(
                    instance);
                const QString block = matches.size() == 1
                    ? matches.first().block
                    : QString{};
                hierarchyMatchesTop = hierarchyMatchesTop
                    && matches.size() == 1
                    && matches.first().module == module
                    && !block.isEmpty()
                    && !hierarchyInstanceBlocks.contains(instance);
                hierarchyInstanceBlocks.insert(instance, block);
            }
            const QJsonArray flows = direction.value(
                QStringLiteral("signalFlows")).toArray();
            hierarchyMatchesTop = hierarchyMatchesTop
                && flows.size() == (bridged ? 6 : 3);
            for (const QJsonValue& flowValue : flows) {
                const QJsonObject flow = flowValue.toObject();
                const QString type = flow.value(
                    QStringLiteral("type")).toString();
                const QString signal = flow.value(
                    QStringLiteral("signal")).toString();
                if (type == QStringLiteral("ready")) {
                    hierarchyMatchesTop = hierarchyMatchesTop
                        && flow.value(QStringLiteral("direction")).toString()
                            == QStringLiteral("consumer-to-producer");
                }
                for (const QString& terminalName : {
                         QStringLiteral("driver"),
                         QStringLiteral("receiver")}) {
                    const QJsonObject terminal = flow.value(
                        terminalName).toObject();
                    const QString instance = terminal.value(
                        QStringLiteral("instance")).toString();
                    const QString pin = terminal.value(
                        QStringLiteral("pin")).toString();
                    const QString instanceBlock = hierarchyInstanceBlocks.value(
                        instance);
                    hierarchyMatchesTop = hierarchyMatchesTop
                        && !signal.isEmpty()
                        && !instance.isEmpty()
                        && !pin.isEmpty()
                        && !instanceBlock.isEmpty()
                        && instanceBlock.contains(
                            QStringLiteral(".%1(%2)").arg(pin, signal));
                }
            }
            const QJsonObject powerBoundary = direction.value(
                QStringLiteral("powerBoundary")).toObject();
            if (powerBoundary.value(QStringLiteral("status")).toString()
                == QStringLiteral("deferred")) {
                ++deferredCombinedBoundaries;
                hierarchyMatchesTop = hierarchyMatchesTop
                    && bridged
                    && direction.value(
                           QStringLiteral("sourceSupplyDomain")).toString()
                        != direction.value(
                               QStringLiteral("destinationSupplyDomain"))
                               .toString()
                    && powerBoundary.value(
                           QStringLiteral("reasonCode")).toString()
                        == QStringLiteral(
                            "rtl_hierarchy.infrastructure_bridge_supply_unowned");
            }
        }
        check(hierarchyArtifact
                  && crossingHierarchy.value(QStringLiteral("format")).toString()
                      == QStringLiteral("finepaper.noc-rtl-hierarchy")
                  && crossingHierarchy.value(
                         QStringLiteral("formatVersion")).toInt() == 1
                  && crossingHierarchy.value(QStringLiteral("topModule")).toString()
                      == crossingEvidence.value(QStringLiteral("rtl")).toObject()
                             .value(QStringLiteral("topModule")).toString()
                  && hierarchyElements.size()
                      == crossingImplementation.value(
                             QStringLiteral("entityBindings")).toArray().size()
                  && hierarchyDirections.size()
                      == crossingImplementation.value(
                             QStringLiteral("edgeBindings")).toArray().size() * 2
                  && deferredCombinedBoundaries > 0
                  && hierarchyMatchesTop,
              QStringLiteral(
                  "RTL hierarchy artifact binds every emitted element and ready/valid flow to concrete top-level instances and pins"));

        QStringList realizedDirections;
        QStringList realizedInstances;
        bool realizedCdcMatchesTop = true;
        int directionalFifos = 0;
        const QString evidenceTopModule = crossingEvidence.value(
            QStringLiteral("rtl")).toObject().value(
            QStringLiteral("topModule")).toString();
        const QRegularExpression fifoInstancePattern(
            QStringLiteral(
                R"(\bfp_async_ready_valid_fifo\s*#\s*\([^;]*?\)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()"));
        QStringList topFifoInstances;
        QRegularExpressionMatchIterator fifoMatches =
            fifoInstancePattern.globalMatch(crossingTopText);
        while (fifoMatches.hasNext()) {
            topFifoInstances.append(fifoMatches.next().captured(1));
        }
        realizedCdcMatchesTop = !evidenceTopModule.isEmpty()
            && crossingTopText.contains(
                QStringLiteral("module %1 #(").arg(evidenceTopModule));
        const QJsonArray edgeRealizations = crossingEvidence.value(
            QStringLiteral("edgeRealizations")).toArray();
        for (const QJsonValue& value : edgeRealizations) {
            const QJsonObject edge = value.toObject();
            if (edge.value(QStringLiteral("status")).toString()
                != QStringLiteral("realized")) {
                continue;
            }
            const QJsonObject reference = edge.value(
                QStringLiteral("edge")).toObject();
            const QJsonArray directions = edge.value(
                QStringLiteral("directions")).toArray();
            for (const QJsonValue& directionValue : directions) {
                const QJsonObject direction = directionValue.toObject();
                const QJsonObject parameters = direction.value(
                    QStringLiteral("parameters")).toObject();
                const QString hierarchy = direction.value(
                    QStringLiteral("instance")).toString();
                const qsizetype hierarchySeparator = hierarchy.lastIndexOf(u'.');
                const QString instanceName = hierarchySeparator > 0
                    ? hierarchy.sliced(hierarchySeparator + 1)
                    : QString{};
                const bool instanceIsUnique =
                    !realizedInstances.contains(instanceName);
                realizedInstances.append(instanceName);
                realizedDirections.append(
                    reference.value(QStringLiteral("id")).toString()
                    + QStringLiteral(":")
                    + direction.value(QStringLiteral("orientation")).toString());
                ++directionalFifos;
                realizedCdcMatchesTop = realizedCdcMatchesTop
                    && reference.value(QStringLiteral("kind")).toString()
                        == QStringLiteral("router-link")
                    && direction.value(QStringLiteral("module")).toString()
                        == QStringLiteral("fp_async_ready_valid_fifo")
                    && parameters.value(QStringLiteral("DEPTH")).toObject()
                           .value(QStringLiteral("value")).toInt() == 4
                    && parameters.value(QStringLiteral("SYNC_STAGES")).toObject()
                           .value(QStringLiteral("value")).toInt() == 3
                    && hierarchy == evidenceTopModule + u'.' + instanceName
                    && !instanceName.isEmpty()
                    && instanceIsUnique
                    && topFifoInstances.contains(instanceName);
            }
        }
        realizedDirections.sort();
        QStringList expectedDirections = {
            linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
                + QStringLiteral(":from-to"),
            linkId(QStringLiteral("r-0-0"), QStringLiteral("r-1-0"))
                + QStringLiteral(":to-from"),
            linkId(QStringLiteral("r-0-1"), QStringLiteral("r-1-1"))
                + QStringLiteral(":from-to"),
            linkId(QStringLiteral("r-0-1"), QStringLiteral("r-1-1"))
                + QStringLiteral(":to-from")};
        expectedDirections.sort();
        realizedInstances.sort();
        topFifoInstances.sort();
        check(realizedCdcMatchesTop
                  && directionalFifos == 4
                  && realizedDirections == expectedDirections
                  && realizedInstances == topFifoInstances
                  && crossingTopText.count(
                         QStringLiteral("fp_async_ready_valid_fifo #(")) == 4
                  && crossingEvidence.value(QStringLiteral("summary")).toObject()
                         .value(QStringLiteral("directionalFifos")).toInt() == 4,
              QStringLiteral(
                  "Domain RTL evidence proves two orientations for each asynchronous Router boundary"));

        bool deferredIsolation = false;
        bool deferredLevelShifter = false;
        bool everyDeferredItemIsExplicit = true;
        const QJsonArray deferredItems = crossingEvidence.value(
            QStringLiteral("deferredPlanItems")).toArray();
        for (const QJsonValue& value : deferredItems) {
            const QJsonObject item = value.toObject();
            everyDeferredItemIsExplicit = everyDeferredItemIsExplicit
                && item.value(QStringLiteral("status")).toString()
                    == QStringLiteral("deferred")
                && !item.value(QStringLiteral("reasonCode")).toString().isEmpty();
            const QJsonArray recipes = item.value(
                QStringLiteral("recipes")).toArray();
            deferredIsolation = deferredIsolation
                || recipes.contains(QStringLiteral("power-isolation"));
            deferredLevelShifter = deferredLevelShifter
                || recipes.contains(QStringLiteral("power-level-shifter"));
        }
        check(!crossingEvidence.value(QStringLiteral("claims")).toObject()
                   .value(QStringLiteral("completePlan")).toBool()
                  && everyDeferredItemIsExplicit
                  && deferredIsolation
                  && deferredLevelShifter,
              QStringLiteral(
                  "Domain RTL evidence marks unmaterialized Power stages as explicit deferred work"));
    }

    NocDesign completePowerDesign = crossingDesign;
    for (DomainMembership& membership : completePowerDesign.domainMemberships) {
        if (membership.element.kind == ElementKind::Endpoint
            && membership.element.id == QStringLiteral("z_cpu")) {
            membership.assignments.insert(
                QStringLiteral("clock"),
                QStringList{QStringLiteral("clock-main")});
            membership.assignments.insert(
                QStringLiteral("power"),
                QStringList{QStringLiteral("power-low")});
        }
    }
    completePowerDesign.packageData.insert(
        QStringLiteral("finepaper.noc.powerIntent"),
        completePowerIntent());
    const ValidationResult completePowerValidation = routerConfigured.success
        ? finepaper.validate(completePowerDesign, true)
        : ValidationResult{};
    check(completePowerValidation.success,
          QStringLiteral(
              "Core validates a complete Package-owned Power intent extension"));
    QTemporaryDir completePowerOutput(
        QStringLiteral("/tmp/finepaper-complete-power-test-XXXXXX"));
    check(completePowerOutput.isValid(),
          QStringLiteral("complete Power output directory is available"));
    if (completePowerValidation.success && completePowerOutput.isValid()) {
        checkCompletePowerGeneration(finepaper.generate(
            completePowerDesign,
            GenerationOptions{completePowerOutput.path()}));
    }

    NocDesign reorderedDomainDesign = crossingDesign;
    std::reverse(reorderedDomainDesign.domains.begin(),
                 reorderedDomainDesign.domains.end());
    std::reverse(reorderedDomainDesign.domainMemberships.begin(),
                 reorderedDomainDesign.domainMemberships.end());
    std::reverse(reorderedDomainDesign.domainRelations.begin(),
                 reorderedDomainDesign.domainRelations.end());
    std::reverse(reorderedDomainDesign.crossingPolicies.begin(),
                 reorderedDomainDesign.crossingPolicies.end());
    std::reverse(reorderedDomainDesign.edgeOverrides.begin(),
                 reorderedDomainDesign.edgeOverrides.end());
    QTemporaryDir reorderedDomainOutput(
        QStringLiteral("/tmp/finepaper-domain-order-test-XXXXXX"));
    check(reorderedDomainOutput.isValid(),
          QStringLiteral("Domain determinism output directory is available"));
    if (crossingValidation.success && reorderedDomainOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            reorderedDomainDesign,
            GenerationOptions{reorderedDomainOutput.path()});
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints");
            });
        const auto implementation = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("implementation-plan");
            });
        QString reorderedText;
        QString reorderedImplementationText;
        const QString reorderedTopText = readArtifact(
            generation, primaryArtifact(generation));
        const QString reorderedEvidenceText = readArtifact(
            generation,
            artifactWithType(
                generation, QStringLiteral("implementation-evidence")));
        if (constraints != generation.artifacts.cend()) {
            QFile constraintsFile(QDir(generation.outputDirectory).filePath(
                constraints->path));
            if (constraintsFile.open(QIODevice::ReadOnly)) {
                reorderedText = QString::fromUtf8(constraintsFile.readAll());
            }
        }
        if (implementation != generation.artifacts.cend()) {
            QFile implementationFile(QDir(generation.outputDirectory).filePath(
                implementation->path));
            if (implementationFile.open(QIODevice::ReadOnly)) {
                reorderedImplementationText = QString::fromUtf8(
                    implementationFile.readAll());
            }
        }
        check(generation.success
                  && !crossingConstraintsText.isEmpty()
                  && !crossingImplementationText.isEmpty()
                  && !crossingTopText.isEmpty()
                  && !crossingEvidenceText.isEmpty()
                  && reorderedText == crossingConstraintsText
                  && reorderedImplementationText == crossingImplementationText
                  && reorderedTopText == crossingTopText
                  && reorderedEvidenceText == crossingEvidenceText,
              QStringLiteral(
                  "Domain plan, RTL, and implementation evidence are deterministic across equivalent input ordering"));
    }

    NocDesign changedDomainDesign = crossingDesign;
    for (DomainDefinition& domain : changedDomainDesign.domains) {
        if (domain.id == QStringLiteral("clock-io")) {
            domain.properties.insert(QStringLiteral("frequencyMHz"), 625);
        } else if (domain.id == QStringLiteral("clock-main")) {
            domain.properties.insert(QStringLiteral("frequencyMHz"), 1250);
        }
    }
    QTemporaryDir changedDomainOutput(
        QStringLiteral("/tmp/finepaper-domain-change-test-XXXXXX"));
    check(changedDomainOutput.isValid(),
          QStringLiteral("changed Domain output directory is available"));
    if (crossingValidation.success && changedDomainOutput.isValid()) {
        const GenerationResult generation = finepaper.generate(
            changedDomainDesign,
            GenerationOptions{changedDomainOutput.path()});
        const auto constraints = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("constraints");
            });
        const auto implementation = std::find_if(
            generation.artifacts.cbegin(), generation.artifacts.cend(),
            [](const Artifact& artifact) {
                return artifact.type == QStringLiteral("implementation-plan");
            });
        QString changedText;
        QString changedImplementationText;
        QJsonObject changedConstraints;
        QJsonObject changedImplementation;
        if (constraints != generation.artifacts.cend()) {
            const QString path = QDir(generation.outputDirectory).filePath(
                constraints->path);
            const JsonObjectLoadResult loaded = loadJsonObject(path);
            changedConstraints = loaded.object;
            QFile constraintsFile(path);
            if (constraintsFile.open(QIODevice::ReadOnly)) {
                changedText = QString::fromUtf8(constraintsFile.readAll());
            }
        }
        if (implementation != generation.artifacts.cend()) {
            const QString path = QDir(generation.outputDirectory).filePath(
                implementation->path);
            const JsonObjectLoadResult loaded = loadJsonObject(path);
            changedImplementation = loaded.object;
            QFile implementationFile(path);
            if (implementationFile.open(QIODevice::ReadOnly)) {
                changedImplementationText = QString::fromUtf8(
                    implementationFile.readAll());
            }
        }
        const QJsonObject changedClock = objectWithStringField(
            changedConstraints.value(QStringLiteral("instances")).toArray(),
            QStringLiteral("id"),
            QStringLiteral("clock-io"));
        check(generation.success
                  && !crossingConstraintsText.isEmpty()
                  && !crossingImplementationText.isEmpty()
                  && crossingConstraintsText != changedText
                  && crossingImplementationText != changedImplementationText
                  && changedClock.value(QStringLiteral("properties"))
                         .toObject().value(QStringLiteral("frequencyMHz")).toInt()
                      == 625
                  && changedImplementation.value(
                         QStringLiteral("relationBindings")).toArray().at(0)
                         .toObject().value(QStringLiteral("resolved")).toObject()
                         .value(QStringLiteral("calculatedTarget")).toObject()
                         .value(QStringLiteral("value")).toDouble() == 625.0,
              QStringLiteral(
                  "changing related Domain properties deterministically changes constraints and typed lowering"));
    }

    NocDesign missingStrategyDesign = crossingDesign;
    if (!missingStrategyDesign.crossingPolicies.isEmpty()) {
        missingStrategyDesign.crossingPolicies.removeFirst();
    }
    const ValidationResult missingStrategyValidation = routerConfigured.success
        ? finepaper.validate(missingStrategyDesign, true)
        : ValidationResult{};
    check(!missingStrategyValidation.success
              && hasDiagnosticCode(
                  missingStrategyValidation.diagnostics,
                  QStringLiteral("finepaper_noc.adapter_failed")),
          QStringLiteral(
              "bundled V3 runtime rejects a Mesh Domain crossing without a directed strategy"));

    NocDesign unsafeSynchronizerDesign = crossingDesign;
    for (DomainCrossingPolicy& policy : unsafeSynchronizerDesign.crossingPolicies) {
        if (policy.domainType == QStringLiteral("clock")) {
            policy.properties.insert(
                QStringLiteral("implementation"),
                QStringLiteral("synchronizer"));
        }
    }
    const ValidationResult unsafeSynchronizerValidation = routerConfigured.success
        ? finepaper.validate(unsafeSynchronizerDesign, true)
        : ValidationResult{};
    check(!unsafeSynchronizerValidation.success
              && hasDiagnosticCode(
                  unsafeSynchronizerValidation.diagnostics,
                  QStringLiteral("domain_property.invalid_enum")),
          QStringLiteral(
              "Package schema does not offer an unsafe multi-bit synchronizer strategy"));

    QTemporaryDir unsafeRuntimeRun(
        QStringLiteral("/tmp/finepaper-domain-unsafe-runtime-XXXXXX"));
    if (routerConfigured.success && unsafeRuntimeRun.isValid()) {
        const QString designPath = QDir(unsafeRuntimeRun.path()).filePath(
            QStringLiteral("design.json"));
        const QString resultPath = QDir(unsafeRuntimeRun.path()).filePath(
            QStringLiteral("result.json"));
        const bool saved = saveDesign(designPath, unsafeSynchronizerDesign);
        const ProcessResult process = saved
            ? runProcess(
                  QDir(projectRoot).filePath(
                      QStringLiteral(
                          "packages/finepaper-noc-v3/runtime/bin/generate")),
                  QStringList{
                      QStringLiteral("validate"),
                      QStringLiteral("--design"),
                      designPath,
                      QStringLiteral("--result"),
                      resultPath
                  },
                  unsafeRuntimeRun.path(),
                  30'000)
            : ProcessResult{};
        const JsonObjectLoadResult result = loadJsonObject(resultPath);
        const QJsonArray diagnostics = result.object.value(
            QStringLiteral("diagnostics")).toArray();
        const QJsonObject diagnostic = diagnostics.isEmpty()
            ? QJsonObject{}
            : diagnostics.at(0).toObject();
        check(saved
                  && process.started
                  && process.exitCode != 0
                  && result.success
                  && diagnostic.value(QStringLiteral("code")).toString()
                      == QStringLiteral("realization.unsupported_recipe")
                  && diagnostic.value(QStringLiteral("path")).toString()
                         .startsWith(QStringLiteral("/constraints/meshCrossings/")),
              QStringLiteral(
                  "V3 runtime preserves structured Domain realization code and path across its process boundary"));
    }

    QTemporaryDir strictDomainRun(
        QStringLiteral("/tmp/finepaper-domain-strict-fields-XXXXXX"));
    check(strictDomainRun.isValid(),
          QStringLiteral("strict Domain validation directory is available"));
    if (routerConfigured.success && strictDomainRun.isValid()) {
        const QStringList domainArrayFields{
            QStringLiteral("domains"),
            QStringLiteral("domainMemberships"),
            QStringLiteral("domainRelations"),
            QStringLiteral("crossingPolicies"),
            QStringLiteral("edgeOverrides")
        };
        bool allRejected = true;
        for (const QString& field : domainArrayFields) {
            QJsonObject invalidDesign = designToJson(crossingDesign);
            invalidDesign.insert(field, QJsonObject{});
            const QString designPath = QDir(strictDomainRun.path()).filePath(
                field + QStringLiteral("-design.json"));
            const QString resultPath = QDir(strictDomainRun.path()).filePath(
                field + QStringLiteral("-result.json"));
            const bool saved = saveJsonObject(designPath, invalidDesign);
            const ProcessResult process = saved
                ? runProcess(
                      QDir(projectRoot).filePath(
                          QStringLiteral(
                              "packages/finepaper-noc-v3/runtime/bin/generate")),
                      QStringList{
                          QStringLiteral("validate"),
                          QStringLiteral("--design"),
                          designPath,
                          QStringLiteral("--result"),
                          resultPath
                      },
                      strictDomainRun.path(),
                      30'000)
                : ProcessResult{};
            const JsonObjectLoadResult result = loadJsonObject(resultPath);
            allRejected = allRejected
                && saved
                && process.started
                && process.exitCode != 0
                && result.success
                && !result.object.value(QStringLiteral("success")).toBool();
        }
        check(allRejected,
              QStringLiteral(
                  "bundled V3 runtime strictly parses every Domain data plane as an array"));
    }

    QTemporaryDir invalidStrategyRun(
        QStringLiteral("/tmp/finepaper-domain-invalid-strategy-XXXXXX"));
    check(invalidStrategyRun.isValid(),
          QStringLiteral("invalid Domain strategy directory is available"));
    if (routerConfigured.success && invalidStrategyRun.isValid()) {
        QJsonObject invalidStrategy = designToJson(crossingDesign);
        QJsonArray policies = invalidStrategy.value(
            QStringLiteral("crossingPolicies")).toArray();
        for (qsizetype index = 0; index < policies.size(); ++index) {
            QJsonObject policy = policies.at(index).toObject();
            if (policy.value(QStringLiteral("id")).toString()
                != QStringLiteral("power-main-to-low")) {
                continue;
            }
            policy.insert(QStringLiteral("from"), QStringLiteral("power-low"));
            policy.insert(QStringLiteral("to"), QStringLiteral("power-main"));
            policies[index] = policy;
        }
        invalidStrategy.insert(QStringLiteral("crossingPolicies"), policies);
        const QString designPath = QDir(invalidStrategyRun.path()).filePath(
            QStringLiteral("design.json"));
        const QString resultPath = QDir(invalidStrategyRun.path()).filePath(
            QStringLiteral("result.json"));
        const bool saved = saveJsonObject(designPath, invalidStrategy);
        const ProcessResult process = saved
            ? runProcess(
                  QDir(projectRoot).filePath(
                      QStringLiteral(
                          "packages/finepaper-noc-v3/runtime/bin/generate")),
                  QStringList{
                      QStringLiteral("validate"),
                      QStringLiteral("--design"),
                      designPath,
                      QStringLiteral("--result"),
                      resultPath
                  },
                  invalidStrategyRun.path(),
                  30'000)
            : ProcessResult{};
        const JsonObjectLoadResult result = loadJsonObject(resultPath);
        const QJsonArray diagnostics = result.object.value(
            QStringLiteral("diagnostics")).toArray();
        const QString message = diagnostics.isEmpty()
            ? QString()
            : diagnostics.at(0).toObject().value(
                  QStringLiteral("message")).toString();
        check(saved
                  && process.started
                  && process.exitCode != 0
                  && result.success
                  && !result.object.value(QStringLiteral("success")).toBool()
                  && message.contains(QStringLiteral("does not match")),
              QStringLiteral(
                  "bundled V3 runtime rejects an override policy with the reverse canonical edge orientation"));
    }

    FinepaperApplication complexApplication;
    const PackageCatalogReloadResult complexPackageReload = complexApplication.reloadPackages(
        QStringList{QDir(projectRoot).filePath(QStringLiteral("tests/fixtures"))});
    check(complexPackageReload.committed()
              && !hasErrors(complexPackageReload.diagnostics),
          QStringLiteral("complex Engine fixture Package loads"));
    const DesignResult complexDesign = complexApplication.createDesign(complexRequest());
    check(complexDesign.success, QStringLiteral("complex Package can retain opaque packageData"));
    const ValidationResult complexValidation = complexApplication.validate(complexDesign.design, true);
    check(complexValidation.success, QStringLiteral("complex Package validation succeeds through its Engine"));
    check(hasDiagnosticCode(complexValidation.diagnostics, QStringLiteral("mock.engine_used")),
          QStringLiteral("Engine, not the Generator, performs complex Package validation"));
    NocDesign rejectedComplexDesign = complexDesign.design;
    rejectedComplexDesign.packageData.insert(QStringLiteral("forceEngineError"), true);
    const ValidationResult rejectedComplexValidation = complexApplication.validate(
        rejectedComplexDesign, true);
    check(!rejectedComplexValidation.success
              && hasDiagnosticCode(rejectedComplexValidation.diagnostics,
                                   QStringLiteral("mock.engine_rejected")),
          QStringLiteral("structured Engine diagnostics survive a non-zero process exit"));
    QTemporaryDir complexOutput(QStringLiteral("/tmp/finepaper-engine-test-XXXXXX"));
    if (complexOutput.isValid()) {
        const GenerationResult complexGeneration = complexApplication.generate(
            complexDesign.design, GenerationOptions{complexOutput.path()});
        check(complexGeneration.success, QStringLiteral("complex Package still uses its Generator for artifacts"));
        check(complexGeneration.artifacts.size() == 1 && complexGeneration.artifacts.at(0).primary,
              QStringLiteral("complex Generator returns a contained primary artifact"));
    }

    FinepaperApplication multiPackageApplication;
    const PackageCatalogReloadResult multiPackageReload = multiPackageApplication.reloadPackages(
        QStringList{
            QDir(projectRoot).filePath(QStringLiteral("packages")),
            QDir(projectRoot).filePath(QStringLiteral("tests/fixtures"))
        });
    check(multiPackageReload.committed()
              && !hasErrors(multiPackageReload.diagnostics),
          QStringLiteral("multiple Package roots load without a shared build step"));
    check(multiPackageApplication.packages().size() == 4,
          QStringLiteral("catalog exposes V1/V3 automatic, explicit-slot and Engine-backed Packages together"));
    const auto explicitPackage = std::find_if(
        multiPackageApplication.packages().cbegin(),
        multiPackageApplication.packages().cend(),
        [](const PackageDefinition& package) {
            return package.id == QStringLiteral("test.explicit-slots");
        });
    check(explicitPackage != multiPackageApplication.packages().cend()
              && explicitPackage->attachment.slotMode == AttachmentSlotMode::Explicit
              && explicitPackage->attachment.positions.size() == 2
              && explicitPackage->attachment.positions.at(1).id == QStringLiteral("local1"),
          QStringLiteral("explicit Package exposes centrally declared attachment positions"));
    const DesignResult explicitDesign = multiPackageApplication.createDesign(
        explicitSlotRequest());
    check(explicitDesign.success
              && explicitDesign.design.endpoints.at(0).attachment.slot
                     == QStringLiteral("local1"),
          QStringLiteral("explicit attachment position persists in NocDesign"));
    QJsonObject nestedExplicitRequest = explicitSlotRequest();
    QJsonArray nestedExplicitEndpoints = nestedExplicitRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject nestedExplicitEndpoint = nestedExplicitEndpoints.at(0).toObject();
    nestedExplicitEndpoint.remove(QStringLiteral("slot"));
    nestedExplicitEndpoint.insert(
        QStringLiteral("attachment"),
        QJsonObject{{QStringLiteral("slot"), QStringLiteral("local1")}});
    nestedExplicitEndpoints[0] = nestedExplicitEndpoint;
    nestedExplicitRequest.insert(QStringLiteral("endpoints"), nestedExplicitEndpoints);
    const DesignResult nestedExplicitDesign = multiPackageApplication.createDesign(
        nestedExplicitRequest);
    check(nestedExplicitDesign.success &&
              nestedExplicitDesign.design.endpoints.at(0).attachment.router ==
                  RouterPosition{0, 0},
          QStringLiteral("a nested slot keeps the endpoint's top-level Router attachment"));

    QJsonObject duplicateExplicitRequest = explicitSlotRequest();
    QJsonArray duplicateExplicitEndpoints = duplicateExplicitRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject duplicateExplicitEndpoint = duplicateExplicitEndpoints.at(0).toObject();
    duplicateExplicitEndpoint.insert(QStringLiteral("id"), QStringLiteral("device_1"));
    duplicateExplicitEndpoints.append(duplicateExplicitEndpoint);
    duplicateExplicitRequest.insert(QStringLiteral("endpoints"), duplicateExplicitEndpoints);
    const DesignResult duplicateExplicitDesign = multiPackageApplication.createDesign(
        duplicateExplicitRequest);
    check(!duplicateExplicitDesign.success &&
              hasDiagnosticCode(duplicateExplicitDesign.diagnostics,
                                QStringLiteral("endpoint.duplicate_slot")),
          QStringLiteral("duplicate attachment slots are rejected in explicit mode"));
    QJsonObject invalidExplicitRequest = explicitSlotRequest();
    QJsonArray invalidEndpoints = invalidExplicitRequest.value(
        QStringLiteral("endpoints")).toArray();
    QJsonObject invalidEndpoint = invalidEndpoints.at(0).toObject();
    invalidEndpoint.insert(QStringLiteral("slot"), QStringLiteral("undeclared"));
    invalidEndpoints[0] = invalidEndpoint;
    invalidExplicitRequest.insert(QStringLiteral("endpoints"), invalidEndpoints);
    const DesignResult invalidExplicitDesign = multiPackageApplication.createDesign(
        invalidExplicitRequest);
    check(!invalidExplicitDesign.success
              && hasDiagnosticCode(invalidExplicitDesign.diagnostics,
                                   QStringLiteral("endpoint.unknown_slot")),
          QStringLiteral("shared application validation rejects undeclared explicit slots"));

#ifdef Q_OS_UNIX
    QTemporaryDir processOutput(QStringLiteral("/tmp/finepaper-process-test-XXXXXX"));
    if (processOutput.isValid()) {
        const QString marker = QDir(processOutput.path()).filePath(QStringLiteral("child-survived"));
        const ProcessResult timedOut = runProcess(
            QStringLiteral("/bin/sh"),
            QStringList{QStringLiteral("-c"),
                        QStringLiteral("(sleep 1; touch %1) & wait").arg(marker)},
            processOutput.path(),
            30);
        check(timedOut.timedOut, QStringLiteral("Process runner reports a timeout"));
        QThread::msleep(1200);
        check(!QFileInfo::exists(marker),
              QStringLiteral("timeout terminates Package child processes with their parent"));

        const QString stubbornMarker = QDir(processOutput.path()).filePath(
            QStringLiteral("stubborn-child-survived"));
        const ProcessResult stubbornTimeout = runProcess(
            QStringLiteral("/bin/sh"),
            QStringList{
                QStringLiteral("-c"),
                QStringLiteral("(trap '' TERM; sleep 1; touch %1) & wait").arg(stubbornMarker)
            },
            processOutput.path(),
            30);
        check(stubbornTimeout.timedOut,
              QStringLiteral("Process runner reports a timeout for a stubborn descendant"));
        QThread::msleep(1200);
        check(!QFileInfo::exists(stubbornMarker),
              QStringLiteral("timeout force-kills descendants that ignore termination"));
    }
#endif

    if (failures == 0) {
        QTextStream(stdout) << "finepaper-tests passed" << Qt::endl;
        return 0;
    }
    return 1;
}
