#pragma once

#include <QString>

class QApplication;
class QPalette;

namespace finepaper::ui {

// Builds a palette-derived stylesheet for the workbench shell and common Qt
// Widgets. Body typography remains controlled by QApplication / the platform.
[[nodiscard]] QString workbenchStyleSheet(const QPalette& palette);

// Applies the stylesheet and keeps it synchronized with application palette
// changes. The connection is owned by QApplication and is installed once.
void applyWorkbenchStyle(QApplication& application);

} // namespace finepaper::ui
