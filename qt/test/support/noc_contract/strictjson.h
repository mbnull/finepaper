#pragma once

#include <QByteArrayView>
#include <QString>

namespace noc_contract {

// Validates the byte-level JSON admission contract before QJsonDocument is
// constructed. It rejects duplicate decoded member names, malformed UTF-8,
// unpaired surrogates, non-finite numbers, and non-exact integer binary64
// values. The caller may then safely pass the same bytes to Qt JSON APIs.
bool validateStrictJson(QByteArrayView bytes, QString *error = nullptr);

} // namespace noc_contract
