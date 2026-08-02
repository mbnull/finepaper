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

QString operationName(const QString& operation) {
    const QString normalized = operation.trimmed();
    return normalized.isEmpty() ? QStringLiteral("This operation")
                                : normalized;
}

QString unresolvedInventoryText(const EndpointCanvasDraftState& state) {
    return inventoryText(state)
        + (state.size() == 1 ? QStringLiteral(" remains unresolved")
                             : QStringLiteral(" remain unresolved"));
}

QString pendingResolutionText(const EndpointCanvasDraftState& state) {
    if (state.pendingNewCount() == 0) {
        return {};
    }
    return state.pendingNewCount() == 1
        ? QStringLiteral(
              "Connect the new draft to a Router to add it to the design, "
              "or discard it.")
        : QStringLiteral(
              "Connect each new draft to a Router to add it to the design, "
              "or discard the drafts.");
}

QString detachedResolutionText(const EndpointCanvasDraftState& state) {
    if (state.detachedCount() == 0) {
        return {};
    }
    return state.detachedCount() == 1
        ? QStringLiteral(
              "Reconnect the disconnected Endpoint to restore it, or delete "
              "it permanently.")
        : QStringLiteral(
              "Reconnect the disconnected Endpoints to restore them, or "
              "delete them permanently.");
}

QString resolutionText(const EndpointCanvasDraftState& state) {
    QStringList instructions;
    const QString pending = pendingResolutionText(state);
    const QString detached = detachedResolutionText(state);
    if (!pending.isEmpty()) {
        instructions.append(pending);
    }
    if (!detached.isEmpty()) {
        instructions.append(detached);
    }
    return instructions.join(QLatin1Char(' '));
}

QString pendingDiscardImpact(const EndpointCanvasDraftState& state) {
    if (state.pendingNewCount() == 0) {
        return {};
    }
    return state.pendingNewCount() == 1
        ? QStringLiteral(
              "The new Endpoint draft has not been added to the design. "
              "Discarding it removes it from the canvas.")
        : QStringLiteral(
              "The new Endpoint drafts have not been added to the design. "
              "Discarding them removes them from the canvas.");
}

QString detachedDiscardImpact(const EndpointCanvasDraftState& state) {
    if (state.detachedCount() == 0) {
        return {};
    }
    return state.detachedCount() == 1
        ? QStringLiteral(
              "The disconnected Endpoint is recoverable in this session. "
              "Continuing permanently deletes it from the current design. "
              "Its preserved Domain assignments, attachment settings, and "
              "configuration will be lost.")
        : QStringLiteral(
              "The disconnected Endpoints are recoverable in this session. "
              "Continuing permanently deletes them from the current design. "
              "Their preserved Domain assignments, attachment settings, and "
              "configuration will be lost.");
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
            + pendingResolutionText(state) + QLatin1Char(' ');
    } else if (state.pendingNewCount() == 0) {
        text = inventoryText(state)
            + (state.detachedCount() == 1
                   ? QStringLiteral(" is preserved for this session: ")
                   : QStringLiteral(" are preserved for this session: "))
            + compactDetachedIds(state)
            + QStringLiteral(". ") + detachedResolutionText(state)
            + QLatin1Char(' ');
    } else {
        text = inventoryText(state)
            + QStringLiteral(" need attention. ") + resolutionText(state)
            + QLatin1Char(' ');
    }
    text += QStringLiteral(
        "Save, Validate, Generate, and Resize remain unavailable until this "
        "canvas work is resolved.");
    return text;
}

QString taskTitle(const EndpointCanvasDraftState& state) {
    if (state.empty()) {
        return {};
    }
    return QStringLiteral("Resolve ") + inventoryText(state);
}

QString reviewAction(const EndpointCanvasDraftState& state) {
    if (state.empty()) {
        return {};
    }
    if (state.size() > 1) {
        return QStringLiteral("Review drafts");
    }
    return state.pendingNewCount() == 1
        ? QStringLiteral("Review draft")
        : QStringLiteral("Review Endpoint");
}

QString discardAction(const EndpointCanvasDraftState& state) {
    if (state.empty()) {
        return {};
    }
    if (state.detachedCount() == 0) {
        return state.pendingNewCount() == 1
            ? QStringLiteral("Discard draft")
            : QStringLiteral("Discard drafts");
    }
    if (state.pendingNewCount() == 0) {
        return state.detachedCount() == 1
            ? QStringLiteral("Delete Endpoint…")
            : QStringLiteral("Delete Endpoints…");
    }
    return QStringLiteral("Discard / Delete…");
}

QString taskDiscardConfirmation(const EndpointCanvasDraftState& state) {
    if (state.empty()) {
        return {};
    }

    QStringList impacts;
    const QString pending = pendingDiscardImpact(state);
    const QString detached = detachedDiscardImpact(state);
    if (!pending.isEmpty()) {
        impacts.append(pending);
    }
    if (!detached.isEmpty()) {
        impacts.append(detached);
    }
    return QStringLiteral("The canvas contains ") + inventoryText(state)
        + QStringLiteral(".\n\n")
        + impacts.join(QStringLiteral("\n\n"));
}

QString operationUnavailableHint(const EndpointCanvasDraftState& state,
                                 const QString& operation) {
    if (state.empty()) {
        return {};
    }
    return operationName(operation) + QStringLiteral(" is unavailable while ")
        + unresolvedInventoryText(state) + QStringLiteral(". ")
        + resolutionText(state);
}

QString operationBlocker(const EndpointCanvasDraftState& state,
                         const QString& operation) {
    if (state.empty()) {
        return {};
    }
    QString text = operationName(operation)
        + QStringLiteral(" cannot continue while ")
        + inventoryText(state)
        + (state.size() == 1
               ? QStringLiteral(" remains on the canvas.")
               : QStringLiteral(" remain on the canvas."));
    if (state.detachedCount() > 0) {
        text += QStringLiteral(" Disconnected Endpoint IDs: ")
            + compactDetachedIds(state) + QLatin1Char('.');
    }
    text += QStringLiteral("\n\n") + resolutionText(state)
        + QStringLiteral(" The requested operation did not start.");
    return text;
}

QString discardConfirmation(const EndpointCanvasDraftState& state,
                            bool discardsOtherUnsavedChanges) {
    if (state.empty()) {
        return {};
    }
    QString text = QStringLiteral("The canvas contains ")
        + inventoryText(state)
        + (state.size() == 1
               ? QStringLiteral(
                     ". This draft exists only in the current editing "
                     "session and cannot be saved in its unresolved "
                     "state.\n\n")
               : QStringLiteral(
                     ". These drafts exist only in the current editing "
                     "session and cannot be saved in their unresolved "
                     "state.\n\n"));
    const QString draftReference = state.size() == 1
        ? QStringLiteral("this Endpoint draft")
        : QStringLiteral("these Endpoint drafts");
    text += QStringLiteral("Continuing will discard ") + draftReference;
    text += discardsOtherUnsavedChanges
        ? QStringLiteral(
              " and every other unsaved change in the current design.")
        : QStringLiteral(". The durable design is unchanged.");
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
