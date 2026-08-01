#pragma once

#include <QString>

class QApplication;
class QPalette;

namespace finepaper::ui {

// Builds a palette-derived stylesheet for the workbench shell and common Qt
// Widgets. Body typography remains controlled by QApplication / the platform.
[[nodiscard]] QString workbenchStyleSheet(const QPalette& palette);

// Raises unusually small platform defaults to the readable body token, then
// applies the stylesheet and keeps it synchronized with palette changes. User
// and accessibility fonts above that minimum remain unchanged.
void applyWorkbenchStyle(QApplication& application);

} // namespace finepaper::ui
