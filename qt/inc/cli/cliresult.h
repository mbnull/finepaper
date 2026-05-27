#pragma once

#include "ipcraft/diagnostics.h"

#include <QJsonObject>
#include <QJsonValue>

namespace ipcraft::cli {

struct CliResult {
    bool ok = false;
    QJsonValue result;
    DiagnosticStore diagnostics;

    QJsonObject toJson() const;
};

Diagnostic cliDiagnostic(const QString& ruleId,
                         const QString& message,
                         const QString& locationKind = QStringLiteral("project"),
                         const QString& path = {});

int writeCliResult(const CliResult& result);

} // namespace ipcraft::cli
