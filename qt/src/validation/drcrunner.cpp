// DRCRunner serializes the graph, invokes external DRC, and maps findings back to editor IDs.
#include "validation/drcrunner.h"
#include "common/frameworkpaths.h"
#include "graph/graph.h"
#include "modules/modulelabels.h"
#include <QProcess>
#include <QTemporaryFile>
#include <QRegularExpression>

// Run external DRC tool on graph and parse validation results
QList<ValidationResult> DRCRunner::validate(const Graph* graph) {
    QString json = graph->toJsonDocument(QStringLiteral("design"),
                                         GraphJsonFlavor::Framework,
                                         &m_externalToInternalIds).toJson();

    QTemporaryFile tmpFile;
    if (!tmpFile.open()) return {};
    tmpFile.write(json.toUtf8());
    tmpFile.flush();

    const QString frameworkPath = FrameworkPaths::resolveFrameworkPath();
    const QString templatePath = FrameworkPaths::resolveTemplatePath();
    if (frameworkPath.isEmpty() || templatePath.isEmpty()) {
        return {ValidationResult(ValidationSeverity::Error, "Framework not found. Set FRAMEWORK_PATH or place framework/ in parent directory.", "", "DRC")};
    }

    QProcess proc;
    proc.setWorkingDirectory(frameworkPath);
    proc.start("ruby", {"bin/generate", "-i", tmpFile.fileName(), "-o", "/tmp", "-t", templatePath});

    if (!proc.waitForStarted()) {
        return {ValidationResult(ValidationSeverity::Error, "DRC process failed to start: " + proc.errorString(), "", "DRC")};
    }

    if (!proc.waitForFinished()) {
        return {ValidationResult(ValidationSeverity::Error, "DRC process failed to start or timed out", "", "DRC")};
    }

    if (proc.exitStatus() != QProcess::NormalExit) {
        QString stderr = QString::fromUtf8(proc.readAllStandardError());
        return {ValidationResult(ValidationSeverity::Error, "DRC process crashed: " + stderr, "", "DRC")};
    }

    if (proc.exitCode() != 0) {
        QString stderr = QString::fromUtf8(proc.readAllStandardError());
        return {ValidationResult(ValidationSeverity::Error, "DRC validation failed (exit code " + QString::number(proc.exitCode()) + "): " + stderr, "", "DRC")};
    }

    return parseErrors(proc.readAllStandardError());
}

QList<ValidationResult> DRCRunner::parseErrors(const QString& stderr) {
    QList<ValidationResult> results;
    QRegularExpression re("^(ERROR|WARNING|error|warning)\\s+(.+?):\\s+(.+)$", QRegularExpression::MultilineOption);
    auto it = re.globalMatch(stderr);

    while (it.hasNext()) {
        auto match = it.next();
        auto severity = match.captured(1).toLower().startsWith("warn")
            ? ValidationSeverity::Warning : ValidationSeverity::Error;
        const QString elementId = m_externalToInternalIds.value(match.captured(2), match.captured(2));
        results.append(ValidationResult(severity, match.captured(3), elementId, "DRC"));
    }

    for (const auto& line : stderr.split('\n')) {
        if (line.trimmed().isEmpty() || line.contains("DRC violations:")) continue;

        bool found = false;
        for (const auto& r : results) {
            if (r.message().contains(line.trimmed())) { found = true; break; }
        }
        if (found) continue;

        if (line.contains(QRegularExpression("^(Duplicate .+|Invalid .+|Missing .+|.+ not found|XP .+:|Endpoint .+:)"))) {
            QString elementId;
            QRegularExpression xpPattern("^XP\\s+([^:]+):");
            QRegularExpression epPattern("^Endpoint\\s+([^:]+):");
            QRegularExpression dupXpPattern("^Duplicate XP id:\\s*(\\S+)");
            QRegularExpression dupEpPattern("^Duplicate endpoint id:\\s*(\\S+)");

            auto match = xpPattern.match(line);
            if (!match.hasMatch()) match = epPattern.match(line);
            if (!match.hasMatch()) match = dupXpPattern.match(line);
            if (!match.hasMatch()) match = dupEpPattern.match(line);

            if (match.hasMatch()) {
                elementId = match.captured(1);
                if (elementId.endsWith(" (RuntimeError)")) {
                    elementId = elementId.left(elementId.length() - 15);
                }
                elementId = m_externalToInternalIds.value(elementId, elementId);
            }
            results.append(ValidationResult(ValidationSeverity::Error, line.trimmed(), elementId, "DRC"));
        }
    }

    return results;
}
