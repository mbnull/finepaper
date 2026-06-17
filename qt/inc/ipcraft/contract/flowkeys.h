#pragma once

#include <QString>

namespace ipcraft::contract::flowkeys {

inline QString command() { return QStringLiteral("command"); }
inline QString executable() { return QStringLiteral("executable"); }
inline QString frameworkTool() { return QStringLiteral("framework_tool"); }
inline QString args() { return QStringLiteral("args"); }
inline QString env() { return QStringLiteral("env"); }
inline QString allow() { return QStringLiteral("allow"); }
inline QString capture() { return QStringLiteral("capture"); }
inline QString stdout() { return QStringLiteral("stdout"); }
inline QString stderr() { return QStringLiteral("stderr"); }
inline QString cwd() { return QStringLiteral("cwd"); }
inline QString timeoutMs() { return QStringLiteral("timeout_ms"); }
inline QString native() { return QStringLiteral("native"); }

inline QString maxBytes() { return QStringLiteral("max_bytes"); }
inline QString kind() { return QStringLiteral("kind"); }
inline QString steps() { return QStringLiteral("steps"); }

inline QString commandPath(const QString& key) {
    return command() + QLatin1Char('.') + key;
}

inline QString commandCapturePath(const QString& key) {
    return commandPath(capture()) + QLatin1Char('.') + key;
}

} // namespace ipcraft::contract::flowkeys
