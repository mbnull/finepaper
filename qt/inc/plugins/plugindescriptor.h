// PluginDescriptor stores directory plugin manifest metadata.
#pragma once

#include "graph/parameter.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct PluginCommandDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("legacy_noc_json");
    QStringList args;

    bool hasCommand() const { return !command.trimmed().isEmpty(); }
    bool hasGenerator() const { return hasCommand(); }
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

using PluginGeneratorDescriptor = PluginCommandDescriptor;

struct PluginNativeDescriptor {
    bool enabled = false;
    QString library;
};

struct TopologyPresetParameterDescriptor {
    QString label;
    int defaultValue = 0;
    int minimumValue = 0;
    int maximumValue = 0;
};

struct TopologyPresetDescriptor {
    QString id;
    QString label;
    QString kind;
    QString routerModule;
    QString idPattern;
    QHash<QString, QString> ports;
    QHash<QString, TopologyPresetParameterDescriptor> parameters;
};

struct PluginInstanceParameterChoice {
    QString value;
    QString label;
};

struct PluginInstanceParameterDescriptor {
    QString name;
    QString type;
    Parameter::Value defaultValue = QString();
    QString label;
    QString description;
    QVector<PluginInstanceParameterChoice> choices;
    std::optional<double> minimumValue;
    std::optional<double> maximumValue;
    bool configurable = true;
};

struct PluginDescriptor {
    QString id;
    QString name;
    QString version;
    QString kind;
    QString rootPath;
    QString modulesPath;
    QString graphicsPath;
    QHash<QString, PluginInstanceParameterDescriptor> instanceParameters;
    PluginCommandDescriptor generator;
    PluginCommandDescriptor drc;
    PluginNativeDescriptor native;
    QVector<TopologyPresetDescriptor> topologyPresets;

    bool hasModules() const { return !modulesPath.isEmpty(); }
};
