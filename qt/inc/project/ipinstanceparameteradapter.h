// IP instance parameter adapters expose manifest-declared project state to the core UI.
#pragma once

#include "graph/parameter.h"
#include "plugins/plugindescriptor.h"

#include <QString>
#include <QVector>

struct IpInstanceParameterField {
    QString name;
    QString label;
    QString description;
    QString type;
    Parameter::Value defaultValue = QString();
    QVector<PluginInstanceParameterChoice> choices;
    bool configurable = true;
};

struct IpInstanceParameterSection {
    QString ipcoreId;
    QString instanceId;
    QString id;
    QString label;
    bool expandedByDefault = true;
    QVector<IpInstanceParameterField> fields;
};

class IIpInstanceParameterAdapter {
public:
    virtual ~IIpInstanceParameterAdapter() = default;
    virtual QVector<IpInstanceParameterSection> parameterSections() const = 0;
};

class ManifestIpInstanceParameterAdapter final : public IIpInstanceParameterAdapter {
public:
    explicit ManifestIpInstanceParameterAdapter(PluginDescriptor plugin);
    QVector<IpInstanceParameterSection> parameterSections() const override;

private:
    PluginDescriptor m_plugin;
};
