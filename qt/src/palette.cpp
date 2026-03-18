#include "palette.h"
#include "moduleregistry.h"
#include "commands/addmodulecommand.h"
#include "module.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <memory>

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
        mimeData->setData("application/x-moduletype", item->text().toUtf8());
        drag->setMimeData(mimeData);
        drag->exec(Qt::CopyAction);
    }

private:
    QPoint m_dragStartPos;
};

Palette::Palette(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent), m_graph(graph), m_commandManager(commandManager) {
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
    QStringList types = ModuleRegistry::instance().availableTypes();
    for (const QString& type : types) {
        m_listWidget->addItem(type);
    }
}
