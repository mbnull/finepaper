#pragma once

#include <QString>

#include <optional>

namespace finepaper::operations {

enum class RunKind {
    Validation,
    Generation,
};

struct DesignStamp final {
    quint64 session = 0;
    quint64 revision = 0;
    quint64 catalogRevision = 0;
    QString designName;

    bool operator==(const DesignStamp&) const = default;
};

struct RunTicket final {
    quint64 runId = 0;
    RunKind kind = RunKind::Validation;
    DesignStamp input;
    QString outputRoot;

    bool operator==(const RunTicket&) const = default;
};

enum class CompletionDisposition {
    Current,
    StaleRevision,
    DifferentSession,
    StaleCatalog,
    Superseded,
    CancelRequested,
};

// Pure workbench state for accepting or rejecting asynchronous results.  The
// worker owns immutable Application/Design copies; this class decides whether
// the result still belongs to the document revision visible in the GUI.
class DesignRunState final {
public:
    void beginSession(const QString& designName);
    void advanceDesignRevision(const QString& designName);
    void advanceCatalogRevision();

    [[nodiscard]] RunTicket beginRun(
        RunKind kind,
        QString outputRoot = {});
    [[nodiscard]] CompletionDisposition disposition(
        const RunTicket& ticket) const;
    [[nodiscard]] bool requestCancel(const RunTicket& ticket);
    [[nodiscard]] bool finishRun(const RunTicket& ticket);

    [[nodiscard]] const DesignStamp& currentStamp() const;
    [[nodiscard]] bool hasActiveRun() const;

private:
    struct ActiveRun final {
        RunTicket ticket;
        bool cancelRequested = false;
    };

    DesignStamp m_current;
    quint64 m_nextRunId = 0;
    std::optional<ActiveRun> m_activeRun = std::nullopt;
};

} // namespace finepaper::operations
