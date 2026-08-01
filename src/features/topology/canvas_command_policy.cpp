#include "features/topology/canvas_command_policy.h"

namespace finepaper {

UnavailableCanvasCommandPresentation unavailableCanvasCommandPresentation(
    NocCanvasCommand command) {
    switch (command) {
    case NocCanvasCommand::Copy:
        return {
            QStringLiteral("Copy topology element (unavailable)"),
            QStringLiteral("finepaper.canvasCommandUnavailable.copy"),
            QStringLiteral("NoC topology elements cannot be copied.")};
    case NocCanvasCommand::Paste:
        return {
            QStringLiteral("Paste topology element (unavailable)"),
            QStringLiteral("finepaper.canvasCommandUnavailable.paste"),
            QStringLiteral(
                "Paste is unavailable on the NoC canvas. Create Endpoints "
                "from the Endpoint Library; Routers are created through "
                "Resize Mesh.")};
    case NocCanvasCommand::Duplicate:
        return {
            QStringLiteral("Duplicate topology element (unavailable)"),
            QStringLiteral("finepaper.canvasCommandUnavailable.duplicate"),
            QStringLiteral(
                "Duplicate is unavailable because IDs, attachments, and "
                "Domain assignments must be created explicitly.")};
    case NocCanvasCommand::Undo:
        return {
            QStringLiteral("Undo topology edit (unavailable)"),
            QStringLiteral("finepaper.canvasCommandUnavailable.undo"),
            QStringLiteral(
                "Undo is unavailable for topology edits. Destructive NoC "
                "edits require confirmation.")};
    case NocCanvasCommand::Redo:
        return {
            QStringLiteral("Redo topology edit (unavailable)"),
            QStringLiteral("finepaper.canvasCommandUnavailable.redo"),
            QStringLiteral(
                "Redo is unavailable because topology Undo is not "
                "supported.")};
    }
    return {};
}

} // namespace finepaper
