#pragma once

#include <QHash>
#include <QPointF>
#include <QSettings>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace finepaper {

struct TopologyWorkspaceIdentity {
    QString packageId;
    QString packageVersion;
    QString designId;

    [[nodiscard]] bool isValid() const;
    bool operator==(const TopologyWorkspaceIdentity&) const = default;
};

// Visual placement is workspace state, not NoC design data. A missing
// collapsedRouterIds value is intentionally distinct from an empty set:
// missing means the editor has never chosen an initial collapse state, while
// empty means the user explicitly expanded every Router.
struct TopologyWorkspaceState {
    QHash<QString, QPointF> routerPositionOverrides;
    QHash<QString, QPointF> endpointPositionOverrides;
    std::optional<QSet<QString>> collapsedRouterIds = std::nullopt;

    [[nodiscard]] bool retainKnownElements(
        const QSet<QString>& routerIds,
        const QSet<QString>& endpointIds);

    bool operator==(const TopologyWorkspaceState&) const = default;
};

enum class TopologyWorkspaceLoadSource : quint8 {
    None,
    CurrentV1,
    LengthPrefixedLegacy,
    DelimitedLegacy,
};

struct TopologyWorkspaceLoadResult {
    std::optional<TopologyWorkspaceState> state = std::nullopt;
    TopologyWorkspaceLoadSource source = TopologyWorkspaceLoadSource::None;
    QString error;
    // Recoverable compatibility problems are reported without preventing a
    // clean V1 record from being created in the independent current namespace.
    QString warning;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

struct TopologyWorkspaceSaveResult {
    bool success = false;
    QString error;
};

// Owns the compatibility boundary for the existing QSettings representation.
// The editor consumes typed state and does not know setting paths or QVariant
// serialization details.
class TopologyWorkspaceStore final {
public:
    TopologyWorkspaceStore();
    explicit TopologyWorkspaceStore(
        QString settingsFile,
        QSettings::Format format = QSettings::IniFormat);

    [[nodiscard]] TopologyWorkspaceLoadResult load(
        const TopologyWorkspaceIdentity& identity) const;
    [[nodiscard]] TopologyWorkspaceSaveResult save(
        const TopologyWorkspaceIdentity& identity,
        const TopologyWorkspaceState& state) const;

private:
    [[nodiscard]] std::unique_ptr<QSettings> openSettings() const;

    QString m_settingsFile;
    QSettings::Format m_format = QSettings::NativeFormat;
};

} // namespace finepaper
