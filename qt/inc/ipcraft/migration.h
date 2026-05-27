#pragma once

#include "ipcraft/diagnostics.h"
#include "project/projectdocument.h"

#include <QJsonObject>
#include <QString>

namespace ipcraft {

struct ProjectMigrationResult {
    bool ok = false;
    ProjectDocument document;
    DiagnosticStore diagnostics;
};

Diagnostic migrationDiagnostic(const QString& ruleId,
                               const QString& message,
                               const QString& path = QStringLiteral("$"));

class ProjectMigrator {
public:
    static ProjectMigrationResult migrateFile(const QString& path, const QString& targetSchema);
    static ProjectMigrationResult migrateJson(const QJsonObject& root, const QString& targetSchema);
};

} // namespace ipcraft
