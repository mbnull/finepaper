// FrameworkPaths resolves runtime paths to framework binaries, templates, and module bundles.
#pragma once

#include <QString>

namespace FrameworkPaths {

QString resolveFrameworkPath();
QString resolveTemplatePath();

} // namespace FrameworkPaths
