// RuntimeIpInstanceParameterAdapter exposes runtime-declared instance parameters to the UI.
#include "project/ipinstanceparameteradapter.h"

#include <QStringList>
#include <utility>

RuntimeIpInstanceParameterAdapter::RuntimeIpInstanceParameterAdapter(IpCoreRuntimeDescriptor runtime)
    : m_runtime(std::move(runtime)) {}

QVector<IpInstanceParameterSection> RuntimeIpInstanceParameterAdapter::parameterSections() const {
    IpInstanceParameterSection section;
    section.ipcoreId = m_runtime.id;
    section.instanceId = m_runtime.id.section(QLatin1Char('.'), -1) + QStringLiteral("_0");
    section.id = QStringLiteral("global_parameters");
    section.label = m_runtime.name.isEmpty() ? m_runtime.id : m_runtime.name;
    section.expandedByDefault = true;

    QStringList names = m_runtime.instanceParameters.keys();
    names.sort();
    for (const QString& name : names) {
        const IpCoreInstanceParameterDescriptor& descriptor = m_runtime.instanceParameters.value(name);
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
