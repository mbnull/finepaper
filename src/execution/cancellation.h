#pragma once

#include <atomic>
#include <memory>

namespace finepaper {

class CancellationSource;

// A cheap, thread-safe view of an operation's cancellation state. A default
// token is intentionally never cancelled so existing synchronous callers do
// not need to create a source.
class CancellationToken {
public:
    CancellationToken() = default;

    [[nodiscard]] bool isCancellationRequested() const noexcept;

private:
    struct State {
        std::atomic_bool cancellationRequested = false;
    };

    explicit CancellationToken(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> m_state;

    friend class CancellationSource;
};

// Sources and their tokens share one monotonic state: once requested,
// cancellation cannot be withdrawn. Copies are safe to use across threads.
class CancellationSource {
public:
    CancellationSource();

    [[nodiscard]] CancellationToken token() const noexcept;
    [[nodiscard]] bool isCancellationRequested() const noexcept;
    void requestCancellation() noexcept;

private:
    std::shared_ptr<CancellationToken::State> m_state;
};

} // namespace finepaper
