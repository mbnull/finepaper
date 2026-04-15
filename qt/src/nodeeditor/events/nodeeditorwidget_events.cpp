// NodeEditorWidget event handlers: drag/drop, context menu, and interactive editing shortcuts.
#include "nodeeditorwidget.h"
#include "animatedgraphicsview.h"
#include "editorgraphmodel.h"
#include "nodeeditorentityfactory.h"
#include "commands/addmodulecommand.h"
#include "commands/setparametercommand.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QMouseEvent>

namespace {

QString draggedModuleType(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasFormat("application/x-moduletype")) {
        return {};
    }
    return QString::fromUtf8(mimeData->data("application/x-moduletype"));
}

} // namespace

void NodeEditorWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (m_graphModel->isEditingLocked()) {
        event->ignore();
        return;
    }

    const QString moduleType = draggedModuleType(event->mimeData());
    if (!moduleType.isEmpty()) {
        m_view->beginPaletteDrag(m_view->viewport()->mapFrom(this, event->position().toPoint()), moduleType);
        event->acceptProposedAction();
        return;
    }

    QWidget::dragEnterEvent(event);
}

void NodeEditorWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (m_graphModel->isEditingLocked()) {
        event->ignore();
        return;
    }

    const QString moduleType = draggedModuleType(event->mimeData());
    if (!moduleType.isEmpty()) {
        m_view->updatePaletteDrag(m_view->viewport()->mapFrom(this, event->position().toPoint()), moduleType);
        event->acceptProposedAction();
        return;
    }

    QWidget::dragMoveEvent(event);
}

void NodeEditorWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    m_view->endPaletteDrag();
    event->accept();
}

bool NodeEditorWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj != m_view->viewport()) {
        return QWidget::eventFilter(obj, event);
    }

    // Forward viewport events to keep drag/drop and custom mouse interactions
    // centralized in this widget instead of the child GraphicsView.
    switch (event->type()) {
    case QEvent::DragEnter:
        return handleViewportDragEnter(static_cast<QDragEnterEvent*>(event));
    case QEvent::DragMove:
        return handleViewportDragMove(static_cast<QDragMoveEvent*>(event));
    case QEvent::DragLeave:
        m_view->endPaletteDrag();
        return true;
    case QEvent::Drop:
        return handleViewportDrop(static_cast<QDropEvent*>(event));
    case QEvent::MouseButtonRelease:
        return handleViewportMouseRelease(static_cast<QMouseEvent*>(event));
    case QEvent::MouseButtonPress:
        return handleViewportMousePress(static_cast<QMouseEvent*>(event));
    case QEvent::MouseButtonDblClick:
        return handleViewportMouseDoubleClick(static_cast<QMouseEvent*>(event));
    case QEvent::ContextMenu:
        return handleViewportContextMenu(static_cast<QContextMenuEvent*>(event));
    default:
        break;
    }

    return QWidget::eventFilter(obj, event);
}

bool NodeEditorWidget::handleViewportDragEnter(QDragEnterEvent* event) {
    const QString moduleType = draggedModuleType(event->mimeData());
    if (moduleType.isEmpty()) {
        return false;
    }

    if (m_graphModel->isEditingLocked()) {
        event->ignore();
        return true;
    }

    m_view->beginPaletteDrag(event->position().toPoint(), moduleType);
    event->acceptProposedAction();
    return true;
}

bool NodeEditorWidget::handleViewportDragMove(QDragMoveEvent* event) {
    const QString moduleType = draggedModuleType(event->mimeData());
    if (moduleType.isEmpty()) {
        return false;
    }

    if (m_graphModel->isEditingLocked()) {
        event->ignore();
        return true;
    }

    m_view->updatePaletteDrag(event->position().toPoint(), moduleType);
    event->acceptProposedAction();
    return true;
}

bool NodeEditorWidget::handleViewportDrop(QDropEvent* event) {
    const QString moduleType = draggedModuleType(event->mimeData());
    if (moduleType.isEmpty()) {
        return false;
    }

    if (m_graphModel->isEditingLocked()) {
        m_view->endPaletteDrag();
        event->ignore();
        return true;
    }

    m_view->updatePaletteDrag(event->position().toPoint(), moduleType);
    dropEvent(event);
    return true;
}

bool NodeEditorWidget::handleViewportMouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }

    if (tryCompleteRouterDraftConnection(event->position().toPoint())) {
        return true;
    }

    return tryCompleteEndpointDraftConnection(event->position().toPoint());
}

bool NodeEditorWidget::handleViewportMousePress(QMouseEvent* event) {
    return event->button() == Qt::LeftButton &&
           tryToggleCollapsed(event->position().toPoint(), true);
}

bool NodeEditorWidget::handleViewportMouseDoubleClick(QMouseEvent* event) {
    return event->button() == Qt::LeftButton &&
           tryToggleCollapsed(event->position().toPoint(), false);
}

bool NodeEditorWidget::handleViewportContextMenu(QContextMenuEvent* event) {
    showNodeContextMenu(event->pos(), event->globalPos());
    return true;
}

void NodeEditorWidget::dropEvent(QDropEvent* event) {
    if (m_graphModel->isEditingLocked()) {
        m_view->endPaletteDrag();
        event->ignore();
        return;
    }

    const QString moduleType = draggedModuleType(event->mimeData());
    if (moduleType.isEmpty()) {
        m_view->endPaletteDrag();
        return;
    }

    const QString moduleId = NodeEditorEntityFactory::generateEntityId();
    auto module = NodeEditorEntityFactory::createModule(m_graph, moduleId, moduleType);
    if (!module) {
        m_view->endPaletteDrag();
        return;
    }

    auto command = std::make_unique<AddModuleCommand>(m_graph, std::move(module));
    m_commandManager->executeCommand(std::move(command));

    if (m_moduleToNodeId.contains(moduleId)) {
        const auto nodeId = m_moduleToNodeId.value(moduleId);
        const QPointF scenePos = clampNodePosition(nodeId, m_view->mapToScene(event->position().toPoint()));

        if (m_graph->getModule(moduleId)) {
            ++m_updatingFromGraph;
            m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);
            auto xCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "x", static_cast<int>(scenePos.x()));
            auto yCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "y", static_cast<int>(scenePos.y()));
            m_commandManager->executeCommand(std::move(xCmd));
            m_commandManager->executeCommand(std::move(yCmd));
            --m_updatingFromGraph;
        }
    }

    m_view->endPaletteDrag();
    event->acceptProposedAction();
}
