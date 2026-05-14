#pragma once

#include "graph/parameter.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct IpCoreCommandDescriptor {
    QString command;
    QString inputFormat = QStringLiteral("ipcore_graph_v1");
    QStringList args;

    bool hasCommand() const { return !command.trimmed().isEmpty(); }
    bool usesIpcoreGraphInput() const {
        return inputFormat == QStringLiteral("ipcore_graph_v1");
    }
    bool usesIpcraftNocProjectInput() const {
        return inputFormat == QStringLiteral("ipcraft.noc.project.v1");
    }
    QStringList arguments(const QString& inputPath, const QString& outputDirectory) const;
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

struct IpCoreInstanceParameterChoice {
    QString value;
    QString label;
};

struct IpCoreInstanceParameterDescriptor {
    QString name;
    QString type;
    Parameter::Value defaultValue = QString();
    QString label;
    QString description;
    QVector<IpCoreInstanceParameterChoice> choices;
    std::optional<double> minimumValue;
    std::optional<double> maximumValue;
    bool configurable = true;
};

struct IpCoreRuntimeDescriptor {
    QString id;
    QString name;
    QString version;
    QString kind;
    QString runtimeRootPath;
    QString sourceRootPath;
    QString modulesPath;
    QString graphicsPath;
    QHash<QString, IpCoreInstanceParameterDescriptor> instanceParameters;
    IpCoreCommandDescriptor generator;
    IpCoreCommandDescriptor drc;
    QVector<TopologyPresetDescriptor> topologyPresets;

    bool hasModules() const { return !modulesPath.isEmpty(); }
};
