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

QList<ValidationResult> DRCRunner::validate(const Graph* graph) {
    QString json = serializeToJson(graph);

    QTemporaryFile tmpFile;
    if (!tmpFile.open()) return {};
    tmpFile.write(json.toUtf8());
    tmpFile.flush();

    QProcess proc;
    proc.setWorkingDirectory("../framework");
    proc.start("ruby", {"bin/generate", "-i", tmpFile.fileName(), "-o", "/tmp", "-t", "templates"});
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
            xps.append(xp);
            xpEndpoints[mod->id()] = QJsonArray();
        } else if (mod->type() == "Endpoint") {
            QJsonObject ep;
            ep["id"] = mod->id();
            const auto& p = mod->parameters();
            if (p.count("type")) ep["type"] = std::get<QString>(p.at("type").value());
            if (p.count("protocol")) ep["protocol"] = std::get<QString>(p.at("protocol").value());
            if (p.count("data_width")) ep["data_width"] = std::get<int>(p.at("data_width").value());
            eps.append(ep);
        }
    }

    for (const auto& conn : graph->connections()) {
        auto src = conn->source().moduleId;
        auto tgt = conn->target().moduleId;
        auto srcMod = graph->findModule(src);
        auto tgtMod = graph->findModule(tgt);

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

    QRegularExpression re2("^(Duplicate .+|Invalid .+|Missing .+|.+ not found)", QRegularExpression::MultilineOption);
    auto it2 = re2.globalMatch(stderr);
    while (it2.hasNext()) {
        auto match = it2.next();
        bool found = false;
        for (const auto& r : results) {
            if (r.message().contains(match.captured(1))) { found = true; break; }
        }
        if (!found) results.append(ValidationResult(ValidationSeverity::Error, match.captured(1), "", "DRC"));
    }

    return results;
}

