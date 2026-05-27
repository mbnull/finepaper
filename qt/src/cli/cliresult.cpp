#include "cli/cliresult.h"

#include "ipcraft/jsonhelpers.h"
#include "ipcraft/schemaids.h"

#include <QJsonValue>
#include <QTextStream>

namespace ipcraft::cli {

QJsonObject CliResult::toJson() const {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), schemaids::cliResultV1);
    object.insert(QStringLiteral("ok"), ok);
    if (ok || !result.isUndefined()) {
        object.insert(QStringLiteral("result"), result);
    }
    object.insert(QStringLiteral("diagnostics"), diagnostics.toJson());
    return sortedJsonObject(object);
}

Diagnostic cliDiagnostic(const QString& ruleId,
                         const QString& message,
                         const QString& locationKind,
                         const QString& path) {
    Diagnostic diagnostic;
    diagnostic.severity = QStringLiteral("error");
    diagnostic.source = QStringLiteral("cli");
    diagnostic.category = QStringLiteral("cli");
    diagnostic.ruleId = ruleId;
    diagnostic.message = message;
    DiagnosticLocation location;
    location.kind = locationKind.isEmpty() ? QStringLiteral("project") : locationKind;
    location.path = path;
    diagnostic.locations.append(location);
    return diagnostic;
}

int writeCliResult(const CliResult& result) {
    QTextStream out(stdout);
    out << QString::fromUtf8(toDeterministicJson(result.toJson(), QJsonDocument::Indented));
    return result.ok ? 0 : 1;
}

} // namespace ipcraft::cli
