#include "features/topology/drafts/endpoint_canvas_draft_state.h"

#include <algorithm>
#include <utility>

namespace finepaper {
namespace {

QString countText(qsizetype count,
                  const QString& singular,
                  const QString& plural) {
    return QString::number(count) + QLatin1Char(' ')
        + (count == 1 ? singular : plural);
}

QString compactDetachedIds(const EndpointCanvasDraftState& state) {
    constexpr qsizetype maximumVisibleIds = 3;
    const QStringList& ids = state.detachedEndpointIds();
    QStringList visibleIds = ids.mid(0, maximumVisibleIds);
    QString text = visibleIds.join(QStringLiteral(", "));
    if (ids.size() > maximumVisibleIds) {
        text += QStringLiteral(" and ")
            + QString::number(ids.size() - maximumVisibleIds)
            + QStringLiteral(" more");
    }
    return text;
}

QString inventoryText(const EndpointCanvasDraftState& state) {
    QStringList parts;
    if (state.pendingNewCount() > 0) {
        parts.append(countText(
            state.pendingNewCount(),
            QStringLiteral("new Endpoint draft"),
            QStringLiteral("new Endpoint drafts")));
    }
    if (state.detachedCount() > 0) {
        parts.append(countText(
            state.detachedCount(),
            QStringLiteral("disconnected Endpoint"),
            QStringLiteral("disconnected Endpoints")));
    }
    return parts.join(QStringLiteral(" and "));
}

} // namespace

EndpointCanvasDraftState::EndpointCanvasDraftState(
    QVector<EndpointCanvasDraftInfo> drafts)
    : m_drafts(std::move(drafts)) {
    std::sort(
        m_drafts.begin(), m_drafts.end(),
        [](const EndpointCanvasDraftInfo& left,
           const EndpointCanvasDraftInfo& right) {
            return left.id.value < right.id.value;
        });
    m_draftIndexes.reserve(m_drafts.size());
    m_detachedEndpointIds.reserve(m_drafts.size());
    for (qsizetype index = 0; index < m_drafts.size(); ++index) {
        const EndpointCanvasDraftInfo& draft = m_drafts.at(index);
        m_draftIndexes.insert(draft.id.value, index);
        if (draft.lifecycle == EndpointCanvasDraftLifecycle::Detached) {
            ++m_detachedCount;
            if (!draft.endpointId.isEmpty()) {
                m_detachedEndpointIds.append(draft.endpointId);
            }
        } else if (draft.lifecycle
                   == EndpointCanvasDraftLifecycle::PendingNew) {
            ++m_pendingNewCount;
        }
    }
    std::sort(m_detachedEndpointIds.begin(), m_detachedEndpointIds.end());
    m_detachedEndpointIds.removeDuplicates();
}

std::optional<EndpointCanvasDraftInfo> EndpointCanvasDraftState::find(
    const EndpointCanvasDraftId& id) const {
    const auto index = m_draftIndexes.constFind(id.value);
    return index == m_draftIndexes.constEnd()
        ? std::nullopt
        : std::optional<EndpointCanvasDraftInfo>{m_drafts.at(*index)};
}

namespace endpoint_canvas_draft_text {

QString notice(const EndpointCanvasDraftState& state) {
    if (state.empty()) {
        return {};
    }
    QString text;
    if (state.detachedCount() == 0) {
        text = inventoryText(state)
            + (state.pendingNewCount() == 1
                   ? QStringLiteral(" is not in the design. ")
                   : QStringLiteral(" are not in the design. "))
            + (state.pendingNewCount() == 1
                   ? QStringLiteral(
                         "Connect it to a Router to add it, or discard it. ")
                   : QStringLiteral(
                         "Connect each to a Router to add it, or discard it. "));
    } else if (state.pendingNewCount() == 0) {
        text = inventoryText(state)
            + (state.detachedCount() == 1
                   ? QStringLiteral(" is preserved for this session: ")
                   : QStringLiteral(" are preserved for this session: "))
            + compactDetachedIds(state)
            + QStringLiteral(". Reconnect to keep, or delete permanently. ");
    } else {
        text = inventoryText(state)
            + QStringLiteral(
                " need attention. Connect new drafts; reconnect or delete "
                "disconnected Endpoints. ");
    }
    text += QStringLiteral("Save, Validate, Generate, and Resize are paused.");
    return text;
}

QString operationBlocker(const EndpointCanvasDraftState& state,
                         const QString& operation) {
    QString text = operation + QStringLiteral(" cannot continue while ")
        + inventoryText(state)
        + (state.size() == 1
               ? QStringLiteral(" remains on the canvas.")
               : QStringLiteral(" remain on the canvas."));
    if (state.detachedCount() > 0) {
        text += QStringLiteral(" Disconnected Endpoint IDs: ")
            + compactDetachedIds(state) + QLatin1Char('.');
    }
    text += QStringLiteral(
        "\n\nConnect new drafts to a Router to create them. Reconnect "
        "disconnected Endpoints to restore them, or use their canvas menu "
        "to discard or delete them. The requested operation did not start.");
    return text;
}

QString discardConfirmation(const EndpointCanvasDraftState& state,
                            bool discardsOtherUnsavedChanges) {
    QString text = QStringLiteral("The canvas contains ")
        + inventoryText(state)
        + QStringLiteral(
            ". These drafts exist only in the current editing session and "
            "cannot be saved in their unresolved state.\n\n");
    text += discardsOtherUnsavedChanges
        ? QStringLiteral(
              "Continuing will discard these Endpoint drafts and every other "
              "unsaved change in the current design.")
        : QStringLiteral(
              "Continuing will discard these Endpoint drafts. The durable "
              "design is unchanged.");
    return text;
}

QString accessibleSummary(const EndpointCanvasDraftState& state) {
    if (state.empty()) {
        return {};
    }
    return QStringLiteral(" The canvas contains ") + inventoryText(state)
        + QStringLiteral(" that must be resolved before persistent operations.");
}

} // namespace endpoint_canvas_draft_text
} // namespace finepaper
