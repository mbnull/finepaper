// Plugin project adapters expose plugin-owned project state to the core UI.
#pragma once

#include "graph/parameter.h"
#include "plugins/plugindescriptor.h"

#include <QString>
#include <QVector>

struct PluginParameterField {
    QString name;
    QString label;
    QString description;
    QString type;
    Parameter::Value defaultValue = QString();
    QVector<PluginInstanceParameterChoice> choices;
    bool configurable = true;
};

struct PluginParameterSection {
    QString pluginId;
    QString instanceId;
    QString id;
    QString label;
    bool expandedByDefault = true;
    QVector<PluginParameterField> fields;
};

class IPluginProjectAdapter {
public:
    virtual ~IPluginProjectAdapter() = default;
    virtual QVector<PluginParameterSection> parameterSections() const = 0;
};

class ManifestPluginProjectAdapter final : public IPluginProjectAdapter {
public:
    explicit ManifestPluginProjectAdapter(PluginDescriptor plugin);
    QVector<PluginParameterSection> parameterSections() const override;

private:
    PluginDescriptor m_plugin;
};
