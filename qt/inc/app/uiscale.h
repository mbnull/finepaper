// UiScale applies the editor's default whole-application display scale.
#pragma once

#include <QByteArray>
#include <QProcessEnvironment>
#include <optional>

namespace UiScale {

std::optional<QByteArray> defaultScaleFactor(const QProcessEnvironment& environment);
void applyDefaultScaleFactor();

} // namespace UiScale
