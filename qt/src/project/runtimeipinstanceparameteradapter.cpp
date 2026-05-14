// RuntimeIpInstanceParameterAdapter exposes runtime-declared instance parameters to the UI.
#include "project/ipinstanceparameteradapter.h"

#include <QStringList>
#include <utility>

namespace {

QVector<IpInstanceParameterSection>
parameterSectionsFor(const QString& ipcoreId,
                     const QString& displayName,
                     const QHash<QString, IpCoreInstanceParameterDescriptor>& instanceParameters) {
    IpInstanceParameterSection section;
    section.ipcoreId = ipcoreId;
    section.instanceId = ipcoreId.section(QLatin1Char('.'), -1) + QStringLiteral("_0");
    section.id = QStringLiteral("global_parameters");
    section.label = displayName.isEmpty() ? ipcoreId : displayName;
    section.expandedByDefault = true;

    QStringList names = instanceParameters.keys();
    names.sort();
    for (const QString& name : names) {
        const IpCoreInstanceParameterDescriptor& descriptor = instanceParameters.value(name);
        IpInstanceParameterField field;
        field.name = descriptor.name;
        field.label = descriptor.label.isEmpty() ? descriptor.name : descriptor.label;
        field.description = descriptor.description;
        field.type = descriptor.type;
        field.defaultValue = descriptor.defaultValue;
        field.choices = descriptor.choices;
        field.configurable = descriptor.configurable;
        section.fields.push_back(field);
    }

    return section.fields.isEmpty() ? QVector<IpInstanceParameterSection>{}
                                    : QVector<IpInstanceParameterSection>{section};
}

} // namespace

RuntimeIpInstanceParameterAdapter::RuntimeIpInstanceParameterAdapter(IpCoreRuntimeDescriptor runtime)
    : m_runtime(std::move(runtime)) {}

QVector<IpInstanceParameterSection> RuntimeIpInstanceParameterAdapter::parameterSections() const {
    return parameterSectionsFor(m_runtime.id, m_runtime.name, m_runtime.instanceParameters);
}

CatalogIpInstanceParameterAdapter::CatalogIpInstanceParameterAdapter(
    QString ipcoreId,
    QString displayName,
    QHash<QString, IpCoreInstanceParameterDescriptor> instanceParameters)
    : m_ipcoreId(std::move(ipcoreId)),
      m_displayName(std::move(displayName)),
      m_instanceParameters(std::move(instanceParameters)) {}

QVector<IpInstanceParameterSection> CatalogIpInstanceParameterAdapter::parameterSections() const {
    return parameterSectionsFor(m_ipcoreId, m_displayName, m_instanceParameters);
}
