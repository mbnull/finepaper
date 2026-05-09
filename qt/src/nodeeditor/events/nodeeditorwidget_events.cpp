// NodeEditorWidget event handlers: drag/drop, context menu, and interactive editing shortcuts.
#include "nodeeditor/nodeeditorwidget.h"
#include "nodeeditor/animatedgraphicsview.h"
#include "nodeeditor/editorgraphmodel.h"
#include "nodeeditor/nodeeditorentityfactory.h"
#include "nodeeditor/graphnodemodel.h"
#include "modules/moduletypemetadata.h"
#include "commands/addmodulecommand.h"
#include "commands/setparametercommand.h"

#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/locateNode.hpp>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
constexpr auto ScopedModuleMime = "application/x-finepaper-module";
} // namespace

std::optional<NodeEditorWidget::ScopedModulePayload>
NodeEditorWidget::scopedModulePayload(const QMimeData* mimeData) const {
    if (!mimeData || !mimeData->hasFormat(ScopedModuleMime)) {
        return std::nullopt;
    }

    const QJsonDocument document = QJsonDocument::fromJson(mimeData->data(ScopedModuleMime));
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    ScopedModulePayload payload;
    payload.ipcoreId = object.value(QStringLiteral("ipcore")).toString();
    payload.instanceId = object.value(QStringLiteral("instance")).toString();
    payload.moduleType = object.value(QStringLiteral("type")).toString();
    if (payload.ipcoreId.isEmpty() || payload.instanceId.isEmpty() || payload.moduleType.isEmpty()) {
        return std::nullopt;
    }
    return payload;
}

void NodeEditorWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (m_graphModel->isEditingLocked()) {
        event->ignore();
        return;
    }

    const std::optional<ScopedModulePayload> payload = scopedModulePayload(event->mimeData());
    if (payload.has_value() && acceptsScopedModulePayload(*payload)) {
        m_view->beginModuleDrag(m_view->viewport()->mapFrom(this, event->position().toPoint()),
                                payload->moduleType);
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

    const std::optional<ScopedModulePayload> payload = scopedModulePayload(event->mimeData());
    if (payload.has_value() && acceptsScopedModulePayload(*payload)) {
        m_view->updateModuleDrag(m_view->viewport()->mapFrom(this, event->position().toPoint()),
                                 payload->moduleType);
        event->acceptProposedAction();
        return;
    }

    QWidget::dragMoveEvent(event);
}

void NodeEditorWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    m_view->endModuleDrag();
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
        m_view->endModuleDrag();
        return true;
    case QEvent::Drop:
        return handleViewportDrop(static_cast<QDropEvent*>(event));
    case QEvent::MouseButtonRelease:
        return handleViewportMouseRelease(static_cast<QMouseEvent*>(event));
    case QEvent::MouseButtonPress:
        return handleViewportMousePress(static_cast<QMouseEvent*>(event));
    case QEvent::MouseMove:
        return handleViewportMouseMove(static_cast<QMouseEvent*>(event));
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
    const std::optional<ScopedModulePayload> payload = scopedModulePayload(event->mimeData());
    if (!payload.has_value()) {
        return false;
    }

    if (m_graphModel->isEditingLocked() || !acceptsScopedModulePayload(*payload)) {
        event->ignore();
        return true;
    }

    m_view->beginModuleDrag(event->position().toPoint(), payload->moduleType);
    event->acceptProposedAction();
    return true;
}

bool NodeEditorWidget::handleViewportDragMove(QDragMoveEvent* event) {
    const std::optional<ScopedModulePayload> payload = scopedModulePayload(event->mimeData());
    if (!payload.has_value()) {
        return false;
    }

    if (m_graphModel->isEditingLocked() || !acceptsScopedModulePayload(*payload)) {
        event->ignore();
        return true;
    }

    m_view->updateModuleDrag(event->position().toPoint(), payload->moduleType);
    event->acceptProposedAction();
    return true;
}

bool NodeEditorWidget::handleViewportDrop(QDropEvent* event) {
    const std::optional<ScopedModulePayload> payload = scopedModulePayload(event->mimeData());
    if (!payload.has_value()) {
        return false;
    }

    if (m_graphModel->isEditingLocked() || !acceptsScopedModulePayload(*payload)) {
        m_view->endModuleDrag();
        event->ignore();
        return true;
    }

    m_view->updateModuleDrag(event->position().toPoint(), payload->moduleType);
    if (createModuleAt(*payload, m_view->mapToScene(event->position().toPoint()))) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
    m_view->endModuleDrag();
    return true;
}

bool NodeEditorWidget::handleViewportMouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }

    if (m_resize.active) {
        finishNodeResize();
        event->accept();
        return true;
    }

    if (tryCompleteDraftConnection(event->position().toPoint())) {
        return true;
    }

    return false;
}

bool NodeEditorWidget::handleViewportMousePress(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && tryBeginNodeResize(event->position().toPoint())) {
        event->accept();
        return true;
    }

    return event->button() == Qt::LeftButton &&
           tryToggleCollapsed(event->position().toPoint(), true);
}

bool NodeEditorWidget::handleViewportMouseMove(QMouseEvent* event) {
    if (!m_resize.active) {
        return false;
    }

    updateNodeResize(event->position().toPoint());
    event->accept();
    return true;
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
        m_view->endModuleDrag();
        event->ignore();
        return;
    }

    const std::optional<ScopedModulePayload> payload = scopedModulePayload(event->mimeData());
    if (!payload.has_value() || !acceptsScopedModulePayload(*payload)) {
        m_view->endModuleDrag();
        event->ignore();
        return;
    }

    const bool created = createModuleAt(*payload, m_view->mapToScene(event->position().toPoint()));
    m_view->endModuleDrag();
    if (created) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

QSize NodeEditorWidget::minimumNodeSize(QtNodes::NodeId nodeId) const {
    auto* model = graphNodeModel(nodeId);
    Module* module = model ? model->module() : nullptr;
    if (!module) {
        return QSize(80, 54);
    }

    const bool collapsed = model->isCollapsed();
    return {
        collapsed ? ModuleTypeMetadata::collapsedNodeMinWidth(module)
                  : ModuleTypeMetadata::expandedNodeMinWidth(module),
        collapsed ? ModuleTypeMetadata::collapsedNodeHeight(module)
                  : ModuleTypeMetadata::expandedNodeHeight(module)
    };
}

bool NodeEditorWidget::tryBeginNodeResize(const QPoint& viewportPos) {
    if (m_graphModel->isEditingLocked()) {
        return false;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* nodeGraphics = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!nodeGraphics) {
        return false;
    }

    const QPointF localPos = nodeGraphics->mapFromScene(scenePos);
    const QRect handleRect = m_scene->nodeGeometry().resizeHandleRect(nodeGraphics->nodeId());
    if (!handleRect.contains(localPos.toPoint())) {
        return false;
    }

    const QString moduleId = m_nodeToModuleId.value(nodeGraphics->nodeId());
    Module* module = moduleId.isEmpty() ? nullptr : m_graph->getModule(moduleId);
    if (!module) {
        return false;
    }

    m_resize = ResizeInteraction{};
    m_resize.active = true;
    m_resize.nodeId = nodeGraphics->nodeId();
    m_resize.moduleId = moduleId;
    m_resize.pressScenePos = scenePos;
    m_resize.startSize = m_scene->nodeGeometry().size(nodeGraphics->nodeId());
    m_resize.currentSize = m_resize.startSize;

    const auto& params = module->parameters();
    const auto widthIt = params.find(QStringLiteral("node_width"));
    const auto heightIt = params.find(QStringLiteral("node_height"));
    m_resize.hadWidth = widthIt != params.end();
    m_resize.hadHeight = heightIt != params.end();
    if (m_resize.hadWidth) {
        m_resize.oldWidth = widthIt.value().value();
    }
    if (m_resize.hadHeight) {
        m_resize.oldHeight = heightIt.value().value();
    }

    m_view->viewport()->setCursor(Qt::SizeFDiagCursor);
    return true;
}

void NodeEditorWidget::updateNodeResize(const QPoint& viewportPos) {
    if (!m_resize.active) {
        return;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    const QPointF delta = scenePos - m_resize.pressScenePos;
    const QSize minimum = minimumNodeSize(m_resize.nodeId);
    QSize nextSize(std::max(minimum.width(), static_cast<int>(std::lround(m_resize.startSize.width() + delta.x()))),
                   std::max(minimum.height(), static_cast<int>(std::lround(m_resize.startSize.height() + delta.y()))));
    if (nextSize == m_resize.currentSize) {
        return;
    }

    m_resize.currentSize = nextSize;
    applyTransientNodeSize(m_resize.moduleId, m_resize.nodeId, nextSize);
}

void NodeEditorWidget::finishNodeResize() {
    if (!m_resize.active) {
        return;
    }

    Module* module = m_graph->getModule(m_resize.moduleId);
    const QString moduleId = m_resize.moduleId;
    const QtNodes::NodeId nodeId = m_resize.nodeId;
    const QSize finalSize = m_resize.currentSize;

    if (module) {
        restoreResizeParameters(module);
        auto widthCommand = std::make_unique<SetParameterCommand>(
            m_graph, moduleId, QStringLiteral("node_width"), finalSize.width());
        auto heightCommand = std::make_unique<SetParameterCommand>(
            m_graph, moduleId, QStringLiteral("node_height"), finalSize.height());
        m_commandManager->executeCommand(std::move(widthCommand));
        m_commandManager->executeCommand(std::move(heightCommand));
    }

    m_resize = ResizeInteraction{};
    m_view->viewport()->unsetCursor();
    refreshNodeGraphics(nodeId, true);
    if (Module* resizedModule = m_graph->getModule(moduleId);
        resizedModule && ModuleTypeMetadata::supportsCollapse(resizedModule)) {
        refreshModulePresentation(moduleId);
    }
}

void NodeEditorWidget::cancelNodeResize() {
    if (!m_resize.active) {
        return;
    }

    if (Module* module = m_graph->getModule(m_resize.moduleId)) {
        restoreResizeParameters(module);
    }
    m_resize = ResizeInteraction{};
    m_view->viewport()->unsetCursor();
}

void NodeEditorWidget::applyTransientNodeSize(const QString& moduleId,
                                              QtNodes::NodeId nodeId,
                                              QSize const& size) {
    Module* module = m_graph->getModule(moduleId);
    if (!module) {
        return;
    }

    module->setParameter(QStringLiteral("node_width"), size.width());
    module->setParameter(QStringLiteral("node_height"), size.height());
    refreshNodeGraphics(nodeId, true);
    if (ModuleTypeMetadata::supportsCollapse(module)) {
        refreshModulePresentation(moduleId);
    }
}

void NodeEditorWidget::restoreResizeParameters(Module* module) {
    if (!module) {
        return;
    }

    if (m_resize.hadWidth) {
        module->setParameter(QStringLiteral("node_width"), m_resize.oldWidth);
    } else {
        module->removeParameter(QStringLiteral("node_width"));
    }

    if (m_resize.hadHeight) {
        module->setParameter(QStringLiteral("node_height"), m_resize.oldHeight);
    } else {
        module->removeParameter(QStringLiteral("node_height"));
    }
}
