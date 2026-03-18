#include "drcrunner.h"
#include "graph.h"
#include "module.h"
#include "connection.h"
#include <QProcess>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

QList<ValidationResult> DRCRunner::validate(const Graph* graph) {
    QString json = serializeToJson(graph);

    QTemporaryFile tmpFile;
    if (!tmpFile.open()) return {};
    tmpFile.write(json.toUtf8());
    tmpFile.flush();

    QProcess proc;
    proc.setWorkingDirectory("/home/bnl/dev/finepaper/framework");
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

    for (const auto& mod : graph->modules()) {
        if (mod->type() == "XP") {
            QJsonObject xp;
            xp["id"] = mod->id();
            const auto& p = mod->parameters();
            if (p.count("x")) xp["x"] = std::get<int>(p.at("x").value());
            if (p.count("y")) xp["y"] = std::get<int>(p.at("y").value());
            xps.append(xp);
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
        QJsonObject c;
        c["from"] = conn->source().moduleId;
        c["to"] = conn->target().moduleId;
        conns.append(c);
    }

    root["parameters"] = params;
    root["xps"] = xps;
    root["connections"] = conns;
    root["endpoints"] = eps;

    return QJsonDocument(root).toJson();
}

QList<ValidationResult> DRCRunner::parseErrors(const QString& stderr) {
    QList<ValidationResult> results;
    QRegularExpression re("^(\\w+)\\s+(\\S+):\\s+(.+)$", QRegularExpression::MultilineOption);
    auto it = re.globalMatch(stderr);

    while (it.hasNext()) {
        auto match = it.next();
        results.append(ValidationResult(
            ValidationSeverity::Error,
            match.captured(3),
            match.captured(2),
            "DRC"
        ));
    }

    return results;
}

