#include "drcrunner.h"
#include "graph.h"
#include "module.h"
#include "connection.h"
#include <QProcess>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDir>

QList<ValidationResult> DRCRunner::validate(const Graph* graph) {
    QString json = serializeToJson(graph);

    QTemporaryFile tmpFile;
    if (!tmpFile.open()) return {};
    tmpFile.write(json.toUtf8());
    tmpFile.flush();

    QString frameworkPath = qEnvironmentVariable("FRAMEWORK_PATH");
    if (frameworkPath.isEmpty()) {
        QDir dir(QCoreApplication::applicationDirPath());
        while (!dir.isRoot()) {
            if (dir.cd("framework") && QFileInfo(dir.filePath("bin/generate")).exists()) {
                frameworkPath = dir.absolutePath();
                break;
            }
            dir.cdUp();
            if (QFileInfo(dir.filePath("framework/bin/generate")).exists()) {
                frameworkPath = dir.filePath("framework");
                break;
            }
        }
    }

    if (frameworkPath.isEmpty() || !QFileInfo(frameworkPath + "/bin/generate").exists()) {
        return {ValidationResult(ValidationSeverity::Error, "Framework not found. Set FRAMEWORK_PATH or place framework/ in parent directory.", "", "DRC")};
    }

    QProcess proc;
    proc.setWorkingDirectory(frameworkPath);
    proc.start("ruby", {"bin/generate", "-i", tmpFile.fileName(), "-o", "/tmp", "-t", "template"});
    proc.waitForFinished();

    return parseErrors(proc.readAllStandardError());
}

QString DRCRunner::serializeToJson(const Graph* graph) {
    QJsonObject root;
    root["name"] = "design";
    root["version"] = "1.0";

    QJsonObject params;
    QJsonArray xps, conns, eps;
    QMap<QString, QJsonArray> xpEndpoints;

    for (const auto& mod : graph->modules()) {
        if (mod->type() == "XP") {
            QJsonObject xp;
            xp["id"] = mod->id();
            const auto& p = mod->parameters();
            if (p.count("x")) xp["x"] = std::get<int>(p.at("x").value());
            if (p.count("y")) xp["y"] = std::get<int>(p.at("y").value());
            xp["endpoints"] = QJsonArray();

            QJsonObject config;
            if (p.count("routing_algorithm")) config["routing_algorithm"] = std::get<QString>(p.at("routing_algorithm").value());
            if (p.count("vc_count")) config["vc_count"] = std::get<int>(p.at("vc_count").value());
            if (p.count("buffer_depth")) config["buffer_depth"] = std::get<int>(p.at("buffer_depth").value());
            if (!config.isEmpty()) xp["config"] = config;

            xps.append(xp);
            xpEndpoints[mod->id()] = QJsonArray();
        } else if (mod->type() == "Endpoint") {
            QJsonObject ep;
            ep["id"] = mod->id();
            const auto& p = mod->parameters();
            if (p.count("type")) ep["type"] = std::get<QString>(p.at("type").value());
            if (p.count("protocol")) ep["protocol"] = std::get<QString>(p.at("protocol").value());
            if (p.count("data_width")) ep["data_width"] = std::get<int>(p.at("data_width").value());

            QJsonObject config;
            if (p.count("buffer_depth")) config["buffer_depth"] = std::get<int>(p.at("buffer_depth").value());
            if (p.count("qos_enabled")) config["qos_enabled"] = std::get<bool>(p.at("qos_enabled").value());
            if (!config.isEmpty()) ep["config"] = config;

            eps.append(ep);
        }
    }

    for (const auto& conn : graph->connections()) {
        auto src = conn->source().moduleId;
        auto tgt = conn->target().moduleId;
        auto srcMod = graph->getModule(src);
        auto tgtMod = graph->getModule(tgt);

        if (srcMod && tgtMod) {
            if (srcMod->type() == "XP" && tgtMod->type() == "XP") {
                QJsonObject c;
                c["from"] = src;
                c["to"] = tgt;
                conns.append(c);
            } else if (srcMod->type() == "Endpoint" && tgtMod->type() == "XP") {
                xpEndpoints[tgt].append(src);
            } else if (srcMod->type() == "XP" && tgtMod->type() == "Endpoint") {
                xpEndpoints[src].append(tgt);
            }
        }
    }

    for (int i = 0; i < xps.size(); ++i) {
        QJsonObject xp = xps[i].toObject();
        QString xpId = xp["id"].toString();
        xp["endpoints"] = xpEndpoints[xpId];
        xps[i] = xp;
    }

    root["parameters"] = params;
    root["xps"] = xps;
    root["connections"] = conns;
    root["endpoints"] = eps;

    return QJsonDocument(root).toJson();
}

QList<ValidationResult> DRCRunner::parseErrors(const QString& stderr) {
    QList<ValidationResult> results;
    QRegularExpression re("^(ERROR|WARNING|error|warning)\\s+(.+?):\\s+(.+)$", QRegularExpression::MultilineOption);
    auto it = re.globalMatch(stderr);

    while (it.hasNext()) {
        auto match = it.next();
        auto severity = match.captured(1).toLower().startsWith("warn")
            ? ValidationSeverity::Warning : ValidationSeverity::Error;
        results.append(ValidationResult(severity, match.captured(3), match.captured(2), "DRC"));
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
            }
            results.append(ValidationResult(ValidationSeverity::Error, line.trimmed(), elementId, "DRC"));
        }
    }

    return results;
}

