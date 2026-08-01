#include "execution/cancellation.h"

#include <utility>

namespace finepaper {

CancellationToken::CancellationToken(std::shared_ptr<State> state) noexcept
    : m_state(std::move(state)) {}

bool CancellationToken::isCancellationRequested() const noexcept {
    return m_state
        && m_state->cancellationRequested.load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : m_state(std::make_shared<CancellationToken::State>()) {}

CancellationToken CancellationSource::token() const noexcept {
    return CancellationToken(m_state);
}

bool CancellationSource::isCancellationRequested() const noexcept {
    return m_state
        && m_state->cancellationRequested.load(std::memory_order_acquire);
}

void CancellationSource::requestCancellation() noexcept {
    if (m_state) {
        m_state->cancellationRequested.store(true, std::memory_order_release);
    }
}

} // namespace finepaper
