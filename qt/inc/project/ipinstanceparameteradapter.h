// IP instance parameter adapters expose runtime-declared project state to the core UI.
#pragma once

#include "graph/parameter.h"
#include "ipcore/ipcoreruntimedescriptor.h"

#include <QString>
#include <QVector>

struct IpInstanceParameterField {
    QString name;
    QString label;
    QString description;
    QString type;
    Parameter::Value defaultValue = QString();
    QVector<IpCoreInstanceParameterChoice> choices;
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

class RuntimeIpInstanceParameterAdapter final : public IIpInstanceParameterAdapter {
public:
    explicit RuntimeIpInstanceParameterAdapter(IpCoreRuntimeDescriptor runtime);
    QVector<IpInstanceParameterSection> parameterSections() const override;

private:
    IpCoreRuntimeDescriptor m_runtime;
};
