#pragma once

#include <QString>

#include <array>

namespace finepaper {

inline constexpr int unavailableCanvasCommandStatusDurationMs = 6000;

// QtNodes exposes projection-oriented editing commands by default. Finepaper's
// canvas is a projection of NocDesign, so commands without a semantic design
// operation are deliberately reported instead of mutating that projection.
enum class NocCanvasCommand {
    Copy,
    Paste,
    Duplicate,
    Undo,
    Redo,
};

inline constexpr std::array unavailableCanvasCommands{
    NocCanvasCommand::Copy,
    NocCanvasCommand::Paste,
    NocCanvasCommand::Duplicate,
    NocCanvasCommand::Undo,
    NocCanvasCommand::Redo,
};

struct UnavailableCanvasCommandPresentation {
    QString actionText;
    QString actionObjectName;
    QString statusMessage;
};

[[nodiscard]] UnavailableCanvasCommandPresentation
unavailableCanvasCommandPresentation(NocCanvasCommand command);

} // namespace finepaper
