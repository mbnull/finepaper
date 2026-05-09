// Legacy palette lists available module types and starts drag payloads for node creation.
#include "panels/palette.h"
#include "modules/moduleregistry.h"
#include "modules/moduletypemetadata.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>

// Custom list widget that supports drag-and-drop of module types
class DraggableListWidget : public QListWidget {
public:
    DraggableListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setDragEnabled(true);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragStartPos = event->pos();
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton)) return;
        if ((event->pos() - m_dragStartPos).manhattanLength() < 10) return;

        QListWidgetItem* item = currentItem();
        if (!item) return;

        QDrag* drag = new QDrag(this);
        QMimeData* mimeData = new QMimeData;
        mimeData->setData("application/x-moduletype", item->data(Qt::UserRole).toString().toUtf8());
        drag->setMimeData(mimeData);
        drag->exec(Qt::CopyAction);
    }

private:
    QPoint m_dragStartPos;
};

Palette::Palette(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    populateModuleTypes();
}

void Palette::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Module Types:"));

    m_listWidget = new DraggableListWidget(this);
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
