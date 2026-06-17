#pragma once

#include <QString>

namespace ipcraft::contract::packagekeys {

inline QString schema() { return QStringLiteral("schema"); }
inline QString id() { return QStringLiteral("id"); }
inline QString version() { return QStringLiteral("version"); }
inline QString name() { return QStringLiteral("name"); }
inline QString display() { return QStringLiteral("display"); }
inline QString extensions() { return QStringLiteral("extensions"); }
inline QString configSchema() { return QStringLiteral("config_schema"); }
inline QString interfaces() { return QStringLiteral("interfaces"); }
inline QString connectionRules() { return QStringLiteral("connection_rules"); }
inline QString emitters() { return QStringLiteral("emitters"); }
inline QString flows() { return QStringLiteral("flows"); }
inline QString artifacts() { return QStringLiteral("artifacts"); }
inline QString diagnostics() { return QStringLiteral("diagnostics"); }
inline QString views() { return QStringLiteral("views"); }
inline QString plugin() { return QStringLiteral("plugin"); }
inline QString nativeSchema() { return QStringLiteral("native_schema"); }
inline QString metadata() { return QStringLiteral("metadata"); }
inline QString native() { return QStringLiteral("native"); }
inline QString graphConfig() { return QStringLiteral("graph_config"); }

inline QString required() { return QStringLiteral("required"); }
inline QString library() { return QStringLiteral("library"); }
inline QString entry() { return QStringLiteral("entry"); }

} // namespace ipcraft::contract::packagekeys
