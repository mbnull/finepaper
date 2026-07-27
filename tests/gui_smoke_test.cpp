#include "gui/main_window.h"
#include "gui/noc_editor_style.h"
#include "gui/noc_node_editor.h"
#include "gui/workbench_config.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGraphicsView>
#include <QGroupBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolBar>
#include <QTimer>

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <optional>
#include <memory>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

QAction* actionWithText(QWidget& widget, const QString& text) {
    for (QAction* action : widget.findChildren<QAction*>()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

std::optional<QtNodes::NodeId> nodeIdWithCaption(
    QtNodes::BasicGraphicsScene* scene,
    const QString& caption) {
    if (!scene) {
        return std::nullopt;
    }
    for (QtNodes::NodeId nodeId : scene->graphModel().allNodeIds()) {
        if (scene->graphModel().nodeData(
                nodeId, QtNodes::NodeRole::Caption).toString() == caption) {
            return nodeId;
        }
    }
    return std::nullopt;
}

std::optional<QtNodes::NodeId> nodeIdWithCaptionPrefix(
    QtNodes::BasicGraphicsScene* scene,
    const QString& prefix) {
    if (!scene) {
        return std::nullopt;
    }
    for (QtNodes::NodeId nodeId : scene->graphModel().allNodeIds()) {
        if (scene->graphModel().nodeData(
                nodeId, QtNodes::NodeRole::Caption).toString().startsWith(prefix)) {
            return nodeId;
        }
    }
    return std::nullopt;
}

std::optional<QtNodes::NodeId> endpointAttachedToRouter(
    QtNodes::BasicGraphicsScene* scene,
    const QString& routerCaption) {
    const auto router = nodeIdWithCaption(scene, routerCaption);
    if (!scene || !router) {
        return std::nullopt;
    }
    for (const QtNodes::ConnectionId& connection
         : scene->graphModel().allConnectionIds(*router)) {
        if (connection.inNodeId == *router
            && connection.inPortIndex
                   == finepaper::portIndex(finepaper::RouterInputPort::Endpoint)) {
            return connection.outNodeId;
        }
    }
    return std::nullopt;
}

QPoint blankViewportPosition(QGraphicsView* view) {
    if (!view || !view->viewport()) {
        return {};
    }
    for (int y = 24; y < view->viewport()->height() - 24; y += 32) {
        for (int x = 24; x < view->viewport()->width() - 24; x += 32) {
            QGraphicsItem* item = view->itemAt(QPoint(x, y));
            bool overNode = false;
            while (item) {
                if (qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
                    overNode = true;
                    break;
                }
                item = item->parentItem();
            }
            if (!overNode) {
                return QPoint(x, y);
            }
        }
    }
    return QPoint(8, 8);
}

void sendContextMenu(QGraphicsView* view, const QPoint& viewportPosition) {
    if (!view || !view->viewport()) {
        return;
    }
    QContextMenuEvent event(QContextMenuEvent::Mouse,
                            viewportPosition,
                            view->viewport()->mapToGlobal(viewportPosition));
    QApplication::sendEvent(view->viewport(), &event);
}

bool dragPortConnection(QGraphicsView* view,
                        QtNodes::BasicGraphicsScene* scene,
                        QtNodes::NodeId sourceNode,
                        QtNodes::PortIndex sourcePort,
                        QtNodes::NodeId targetNode,
                        QtNodes::PortIndex targetPort) {
    if (!view || !scene || !view->viewport()) {
        return false;
    }
    auto* sourceGraphics = scene->nodeGraphicsObject(sourceNode);
    auto* targetGraphics = scene->nodeGraphicsObject(targetNode);
    if (!sourceGraphics || !targetGraphics) {
        return false;
    }
    const QPointF sourceScene = sourceGraphics->mapToScene(
        scene->nodeGeometry().portPosition(
            sourceNode, QtNodes::PortType::Out, sourcePort));
    const QPointF targetScene = targetGraphics->mapToScene(
        scene->nodeGeometry().portPosition(
            targetNode, QtNodes::PortType::In, targetPort));
    const QPoint source = view->mapFromScene(sourceScene);
    const QPoint target = view->mapFromScene(targetScene);
    QWidget* viewport = view->viewport();

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(source),
                      QPointF(viewport->mapToGlobal(source)),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    press.setAccepted(false);
    QApplication::sendEvent(viewport, &press);
    const bool pressWasConsumed = press.isAccepted();
    for (int step = 1; step <= 5; ++step) {
        const QPoint position = source + (target - source) * step / 5;
        QMouseEvent move(QEvent::MouseMove,
                         QPointF(position),
                         QPointF(viewport->mapToGlobal(position)),
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(target),
                        QPointF(viewport->mapToGlobal(target)),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(viewport, &release);
    return pressWasConsumed;
}

void dragNodeTo(QGraphicsView* view,
                QtNodes::BasicGraphicsScene* scene,
                QtNodes::NodeId nodeId,
                const QPoint& target) {
    if (!view || !scene || !view->viewport()) {
        return;
    }
    auto* node = scene->nodeGraphicsObject(nodeId);
    if (!node) {
        return;
    }
    const QPoint source = view->mapFromScene(node->sceneBoundingRect().center());
    QWidget* viewport = view->viewport();
    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(source),
                      QPointF(viewport->mapToGlobal(source)),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    for (int step = 1; step <= 6; ++step) {
        const QPoint position = source + (target - source) * step / 6;
        QMouseEvent move(QEvent::MouseMove,
                         QPointF(position),
                         QPointF(viewport->mapToGlobal(position)),
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(target),
                        QPointF(viewport->mapToGlobal(target)),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(viewport, &release);
}

void chooseInputDialogItem(int index) {
    QTimer::singleShot(0, [index] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QInputDialog*>(widget);
            if (!dialog) {
                continue;
            }
            if (auto* combo = dialog->findChild<QComboBox*>()) {
                combo->setCurrentIndex(index);
            }
            dialog->accept();
            return;
        }
    });
}

} // namespace

int main(int argc, char** argv) {
    QTemporaryDir configRoot(QStringLiteral("/tmp/finepaper-gui-config-XXXXXX"));
    QTemporaryDir outputRoot(QStringLiteral("/tmp/finepaper-gui-output-XXXXXX"));
    check(configRoot.isValid(), QStringLiteral("temporary GUI settings root is available"));
    check(outputRoot.isValid(), QStringLiteral("temporary GUI output root is available"));
    qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(QStringLiteral("finepaper-gui-smoke"));

    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);
    finepaper::RuntimeLocations locations{
        QStringList{QDir(projectRoot).filePath(QStringLiteral("packages/finepaper-noc"))},
        outputRoot.path()};
    finepaper::FinepaperMainWindow window(locations);
    window.show();
    application.processEvents();

    auto* centerViews = qobject_cast<QTabWidget*>(window.centralWidget());
    check(centerViews && centerViews->count() == 3,
          QStringLiteral("central workbench exposes three switchable views"));
    if (centerViews) {
        check(centerViews->tabText(0) == QStringLiteral("NoC Editor"),
              QStringLiteral("NoC Editor is the default central view"));
        check(centerViews->tabText(1) == QStringLiteral("Performance Analysis"),
              QStringLiteral("performance analysis is a central view"));
        check(centerViews->tabText(2) == QStringLiteral("Problem Report"),
              QStringLiteral("problem report is a central view"));
    }

    auto* packageDock = window.findChild<QDockWidget*>(finepaper::workbench::packageDockName);
    auto* inspectorDock = window.findChild<QDockWidget*>(finepaper::workbench::inspectorDockName);
    auto* resultsDock = window.findChild<QDockWidget*>(finepaper::workbench::resultsDockName);
    check(packageDock && window.dockWidgetArea(packageDock) == Qt::LeftDockWidgetArea,
          QStringLiteral("Package and Endpoint library is docked on the left"));
    check(inspectorDock && window.dockWidgetArea(inspectorDock) == Qt::RightDockWidgetArea,
          QStringLiteral("Inspector is docked on the right"));
    check(resultsDock && window.dockWidgetArea(resultsDock) == Qt::BottomDockWidgetArea,
          QStringLiteral("diagnostics and outputs are docked at the bottom"));

    auto* activityBar = window.findChild<QToolBar*>(finepaper::workbench::activityBarName);
    QAction* packagePanelAction = window.findChild<QAction*>(
        finepaper::workbench::packageToggleActionName);
    QAction* inspectorPanelAction = window.findChild<QAction*>(
        finepaper::workbench::inspectorToggleActionName);
    QAction* resultsPanelAction = window.findChild<QAction*>(
        finepaper::workbench::resultsToggleActionName);
    check(activityBar && activityBar->orientation() == Qt::Vertical,
          QStringLiteral("a persistent vertical Activity Bar controls workbench panels"));
    check(packagePanelAction && packagePanelAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+B")),
          QStringLiteral("left Package panel has the VS Code style Ctrl+B shortcut"));
    check(inspectorPanelAction
              && inspectorPanelAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+B")),
          QStringLiteral("right Inspector panel has a direct collapse shortcut"));
    check(resultsPanelAction
              && resultsPanelAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+J")),
          QStringLiteral("bottom results panel has the VS Code style Ctrl+J shortcut"));

    if (packagePanelAction && packageDock) {
        packagePanelAction->trigger();
        application.processEvents();
        check(!packageDock->isVisible() && !packagePanelAction->isChecked(),
              QStringLiteral("Activity Bar collapses the left Package panel"));
        packagePanelAction->trigger();
        application.processEvents();
        check(packageDock->isVisible() && packagePanelAction->isChecked(),
              QStringLiteral("Activity Bar restores the left Package panel"));
    }
    if (inspectorPanelAction && inspectorDock) {
        inspectorPanelAction->trigger();
        application.processEvents();
        check(!inspectorDock->isVisible(),
              QStringLiteral("Activity Bar collapses the right Inspector panel"));
        inspectorPanelAction->trigger();
        application.processEvents();
        check(inspectorDock->isVisible(),
              QStringLiteral("Activity Bar restores the right Inspector panel"));
    }
    if (resultsPanelAction && resultsDock) {
        resultsPanelAction->trigger();
        application.processEvents();
        check(!resultsDock->isVisible(),
              QStringLiteral("Activity Bar collapses the bottom diagnostics panel"));
        resultsPanelAction->trigger();
        application.processEvents();
        check(resultsDock->isVisible(),
              QStringLiteral("Activity Bar restores the bottom diagnostics panel"));
    }

    auto* resultTabs = resultsDock ? qobject_cast<QTabWidget*>(resultsDock->widget()) : nullptr;
    check(resultTabs && resultTabs->count() == 3,
          QStringLiteral("bottom dock exposes DRC, log and generation tabs"));
    if (resultTabs) {
        check(resultTabs->tabText(0) == QStringLiteral("DRC Problems"),
              QStringLiteral("bottom DRC page is present"));
        check(resultTabs->tabText(1) == QStringLiteral("Activity Log"),
              QStringLiteral("bottom log page is present"));
        check(resultTabs->tabText(2) == QStringLiteral("Generation Outputs"),
              QStringLiteral("bottom generation page is present"));
    }

    auto* packageSelector = window.findChild<QComboBox*>(QStringLiteral("finepaper.packageSelector"));
    auto* endpointPalette = window.findChild<QListWidget*>(QStringLiteral("finepaper.endpointPalette"));
    check(packageSelector && packageSelector->count() == 1,
          QStringLiteral("runtime NoC Package is loaded into the workbench"));
    check(endpointPalette && endpointPalette->count() == 2,
          QStringLiteral("Package Endpoint types populate the drag palette"));

    auto* createButton = window.findChild<QPushButton*>(QStringLiteral("finepaper.createDesign"));
    check(createButton != nullptr, QStringLiteral("Mesh create action is available in the Package dock"));
    if (createButton) {
        createButton->click();
        application.processEvents();
    }

    auto* editorWidget = window.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor"));
    auto* nodeEditor = dynamic_cast<finepaper::NocNodeEditor*>(editorWidget);
    auto* graphicsView = nodeEditor ? nodeEditor->findChild<QGraphicsView*>() : nullptr;
    auto* graphicsScene = graphicsView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(graphicsView->scene())
        : nullptr;
    check(graphicsView && graphicsView->scene() && !graphicsView->scene()->items().isEmpty(),
          QStringLiteral("created Mesh is projected into the QtNodes editor"));

    const QPainterPath orthogonalPath = finepaper::orthogonalConnectionPath(
        QPointF(0.0, 0.0), QPointF(120.0, 80.0));
    check(orthogonalPath.elementCount() == 4,
          QStringLiteral("NoC connections use a three-segment orthogonal route"));
    bool containsBezierCurve = false;
    for (int index = 0; index < orthogonalPath.elementCount(); ++index) {
        const QPainterPath::Element element = orthogonalPath.elementAt(index);
        containsBezierCurve = containsBezierCurve
            || element.type == QPainterPath::CurveToElement
            || element.type == QPainterPath::CurveToDataElement;
    }
    check(!containsBezierCurve,
          QStringLiteral("NoC connection routing contains no Bezier curve elements"));

    const QPainterPath verticalPath = finepaper::orthogonalConnectionPath(
        QPointF(0.0, 0.0), QPointF(120.0, 80.0),
        finepaper::OrthogonalRouteAxis::Vertical);
    check(verticalPath.elementCount() == 4
              && verticalPath.elementAt(1).y == 40.0
              && verticalPath.elementAt(2).y == 40.0,
          QStringLiteral("vertical NoC links route through a stable horizontal middle segment"));

    const auto router00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    const auto router10 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-1-0"));
    const auto router01 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-1"));
    const auto router11 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-1-1"));
    check(router00 && router10 && router01 && router11,
          QStringLiteral("Mesh Router identities are present in the editor projection"));
    if (graphicsScene && router00) {
        const QSize routerSize = graphicsScene->nodeGeometry().size(*router00);
        check(routerSize.width() == routerSize.height(),
              QStringLiteral("Router is rendered as a square device"));
        check(graphicsScene->nodeGeometry().portPosition(
                  *router00, QtNodes::PortType::In,
                  finepaper::portIndex(finepaper::RouterInputPort::West))
                  == QPointF(0.0, routerSize.height() / 2.0),
              QStringLiteral("Router west input is fixed on the west edge"));
        check(graphicsScene->nodeGeometry().portPosition(
                  *router00, QtNodes::PortType::In,
                  finepaper::portIndex(finepaper::RouterInputPort::North))
                  == QPointF(routerSize.width() / 2.0, 0.0),
              QStringLiteral("Router north input is fixed on the north edge"));
        check(graphicsScene->nodeGeometry().portPosition(
                  *router00, QtNodes::PortType::Out,
                  finepaper::portIndex(finepaper::RouterOutputPort::East))
                  == QPointF(routerSize.width(), routerSize.height() / 2.0),
              QStringLiteral("Router east output is fixed on the east edge"));
        check(graphicsScene->nodeGeometry().portPosition(
                  *router00, QtNodes::PortType::Out,
                  finepaper::portIndex(finepaper::RouterOutputPort::South))
                  == QPointF(routerSize.width() / 2.0, routerSize.height()),
              QStringLiteral("Router south output is fixed on the south edge"));
    }
    if (graphicsScene && router00 && router10 && router01) {
        check(graphicsScene->graphModel().connectionExists(
                  {*router00,
                   finepaper::portIndex(finepaper::RouterOutputPort::East),
                   *router10,
                   finepaper::portIndex(finepaper::RouterInputPort::West)}),
              QStringLiteral("horizontal Mesh link connects east output to west input"));
        check(graphicsScene->graphModel().connectionExists(
                  {*router00,
                   finepaper::portIndex(finepaper::RouterOutputPort::South),
                   *router01,
                   finepaper::portIndex(finepaper::RouterInputPort::North)}),
              QStringLiteral("vertical Mesh link connects south output to north input"));
    }

    if (graphicsScene && router00 && router10 && router01 && router11) {
        auto* selectedRouter = graphicsScene->nodeGraphicsObject(*router00);
        selectedRouter->setSelected(true);
        graphicsScene->nodeSelected(*router00);
        application.processEvents();
        check(graphicsScene->nodeGraphicsObject(*router10)
                  ->data(finepaper::relatedHighlightDataRole).toBool()
                  && graphicsScene->nodeGraphicsObject(*router01)
                         ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("selecting a Router highlights directly connected Routers"));
        check(!graphicsScene->nodeGraphicsObject(*router11)
                   ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("selection highlight does not spread beyond one hop"));
        const QtNodes::ConnectionId eastLink{
            *router00,
            finepaper::portIndex(finepaper::RouterOutputPort::East),
            *router10,
            finepaper::portIndex(finepaper::RouterInputPort::West)};
        const QtNodes::ConnectionId southLink{
            *router00,
            finepaper::portIndex(finepaper::RouterOutputPort::South),
            *router01,
            finepaper::portIndex(finepaper::RouterInputPort::North)};
        check(graphicsScene->connectionGraphicsObject(eastLink)
                  ->data(finepaper::relatedHighlightDataRole).toBool()
                  && graphicsScene->connectionGraphicsObject(southLink)
                         ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("selecting a Router highlights all directly connected lines"));
        graphicsScene->clearSelection();
        application.processEvents();
        check(!graphicsScene->nodeGraphicsObject(*router10)
                   ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("clearing selection clears connected-element highlights"));
    }

    QtNodes::NodeGraphicsObject* routerNode = nullptr;
    if (graphicsView && graphicsView->scene()) {
        for (QGraphicsItem* item : graphicsView->scene()->items()) {
            auto* node = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item);
            if (node && node->graphModel().nodeData(
                            node->nodeId(), QtNodes::NodeRole::Caption).toString()
                            == QStringLiteral("r-0-0")) {
                routerNode = node;
                break;
            }
        }
    }
    check(routerNode
              && !routerNode->graphModel().nodeFlags(routerNode->nodeId())
                      .testFlag(QtNodes::NodeFlag::Locked),
          QStringLiteral("Router nodes are draggable rather than locked"));
    const QPointF movedRouterPosition(525.0, 415.0);
    check(nodeEditor
              && nodeEditor->setRouterVisualPosition(
                  QStringLiteral("r-0-0"), movedRouterPosition),
          QStringLiteral("Router visual position can be changed without editing topology"));
    check(nodeEditor && nodeEditor->routerVisualPosition(QStringLiteral("r-0-0"))
                            == std::optional<QPointF>(movedRouterPosition),
          QStringLiteral("Router remains at its user-arranged workspace position"));

    const std::size_t nodesBeforeEndpoint = graphicsScene
        ? graphicsScene->graphModel().allNodeIds().size() : 0;
    const auto arrangedRouter00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    if (graphicsScene && arrangedRouter00) {
        graphicsScene->nodeGraphicsObject(*arrangedRouter00)->setSelected(true);
        graphicsScene->nodeSelected(*arrangedRouter00);
        application.processEvents();
    }
    check(!window.findChild<QPushButton*>(QStringLiteral("finepaper.attachEndpoint")),
          QStringLiteral("Inspector contains properties rather than Endpoint wiring tools"));
    auto* selectionInspector = window.findChild<QGroupBox*>(
        finepaper::workbench::selectionInspectorName);
    check(selectionInspector && selectionInspector->findChildren<QPushButton*>().isEmpty(),
          QStringLiteral("selection Inspector contains no connect or delete controls"));
    std::unique_ptr<QMimeData> endpointMime;
    if (endpointPalette && endpointPalette->count() > 0) {
        endpointMime.reset(endpointPalette->model()->mimeData(
            {endpointPalette->model()->index(0, 0)}));
    }
    check(endpointMime
              && endpointMime->hasFormat(finepaper::workbench::endpointTypeMime)
              && QString::fromUtf8(endpointMime->data(
                     finepaper::workbench::endpointTypeMime)) == QStringLiteral("master"),
          QStringLiteral("Endpoint Palette exports a runtime Endpoint MIME payload"));
    if (graphicsView && graphicsScene && arrangedRouter00 && endpointMime) {
        const QPoint dropPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*arrangedRouter00)
                ->sceneBoundingRect().center());
        QDragEnterEvent dragEnter(dropPosition,
                                  Qt::CopyAction,
                                  endpointMime.get(),
                                  Qt::LeftButton,
                                  Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &dragEnter);
        QDropEvent drop(QPointF(dropPosition),
                        Qt::CopyAction,
                        endpointMime.get(),
                        Qt::LeftButton,
                        Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &drop);
        application.processEvents();
        check(dragEnter.isAccepted() && drop.isAccepted(),
              QStringLiteral("Endpoint MIME can be dragged from the Palette onto a Router"));
    }
    const std::size_t nodesAfterEndpoint = graphicsScene
        ? graphicsScene->graphModel().allNodeIds().size() : 0;
    check(nodesAfterEndpoint == nodesBeforeEndpoint + 1,
          QStringLiteral("dropping an Endpoint type attaches it to the target Router"));

    const auto attachedEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    const auto currentRouter00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    check(attachedEndpoint && currentRouter00
              && graphicsScene->graphModel().connectionExists(
                  {*attachedEndpoint,
                   finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
                   *currentRouter00,
                   finepaper::portIndex(finepaper::RouterInputPort::Endpoint)}),
          QStringLiteral("Endpoint uses the Router's dedicated EP attachment port"));

    if (graphicsView && graphicsScene && currentRouter00) {
        const QPoint routerPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*currentRouter00)
                ->sceneBoundingRect().center());
        sendContextMenu(graphicsView, routerPosition);
        application.processEvents();
    }
    auto* routerMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::routerContextMenuName) : nullptr;
    auto* createEndpointMenu = routerMenu ? routerMenu->findChild<QMenu*>(
        finepaper::workbench::createEndpointMenuName) : nullptr;
    check(routerMenu && createEndpointMenu && createEndpointMenu->actions().size() == 2,
          QStringLiteral("Router right-click menu creates any Package Endpoint type without selection"));
    if (routerMenu) {
        routerMenu->close();
        application.processEvents();
    }

    if (graphicsView && graphicsScene && attachedEndpoint) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*attachedEndpoint)
                ->sceneBoundingRect().center());
        sendContextMenu(graphicsView, endpointPosition);
        application.processEvents();
    }
    auto* endpointMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    auto* connectRouterMenu = endpointMenu ? endpointMenu->findChild<QMenu*>(
        finepaper::workbench::connectRouterMenuName) : nullptr;
    QAction* deleteEndpoint = endpointMenu ? endpointMenu->findChild<QAction*>(
        finepaper::workbench::deleteEndpointActionName) : nullptr;
    check(endpointMenu && connectRouterMenu
              && connectRouterMenu->actions().size() == 4
              && deleteEndpoint,
          QStringLiteral("Endpoint right-click menu exposes connect and delete actions"));
    if (deleteEndpoint) {
        deleteEndpoint->trigger();
        if (endpointMenu) {
            endpointMenu->close();
        }
        application.processEvents();
    }
    check(!nodeIdWithCaptionPrefix(graphicsScene, QStringLiteral("master")),
          QStringLiteral("Endpoint right-click delete removes it through the application layer"));

    const std::size_t nodesBeforePending = graphicsScene
        ? graphicsScene->graphModel().allNodeIds().size() : 0;
    const QPoint blankDropPosition = blankViewportPosition(graphicsView);
    if (graphicsView && endpointMime) {
        QDragEnterEvent dragEnter(blankDropPosition,
                                  Qt::CopyAction,
                                  endpointMime.get(),
                                  Qt::LeftButton,
                                  Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &dragEnter);
        QDropEvent drop(QPointF(blankDropPosition),
                        Qt::CopyAction,
                        endpointMime.get(),
                        Qt::LeftButton,
                        Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &drop);
        application.processEvents();
        check(dragEnter.isAccepted() && drop.isAccepted(),
              QStringLiteral("real viewport drag/drop accepts an Endpoint on blank canvas"));
    }
    const auto pendingEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("Unattached\nMaster endpoint"));
    check(pendingEndpoint
              && graphicsScene->graphModel().allNodeIds().size() == nodesBeforePending + 1,
          QStringLiteral("blank-canvas drop creates a movable unattached Endpoint"));

    const auto connectionRouter = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-1-0"));
    const QtNodes::ConnectionId pendingAttachment{
        pendingEndpoint.value_or(0),
        finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
        connectionRouter.value_or(0),
        finepaper::portIndex(finepaper::RouterInputPort::Endpoint)};
    check(pendingEndpoint && connectionRouter
              && !graphicsScene->graphModel().connectionPossible(pendingAttachment),
          QStringLiteral("manual port wiring is disabled in favor of node-body attachment"));
    const auto routerForConnectionRule = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    check(routerForConnectionRule && connectionRouter
              && !graphicsScene->graphModel().connectionPossible({
                  *routerForConnectionRule,
                  finepaper::portIndex(finepaper::RouterOutputPort::East),
                  *connectionRouter,
                  finepaper::portIndex(finepaper::RouterInputPort::West)}),
          QStringLiteral("manual connection cannot edit derived Router topology links"));
    bool endpointPortBlocked = false;
    if (pendingEndpoint && connectionRouter) {
        endpointPortBlocked = dragPortConnection(
            graphicsView,
            graphicsScene,
            *pendingEndpoint,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *connectionRouter,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint));
        application.processEvents();
        application.processEvents();
    }
    check(endpointPortBlocked && nodeIdWithCaptionPrefix(
              graphicsScene, QStringLiteral("Unattached\nMaster endpoint")).has_value(),
          QStringLiteral("dragging from a precise port does not create a user connection"));
    bool routerPortBlocked = false;
    if (routerForConnectionRule && connectionRouter) {
        routerPortBlocked = dragPortConnection(
            graphicsView,
            graphicsScene,
            *routerForConnectionRule,
            finepaper::portIndex(finepaper::RouterOutputPort::East),
            *connectionRouter,
            finepaper::portIndex(finepaper::RouterInputPort::West));
        application.processEvents();
    }
    check(routerPortBlocked,
          QStringLiteral("Router direction ports consume clicks without starting user wiring"));
    if (pendingEndpoint && connectionRouter) {
        const QPoint routerCenter = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*connectionRouter)
                ->sceneBoundingRect().center());
        dragNodeTo(graphicsView, graphicsScene, *pendingEndpoint, routerCenter);
        application.processEvents();
        application.processEvents();
    }
    check(!nodeIdWithCaptionPrefix(
               graphicsScene, QStringLiteral("Unattached\nMaster endpoint")),
          QStringLiteral("dropping an Endpoint node onto a Router body attaches it"));
    const auto manuallyAttachedEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    const auto currentRouter10 = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-1-0"));
    check(manuallyAttachedEndpoint && currentRouter10
              && graphicsScene->graphModel().connectionExists({
                  *manuallyAttachedEndpoint,
                  finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
                  *currentRouter10,
                  finepaper::portIndex(finepaper::RouterInputPort::Endpoint)}),
          QStringLiteral("node-body drop becomes a durable design attachment"));

    const QPoint freeEndpointTarget = blankViewportPosition(graphicsView);
    if (manuallyAttachedEndpoint) {
        dragNodeTo(graphicsView, graphicsScene,
                   *manuallyAttachedEndpoint, freeEndpointTarget);
        application.processEvents();
        application.processEvents();
    }
    const std::optional<QPointF> freelyPlacedEndpoint = nodeEditor
        ? nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
        : std::nullopt;
    check(freelyPlacedEndpoint
              && endpointAttachedToRouter(graphicsScene, QStringLiteral("r-1-0")),
          QStringLiteral("moving an attached Endpoint freely does not change its Router attachment"));
    if (nodeEditor) {
        nodeEditor->setRouterVisualPosition(
            QStringLiteral("r-0-0"), movedRouterPosition);
    }
    check(freelyPlacedEndpoint && nodeEditor
              && nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
                     == freelyPlacedEndpoint,
          QStringLiteral("free Endpoint placement survives a projection rebuild"));

    const auto freelyPlacedEndpointNode = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));

    if (graphicsView && graphicsScene && freelyPlacedEndpointNode) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*freelyPlacedEndpointNode)
                ->sceneBoundingRect().center());
        sendContextMenu(graphicsView, endpointPosition);
        application.processEvents();
    }
    auto* moveEndpointMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    auto* moveRouterMenu = moveEndpointMenu ? moveEndpointMenu->findChild<QMenu*>(
        finepaper::workbench::connectRouterMenuName) : nullptr;
    QAction* moveToRouter11 = nullptr;
    if (moveRouterMenu) {
        for (QAction* action : moveRouterMenu->actions()) {
            if (action->data().toString() == QStringLiteral("r-1-1")) {
                moveToRouter11 = action;
                break;
            }
        }
    }
    check(moveToRouter11 != nullptr,
          QStringLiteral("Endpoint context menu lists concrete Router connection targets"));
    if (moveToRouter11) {
        moveToRouter11->trigger();
        if (moveEndpointMenu) {
            moveEndpointMenu->close();
        }
        application.processEvents();
    }
    check(endpointAttachedToRouter(graphicsScene, QStringLiteral("r-1-1")).has_value()
              && !endpointAttachedToRouter(graphicsScene, QStringLiteral("r-1-0"))
              && nodeEditor
              && nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
                     == freelyPlacedEndpoint,
          QStringLiteral("right-click Router reassignment preserves independent visual placement"));

    const QPoint canvasMenuPosition = blankViewportPosition(graphicsView);
    if (graphicsView) {
        sendContextMenu(graphicsView, canvasMenuPosition);
        application.processEvents();
    }
    auto* canvasMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::canvasContextMenuName) : nullptr;
    check(canvasMenu && canvasMenu->actions().size() == 2,
          QStringLiteral("blank-canvas right-click menu creates Package Endpoint types"));
    if (canvasMenu && !canvasMenu->actions().isEmpty()) {
        canvasMenu->actions().first()->trigger();
        canvasMenu->close();
        application.processEvents();
    }
    const auto contextPendingEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("Unattached\nMaster endpoint"));
    check(contextPendingEndpoint.has_value(),
          QStringLiteral("canvas right-click create does not require a selected Router"));
    if (graphicsView && graphicsScene && contextPendingEndpoint) {
        const QPoint pendingPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*contextPendingEndpoint)
                ->sceneBoundingRect().center());
        sendContextMenu(graphicsView, pendingPosition);
        application.processEvents();
    }
    auto* pendingMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    QAction* deletePending = pendingMenu ? pendingMenu->findChild<QAction*>(
        finepaper::workbench::deleteEndpointActionName) : nullptr;
    check(pendingMenu && pendingMenu->findChild<QMenu*>(
                              finepaper::workbench::connectRouterMenuName)
              && deletePending,
          QStringLiteral("unattached Endpoint right-click menu supports connect and delete"));
    if (deletePending) {
        deletePending->trigger();
        if (pendingMenu) {
            pendingMenu->close();
        }
        application.processEvents();
    }
    check(!nodeIdWithCaptionPrefix(
               graphicsScene, QStringLiteral("Unattached\nMaster endpoint")),
          QStringLiteral("right-click delete removes an unattached Endpoint draft"));

    const auto routerForSecondEndpoint = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    if (graphicsScene && routerForSecondEndpoint) {
        graphicsScene->nodeGraphicsObject(*routerForSecondEndpoint)->setSelected(true);
        graphicsScene->nodeSelected(*routerForSecondEndpoint);
        application.processEvents();
    }
    if (endpointPalette && endpointPalette->count() > 0) {
        endpointPalette->itemDoubleClicked(endpointPalette->item(0));
        application.processEvents();
    }
    check(nodeIdWithCaptionPrefix(graphicsScene, QStringLiteral("master")).has_value(),
          QStringLiteral("selected-Router double-click remains available as an attach shortcut"));
    check(endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-0")).has_value(),
          QStringLiteral("selected-Router shortcut attaches specifically to that Router"));

    check(nodeEditor && nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), true),
          QStringLiteral("Router can be collapsed from the workspace"));
    const auto collapsedRouter00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    check(nodeEditor && nodeEditor->routerCollapsed(QStringLiteral("r-0-0")),
          QStringLiteral("Router collapsed state is tracked independently of NocDesign"));
    if (graphicsScene && collapsedRouter00) {
        const QSize collapsedSize = graphicsScene->nodeGeometry().size(*collapsedRouter00);
        check(collapsedSize.width() == collapsedSize.height()
                  && collapsedSize.width()
                         < finepaper::nocEditorMetrics().expandedRouterSize.width(),
              QStringLiteral("collapsed Router remains square and becomes compact"));
    }
    check(!endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-0")),
          QStringLiteral("collapsed Router hides its Endpoint projection"));
    check(nodeEditor && nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), false),
          QStringLiteral("Router can be expanded again"));
    check(endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-0")).has_value(),
          QStringLiteral("expanding Router restores its Endpoint projection"));

    QAction* validateAction = actionWithText(window, QStringLiteral("Validate / DRC"));
    check(validateAction != nullptr, QStringLiteral("shared validation action is available"));
    if (validateAction) {
        validateAction->trigger();
        application.processEvents();
    }

    auto* outputPath = window.findChild<QLineEdit*>(QStringLiteral("finepaper.outputRoot"));
    if (outputPath) {
        outputPath->setText(outputRoot.path());
    }
    QAction* generateAction = actionWithText(window, QStringLiteral("Generate RTL"));
    check(generateAction != nullptr, QStringLiteral("shared RTL generation action is available"));
    if (generateAction) {
        generateAction->trigger();
        application.processEvents();
    }

    auto* artifacts = window.findChild<QTableWidget*>(QStringLiteral("finepaper.artifactTable"));
    check(artifacts && artifacts->rowCount() > 0,
          QStringLiteral("GUI generation flow reports real RTL artifacts"));
    if (artifacts && artifacts->rowCount() > 0) {
        const QString artifactPath = artifacts->item(0, 2)->text();
        check(!artifactPath.isEmpty(), QStringLiteral("reported GUI artifact has a path"));
    }

    if (packagePanelAction) {
        packagePanelAction->trigger();
        application.processEvents();
    }
    if (nodeEditor) {
        nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), true);
    }
    window.close();
    application.processEvents();

    finepaper::FinepaperMainWindow restoredWindow(locations);
    restoredWindow.show();
    application.processEvents();
    auto* restoredPackageDock = restoredWindow.findChild<QDockWidget*>(
        finepaper::workbench::packageDockName);
    check(restoredPackageDock && !restoredPackageDock->isVisible(),
          QStringLiteral("collapsed panel state is restored in the next workbench session"));
    auto* restoredCreateButton = restoredWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    if (restoredCreateButton) {
        restoredCreateButton->click();
        application.processEvents();
    }
    auto* restoredEditorWidget = restoredWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.nodeEditor"));
    auto* restoredNodeEditor = dynamic_cast<finepaper::NocNodeEditor*>(restoredEditorWidget);
    check(restoredNodeEditor
              && restoredNodeEditor->routerVisualPosition(QStringLiteral("r-0-0"))
                     == std::optional<QPointF>(movedRouterPosition),
          QStringLiteral("Router workspace placement is restored in the next session"));
    check(restoredNodeEditor
              && restoredNodeEditor->routerCollapsed(QStringLiteral("r-0-0")),
          QStringLiteral("Router collapsed state is restored in the next session"));
    restoredWindow.close();

    finepaper::RuntimeLocations explicitLocations{
        QStringList{QDir(projectRoot).filePath(
            QStringLiteral("tests/fixtures/explicit-package"))},
        outputRoot.path()};
    finepaper::FinepaperMainWindow explicitWindow(explicitLocations);
    explicitWindow.show();
    application.processEvents();
    auto* explicitPackageSelector = explicitWindow.findChild<QComboBox*>(
        QStringLiteral("finepaper.packageSelector"));
    check(explicitPackageSelector && explicitPackageSelector->count() == 1,
          QStringLiteral("explicit-slot Package fixture loads at runtime"));
    auto* explicitCreate = explicitWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    if (explicitCreate) {
        explicitCreate->click();
        application.processEvents();
    }
    auto* explicitEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        explicitWindow.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor")));
    auto* explicitView = explicitEditor
        ? explicitEditor->findChild<QGraphicsView*>() : nullptr;
    auto* explicitScene = explicitView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(explicitView->scene()) : nullptr;
    auto* explicitPalette = explicitWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointPalette"));
    const auto explicitRouter00 = nodeIdWithCaption(
        explicitScene, QStringLiteral("r-0-0"));
    if (explicitScene && explicitRouter00) {
        explicitScene->nodeGraphicsObject(*explicitRouter00)->setSelected(true);
        explicitScene->nodeSelected(*explicitRouter00);
        application.processEvents();
    }
    chooseInputDialogItem(1);
    if (explicitPalette && explicitPalette->count() > 0) {
        explicitPalette->itemDoubleClicked(explicitPalette->item(0));
        application.processEvents();
    }
    const auto explicitEndpoint = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    check(explicitEndpoint
              && explicitScene->graphModel().nodeData(
                     *explicitEndpoint, QtNodes::NodeRole::Caption)
                     .toString().contains(QStringLiteral("slot local1")),
          QStringLiteral("explicit Package lets the user choose the Endpoint attachment slot"));

    chooseInputDialogItem(0);
    const bool explicitMove = explicitEditor && explicitEditor->endpointMoveRequested
        ? explicitEditor->endpointMoveRequested(
              QStringLiteral("device_0"), finepaper::RouterPosition{1, 0})
        : false;
    application.processEvents();
    const auto movedExplicitEndpoint = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    const auto explicitRouter10 = nodeIdWithCaption(
        explicitScene, QStringLiteral("r-1-0"));
    check(explicitMove && movedExplicitEndpoint && explicitRouter10
              && explicitScene->graphModel().nodeData(
                     *movedExplicitEndpoint, QtNodes::NodeRole::Caption)
                     .toString().contains(QStringLiteral("slot local0"))
              && explicitScene->graphModel().connectionExists(
                  {*movedExplicitEndpoint,
                   finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
                   *explicitRouter10,
                   finepaper::portIndex(finepaper::RouterInputPort::Endpoint)}),
          QStringLiteral("moving an explicit Endpoint chooses a new free slot on its Router"));
    explicitWindow.close();

    QTextStream(stdout) << (failures == 0 ? "finepaper-gui-smoke passed"
                                          : "finepaper-gui-smoke failed")
                        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
