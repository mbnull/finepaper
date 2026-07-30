#include "features/topology/topology_workspace_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVariantMap>

#include <limits>

namespace {

using namespace finepaper;

int failures = 0;

void check(bool condition, const QString& message) {
    if (condition) {
        return;
    }
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
}

TopologyWorkspaceIdentity identity(
    QString designId = QStringLiteral("design-a")) {
    return {
        QStringLiteral("test.finepaper.topology"),
        QStringLiteral("1.0.0"),
        std::move(designId),
    };
}

TopologyWorkspaceState sampleState() {
    TopologyWorkspaceState state;
    state.routerPositionOverrides.insert(
        QStringLiteral("r-0-0"), QPointF(-13.25, 27.5));
    state.endpointPositionOverrides.insert(
        QStringLiteral("端点/α"), QPointF(901.125, -44.75));
    state.collapsedRouterIds = QSet<QString>{
        QStringLiteral("r-0-0"),
        QStringLiteral("r-1-0"),
    };
    return state;
}

QString lengthLegacyKey(const TopologyWorkspaceIdentity& value) {
    const auto segment = [](const QString& text) {
        return QString::number(text.size()) + QLatin1Char(':') + text;
    };
    return segment(value.packageId) + segment(value.packageVersion)
        + segment(value.designId);
}

QString delimitedLegacyKey(const TopologyWorkspaceIdentity& value) {
    return value.packageId + QLatin1Char('@') + value.packageVersion
        + QLatin1Char(':') + value.designId;
}

QVariantMap positionMap(const QHash<QString, QPointF>& positions) {
    QVariantMap result;
    for (auto iterator = positions.constBegin(); iterator != positions.constEnd();
         ++iterator) {
        result.insert(iterator.key(), iterator.value());
    }
    return result;
}

void writeLegacy(const QString& settingsFile,
                 const QString& workspaceKey,
                 const TopologyWorkspaceState& state) {
    QSettings settings(settingsFile, QSettings::IniFormat);
    QVariantMap routers = settings.value(
        QStringLiteral("workbench/routerLayouts")).toMap();
    routers.insert(workspaceKey, positionMap(state.routerPositionOverrides));
    settings.setValue(QStringLiteral("workbench/routerLayouts"), routers);

    QVariantMap endpoints = settings.value(
        QStringLiteral("workbench/endpointLayouts")).toMap();
    endpoints.insert(
        workspaceKey, positionMap(state.endpointPositionOverrides));
    settings.setValue(QStringLiteral("workbench/endpointLayouts"), endpoints);

    if (state.collapsedRouterIds) {
        QVariantMap collapsed = settings.value(
            QStringLiteral("workbench/collapsedRouters")).toMap();
        QStringList ids(state.collapsedRouterIds->cbegin(),
                        state.collapsedRouterIds->cend());
        ids.sort();
        collapsed.insert(workspaceKey, ids);
        settings.setValue(
            QStringLiteral("workbench/collapsedRouters"), collapsed);
    }
    settings.sync();
}

QString currentRecordKey(const QString& settingsFile) {
    QSettings settings(settingsFile, QSettings::IniFormat);
    for (const QString& key : settings.allKeys()) {
        if (key.startsWith(QStringLiteral("workbench/workspaces/v1/"))
            && key.endsWith(QStringLiteral("/topology"))) {
            return key;
        }
    }
    return {};
}

void roundTripAndCollapseThreeState(const QString& settingsFile) {
    TopologyWorkspaceStore store(settingsFile);
    const TopologyWorkspaceIdentity workspace = identity();

    const TopologyWorkspaceLoadResult missing = store.load(workspace);
    check(missing.ok() && !missing.state
              && missing.source == TopologyWorkspaceLoadSource::None,
          QStringLiteral("a missing workspace remains distinguishable from empty state"));

    TopologyWorkspaceState neverInitialized;
    const TopologyWorkspaceSaveResult savedMissingCollapse =
        store.save(workspace, neverInitialized);
    const TopologyWorkspaceLoadResult loadedMissingCollapse =
        store.load(workspace);
    check(savedMissingCollapse.success && loadedMissingCollapse.ok()
              && loadedMissingCollapse.state
              && !loadedMissingCollapse.state->collapsedRouterIds,
          QStringLiteral("missing collapsed state round-trips as nullopt"));

    TopologyWorkspaceState allExpanded;
    allExpanded.collapsedRouterIds.emplace();
    const TopologyWorkspaceSaveResult savedExpanded =
        store.save(workspace, allExpanded);
    const TopologyWorkspaceLoadResult loadedExpanded = store.load(workspace);
    check(savedExpanded.success && loadedExpanded.state
              && loadedExpanded.state->collapsedRouterIds
              && loadedExpanded.state->collapsedRouterIds->isEmpty(),
          QStringLiteral("an explicit empty collapsed set survives restart"));

    const TopologyWorkspaceState expected = sampleState();
    const TopologyWorkspaceSaveResult saved = store.save(workspace, expected);
    const TopologyWorkspaceLoadResult loaded = store.load(workspace);
    check(saved.success && loaded.ok() && loaded.state == expected
              && loaded.source == TopologyWorkspaceLoadSource::CurrentV1,
          QStringLiteral("typed positions and non-empty collapse state round-trip"));

    QSettings raw(settingsFile, QSettings::IniFormat);
    const QString recordKey = currentRecordKey(settingsFile);
    check(!recordKey.isEmpty()
              && raw.value(recordKey).metaType().id() == QMetaType::QVariantMap
              && !raw.contains(QStringLiteral("workbench/routerLayouts"))
              && !raw.contains(QStringLiteral("workbench/endpointLayouts")),
          QStringLiteral("new state uses one versioned per-workspace record"));
}

void workspacesAndWritersRemainIsolated(const QString& settingsFile) {
    TopologyWorkspaceStore firstWriter(settingsFile);
    TopologyWorkspaceStore secondWriter(settingsFile);
    TopologyWorkspaceState first = sampleState();
    TopologyWorkspaceState second = sampleState();
    second.routerPositionOverrides = {
        {QStringLiteral("r-9-9"), QPointF(9.0, 99.0)},
    };
    second.endpointPositionOverrides.clear();
    second.collapsedRouterIds = QSet<QString>{};

    check(firstWriter.save(identity(QStringLiteral("design-one")), first).success
              && secondWriter.save(identity(QStringLiteral("design-two")), second).success,
          QStringLiteral("independent settings instances can save different workspaces"));
    check(firstWriter.load(identity(QStringLiteral("design-one"))).state == first
              && secondWriter.load(identity(QStringLiteral("design-two"))).state
                  == second,
          QStringLiteral("one workspace write never overwrites another workspace"));

    QSettings raw(settingsFile, QSettings::IniFormat);
    qsizetype currentRecords = 0;
    for (const QString& key : raw.allKeys()) {
        if (key.startsWith(QStringLiteral("workbench/workspaces/v1/"))
            && key.endsWith(QStringLiteral("/topology"))) {
            ++currentRecords;
        }
    }
    check(currentRecords == 2,
          QStringLiteral("each workspace owns a separate QSettings leaf"));
}

void safeLegacyFormatsRemainReadable(const QString& rootPath) {
    const TopologyWorkspaceState expected = sampleState();
    const TopologyWorkspaceIdentity workspace = identity(
        QStringLiteral("legacy-safe"));

    const QString lengthFile = rootPath + QStringLiteral("/length.ini");
    writeLegacy(lengthFile, lengthLegacyKey(workspace), expected);
    TopologyWorkspaceStore lengthStore(lengthFile);
    const TopologyWorkspaceLoadResult lengthLoaded = lengthStore.load(workspace);
    check(lengthLoaded.ok() && lengthLoaded.state == expected
              && lengthLoaded.source
                  == TopologyWorkspaceLoadSource::LengthPrefixedLegacy
              && currentRecordKey(lengthFile).isEmpty(),
          QStringLiteral("length-prefixed legacy state is read without hidden writes"));
    check(lengthStore.save(workspace, *lengthLoaded.state).success
              && lengthStore.load(workspace).source
                  == TopologyWorkspaceLoadSource::CurrentV1,
          QStringLiteral("the next explicit save naturally adopts the current format"));

    const QString delimitedFile = rootPath + QStringLiteral("/delimited.ini");
    writeLegacy(delimitedFile, delimitedLegacyKey(workspace), expected);
    TopologyWorkspaceStore delimitedStore(delimitedFile);
    const TopologyWorkspaceLoadResult delimitedLoaded =
        delimitedStore.load(workspace);
    check(delimitedLoaded.ok() && delimitedLoaded.state == expected
              && delimitedLoaded.source
                  == TopologyWorkspaceLoadSource::DelimitedLegacy,
          QStringLiteral("unambiguous delimiter legacy state remains readable"));
}

void crossGenerationCollisionFailsClosed(const QString& settingsFile) {
    const TopologyWorkspaceIdentity current{
        QStringLiteral("a@b"), QStringLiteral("v"), QStringLiteral("d")};
    const TopologyWorkspaceIdentity otherLegacy{
        QStringLiteral("3:a"), QStringLiteral("b1"), QStringLiteral("v1:d")};
    check(lengthLegacyKey(current) == delimitedLegacyKey(otherLegacy),
          QStringLiteral("the collision fixture is exact"));

    const TopologyWorkspaceState legacy = sampleState();
    writeLegacy(settingsFile, delimitedLegacyKey(otherLegacy), legacy);
    TopologyWorkspaceStore store(settingsFile);
    const TopologyWorkspaceLoadResult missing = store.load(current);
    check(missing.ok() && !missing.state
              && missing.source == TopologyWorkspaceLoadSource::None,
          QStringLiteral("an ambiguous legacy key is never attributed automatically"));

    TopologyWorkspaceState currentState;
    currentState.routerPositionOverrides.insert(
        QStringLiteral("r-current"), QPointF(12.0, 34.0));
    currentState.collapsedRouterIds.emplace();
    check(store.save(current, currentState).success
              && store.load(current).state == currentState,
          QStringLiteral("the current namespace remains usable after a legacy collision"));
    QSettings raw(settingsFile, QSettings::IniFormat);
    check(raw.value(QStringLiteral("workbench/routerLayouts"))
              .toMap().contains(delimitedLegacyKey(otherLegacy)),
          QStringLiteral("writing current state never deletes ambiguous legacy data"));
}

void currentRecordValidationIsStrict(const QString& rootPath) {
    const auto verifyCorruption = [&](const QString& name,
                                      const auto& corrupt) {
        const QString settingsFile = rootPath + QLatin1Char('/') + name
            + QStringLiteral(".ini");
        TopologyWorkspaceStore store(settingsFile);
        check(store.save(identity(), sampleState()).success,
              name + QStringLiteral(" fixture saves"));
        const QString key = currentRecordKey(settingsFile);
        QSettings raw(settingsFile, QSettings::IniFormat);
        QVariantMap record = raw.value(key).toMap();
        corrupt(record);
        raw.setValue(key, record);
        raw.sync();
        const TopologyWorkspaceLoadResult loaded = store.load(identity());
        check(!loaded.ok() && !loaded.state
                  && loaded.source == TopologyWorkspaceLoadSource::CurrentV1,
              name + QStringLiteral(" is rejected as corrupt, not treated as missing"));
        check(raw.value(key).toMap() == record,
              name + QStringLiteral(" remains intact after the failed load"));
    };

    verifyCorruption(QStringLiteral("wrong-version"), [](QVariantMap& record) {
        record.insert(QStringLiteral("version"), QStringLiteral("1"));
    });
    verifyCorruption(QStringLiteral("identity-mismatch"), [](QVariantMap& record) {
        QVariantMap storedIdentity = record.value(
            QStringLiteral("identity")).toMap();
        storedIdentity.insert(QStringLiteral("designId"), QStringLiteral("other"));
        record.insert(QStringLiteral("identity"), storedIdentity);
    });
    verifyCorruption(QStringLiteral("unknown-field"), [](QVariantMap& record) {
        record.insert(QStringLiteral("surprise"), true);
    });
    verifyCorruption(QStringLiteral("nan-position"), [](QVariantMap& record) {
        QVariantMap positions = record.value(
            QStringLiteral("routerPositionOverrides")).toMap();
        positions.insert(
            QStringLiteral("r-bad"),
            QPointF(std::numeric_limits<qreal>::quiet_NaN(), 0.0));
        record.insert(QStringLiteral("routerPositionOverrides"), positions);
    });
    verifyCorruption(QStringLiteral("collapsed-type"), [](QVariantMap& record) {
        record.insert(QStringLiteral("collapsedRouterIds"), QStringLiteral("r-0-0"));
    });
    verifyCorruption(QStringLiteral("duplicate-collapse"), [](QVariantMap& record) {
        record.insert(
            QStringLiteral("collapsedRouterIds"),
            QStringList{QStringLiteral("r-0-0"), QStringLiteral("r-0-0")});
    });
}

void invalidStateAndStorageFailuresAreReported(const QString& rootPath) {
    const QString settingsFile = rootPath + QStringLiteral("/invalid.ini");
    TopologyWorkspaceStore store(settingsFile);
    TopologyWorkspaceState invalid;
    invalid.routerPositionOverrides.insert(
        QStringLiteral("r-bad"),
        QPointF(std::numeric_limits<qreal>::infinity(), 0.0));
    check(!store.save(identity(), invalid).success
              && currentRecordKey(settingsFile).isEmpty(),
          QStringLiteral("non-finite state is rejected before storage mutation"));
    invalid.routerPositionOverrides.clear();
    invalid.endpointPositionOverrides.insert(QString(), QPointF(1.0, 2.0));
    check(!store.save(identity(), invalid).success,
          QStringLiteral("empty element ids are rejected before save"));
    check(!store.save(TopologyWorkspaceIdentity{}, TopologyWorkspaceState{}).success,
          QStringLiteral("incomplete workspace identity is rejected"));

    const QString blockedParent =
        rootPath + QStringLiteral("/settings-parent-is-a-file");
    QFile blocker(blockedParent);
    if (!blocker.open(QIODevice::WriteOnly)) {
        check(false, QStringLiteral("storage failure fixture is writable"));
    } else {
        check(blocker.write("blocked") == 7,
              QStringLiteral("storage failure fixture is writable"));
    }
    blocker.close();
    TopologyWorkspaceStore unwritable(
        blockedParent + QStringLiteral("/workspace.ini"));
    check(!unwritable.save(identity(), sampleState()).success,
          QStringLiteral("QSettings write failure is observable"));
}

void corruptLegacyRootCannotBeClobbered(const QString& settingsFile) {
    QSettings raw(settingsFile, QSettings::IniFormat);
    raw.setValue(QStringLiteral("workbench/routerLayouts"), 42);
    raw.sync();

    TopologyWorkspaceStore store(settingsFile);
    const TopologyWorkspaceLoadResult skippedLegacy = store.load(identity());
    check(skippedLegacy.ok() && !skippedLegacy.state
              && !skippedLegacy.warning.isEmpty(),
          QStringLiteral(
              "an unrelated malformed legacy root is recoverable for V1 storage"));
    check(store.save(identity(), sampleState()).success,
          QStringLiteral("new storage does not depend on a corrupt legacy root"));
    check(raw.value(QStringLiteral("workbench/routerLayouts")).toInt() == 42
              && store.load(identity()).state == sampleState(),
          QStringLiteral("current save preserves corrupt legacy bytes and remains readable"));
}

void statePruningIsTypedAndDeterministic() {
    TopologyWorkspaceState state = sampleState();
    state.routerPositionOverrides.insert(
        QStringLiteral("stale-router"), QPointF(1.0, 1.0));
    state.endpointPositionOverrides.insert(
        QStringLiteral("stale-endpoint"), QPointF(2.0, 2.0));
    state.collapsedRouterIds->insert(QStringLiteral("stale-router"));
    check(state.retainKnownElements(
              QSet<QString>{QStringLiteral("r-0-0"), QStringLiteral("r-1-0")},
              QSet<QString>{QStringLiteral("端点/α")})
              && !state.routerPositionOverrides.contains(
                  QStringLiteral("stale-router"))
              && !state.endpointPositionOverrides.contains(
                  QStringLiteral("stale-endpoint"))
              && !state.collapsedRouterIds->contains(
                  QStringLiteral("stale-router")),
          QStringLiteral("pruning removes stale state across all typed facets"));
    check(!state.retainKnownElements(
              QSet<QString>{QStringLiteral("r-0-0"), QStringLiteral("r-1-0")},
              QSet<QString>{QStringLiteral("端点/α")}),
          QStringLiteral("pruning is idempotent"));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir root;
    check(root.isValid(), QStringLiteral("temporary settings directory is available"));
    if (!root.isValid()) {
        return 1;
    }

    roundTripAndCollapseThreeState(
        root.filePath(QStringLiteral("round-trip.ini")));
    workspacesAndWritersRemainIsolated(
        root.filePath(QStringLiteral("isolation.ini")));
    safeLegacyFormatsRemainReadable(root.path());
    crossGenerationCollisionFailsClosed(
        root.filePath(QStringLiteral("collision.ini")));
    currentRecordValidationIsStrict(root.path());
    invalidStateAndStorageFailuresAreReported(root.path());
    corruptLegacyRootCannotBeClobbered(
        root.filePath(QStringLiteral("legacy-corrupt.ini")));
    statePruningIsTypedAndDeterministic();

    if (failures == 0) {
        QTextStream(stdout) << "Topology workspace store tests passed" << Qt::endl;
        return 0;
    }
    QTextStream(stderr) << failures
                        << " topology workspace store test(s) failed"
                        << Qt::endl;
    return 1;
}
