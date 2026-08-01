#pragma once

#include <QWidget>

#include <initializer_list>

namespace finepaper::ui {

// Applies the shared workbench focus contract without caching child pointers
// across a panel rebuild.
[[nodiscard]] inline QWidget* firstAvailableFocusTarget(
    QWidget* visibilityRoot,
    std::initializer_list<QWidget*> candidates) {
    if (!visibilityRoot) {
        return nullptr;
    }
    for (QWidget* candidate : candidates) {
        if (candidate && candidate->isEnabled()
            && candidate->focusPolicy() != Qt::NoFocus
            && candidate->isVisibleTo(visibilityRoot)) {
            return candidate;
        }
    }
    return nullptr;
}

} // namespace finepaper::ui
