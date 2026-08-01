#include "features/operations/design_run_state.h"

#include <utility>

namespace finepaper::operations {

void DesignRunState::beginSession(const QString& designName) {
    ++m_current.session;
    m_current.revision = 0;
    m_current.designName = designName;
}

void DesignRunState::advanceDesignRevision(const QString& designName) {
    ++m_current.revision;
    m_current.designName = designName;
}

void DesignRunState::advanceCatalogRevision() {
    ++m_current.catalogRevision;
}

RunTicket DesignRunState::beginRun(
    RunKind kind,
    QString outputRoot) {
    RunTicket ticket = {
        ++m_nextRunId,
        kind,
        m_current,
        std::move(outputRoot),
    };
    m_activeRun = ActiveRun{ticket, false};
    return ticket;
}

CompletionDisposition DesignRunState::disposition(
    const RunTicket& ticket) const {
    if (ticket.input.session != m_current.session) {
        return CompletionDisposition::DifferentSession;
    }
    if (!m_activeRun || ticket != m_activeRun->ticket) {
        return CompletionDisposition::Superseded;
    }
    if (m_activeRun->cancelRequested) {
        return CompletionDisposition::CancelRequested;
    }
    if (ticket.input.catalogRevision != m_current.catalogRevision) {
        return CompletionDisposition::StaleCatalog;
    }
    if (ticket.input.revision != m_current.revision) {
        return CompletionDisposition::StaleRevision;
    }
    return CompletionDisposition::Current;
}

bool DesignRunState::requestCancel(const RunTicket& ticket) {
    if (!m_activeRun || ticket != m_activeRun->ticket
        || m_activeRun->cancelRequested) {
        return false;
    }
    m_activeRun->cancelRequested = true;
    return true;
}

bool DesignRunState::finishRun(const RunTicket& ticket) {
    if (!m_activeRun || ticket != m_activeRun->ticket) {
        return false;
    }
    m_activeRun = std::nullopt;
    return true;
}

const DesignStamp& DesignRunState::currentStamp() const {
    return m_current;
}

bool DesignRunState::hasActiveRun() const {
    return m_activeRun.has_value();
}

} // namespace finepaper::operations
