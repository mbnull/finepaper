// PluginDescriptor stores directory plugin manifest metadata.
#pragma once

#include <QString>
#include <QStringList>

struct PluginGeneratorDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("legacy_noc_json");
    QStringList args;

    bool hasGenerator() const { return !command.trimmed().isEmpty(); }
    bool usesGenericGraphInput() const {
        return inputFormat == QStringLiteral("generic_graph_v1");
    }
    QStringList arguments(const QString& inputPath, const QString& outputDirectory) const {
        QStringList resolved;
        resolved.reserve(args.size());
        for (QString arg : args) {
            arg.replace(QStringLiteral("{input}"), inputPath);
            arg.replace(QStringLiteral("{output}"), outputDirectory);
            resolved.append(arg);
        }
        return resolved;
    }
};

struct PluginNativeDescriptor {
    bool enabled = false;
    QString library;
};

struct PluginDescriptor {
    QString id;
    QString name;
    QString version;
    QString rootPath;
    QString modulesPath;
    QString graphicsPath;
    PluginGeneratorDescriptor generator;
    PluginNativeDescriptor native;

    bool hasModules() const { return !modulesPath.isEmpty(); }
};
