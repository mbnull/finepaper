// Legacy palette lists available module types while the IP Catalog replaces creation.
#include "panels/palette.h"
#include "modules/moduleregistry.h"
#include "modules/moduletypemetadata.h"
#include <QVBoxLayout>
#include <QLabel>

Palette::Palette(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    populateModuleTypes();
}

void Palette::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Module Types:"));

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);
}

void Palette::populateModuleTypes() {
    m_listWidget->clear();

    const QStringList types = m_activePluginId.isEmpty()
        ? ModuleRegistry::instance().availableTypes()
        : ModuleRegistry::instance().availableTypesForPlugin(m_activePluginId);

    for (const QString& type : types) {
        const ModuleType* moduleType = ModuleRegistry::instance().getType(type);
        auto* item = new QListWidgetItem(ModuleTypeMetadata::paletteLabel(moduleType));
        item->setData(Qt::UserRole, type);
        m_listWidget->addItem(item);
    }
}

void Palette::setActivePluginId(const QString& pluginId) {
    if (m_activePluginId == pluginId) {
        return;
    }
    m_activePluginId = pluginId;
    populateModuleTypes();
}
