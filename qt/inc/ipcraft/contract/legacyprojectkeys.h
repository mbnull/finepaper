#pragma once

#include <QString>

namespace ipcraft::contract::legacyprojectkeys {

inline QString project() { return QStringLiteral("project"); }
inline QString instances() { return QStringLiteral("instances"); }
inline QString composition() { return QStringLiteral("composition"); }
inline QString layout() { return QStringLiteral("layout"); }
inline QString migration() { return QStringLiteral("migration"); }
inline QString native() { return QStringLiteral("native"); }

} // namespace ipcraft::contract::legacyprojectkeys
