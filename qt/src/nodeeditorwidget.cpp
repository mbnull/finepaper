// NodeEditorWidget — Qt widget that bridges the Graph data model and the
// QtNodes visual canvas. Listens to Graph signals to keep the canvas in sync,
// and translates QtNodes connection/deletion events back into Commands.
// m_updatingFromGraph guards against re-entrant signal loops when the widget
// itself drives a graph change.
#include "nodeeditorwidget.h"
#include "graphnodegeometry.h"
#include "graphnodemodel.h"
#include "graphnodepainter.h"
#include "modulelabels.h"
#include "moduleregistry.h"
#include "portlayout.h"
#include "straightconnectionpainter.h"
#include "commands/addmodulecommand.h"
#include "commands/addconnectioncommand.h"
#include "commands/removemodulecommand.h"
#include "commands/removeconnectioncommand.h"
#include "commands/setparametercommand.h"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/locateNode.hpp>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QVariantAnimation>
#include <QUuid>
#include <QGraphicsItem>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <optional>
#include <QSet>

class AnimatedGraphicsView final : public QtNodes::GraphicsView {
public:
    explicit AnimatedGraphicsView(QtNodes::BasicGraphicsScene* scene, QWidget* parent = nullptr)
        : QtNodes::GraphicsView(scene, parent),
          m_pulseAnimation(new QVariantAnimation(this)),
          m_fadeAnimation(new QVariantAnimation(this)) {
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

        m_pulseAnimation->setStartValue(0.0);
        m_pulseAnimation->setEndValue(1.0);
        m_pulseAnimation->setDuration(1400);
        m_pulseAnimation->setLoopCount(-1);
        connect(m_pulseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_pulsePhase = value.toDouble();
            if (m_overlayOpacity > 0.0) {
                viewport()->update();
            }
        });

        m_fadeAnimation->setDuration(180);
        m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_fadeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_overlayOpacity = value.toDouble();
            if (m_overlayOpacity <= 0.001 && !m_dragActive) {
                m_overlayOpacity = 0.0;
                m_pulseAnimation->stop();
            }
            viewport()->update();
        });
    }

    void beginPaletteDrag(const QPoint& viewportPos, const QString& moduleType) {
        m_dragPosition = viewportPos;
        if (!moduleType.isEmpty()) {
            m_moduleType = moduleType;
        }

        const bool needsFadeIn = !m_dragActive && m_overlayOpacity < 0.99;
        m_dragActive = true;

        if (m_pulseAnimation->state() != QAbstractAnimation::Running) {
            m_pulseAnimation->start();
        }
        if (needsFadeIn) {
            animateOverlayTo(1.0);
        } else {
            viewport()->update();
        }
    }

    void updatePaletteDrag(const QPoint& viewportPos, const QString& moduleType) {
        if (!m_dragActive) {
            beginPaletteDrag(viewportPos, moduleType);
            return;
        }

        m_dragPosition = viewportPos;
        if (!moduleType.isEmpty()) {
            m_moduleType = moduleType;
        }
        viewport()->update();
    }

    void endPaletteDrag() {
        if (!m_dragActive && m_overlayOpacity == 0.0) {
            return;
        }

        m_dragActive = false;
        animateOverlayTo(0.0);
    }

protected:
    void drawForeground(QPainter* painter, const QRectF& rect) override {
        QtNodes::GraphicsView::drawForeground(painter, rect);

        if (m_overlayOpacity <= 0.0 || m_moduleType.isEmpty()) {
            return;
        }

        painter->save();
        painter->resetTransform();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setOpacity(m_overlayOpacity);

        const QRectF viewportRect = viewport()->rect();
        const QRectF frameRect = viewportRect.adjusted(16.0, 16.0, -16.0, -16.0);

        QColor accent = palette().highlight().color();
        QColor textColor = palette().text().color();
        QColor surfaceColor = palette().base().color();

        QColor frameFill = accent;
        frameFill.setAlpha(18);
        QPen framePen(accent, 2.0);
        framePen.setStyle(Qt::DashLine);
        framePen.setDashPattern({8.0, 8.0});
        framePen.setDashOffset(-m_pulsePhase * 18.0);
        QColor frameStroke = accent;
        frameStroke.setAlpha(140);
        framePen.setColor(frameStroke);

        painter->setBrush(frameFill);
        painter->setPen(framePen);
        painter->drawRoundedRect(frameRect, 18.0, 18.0);

        const QPointF center(
            std::clamp(static_cast<qreal>(m_dragPosition.x()), frameRect.left() + 28.0, frameRect.right() - 28.0),
            std::clamp(static_cast<qreal>(m_dragPosition.y()), frameRect.top() + 28.0, frameRect.bottom() - 28.0));

        const qreal glowRadius = 78.0 + (m_pulsePhase * 22.0);
        QRadialGradient glow(center, glowRadius);
        QColor glowInner = accent;
        glowInner.setAlpha(84);
        QColor glowMid = accent;
        glowMid.setAlpha(24);
        QColor glowOuter = accent;
        glowOuter.setAlpha(0);
        glow.setColorAt(0.0, glowInner);
        glow.setColorAt(0.55, glowMid);
        glow.setColorAt(1.0, glowOuter);

        painter->setPen(Qt::NoPen);
        painter->setBrush(glow);
        painter->drawEllipse(center, glowRadius, glowRadius);

        QColor ringColor = accent;
        ringColor.setAlpha(static_cast<int>(170 - (m_pulsePhase * 70.0)));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(ringColor, 2.5));
        painter->drawEllipse(center, 26.0 + (m_pulsePhase * 16.0), 26.0 + (m_pulsePhase * 16.0));

        QColor coreColor = accent;
        coreColor.setAlpha(220);
        painter->setPen(Qt::NoPen);
        painter->setBrush(coreColor);
        painter->drawEllipse(center, 6.0, 6.0);

        QFont chipFont = font();
        chipFont.setBold(true);
        QFontMetrics chipMetrics(chipFont);
        const QSizeF chipSize(chipMetrics.horizontalAdvance(m_moduleType) + 52.0, chipMetrics.height() + 18.0);

        QPointF chipTopLeft = center + QPointF(24.0, -chipSize.height() - 10.0);
        chipTopLeft.setX(std::clamp(chipTopLeft.x(), frameRect.left(), frameRect.right() - chipSize.width()));
        chipTopLeft.setY(std::clamp(chipTopLeft.y(), frameRect.top(), frameRect.bottom() - chipSize.height()));

        const QRectF chipRect(chipTopLeft, chipSize);
        QColor chipFill = surfaceColor;
        chipFill.setAlpha(230);
        QColor chipStroke = accent;
        chipStroke.setAlpha(180);
        painter->setBrush(chipFill);
        painter->setPen(QPen(chipStroke, 1.5));
        painter->drawRoundedRect(chipRect, 14.0, 14.0);

        painter->setPen(Qt::NoPen);
        painter->setBrush(coreColor);
        painter->drawEllipse(QRectF(chipRect.left() + 14.0, chipRect.center().y() - 4.0, 8.0, 8.0));

        QColor chipText = textColor;
        chipText.setAlpha(230);
        painter->setFont(chipFont);
        painter->setPen(chipText);
        painter->drawText(chipRect.adjusted(30.0, 0.0, -14.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, m_moduleType);

        painter->restore();
    }

private:
    void animateOverlayTo(qreal targetOpacity) {
        m_fadeAnimation->stop();
        m_fadeAnimation->setStartValue(m_overlayOpacity);
        m_fadeAnimation->setEndValue(targetOpacity);
        m_fadeAnimation->start();
    }

    QPoint m_dragPosition;
    QString m_moduleType;
    QVariantAnimation* m_pulseAnimation;
    QVariantAnimation* m_fadeAnimation;
    qreal m_pulsePhase = 0.0;
    qreal m_overlayOpacity = 0.0;
    bool m_dragActive = false;
};

static std::optional<double> toDouble(const Parameter::Value& v) {
    if (auto* i = std::get_if<int>(&v)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&v)) return *d;
    return std::nullopt;
}

namespace {

int nextModuleIndex(const Graph* graph, const QString& moduleType, const QString& externalIdPrefix) {
    QSet<int> used;
    QRegularExpression pattern("^" + QRegularExpression::escape(externalIdPrefix) + "_(\\d+)$",
                               QRegularExpression::CaseInsensitiveOption);

    for (const auto& module : graph->modules()) {
        if (module->type() != moduleType) {
            continue;
        }

        const auto match = pattern.match(ModuleLabels::externalId(module.get()));
        if (match.hasMatch()) {
            used.insert(match.captured(1).toInt());
        }
    }

    int index = 0;
    while (used.contains(index)) {
        ++index;
    }
    return index;
}

void assignModuleIdentity(Graph* graph, Module* module) {
    if (!graph || !module) return;

    if (module->type() == "XP") {
        const int index = nextModuleIndex(graph, "XP", "xp");
        module->setParameter("display_name", QString("XP_%1").arg(index, 2, 10, QChar('0')));
        module->setParameter("external_id", QString("xp_%1").arg(index, 2, 10, QChar('0')));
    } else if (module->type() == "Endpoint") {
        const int index = nextModuleIndex(graph, "Endpoint", "ep");
        module->setParameter("display_name", QString("EP_%1").arg(index, 2, 10, QChar('0')));
        module->setParameter("external_id", QString("ep_%1").arg(index, 2, 10, QChar('0')));
    }
}

QString draggedModuleType(const QMimeData* mimeData) {
    if (!mimeData || !mimeData->hasFormat("application/x-moduletype")) {
        return {};
    }
    return QString::fromUtf8(mimeData->data("application/x-moduletype"));
}

bool boolParameter(const Module* module, const QString& name, bool fallbackValue) {
    if (!module) return fallbackValue;

    const auto it = module->parameters().find(name);
    if (it == module->parameters().end()) return fallbackValue;

    const Parameter::Value value = it.value().value();
    if (const auto* boolValue = std::get_if<bool>(&value)) {
        return *boolValue;
    }

    return fallbackValue;
}

bool isXpCollapsed(const Module* module) {
    return module && module->type() == "XP" && boolParameter(module, "collapsed", true);
}

bool isXpEndpointConnection(const Graph* graph,
                            const Connection& connection,
                            QString* xpModuleId = nullptr,
                            QString* endpointModuleId = nullptr) {
    if (!graph) return false;

    const Module* sourceModule = graph->getModule(connection.source().moduleId);
    const Module* targetModule = graph->getModule(connection.target().moduleId);
    if (!sourceModule || !targetModule) return false;
    if (sourceModule->type() != "XP" || targetModule->type() != "Endpoint") return false;
    if (!PortLayout::isEndpointPortId(connection.source().portId)) return false;

    if (xpModuleId) *xpModuleId = sourceModule->id();
    if (endpointModuleId) *endpointModuleId = targetModule->id();
    return true;
}

const Port* findPort(const Module* module, const QString& portId) {
    if (!module) return nullptr;

    for (const auto& port : module->ports()) {
        if (port.id() == portId) {
            return &port;
        }
    }

    return nullptr;
}

constexpr qreal kCanvasHalfExtent = 2000.0;
const QRectF kCanvasRect(-kCanvasHalfExtent, -kCanvasHalfExtent,
                         kCanvasHalfExtent * 2.0, kCanvasHalfExtent * 2.0);
constexpr int kConnectedHighlightDataRole = 1;

QString firstAvailablePort(const Graph* graph,
                           const Module* module,
                           Port::Direction direction,
                           const std::function<bool(const Port&)>& predicate) {
    if (!graph || !module) return {};

    for (const auto& port : module->ports()) {
        if (port.direction() != direction || !predicate(port)) {
            continue;
        }

        const bool occupied = std::any_of(graph->connections().begin(), graph->connections().end(),
            [&](const std::unique_ptr<Connection>& connection) {
                const PortRef& ref = direction == Port::Direction::Output
                    ? connection->source()
                    : connection->target();
                return ref.moduleId == module->id() && ref.portId == port.id();
            });

        if (!occupied) {
            return port.id();
        }
    }

    return {};
}

} // namespace

NodeEditorWidget::NodeEditorWidget(Graph* graph, CommandManager* commandManager, QWidget* parent)
    : QWidget(parent),
      m_graph(graph),
      m_commandManager(commandManager),
      m_canvasRect(kCanvasRect) {

    m_registry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    m_registry->registerModel<GraphNodeModel>("GraphNode");

    m_graphModel = new QtNodes::DataFlowGraphModel(m_registry);
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);
    m_scene->setSceneRect(m_canvasRect);
    m_scene->setNodeGeometry(std::make_unique<GraphNodeGeometry>(*m_graphModel));
    m_scene->setNodePainter(std::make_unique<GraphNodePainter>());
    m_scene->setConnectionPainter(std::make_unique<StraightConnectionPainter>());
    m_view = new AnimatedGraphicsView(m_scene);
    m_view->setSceneRect(m_canvasRect);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    setAcceptDrops(true);
    m_view->viewport()->setAcceptDrops(true);
    m_view->viewport()->installEventFilter(this);

    connect(m_graph, &Graph::moduleAdded, this, &NodeEditorWidget::onModuleAdded);
    connect(m_graph, &Graph::moduleRemoved, this, &NodeEditorWidget::onModuleRemoved);
    connect(m_graph, &Graph::connectionAdded, this, &NodeEditorWidget::onConnectionAdded);
    connect(m_graph, &Graph::connectionRemoved, this, &NodeEditorWidget::onConnectionRemoved);

    connect(m_graphModel, &QtNodes::DataFlowGraphModel::connectionCreated, this, &NodeEditorWidget::onConnectionCreated);
    connect(m_graphModel, &QtNodes::DataFlowGraphModel::connectionDeleted, this, &NodeEditorWidget::onConnectionDeleted);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &NodeEditorWidget::onSelectionChanged);
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeMoved, this, &NodeEditorWidget::onNodeMoved);

    for (const auto& module : m_graph->modules()) {
        onModuleAdded(module.get());
    }
    for (const auto& connection : m_graph->connections()) {
        onConnectionAdded(connection.get());
    }

    refreshAllXpPresentations();
}

void NodeEditorWidget::onModuleAdded(Module* module) {
    connect(module, &Module::parameterChanged, this, &NodeEditorWidget::onParameterChanged, Qt::UniqueConnection);
    ensureModuleInView(module);
}

void NodeEditorWidget::ensureModuleInView(Module* module) {
    if (!module || m_moduleToNodeId.contains(module->id())) {
        return;
    }

    ++m_updatingFromGraph;
    auto nodeId = m_graphModel->addNode("GraphNode");
    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (nodeModel) {
        nodeModel->setModule(module);
    }
    m_moduleToNodeId[module->id()] = nodeId;
    m_nodeToModuleId[nodeId] = module->id();

    const auto& params = module->parameters();
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    if (xIt != params.end() && yIt != params.end()) {
        const auto& xValue = xIt.value().value();
        const auto& yValue = yIt.value().value();
        auto xOpt = toDouble(xValue);
        auto yOpt = toDouble(yValue);
        if (xOpt && yOpt) {
            const QPointF clampedPos = clampNodePosition(nodeId, QPointF(*xOpt, *yOpt));
            m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, clampedPos);
        }
    }
    --m_updatingFromGraph;
}

void NodeEditorWidget::onModuleRemoved(const QString& moduleId) {
    removeModuleFromView(moduleId);
}

void NodeEditorWidget::removeModuleFromView(const QString& moduleId) {
    auto it = m_moduleToNodeId.find(moduleId);
    if (it != m_moduleToNodeId.end()) {
        ++m_updatingFromGraph;
        m_nodeToModuleId.remove(it.value());
        m_graphModel->deleteNode(it.value());
        m_moduleToNodeId.erase(it);
        --m_updatingFromGraph;
    }
}

void NodeEditorWidget::onConnectionAdded(Connection* connection) {
    ensureConnectionInView(connection);

    QString xpModuleId;
    if (isXpEndpointConnection(m_graph, *connection, &xpModuleId)) {
        refreshXpPresentation(xpModuleId);
    }
}

bool NodeEditorWidget::ensureConnectionInView(Connection* connection) {
    if (!connection) {
        return false;
    }

    auto srcNodeIt = m_moduleToNodeId.find(connection->source().moduleId);
    auto tgtNodeIt = m_moduleToNodeId.find(connection->target().moduleId);
    if (srcNodeIt == m_moduleToNodeId.end() || tgtNodeIt == m_moduleToNodeId.end()) {
        m_pendingConnections.clear();
        return false;
    }

    QtNodes::NodeId srcNodeId = srcNodeIt.value();
    QtNodes::NodeId tgtNodeId = tgtNodeIt.value();

    auto* srcModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(srcNodeId));
    auto* tgtModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(tgtNodeId));
    if (!srcModel || !tgtModel) {
        m_pendingConnections.clear();
        return false;
    }

    const QtNodes::PortIndex srcPortIdx = srcModel->portIndex(connection->source().portId, QtNodes::PortType::Out);
    const QtNodes::PortIndex tgtPortIdx = tgtModel->portIndex(connection->target().portId, QtNodes::PortType::In);
    if (srcPortIdx == QtNodes::InvalidPortIndex || tgtPortIdx == QtNodes::InvalidPortIndex) {
        return false;
    }

    QtNodes::ConnectionId connId{srcNodeId, srcPortIdx, tgtNodeId, tgtPortIdx};
    m_pendingRemovals.remove(connId);

    auto existingIt = m_connectionToQtId.find(connection->id());
    if (existingIt != m_connectionToQtId.end() && m_graphModel->connectionExists(existingIt.value())) {
        return true;
    }
    if (existingIt != m_connectionToQtId.end()) {
        m_connectionToQtId.erase(existingIt);
    }

    if (m_pendingConnections.contains(connId)) {
        m_pendingConnections.remove(connId);
        m_connectionToQtId[connection->id()] = connId;
        return true;
    }

    ++m_updatingFromGraph;
    m_graphModel->addConnection(connId);
    m_connectionToQtId[connection->id()] = connId;
    --m_updatingFromGraph;
    setConnectionHighlighted(connId, false);

    const auto selectedNodes = m_scene->selectedNodes();
    if (!selectedNodes.empty()) {
        updateConnectedConnectionHighlights(*selectedNodes.begin());
    }
    return true;
}

void NodeEditorWidget::onConnectionRemoved(const QString& connectionId) {
    removeConnectionFromView(connectionId);
}

void NodeEditorWidget::removeConnectionFromView(const QString& connectionId) {
    auto it = m_connectionToQtId.find(connectionId);
    if (it == m_connectionToQtId.end()) {
        return;
    }

    const QtNodes::ConnectionId qtConnectionId = it.value();
    m_connectionToQtId.erase(it);
    m_pendingConnections.remove(qtConnectionId);
    m_pendingRemovals.remove(qtConnectionId);

    if (!m_graphModel->connectionExists(qtConnectionId)) {
        return;
    }

    ++m_updatingFromGraph;
    m_graphModel->deleteConnection(qtConnectionId);
    --m_updatingFromGraph;
}

void NodeEditorWidget::dragEnterEvent(QDragEnterEvent* event) {
    const QString moduleType = draggedModuleType(event->mimeData());
    if (!moduleType.isEmpty()) {
        m_view->beginPaletteDrag(m_view->viewport()->mapFrom(this, event->position().toPoint()), moduleType);
        event->acceptProposedAction();
        return;
    }

    QWidget::dragEnterEvent(event);
}

void NodeEditorWidget::dragMoveEvent(QDragMoveEvent* event) {
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
    if (obj == m_view->viewport()) {
        // Forward drag/drop events from the viewport to our handlers.
        // Without this, the GraphicsView child widget consumes all drag events
        // and our dragEnterEvent/dropEvent on NodeEditorWidget never fire.
        if (event->type() == QEvent::DragEnter) {
            auto* e = static_cast<QDragEnterEvent*>(event);
            const QString moduleType = draggedModuleType(e->mimeData());
            if (!moduleType.isEmpty()) {
                m_view->beginPaletteDrag(e->position().toPoint(), moduleType);
                e->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto* e = static_cast<QDragMoveEvent*>(event);
            const QString moduleType = draggedModuleType(e->mimeData());
            if (!moduleType.isEmpty()) {
                m_view->updatePaletteDrag(e->position().toPoint(), moduleType);
                e->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragLeave) {
            m_view->endPaletteDrag();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto* e = static_cast<QDropEvent*>(event);
            const QString moduleType = draggedModuleType(e->mimeData());
            if (!moduleType.isEmpty()) {
                m_view->updatePaletteDrag(e->position().toPoint(), moduleType);
                dropEvent(e);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton &&
                tryCompleteXpRouterDraftConnection(e->position().toPoint())) {
                return true;
            }
            if (e->button() == Qt::LeftButton &&
                tryCompleteXpEndpointDraftConnection(e->position().toPoint())) {
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton &&
                tryToggleXpCollapsed(e->position().toPoint(), true)) {
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton &&
                tryToggleXpCollapsed(e->position().toPoint(), false)) {
                return true;
            }
        }
        if (event->type() == QEvent::ContextMenu) {
            auto* e = static_cast<QContextMenuEvent*>(event);
            if (showNodeContextMenu(e->pos(), e->globalPos())) {
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void NodeEditorWidget::dropEvent(QDropEvent* event) {
    const QString moduleType = draggedModuleType(event->mimeData());
    if (moduleType.isEmpty()) {
        m_view->endPaletteDrag();
        return;
    }

    const ModuleType* type = ModuleRegistry::instance().getType(moduleType);
    if (!type) {
        m_view->endPaletteDrag();
        return;
    }

    QString moduleId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
    auto module = std::make_unique<Module>(moduleId, moduleType);

    for (const auto& port : type->defaultPorts) {
        module->addPort(port);
    }
    for (auto it = type->defaultParameters.constBegin(); it != type->defaultParameters.constEnd(); ++it) {
        module->setParameter(it.key(), it.value().value());
    }
    assignModuleIdentity(m_graph, module.get());

    auto command = std::make_unique<AddModuleCommand>(m_graph, std::move(module));
    m_commandManager->executeCommand(std::move(command));

    if (m_moduleToNodeId.contains(moduleId)) {
        auto nodeId = m_moduleToNodeId.value(moduleId);
        const QPointF scenePos = clampNodePosition(nodeId, m_view->mapToScene(event->position().toPoint()));

        Module* module = m_graph->getModule(moduleId);
        if (module) {
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

void NodeEditorWidget::onConnectionCreated(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;

    m_pendingConnections.insert(connectionId);

    PortRef source;
    PortRef target;
    if (!resolveConnectionPorts(connectionId, source, target)) {
        m_pendingConnections.remove(connectionId);
        ++m_updatingFromGraph;
        m_graphModel->deleteConnection(connectionId);
        --m_updatingFromGraph;
        return;
    }

    if (!m_graph->isValidConnection(source, target)) {
        m_pendingConnections.remove(connectionId);
        ++m_updatingFromGraph;
        m_graphModel->deleteConnection(connectionId);
        --m_updatingFromGraph;
        return;
    }

    QString connId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
    auto connection = std::make_unique<Connection>(connId, source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph, std::move(connection));
    m_commandManager->executeCommand(std::move(command));
}

void NodeEditorWidget::onConnectionDeleted(QtNodes::ConnectionId connectionId) {
    if (m_updatingFromGraph > 0) return;

    if (m_pendingRemovals.contains(connectionId)) {
        m_pendingRemovals.remove(connectionId);
        return;
    }

    for (auto it = m_connectionToQtId.begin(); it != m_connectionToQtId.end(); ++it) {
        if (it.value() == connectionId) {
            auto command = std::make_unique<RemoveConnectionCommand>(m_graph, it.key());
            m_commandManager->executeCommand(std::move(command));
            break;
        }
    }
}

void NodeEditorWidget::onSelectionChanged() {
    auto selectedNodes = m_scene->selectedNodes();
    if (!selectedNodes.empty()) {
        auto nodeId = *selectedNodes.begin();
        updateConnectedConnectionHighlights(nodeId);
        QString moduleId = m_nodeToModuleId.value(nodeId);
        if (!moduleId.isEmpty()) {
            emit moduleSelected(moduleId);
            return;
        }
    }
    updateConnectedConnectionHighlights(QtNodes::InvalidNodeId);
    emit moduleSelected(QString());
}

QString NodeEditorWidget::getPortId(QtNodes::NodeId nodeId, QtNodes::PortType portType, QtNodes::PortIndex portIndex) const {
    auto* model = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeId));
    if (!model || !model->module()) return "";

    const Port* port = model->portAt(portType, portIndex);
    return port ? port->id() : "";
}

bool NodeEditorWidget::resolveConnectionPorts(QtNodes::ConnectionId connectionId, PortRef& source, PortRef& target) const {
    source.moduleId = m_nodeToModuleId.value(connectionId.outNodeId);
    target.moduleId = m_nodeToModuleId.value(connectionId.inNodeId);
    if (source.moduleId.isEmpty() || target.moduleId.isEmpty()) {
        return false;
    }

    source.portId = getPortId(connectionId.outNodeId, QtNodes::PortType::Out, connectionId.outPortIndex);
    target.portId = getPortId(connectionId.inNodeId, QtNodes::PortType::In, connectionId.inPortIndex);
    if (source.portId.isEmpty() || target.portId.isEmpty()) {
        return false;
    }

    return true;
}

QtNodes::ConnectionGraphicsObject* NodeEditorWidget::findDraftConnection() const {
    const auto items = m_scene->items();
    for (QGraphicsItem* item : items) {
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (connection && connection->connectionState().requiresPort()) {
            return connection;
        }
    }

    return nullptr;
}

void NodeEditorWidget::setConnectionHighlighted(QtNodes::ConnectionId connectionId, bool highlighted) {
    auto* connectionGraphics = m_scene->connectionGraphicsObject(connectionId);
    if (!connectionGraphics) {
        return;
    }

    connectionGraphics->setData(kConnectedHighlightDataRole, highlighted);
    connectionGraphics->update();
}

void NodeEditorWidget::updateConnectedConnectionHighlights(QtNodes::NodeId selectedNodeId) {
    for (auto it = m_connectionToQtId.cbegin(); it != m_connectionToQtId.cend(); ++it) {
        const QtNodes::ConnectionId connectionId = it.value();
        const bool highlighted = selectedNodeId != QtNodes::InvalidNodeId &&
                                 (connectionId.outNodeId == selectedNodeId || connectionId.inNodeId == selectedNodeId);
        setConnectionHighlighted(connectionId, highlighted);
    }
}

bool NodeEditorWidget::tryToggleXpCollapsed(const QPoint& viewportPos, bool requireToggleButton) {
    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* nodeGraphics = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!nodeGraphics) {
        return false;
    }

    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeGraphics->nodeId()));
    if (!nodeModel || !nodeModel->module() || nodeModel->module()->type() != "XP") {
        return false;
    }

    const QSize nodeSize = m_scene->nodeGeometry().size(nodeGraphics->nodeId());
    const QPointF localPos = nodeGraphics->mapFromScene(scenePos);
    if (requireToggleButton && !GraphNodeGeometry::xpToggleButtonRect(nodeSize).contains(localPos)) {
        return false;
    }

    toggleXpCollapsed(nodeModel->module()->id(), !nodeModel->isXpCollapsed());
    return true;
}

void NodeEditorWidget::toggleXpCollapsed(const QString& moduleId, bool collapsed) {
    auto command = std::make_unique<SetParameterCommand>(m_graph, moduleId, "collapsed", collapsed);
    m_commandManager->executeCommand(std::move(command));
}

bool NodeEditorWidget::resolveXpRouterDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                                      QtNodes::NodeId targetNodeId,
                                                      PortRef& source,
                                                      PortRef& target) const {
    const QtNodes::ConnectionId connectionId = draftConnection.connectionId();
    const QtNodes::PortType requiredPort = draftConnection.connectionState().requiredPort();
    if (requiredPort == QtNodes::PortType::None) {
        return false;
    }

    const bool startFromOutput = requiredPort == QtNodes::PortType::In;
    const QtNodes::NodeId startNodeId = startFromOutput ? connectionId.outNodeId : connectionId.inNodeId;
    const QtNodes::PortIndex startPortIndex = startFromOutput ? connectionId.outPortIndex : connectionId.inPortIndex;
    const QtNodes::PortType startPortType = startFromOutput ? QtNodes::PortType::Out : QtNodes::PortType::In;
    if (startNodeId == QtNodes::InvalidNodeId || startPortIndex == QtNodes::InvalidPortIndex) {
        return false;
    }
    if (startNodeId == targetNodeId) {
        return false;
    }

    const QString startModuleId = m_nodeToModuleId.value(startNodeId);
    const QString targetModuleId = m_nodeToModuleId.value(targetNodeId);
    if (startModuleId.isEmpty() || targetModuleId.isEmpty()) {
        return false;
    }

    const Module* startModule = m_graph->getModule(startModuleId);
    const Module* endModule = m_graph->getModule(targetModuleId);
    if (!startModule || !endModule || startModule->type() != "XP" || endModule->type() != "XP") {
        return false;
    }

    const QString startPortId = getPortId(startNodeId, startPortType, startPortIndex);
    if (!PortLayout::isDirectionalRouterPortId(startPortId)) {
        return false;
    }

    const QString startSide = PortLayout::routerSideId(startPortId);
    const QString oppositeSide = PortLayout::oppositeRouterSide(startSide);
    if (startSide.isEmpty() || oppositeSide.isEmpty()) {
        return false;
    }

    if (startFromOutput) {
        source = PortRef{startModuleId, PortLayout::routerOutputPortId(startSide)};
        target = PortRef{targetModuleId, PortLayout::routerInputPortId(oppositeSide)};
    } else {
        source = PortRef{targetModuleId, PortLayout::routerOutputPortId(oppositeSide)};
        target = PortRef{startModuleId, PortLayout::routerInputPortId(startSide)};
    }

    return true;
}

bool NodeEditorWidget::tryCompleteXpRouterDraftConnection(const QPoint& viewportPos) {
    auto* draftConnection = findDraftConnection();
    if (!draftConnection) {
        return false;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* targetNode = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!targetNode) {
        return false;
    }

    PortRef source;
    PortRef target;
    if (!resolveXpRouterDraftConnection(*draftConnection, targetNode->nodeId(), source, target)) {
        return false;
    }

    m_scene->resetDraftConnection();

    if (!m_graph->isValidConnection(source, target)) {
        return true;
    }

    const QString connId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
    auto connection = std::make_unique<Connection>(connId, source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph, std::move(connection));
    m_commandManager->executeCommand(std::move(command));
    return true;
}

bool NodeEditorWidget::resolveXpEndpointDraftConnection(const QtNodes::ConnectionGraphicsObject& draftConnection,
                                                        QtNodes::NodeId targetNodeId,
                                                        PortRef& source,
                                                        PortRef& target) const {
    const QtNodes::ConnectionId connectionId = draftConnection.connectionId();
    const QtNodes::PortType requiredPort = draftConnection.connectionState().requiredPort();
    if (requiredPort == QtNodes::PortType::None) {
        return false;
    }

    const bool startFromOutput = requiredPort == QtNodes::PortType::In;
    const QtNodes::NodeId startNodeId = startFromOutput ? connectionId.outNodeId : connectionId.inNodeId;
    const QtNodes::PortIndex startPortIndex = startFromOutput ? connectionId.outPortIndex : connectionId.inPortIndex;
    const QtNodes::PortType startPortType = startFromOutput ? QtNodes::PortType::Out : QtNodes::PortType::In;
    if (startNodeId == QtNodes::InvalidNodeId || startPortIndex == QtNodes::InvalidPortIndex) {
        return false;
    }
    if (startNodeId == targetNodeId) {
        return false;
    }

    const QString startModuleId = m_nodeToModuleId.value(startNodeId);
    const QString targetModuleId = m_nodeToModuleId.value(targetNodeId);
    if (startModuleId.isEmpty() || targetModuleId.isEmpty()) {
        return false;
    }

    const Module* startModule = m_graph->getModule(startModuleId);
    const Module* endModule = m_graph->getModule(targetModuleId);
    if (!startModule || !endModule) {
        return false;
    }

    const QString startPortId = getPortId(startNodeId, startPortType, startPortIndex);
    const Port* startPort = findPort(startModule, startPortId);
    if (!startPort || startPort->type() != "endpoint") {
        return false;
    }

    if (startFromOutput) {
        if (startModule->type() != "XP" || endModule->type() != "Endpoint") {
            return false;
        }

        const QString endpointPortId = firstAvailablePort(m_graph, endModule, Port::Direction::Input,
            [](const Port& port) { return port.type() == "endpoint"; });
        if (endpointPortId.isEmpty()) {
            return false;
        }

        source = PortRef{startModuleId, startPortId};
        target = PortRef{targetModuleId, endpointPortId};
        return true;
    }

    if (startModule->type() != "Endpoint" || endModule->type() != "XP") {
        return false;
    }

    const QString xpPortId = firstAvailablePort(m_graph, endModule, Port::Direction::Output,
        [](const Port& port) { return PortLayout::isEndpointPort(port); });
    if (xpPortId.isEmpty()) {
        return false;
    }

    source = PortRef{targetModuleId, xpPortId};
    target = PortRef{startModuleId, startPortId};
    return true;
}

bool NodeEditorWidget::tryCompleteXpEndpointDraftConnection(const QPoint& viewportPos) {
    auto* draftConnection = findDraftConnection();
    if (!draftConnection) {
        return false;
    }

    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* targetNode = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!targetNode) {
        return false;
    }

    PortRef source;
    PortRef target;
    if (!resolveXpEndpointDraftConnection(*draftConnection, targetNode->nodeId(), source, target)) {
        return false;
    }

    m_scene->resetDraftConnection();

    if (!m_graph->isValidConnection(source, target)) {
        return true;
    }

    const QString connId = QUuid::createUuid().toString(QUuid::WithoutBraces).replace('-', '_');
    auto connection = std::make_unique<Connection>(connId, source, target);
    auto command = std::make_unique<AddConnectionCommand>(m_graph, std::move(connection));
    m_commandManager->executeCommand(std::move(command));
    return true;
}

void NodeEditorWidget::highlightElement(const QString& elementId) {
    m_scene->clearSelection();

    auto nodeIt = m_moduleToNodeId.find(elementId);
    if (nodeIt != m_moduleToNodeId.end()) {
        auto nodeGraphics = m_scene->nodeGraphicsObject(nodeIt.value());
        if (nodeGraphics) {
            nodeGraphics->setSelected(true);
            m_view->centerOn(nodeGraphics);
        }
        return;
    }

    auto connIt = m_connectionToQtId.find(elementId);
    if (connIt != m_connectionToQtId.end()) {
        auto connGraphics = m_scene->connectionGraphicsObject(connIt.value());
        if (connGraphics) {
            connGraphics->setSelected(true);
            m_view->centerOn(connGraphics);
        }
    }
}

void NodeEditorWidget::onNodeMoved(QtNodes::NodeId nodeId) {
    if (m_updatingFromGraph > 0) return;

    QString moduleId = m_nodeToModuleId.value(nodeId);
    if (moduleId.isEmpty()) return;

    QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).value<QPointF>();
    const QPointF clampedPos = clampNodePosition(nodeId, pos);
    if (clampedPos != pos) {
        ++m_updatingFromGraph;
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, clampedPos);
        --m_updatingFromGraph;
        pos = clampedPos;
    }

    auto xCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "x", static_cast<int>(pos.x()));
    auto yCmd = std::make_unique<SetParameterCommand>(m_graph, moduleId, "y", static_cast<int>(pos.y()));
    m_commandManager->executeCommand(std::move(xCmd));
    m_commandManager->executeCommand(std::move(yCmd));
}

void NodeEditorWidget::onParameterChanged(const QString& paramName) {
    Module* module = qobject_cast<Module*>(sender());
    if (!module) return;

    auto nodeIt = m_moduleToNodeId.find(module->id());
    if (nodeIt == m_moduleToNodeId.end()) return;

    if (paramName == "display_name") {
        if (auto* nodeGraphics = m_scene->nodeGraphicsObject(nodeIt.value())) {
            nodeGraphics->setGeometryChanged();
            nodeGraphics->update();
        }
        return;
    }

    if (paramName == "collapsed") {
        if (auto* nodeGraphics = m_scene->nodeGraphicsObject(nodeIt.value())) {
            nodeGraphics->setGeometryChanged();
            nodeGraphics->moveConnections();
            nodeGraphics->update();
        }
        refreshXpPresentation(module->id());
        return;
    }

    if (paramName != "x" && paramName != "y") return;

    const auto& params = module->parameters();
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    if (xIt == params.end() || yIt == params.end()) return;

    ++m_updatingFromGraph;
    const auto& xValue = xIt.value().value();
    const auto& yValue = yIt.value().value();
    auto xOpt = toDouble(xValue);
    auto yOpt = toDouble(yValue);
    if (xOpt && yOpt) {
        const QPointF clampedPos = clampNodePosition(nodeIt.value(), QPointF(*xOpt, *yOpt));
        m_graphModel->setNodeData(nodeIt.value(), QtNodes::NodeRole::Position, clampedPos);
    }
    --m_updatingFromGraph;
}

bool NodeEditorWidget::showNodeContextMenu(const QPoint& viewportPos, const QPoint& globalPos) {
    const QPointF scenePos = m_view->mapToScene(viewportPos);
    auto* nodeGraphics = QtNodes::locateNodeAt(scenePos, *m_scene, m_view->transform());
    if (!nodeGraphics) {
        return false;
    }

    const QString moduleId = m_nodeToModuleId.value(nodeGraphics->nodeId());
    if (moduleId.isEmpty()) {
        return false;
    }

    auto* nodeModel = dynamic_cast<GraphNodeModel*>(m_graphModel->delegateModel<GraphNodeModel>(nodeGraphics->nodeId()));
    Module* module = nodeModel ? nodeModel->module() : nullptr;
    if (!module) {
        return false;
    }

    QMenu menu(m_view);

    QAction* toggleAction = nullptr;
    if (module->type() == "XP") {
        toggleAction = menu.addAction(nodeModel->isXpCollapsed() ? "Expand Node" : "Collapse Node");
    }

    QAction* deleteAction = menu.addAction("Delete Node");

    QAction* selectedAction = menu.exec(globalPos);
    if (!selectedAction) {
        return true;
    }

    if (selectedAction == toggleAction) {
        toggleXpCollapsed(moduleId, !nodeModel->isXpCollapsed());
        return true;
    }

    if (selectedAction == deleteAction) {
        auto command = std::make_unique<RemoveModuleCommand>(m_graph, moduleId);
        m_commandManager->executeCommand(std::move(command));
        return true;
    }

    return true;
}

QPointF NodeEditorWidget::clampNodePosition(QtNodes::NodeId nodeId, const QPointF& position) const {
    QSize nodeSize;
    if (nodeId != QtNodes::InvalidNodeId) {
        nodeSize = m_scene->nodeGeometry().size(nodeId);
    }

    const qreal maxX = m_canvasRect.right() - nodeSize.width();
    const qreal maxY = m_canvasRect.bottom() - nodeSize.height();
    return {
        std::clamp(position.x(), m_canvasRect.left(), maxX),
        std::clamp(position.y(), m_canvasRect.top(), maxY)
    };
}

void NodeEditorWidget::refreshXpPresentation(const QString& xpModuleId) {
    Module* xpModule = m_graph->getModule(xpModuleId);
    if (!xpModule || xpModule->type() != "XP") {
        return;
    }

    if (auto xpNodeIt = m_moduleToNodeId.find(xpModuleId); xpNodeIt != m_moduleToNodeId.end()) {
        if (auto* xpNode = m_scene->nodeGraphicsObject(xpNodeIt.value())) {
            xpNode->setGeometryChanged();
            xpNode->moveConnections();
            xpNode->update();
        }
    }

    QList<Connection*> xpConnections;
    QList<Connection*> xpEndpointConnections;
    QSet<QString> endpointModuleIds;
    for (const auto& connection : m_graph->connections()) {
        if (connection->source().moduleId != xpModuleId && connection->target().moduleId != xpModuleId) {
            continue;
        }

        xpConnections.append(connection.get());

        QString connectionXpId;
        QString endpointModuleId;
        if (!isXpEndpointConnection(m_graph, *connection, &connectionXpId, &endpointModuleId) ||
            connectionXpId != xpModuleId) {
            continue;
        }

        xpEndpointConnections.append(connection.get());
        endpointModuleIds.insert(endpointModuleId);
    }

    for (Connection* connection : xpConnections) {
        removeConnectionFromView(connection->id());
    }

    const bool collapsed = isXpCollapsed(xpModule);

    if (collapsed) {
        for (const QString& endpointModuleId : endpointModuleIds) {
            removeModuleFromView(endpointModuleId);
        }
        for (Connection* connection : xpConnections) {
            if (!xpEndpointConnections.contains(connection)) {
                ensureConnectionInView(connection);
            }
        }
        return;
    }

    for (const QString& endpointModuleId : endpointModuleIds) {
        ensureModuleInView(m_graph->getModule(endpointModuleId));
    }
    for (Connection* connection : xpConnections) {
        ensureConnectionInView(connection);
    }
}

void NodeEditorWidget::refreshAllXpPresentations() {
    for (const auto& module : m_graph->modules()) {
        if (module->type() == "XP") {
            refreshXpPresentation(module->id());
        }
    }
}
