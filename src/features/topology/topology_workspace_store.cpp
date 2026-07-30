#include "features/topology/topology_workspace_store.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QMetaType>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace finepaper {
namespace {

constexpr int currentSchemaVersion = 1;

const QString workspaceRecordPrefix =
    QStringLiteral("workbench/workspaces/v1/");
const QString workspaceRecordSuffix = QStringLiteral("/topology");
const QString workspaceSchema =
    QStringLiteral("finepaper.topology-workspace");

const QString legacyRouterLayoutsSetting =
    QStringLiteral("workbench/routerLayouts");
const QString legacyEndpointLayoutsSetting =
    QStringLiteral("workbench/endpointLayouts");
const QString legacyCollapsedRoutersSetting =
    QStringLiteral("workbench/collapsedRouters");

const QString schemaField = QStringLiteral("schema");
const QString versionField = QStringLiteral("version");
const QString identityField = QStringLiteral("identity");
const QString packageIdField = QStringLiteral("packageId");
const QString packageVersionField = QStringLiteral("packageVersion");
const QString designIdField = QStringLiteral("designId");
const QString routerPositionsField =
    QStringLiteral("routerPositionOverrides");
const QString endpointPositionsField =
    QStringLiteral("endpointPositionOverrides");
const QString collapsedRouterIdsField = QStringLiteral("collapsedRouterIds");

const QSet<QString> requiredRecordFields{
    schemaField,
    versionField,
    identityField,
    routerPositionsField,
    endpointPositionsField,
};
const QSet<QString> allowedRecordFields = requiredRecordFields
    + QSet<QString>{collapsedRouterIdsField};
const QSet<QString> identityFields{
    packageIdField,
    packageVersionField,
    designIdField,
};

struct LegacyLoadResult {
    bool found = false;
    std::optional<TopologyWorkspaceState> state = std::nullopt;
    QString error;
    QString warning;
};

void appendLengthPrefixed(QByteArray& output, const QString& value) {
    const QByteArray encoded = value.toUtf8();
    const quint64 size = static_cast<quint64>(encoded.size());
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.append(static_cast<char>((size >> shift) & 0xffU));
    }
    output.append(encoded);
}

QByteArray canonicalIdentityBytes(
    const TopologyWorkspaceIdentity& identity) {
    static constexpr char identityDomain[] =
        "finepaper.topology-workspace.identity";
    QByteArray encoded(identityDomain, sizeof(identityDomain));
    encoded.append(static_cast<char>(currentSchemaVersion));
    appendLengthPrefixed(encoded, identity.packageId);
    appendLengthPrefixed(encoded, identity.packageVersion);
    appendLengthPrefixed(encoded, identity.designId);
    return encoded;
}

QString lengthPrefixedLegacyKey(
    const TopologyWorkspaceIdentity& identity) {
    const auto segment = [](const QString& value) {
        return QString::number(value.size()) + QLatin1Char(':') + value;
    };
    return segment(identity.packageId) + segment(identity.packageVersion)
        + segment(identity.designId);
}

QString delimitedLegacyKey(const TopologyWorkspaceIdentity& identity) {
    return identity.packageId + QLatin1Char('@') + identity.packageVersion
        + QLatin1Char(':') + identity.designId;
}

bool legacyIdentityIsUnambiguous(
    const TopologyWorkspaceIdentity& identity) {
    const auto containsDelimiter = [](const QString& value) {
        return value.contains(QLatin1Char('@'))
            || value.contains(QLatin1Char(':'));
    };
    return !containsDelimiter(identity.packageId)
        && !containsDelimiter(identity.packageVersion)
        && !containsDelimiter(identity.designId);
}

QString storageKey(const TopologyWorkspaceIdentity& identity) {
    const QByteArray digest = QCryptographicHash::hash(
        canonicalIdentityBytes(identity), QCryptographicHash::Sha256).toHex();
    return workspaceRecordPrefix + QString::fromLatin1(digest)
        + workspaceRecordSuffix;
}

bool hasExactType(const QVariant& value, QMetaType::Type type) {
    return value.metaType().id() == type;
}

QString fieldSetError(const QVariantMap& object,
                      const QSet<QString>& required,
                      const QSet<QString>& allowed,
                      const QString& path) {
    const QSet<QString> actual(object.keyBegin(), object.keyEnd());
    const QSet<QString> missing = required - actual;
    if (!missing.isEmpty()) {
        QStringList names(missing.cbegin(), missing.cend());
        std::sort(names.begin(), names.end());
        return QStringLiteral("%1 is missing field(s): %2")
            .arg(path, names.join(QStringLiteral(", ")));
    }
    const QSet<QString> unknown = actual - allowed;
    if (!unknown.isEmpty()) {
        QStringList names(unknown.cbegin(), unknown.cend());
        std::sort(names.begin(), names.end());
        return QStringLiteral("%1 has unknown field(s): %2")
            .arg(path, names.join(QStringLiteral(", ")));
    }
    return {};
}

std::optional<QHash<QString, QPointF>> parsePositions(
    const QVariant& stored,
    const QString& path,
    QString* error) {
    if (!hasExactType(stored, QMetaType::QVariantMap)) {
        *error = path + QStringLiteral(" must be an object");
        return std::nullopt;
    }
    const QVariantMap object = stored.toMap();
    QHash<QString, QPointF> positions;
    positions.reserve(object.size());
    for (auto iterator = object.constBegin(); iterator != object.constEnd();
         ++iterator) {
        const QString itemPath = path + QLatin1Char('.') + iterator.key();
        if (iterator.key().trimmed().isEmpty()) {
            *error = path + QStringLiteral(" contains an empty element id");
            return std::nullopt;
        }
        if (!hasExactType(iterator.value(), QMetaType::QPointF)) {
            *error = itemPath + QStringLiteral(" must be a QPointF");
            return std::nullopt;
        }
        const QPointF position = iterator.value().toPointF();
        if (!std::isfinite(position.x()) || !std::isfinite(position.y())) {
            *error = itemPath + QStringLiteral(" must be finite");
            return std::nullopt;
        }
        positions.insert(iterator.key(), position);
    }
    return positions;
}

std::optional<QSet<QString>> parseCollapsedIds(
    const QVariant& stored,
    const QString& path,
    QString* error) {
    if (!hasExactType(stored, QMetaType::QStringList)) {
        *error = path + QStringLiteral(" must be a string list");
        return std::nullopt;
    }
    const QStringList values = stored.toStringList();
    QSet<QString> ids;
    ids.reserve(values.size());
    for (const QString& routerId : values) {
        if (routerId.trimmed().isEmpty()) {
            *error = path + QStringLiteral(" contains an empty Router id");
            return std::nullopt;
        }
        if (ids.contains(routerId)) {
            *error = path + QStringLiteral(" contains duplicate Router id ")
                + routerId;
            return std::nullopt;
        }
        ids.insert(routerId);
    }
    return ids;
}

std::optional<TopologyWorkspaceState> parseRecord(
    const QVariant& stored,
    const TopologyWorkspaceIdentity& expectedIdentity,
    QString* error) {
    if (!hasExactType(stored, QMetaType::QVariantMap)) {
        *error = QStringLiteral("topology workspace record must be an object");
        return std::nullopt;
    }
    const QVariantMap record = stored.toMap();
    if (const QString fieldsError = fieldSetError(
            record,
            requiredRecordFields,
            allowedRecordFields,
            QStringLiteral("topology workspace record"));
        !fieldsError.isEmpty()) {
        *error = fieldsError;
        return std::nullopt;
    }
    if (!hasExactType(record.value(schemaField), QMetaType::QString)
        || record.value(schemaField).toString() != workspaceSchema) {
        *error = QStringLiteral("topology workspace schema is unsupported");
        return std::nullopt;
    }
    if (!hasExactType(record.value(versionField), QMetaType::Int)
        || record.value(versionField).toInt() != currentSchemaVersion) {
        *error = QStringLiteral("topology workspace version is unsupported");
        return std::nullopt;
    }
    if (!hasExactType(record.value(identityField), QMetaType::QVariantMap)) {
        *error = QStringLiteral("topology workspace identity must be an object");
        return std::nullopt;
    }
    const QVariantMap identity = record.value(identityField).toMap();
    if (const QString fieldsError = fieldSetError(
            identity,
            identityFields,
            identityFields,
            QStringLiteral("topology workspace identity"));
        !fieldsError.isEmpty()) {
        *error = fieldsError;
        return std::nullopt;
    }
    for (const QString& field : identityFields) {
        if (!hasExactType(identity.value(field), QMetaType::QString)) {
            *error = QStringLiteral("topology workspace identity.%1 must be a string")
                .arg(field);
            return std::nullopt;
        }
    }
    if (identity.value(packageIdField).toString() != expectedIdentity.packageId
        || identity.value(packageVersionField).toString()
            != expectedIdentity.packageVersion
        || identity.value(designIdField).toString()
            != expectedIdentity.designId) {
        *error = QStringLiteral("topology workspace identity does not match its key");
        return std::nullopt;
    }

    TopologyWorkspaceState state;
    const std::optional<QHash<QString, QPointF>> routerPositions =
        parsePositions(record.value(routerPositionsField),
                       QStringLiteral("routerPositionOverrides"), error);
    if (!routerPositions) {
        return std::nullopt;
    }
    state.routerPositionOverrides = *routerPositions;
    const std::optional<QHash<QString, QPointF>> endpointPositions =
        parsePositions(record.value(endpointPositionsField),
                       QStringLiteral("endpointPositionOverrides"), error);
    if (!endpointPositions) {
        return std::nullopt;
    }
    state.endpointPositionOverrides = *endpointPositions;
    if (record.contains(collapsedRouterIdsField)) {
        const std::optional<QSet<QString>> collapsed = parseCollapsedIds(
            record.value(collapsedRouterIdsField),
            QStringLiteral("collapsedRouterIds"), error);
        if (!collapsed) {
            return std::nullopt;
        }
        state.collapsedRouterIds = *collapsed;
    }
    return state;
}

LegacyLoadResult loadLegacyState(QSettings& settings,
                                 const QString& legacyKey) {
    const QVariant storedRouterLayouts =
        settings.value(legacyRouterLayoutsSetting);
    const QVariant storedEndpointLayouts =
        settings.value(legacyEndpointLayoutsSetting);
    const QVariant storedCollapsedLayouts =
        settings.value(legacyCollapsedRoutersSetting);
    const bool anyRoot = storedRouterLayouts.isValid()
        || storedEndpointLayouts.isValid()
        || storedCollapsedLayouts.isValid();
    if (!anyRoot) {
        return {};
    }
    if ((storedRouterLayouts.isValid()
         && !hasExactType(storedRouterLayouts, QMetaType::QVariantMap))
        || (storedEndpointLayouts.isValid()
            && !hasExactType(storedEndpointLayouts, QMetaType::QVariantMap))
        || (storedCollapsedLayouts.isValid()
            && !hasExactType(storedCollapsedLayouts, QMetaType::QVariantMap))) {
        return {
            false,
            std::nullopt,
            {},
            QStringLiteral(
                "legacy topology workspace data has an invalid root type and "
                "was not imported"),
        };
    }

    const QVariantMap routerLayouts = storedRouterLayouts.toMap();
    const QVariantMap endpointLayouts = storedEndpointLayouts.toMap();
    const QVariantMap collapsedLayouts = storedCollapsedLayouts.toMap();
    const bool hasRouter = routerLayouts.contains(legacyKey);
    const bool hasEndpoint = endpointLayouts.contains(legacyKey);
    const bool hasCollapsed = collapsedLayouts.contains(legacyKey);
    if (!hasRouter && !hasEndpoint && !hasCollapsed) {
        return {};
    }

    TopologyWorkspaceState state;
    QString error;
    if (hasRouter) {
        const auto positions = parsePositions(
            routerLayouts.value(legacyKey),
            QStringLiteral("legacy.routerPositionOverrides"), &error);
        if (!positions) {
            return {true, std::nullopt, error, {}};
        }
        state.routerPositionOverrides = *positions;
    }
    if (hasEndpoint) {
        const auto positions = parsePositions(
            endpointLayouts.value(legacyKey),
            QStringLiteral("legacy.endpointPositionOverrides"), &error);
        if (!positions) {
            return {true, std::nullopt, error, {}};
        }
        state.endpointPositionOverrides = *positions;
    }
    if (hasCollapsed) {
        const auto collapsed = parseCollapsedIds(
            collapsedLayouts.value(legacyKey),
            QStringLiteral("legacy.collapsedRouterIds"), &error);
        if (!collapsed) {
            return {true, std::nullopt, error, {}};
        }
        state.collapsedRouterIds = *collapsed;
    }
    return {true, state, {}, {}};
}

QString validateState(const TopologyWorkspaceState& state) {
    const auto validatePositions = [](const QHash<QString, QPointF>& positions,
                                      const QString& path) {
        for (auto iterator = positions.constBegin(); iterator != positions.constEnd();
             ++iterator) {
            if (iterator.key().trimmed().isEmpty()) {
                return path + QStringLiteral(" contains an empty element id");
            }
            if (!std::isfinite(iterator.value().x())
                || !std::isfinite(iterator.value().y())) {
                return path + QLatin1Char('.') + iterator.key()
                    + QStringLiteral(" must be finite");
            }
        }
        return QString{};
    };
    if (const QString error = validatePositions(
            state.routerPositionOverrides,
            QStringLiteral("routerPositionOverrides"));
        !error.isEmpty()) {
        return error;
    }
    if (const QString error = validatePositions(
            state.endpointPositionOverrides,
            QStringLiteral("endpointPositionOverrides"));
        !error.isEmpty()) {
        return error;
    }
    if (state.collapsedRouterIds) {
        for (const QString& routerId : *state.collapsedRouterIds) {
            if (routerId.trimmed().isEmpty()) {
                return QStringLiteral(
                    "collapsedRouterIds contains an empty Router id");
            }
        }
    }
    return {};
}

QVariantMap serializeRecord(const TopologyWorkspaceIdentity& identity,
                            const TopologyWorkspaceState& state) {
    QVariantMap routerPositions;
    for (auto iterator = state.routerPositionOverrides.constBegin();
         iterator != state.routerPositionOverrides.constEnd(); ++iterator) {
        routerPositions.insert(iterator.key(), iterator.value());
    }
    QVariantMap endpointPositions;
    for (auto iterator = state.endpointPositionOverrides.constBegin();
         iterator != state.endpointPositionOverrides.constEnd(); ++iterator) {
        endpointPositions.insert(iterator.key(), iterator.value());
    }
    QVariantMap record = {
        {schemaField, workspaceSchema},
        {versionField, currentSchemaVersion},
        {identityField, QVariantMap{
            {packageIdField, identity.packageId},
            {packageVersionField, identity.packageVersion},
            {designIdField, identity.designId},
        }},
        {routerPositionsField, routerPositions},
        {endpointPositionsField, endpointPositions},
    };
    if (state.collapsedRouterIds) {
        QStringList collapsed(state.collapsedRouterIds->cbegin(),
                              state.collapsedRouterIds->cend());
        std::sort(collapsed.begin(), collapsed.end());
        record.insert(collapsedRouterIdsField, collapsed);
    }
    return record;
}

} // namespace

bool TopologyWorkspaceIdentity::isValid() const {
    return !packageId.trimmed().isEmpty()
        && !packageVersion.trimmed().isEmpty()
        && !designId.trimmed().isEmpty();
}

bool TopologyWorkspaceState::retainKnownElements(
    const QSet<QString>& routerIds,
    const QSet<QString>& endpointIds) {
    bool changed = false;
    for (auto iterator = routerPositionOverrides.begin();
         iterator != routerPositionOverrides.end();) {
        if (!routerIds.contains(iterator.key())) {
            iterator = routerPositionOverrides.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    for (auto iterator = endpointPositionOverrides.begin();
         iterator != endpointPositionOverrides.end();) {
        if (!endpointIds.contains(iterator.key())) {
            iterator = endpointPositionOverrides.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    if (collapsedRouterIds) {
        for (auto iterator = collapsedRouterIds->begin();
             iterator != collapsedRouterIds->end();) {
            if (!routerIds.contains(*iterator)) {
                iterator = collapsedRouterIds->erase(iterator);
                changed = true;
            } else {
                ++iterator;
            }
        }
    }
    return changed;
}

TopologyWorkspaceStore::TopologyWorkspaceStore() = default;

TopologyWorkspaceStore::TopologyWorkspaceStore(
    QString settingsFile,
    QSettings::Format format)
    : m_settingsFile(std::move(settingsFile)), m_format(format) {}

std::unique_ptr<QSettings> TopologyWorkspaceStore::openSettings() const {
    if (m_settingsFile.isEmpty()) {
        return std::make_unique<QSettings>();
    }
    return std::make_unique<QSettings>(m_settingsFile, m_format);
}

TopologyWorkspaceLoadResult TopologyWorkspaceStore::load(
    const TopologyWorkspaceIdentity& identity) const {
    if (!identity.isValid()) {
        return {std::nullopt, TopologyWorkspaceLoadSource::None,
                QStringLiteral("topology workspace identity is incomplete")};
    }
    std::unique_ptr<QSettings> settings = openSettings();
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        return {std::nullopt, TopologyWorkspaceLoadSource::None,
                QStringLiteral("could not read topology workspace settings")};
    }

    const QVariant currentRecord = settings->value(storageKey(identity));
    if (currentRecord.isValid()) {
        QString error;
        const std::optional<TopologyWorkspaceState> state = parseRecord(
            currentRecord, identity, &error);
        return {state, TopologyWorkspaceLoadSource::CurrentV1, error};
    }

    if (!legacyIdentityIsUnambiguous(identity)) {
        return {};
    }
    const LegacyLoadResult lengthLegacy = loadLegacyState(
        *settings, lengthPrefixedLegacyKey(identity));
    if (!lengthLegacy.warning.isEmpty()) {
        return {std::nullopt,
                TopologyWorkspaceLoadSource::None,
                {},
                lengthLegacy.warning};
    }
    if (!lengthLegacy.error.isEmpty()) {
        return {std::nullopt,
                TopologyWorkspaceLoadSource::LengthPrefixedLegacy,
                lengthLegacy.error};
    }
    if (lengthLegacy.found) {
        return {lengthLegacy.state,
                TopologyWorkspaceLoadSource::LengthPrefixedLegacy,
                {}};
    }

    const LegacyLoadResult delimitedLegacy = loadLegacyState(
        *settings, delimitedLegacyKey(identity));
    if (!delimitedLegacy.warning.isEmpty()) {
        return {std::nullopt,
                TopologyWorkspaceLoadSource::None,
                {},
                delimitedLegacy.warning};
    }
    if (!delimitedLegacy.error.isEmpty()) {
        return {std::nullopt,
                TopologyWorkspaceLoadSource::DelimitedLegacy,
                delimitedLegacy.error};
    }
    if (delimitedLegacy.found) {
        return {delimitedLegacy.state,
                TopologyWorkspaceLoadSource::DelimitedLegacy,
                {}};
    }
    return {};
}

TopologyWorkspaceSaveResult TopologyWorkspaceStore::save(
    const TopologyWorkspaceIdentity& identity,
    const TopologyWorkspaceState& state) const {
    if (!identity.isValid()) {
        return {false, QStringLiteral("topology workspace identity is incomplete")};
    }
    if (const QString error = validateState(state); !error.isEmpty()) {
        return {false, error};
    }
    std::unique_ptr<QSettings> settings = openSettings();
    settings->setValue(storageKey(identity), serializeRecord(identity, state));
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        return {false, QStringLiteral("could not write topology workspace settings")};
    }
    return {true, {}};
}

} // namespace finepaper
