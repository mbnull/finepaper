// ManifestPluginProjectAdapter exposes plugin.json instance_parameters as project parameters.
#include "plugins/pluginprojectadapter.h"

#include <QStringList>
#include <utility>

ManifestPluginProjectAdapter::ManifestPluginProjectAdapter(PluginDescriptor plugin)
    : m_plugin(std::move(plugin)) {}

QVector<PluginParameterSection> ManifestPluginProjectAdapter::parameterSections() const {
    PluginParameterSection section;
    section.pluginId = m_plugin.id;
    section.instanceId = m_plugin.id.section(QLatin1Char('.'), -1) + QStringLiteral("_0");
    section.id = QStringLiteral("global_parameters");
    section.label = m_plugin.name.isEmpty() ? m_plugin.id : m_plugin.name;
    section.expandedByDefault = true;

    QStringList names = m_plugin.instanceParameters.keys();
    names.sort();
    for (const QString& name : names) {
        const PluginInstanceParameterDescriptor& descriptor = m_plugin.instanceParameters.value(name);
        PluginParameterField field;
        field.name = descriptor.name;
        field.label = descriptor.label.isEmpty() ? descriptor.name : descriptor.label;
        field.description = descriptor.description;
        field.type = descriptor.type;
        field.defaultValue = descriptor.defaultValue;
        field.choices = descriptor.choices;
        field.configurable = descriptor.configurable;
        section.fields.push_back(field);
    }

    return section.fields.isEmpty() ? QVector<PluginParameterSection>{}
                                    : QVector<PluginParameterSection>{section};
}
