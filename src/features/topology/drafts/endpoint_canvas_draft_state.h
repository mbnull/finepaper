#pragma once

#include "noc/model.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace finepaper {

struct EndpointCanvasDraftId {
    QString value;

    [[nodiscard]] bool isValid() const { return !value.isEmpty(); }

    bool operator==(const EndpointCanvasDraftId&) const = default;
};

enum class EndpointCanvasDraftLifecycle : quint8 {
    PendingNew,
    Detached,
};

struct EndpointCanvasDraftInfo {
    EndpointCanvasDraftId id;
    EndpointCanvasDraftLifecycle lifecycle =
        EndpointCanvasDraftLifecycle::PendingNew;
    QString endpointType;
    QString endpointTypeLabel;
    QString endpointId;
    std::optional<EndpointAttachment> previousAttachment = std::nullopt;
};

class EndpointCanvasDraftState {
public:
    EndpointCanvasDraftState() = default;
    explicit EndpointCanvasDraftState(
        QVector<EndpointCanvasDraftInfo> drafts);

    [[nodiscard]] bool empty() const { return m_drafts.isEmpty(); }
    [[nodiscard]] qsizetype size() const { return m_drafts.size(); }
    [[nodiscard]] qsizetype pendingNewCount() const {
        return m_pendingNewCount;
    }
    [[nodiscard]] qsizetype detachedCount() const {
        return m_detachedCount;
    }
    [[nodiscard]] const QStringList& detachedEndpointIds() const {
        return m_detachedEndpointIds;
    }
    [[nodiscard]] const QVector<EndpointCanvasDraftInfo>& items() const {
        return m_drafts;
    }
    [[nodiscard]] std::optional<EndpointCanvasDraftInfo> find(
        const EndpointCanvasDraftId& id) const;

private:
    QVector<EndpointCanvasDraftInfo> m_drafts;
    QHash<QString, qsizetype> m_draftIndexes;
    QStringList m_detachedEndpointIds;
    qsizetype m_pendingNewCount = 0;
    qsizetype m_detachedCount = 0;
};

namespace endpoint_canvas_draft_text {

[[nodiscard]] QString notice(const EndpointCanvasDraftState& state);
[[nodiscard]] QString operationBlocker(
    const EndpointCanvasDraftState& state,
    const QString& operation);
[[nodiscard]] QString discardConfirmation(
    const EndpointCanvasDraftState& state,
    bool discardsOtherUnsavedChanges);
[[nodiscard]] QString accessibleSummary(
    const EndpointCanvasDraftState& state);

} // namespace endpoint_canvas_draft_text

} // namespace finepaper
