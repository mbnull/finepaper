// DRCRunner serializes one IP instance graph, invokes external DRC, and maps findings back to editor IDs.
#include "validation/drcrunner.h"
#include "graph/graph.h"
#include "ipcore/ipcorecommandrunner.h"
#include "ipcore/ipcoregraphexporter.h"
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QRegularExpression>

namespace {

QString drcInputSchema(const IpCatalogEntry& ipcore) {
    const IpcraftCommandDescriptor command =
        ipcore.packageManifest.commands.value(QStringLiteral("validate"));
    return command.inputSchema.trimmed().isEmpty()
        ? ipcore.drc.inputFormat
        : command.inputSchema;
}

} // namespace

// Run external DRC tool on graph and parse validation results
QList<ValidationResult> DRCRunner::validate(
    const Graph* graph,
    const IpCatalogEntry& ipcore,
    const ProjectIpInstanceRecord& instance) {
    m_externalToInternalIds.clear();

    // External DRC consumes serialized IP-core input, so keep temporary
    // input/output isolated and disposable for each validation run.
    QTemporaryFile tmpFile;
    if (!tmpFile.open()) {
        return {ValidationResult(ValidationSeverity::Error,
                                 "DRC validation failed: could not create temporary JSON file: " + tmpFile.errorString(),
                                 "",
                                 "DRC")};
    }

    QTemporaryDir outputDir;
    if (!outputDir.isValid()) {
        return {ValidationResult(ValidationSeverity::Error,
                                 "DRC validation failed: could not create temporary output directory",
                                 "",
                                 "DRC")};
    }

    IpCoreGraphExportRequest exportRequest{
        graph,
        ipcore,
        instance,
        QStringLiteral("design"),
        &m_externalToInternalIds
    };
    exportRequest.inputSchema = drcInputSchema(ipcore);
    const IpCoreGraphExportResult exportResult =
        IpCoreGraphExporter::exportGraph(exportRequest);
    if (!exportResult.success) {
        return {ValidationResult(ValidationSeverity::Error, exportResult.error, "", "DRC")};
    }

    const QByteArray jsonBytes = exportResult.document.toJson();
    if (tmpFile.write(jsonBytes) != jsonBytes.size() || !tmpFile.flush()) {
        // The external tool should never start with a partial input file.
        return {ValidationResult(ValidationSeverity::Error,
                                 "DRC validation failed: could not write temporary JSON file: " + tmpFile.errorString(),
                                 "",
                                 "DRC")};
    }

    const IpCoreResolvedCommand generatorCommand =
        IpCoreCommandRunner::resolveDrc(ipcore, tmpFile.fileName(), outputDir.path());
    if (!generatorCommand.valid) {
        // Missing or incompatible DRC command is a validation finding, not a
        // process crash, so report it through the normal log panel path.
        return {ValidationResult(ValidationSeverity::Error, generatorCommand.errorMessage, "", "DRC")};
    }

    QProcess proc;
    proc.setWorkingDirectory(generatorCommand.workingDirectory);
    // Run from the package source root to match package-relative command paths.
    proc.start(generatorCommand.command, generatorCommand.arguments);

    if (!proc.waitForStarted()) {
        // Surface startup failures as DRC results so the UI can present them
        // consistently with rule violations.
        return {ValidationResult(ValidationSeverity::Error, "DRC process failed to start: " + proc.errorString(), "", "DRC")};
    }

    if (!proc.waitForFinished()) {
        // waitForFinished uses Qt's default timeout here; timeout is treated as
        // an external-tool failure rather than an editor exception.
        return {ValidationResult(ValidationSeverity::Error, "DRC process failed to start or timed out", "", "DRC")};
    }

    if (proc.exitStatus() != QProcess::NormalExit) {
        QString stderr = QString::fromUtf8(proc.readAllStandardError());
        // Preserve stderr because Ruby/IP-core stack traces are usually the only
        // actionable detail for IP-core authors.
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
    // Preferred DRC format is "ERROR element: message"; element is translated
    // through m_externalToInternalIds when it came from serialized artifacts.
    QRegularExpression re("^(ERROR|WARNING|error|warning)\\s+(.+?):\\s+(.+)$", QRegularExpression::MultilineOption);
    auto it = re.globalMatch(stderr);

    while (it.hasNext()) {
        auto match = it.next();
        auto severity = match.captured(1).toLower().startsWith("warn")
            ? ValidationSeverity::Warning : ValidationSeverity::Error;
        const QString elementId = m_externalToInternalIds.value(match.captured(2), match.captured(2));
        results.append(ValidationResult(severity, match.captured(3), elementId, "DRC"));
    }

    static const QRegularExpression fallbackLinePattern(
        QStringLiteral("^(Duplicate .+|Invalid .+|Missing .+|.+ not found|XP .+:|Endpoint .+:)"));
    static const QRegularExpression xpPattern(QStringLiteral("^XP\\s+([^:]+):"));
    static const QRegularExpression epPattern(QStringLiteral("^Endpoint\\s+([^:]+):"));
    static const QRegularExpression dupXpPattern(QStringLiteral("^Duplicate XP id:\\s*(\\S+)"));
    static const QRegularExpression dupEpPattern(QStringLiteral("^Duplicate endpoint id:\\s*(\\S+)"));

    for (const auto& line : stderr.split('\n')) {
        if (line.trimmed().isEmpty() || line.contains("DRC violations:")) continue;

        // Older Ruby validators emitted free-form runtime lines. Preserve those
        // as validation errors and extract element IDs from known prefixes.
        bool found = false;
        for (const auto& r : results) {
            if (r.message().contains(line.trimmed())) { found = true; break; }
        }
        if (found) continue;

        if (line.contains(fallbackLinePattern)) {
            QString elementId;
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
