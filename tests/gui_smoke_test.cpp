#include "gui/main_window.h"
#include "features/topology/animated_graphics_view.h"
#include "features/domain/domain_manager_panel.h"
#include "gui/endpoint_configuration_panel.h"
#include "features/topology/noc_editor_style.h"
#include "features/topology/noc_node_editor.h"
#include "features/topology/topology_workspace_store.h"
#include "ui/common/schema_value_editor.h"
#include "ui/workbench/workbench_config.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsView>
#include <QGraphicsPathItem>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QToolBar>
#include <QTimer>

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <optional>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_set>

namespace {

int failures = 0;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMilliseconds = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(1);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

class EndpointCreationAutoAccepter final : public QObject {
public:
    void setEnabled(bool enabled) { enabled_ = enabled; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (enabled_ && dialog && event->type() == QEvent::Show
            && dialog->objectName()
                == QStringLiteral("finepaper.endpointCreationDialog")) {
            QTimer::singleShot(0, dialog, [dialog] {
                auto* buttons = dialog->findChild<QDialogButtonBox*>(
                    QStringLiteral("finepaper.endpointCreation.buttons"));
                if (buttons) {
                    if (QAbstractButton* accept =
                            buttons->button(QDialogButtonBox::Ok)) {
                        accept->click();
                    }
                }
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    bool enabled_ = true;
};

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

std::unordered_set<QtNodes::ConnectionId> sceneConnectionIds(
    QtNodes::BasicGraphicsScene* scene) {
    std::unordered_set<QtNodes::ConnectionId> connections;
    if (!scene) {
        return connections;
    }
    for (const QtNodes::NodeId nodeId : scene->graphModel().allNodeIds()) {
        const auto nodeConnections = scene->graphModel().allConnectionIds(nodeId);
        connections.insert(nodeConnections.cbegin(), nodeConnections.cend());
    }
    return connections;
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

std::optional<QtNodes::PortIndex> attachmentPortForEndpoint(
    QtNodes::BasicGraphicsScene* scene,
    QtNodes::NodeId endpoint) {
    if (!scene) {
        return std::nullopt;
    }
    for (const QtNodes::ConnectionId& connection
         : scene->graphModel().allConnectionIds(endpoint)) {
        if (connection.outNodeId == endpoint
            && connection.outPortIndex
                   == finepaper::portIndex(finepaper::EndpointOutputPort::Attachment)) {
            return connection.inPortIndex;
        }
    }
    return std::nullopt;
}

std::optional<QtNodes::ConnectionId> attachmentConnectionForEndpoint(
    QtNodes::BasicGraphicsScene* scene,
    QtNodes::NodeId endpoint) {
    if (!scene) {
        return std::nullopt;
    }
    for (const QtNodes::ConnectionId& connection
         : scene->graphModel().allConnectionIds(endpoint)) {
        if (connection.outNodeId == endpoint
            && connection.outPortIndex
                == finepaper::portIndex(
                    finepaper::EndpointOutputPort::Attachment)) {
            return connection;
        }
    }
    return std::nullopt;
}

std::optional<QPoint> viewportPointForConnection(
    QGraphicsView* view,
    QtNodes::ConnectionGraphicsObject* connection) {
    if (!view || !connection) {
        return std::nullopt;
    }
    const QPainterPath path = finepaper::orthogonalConnectionPath(
        connection->out(), connection->in());
    for (int step = 1; step < 20; ++step) {
        const QPoint viewportPosition = view->mapFromScene(
            connection->mapToScene(path.pointAtPercent(step / 20.0)));
        bool connectionHit = false;
        bool nodeHit = false;
        for (QGraphicsItem* hitItem : view->items(viewportPosition)) {
            QGraphicsItem* item = hitItem;
            while (item) {
                if (item == connection) {
                    connectionHit = true;
                    break;
                }
                if (qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item)) {
                    nodeHit = true;
                    break;
                }
                item = item->parentItem();
            }
        }
        if (connectionHit && !nodeHit) {
            return viewportPosition;
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

bool hasDraftConnection(QtNodes::BasicGraphicsScene* scene) {
    if (!scene) {
        return false;
    }
    for (QGraphicsItem* item : scene->items()) {
        auto* connection = qgraphicsitem_cast<QtNodes::ConnectionGraphicsObject*>(item);
        if (connection && connection->connectionState().requiresPort()) {
            return true;
        }
        auto* routerEndpointDraft = qgraphicsitem_cast<QGraphicsPathItem*>(item);
        if (routerEndpointDraft
            && routerEndpointDraft->data(Qt::UserRole).toString()
                   .startsWith(QStringLiteral("finepaper."))) {
            return true;
        }
    }
    return false;
}

bool dragPortConnection(QGraphicsView* view,
                        QtNodes::BasicGraphicsScene* scene,
                        QtNodes::NodeId sourceNode,
                        QtNodes::PortIndex sourcePort,
                        QtNodes::NodeId targetNode,
                        QtNodes::PortIndex targetPort,
                        QtNodes::PortType targetPortType =
                            QtNodes::PortType::In) {
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
            targetNode, targetPortType, targetPort));
    const QPoint source = view->mapFromScene(sourceScene);
    const QPoint target = view->mapFromScene(targetScene);
    QWidget* viewport = view->viewport();

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(source),
                      QPointF(viewport->mapToGlobal(source)),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    const bool draftStarted = hasDraftConnection(scene);
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
    return draftStarted;
}

bool dragPortToNodeBody(QGraphicsView* view,
                        QtNodes::BasicGraphicsScene* scene,
                        QtNodes::NodeId sourceNode,
                        QtNodes::PortType sourcePortType,
                        QtNodes::PortIndex sourcePort,
                        QtNodes::NodeId targetNode,
                        QPoint sourceOffset = {}) {
    if (!view || !scene || !view->viewport()) {
        return false;
    }
    auto* sourceGraphics = scene->nodeGraphicsObject(sourceNode);
    auto* targetGraphics = scene->nodeGraphicsObject(targetNode);
    if (!sourceGraphics || !targetGraphics) {
        return false;
    }
    const QPointF sourceScene = sourceGraphics->mapToScene(
        scene->nodeGeometry().portPosition(sourceNode, sourcePortType, sourcePort));
    const QPointF targetScene = targetGraphics->sceneBoundingRect().center();
    const QPoint source = view->mapFromScene(sourceScene) + sourceOffset;
    const QPoint target = view->mapFromScene(targetScene);
    QWidget* viewport = view->viewport();

    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(source),
                      QPointF(viewport->mapToGlobal(source)),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    const bool draftStarted = hasDraftConnection(scene);
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
    return draftStarted;
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

void dragCanvasSelection(QGraphicsView* view,
                         const QPoint& start,
                         const QPoint& end,
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    if (!view || !view->viewport()) {
        return;
    }
    QWidget* viewport = view->viewport();
    QMouseEvent press(QEvent::MouseButtonPress,
                      QPointF(start),
                      QPointF(viewport->mapToGlobal(start)),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      modifiers);
    QApplication::sendEvent(viewport, &press);
    for (int step = 1; step <= 6; ++step) {
        const QPoint position = start + (end - start) * step / 6;
        QMouseEvent move(QEvent::MouseMove,
                         QPointF(position),
                         QPointF(viewport->mapToGlobal(position)),
                         Qt::NoButton,
                         Qt::LeftButton,
                         modifiers);
        QApplication::sendEvent(viewport, &move);
    }
    QMouseEvent release(QEvent::MouseButtonRelease,
                        QPointF(end),
                        QPointF(viewport->mapToGlobal(end)),
                        Qt::LeftButton,
                        Qt::NoButton,
                        modifiers);
    QApplication::sendEvent(viewport, &release);
}

void chooseAttachmentSlot(int index) {
    QTimer::singleShot(0, [index] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog) {
                continue;
            }
            auto* combo = dialog->findChild<QComboBox*>(
                QStringLiteral("finepaper.attachmentSlotSelector"));
            if (!combo) {
                continue;
            }
            combo->setCurrentIndex(index);
            dialog->accept();
            return;
        }
    });
}

bool chooseComboData(QComboBox* combo, const QString& value) {
    if (!combo) {
        return false;
    }
    const int index = combo->findData(value);
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    return true;
}

bool chooseComboTextContaining(QComboBox* combo, const QString& text) {
    if (!combo) {
        return false;
    }
    const int index = combo->findText(text, Qt::MatchContains);
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    QApplication::processEvents();
    return true;
}

void chooseMessageBoxButton(QMessageBox::StandardButton button, int attempts = 100) {
    QTimer::singleShot(0, [button, attempts] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (!messageBox) {
                continue;
            }
            if (QAbstractButton* target = messageBox->button(button)) {
                target->click();
            }
            return;
        }
        if (attempts > 1) {
            QTimer::singleShot(
                10, [button, attempts] { chooseMessageBoxButton(button, attempts - 1); });
        }
    });
}

void rejectEndpointCreationDialog(int attempts = 100) {
    QTimer::singleShot(0, [attempts] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog
                || dialog->objectName()
                    != QStringLiteral("finepaper.endpointCreationDialog")) {
                continue;
            }
            dialog->reject();
            return;
        }
        if (attempts > 1) {
            QTimer::singleShot(
                10, [attempts] { rejectEndpointCreationDialog(attempts - 1); });
        }
    });
}

void respondToNewDesignDialog(
    const QString& packageKey = {},
    const QString& designName = {},
    std::optional<int> rows = std::nullopt,
    std::optional<int> columns = std::nullopt,
    bool accept = true) {
    QTimer::singleShot(0, [packageKey, designName, rows, columns, accept] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog
                || dialog->objectName() != QStringLiteral("finepaper.newDesignDialog")) {
                continue;
            }
            auto* selector = dialog->findChild<QComboBox*>(
                QStringLiteral("finepaper.newDesignPackageSelector"));
            check(selector && selector->count() > 0,
                  QStringLiteral("New NoC Design dialog exposes selectable NoC IP Packages"));
            if (selector && !packageKey.isEmpty()) {
                const int index = selector->findData(packageKey);
                check(index >= 0,
                      QStringLiteral("requested NoC IP is selectable in the creation dialog"));
                if (index >= 0) {
                    selector->setCurrentIndex(index);
                }
            }
            if (auto* name = dialog->findChild<QLineEdit*>(
                    QStringLiteral("finepaper.newDesignName"));
                name && !designName.isEmpty()) {
                name->setText(designName);
            }
            if (auto* rowEditor = dialog->findChild<QSpinBox*>(
                    QStringLiteral("finepaper.newDesignRows"));
                rowEditor && rows) {
                rowEditor->setValue(*rows);
            }
            if (auto* columnEditor = dialog->findChild<QSpinBox*>(
                    QStringLiteral("finepaper.newDesignColumns"));
                columnEditor && columns) {
                columnEditor->setValue(*columns);
            }
            if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) {
                if (QAbstractButton* button = buttons->button(
                        accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)) {
                    button->click();
                }
            }
            return;
        }
    });
}

void respondToMeshResizeDialog(
    bool accept = false,
    std::optional<int> rows = std::nullopt,
    std::optional<int> columns = std::nullopt) {
    QTimer::singleShot(0, [accept, rows, columns] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog
                || dialog->objectName()
                    != QStringLiteral("finepaper.meshResizeDialog")) {
                continue;
            }
            check(dialog->findChild<QSpinBox*>(
                      QStringLiteral("finepaper.meshResize.rows"))
                      && dialog->findChild<QSpinBox*>(
                          QStringLiteral("finepaper.meshResize.columns"))
                      && dialog->findChild<QListWidget*>(
                          QStringLiteral("finepaper.meshResize.routerList"))
                      && dialog->findChild<QListWidget*>(
                          QStringLiteral(
                              "finepaper.meshResize.removedMemberships"))
                      && dialog->findChild<QListWidget*>(
                          QStringLiteral(
                              "finepaper.meshResize.removedEdgeOverrides")),
                  QStringLiteral(
                      "Mesh resize dialog exposes topology, assignment, and exact-impact controls"));
            if (auto* rowEditor = dialog->findChild<QSpinBox*>(
                    QStringLiteral("finepaper.meshResize.rows"));
                rowEditor && rows) {
                rowEditor->setValue(*rows);
            }
            if (auto* columnEditor = dialog->findChild<QSpinBox*>(
                    QStringLiteral("finepaper.meshResize.columns"));
                columnEditor && columns) {
                columnEditor->setValue(*columns);
            }
            QApplication::processEvents();
            if (auto* buttons = dialog->findChild<QDialogButtonBox*>(
                    QStringLiteral("finepaper.meshResize.buttons"))) {
                if (QAbstractButton* button = buttons->button(
                        accept ? QDialogButtonBox::Ok
                               : QDialogButtonBox::Cancel)) {
                    button->click();
                }
            }
            return;
        }
    });
}

void createDesignThroughDialog(
    QWidget& window,
    const QString& packageKey = {},
    const QString& designName = {},
    std::optional<int> rows = std::nullopt,
    std::optional<int> columns = std::nullopt) {
    respondToNewDesignDialog(packageKey, designName, rows, columns, true);
    if (auto* createButton = window.findChild<QPushButton*>(
            QStringLiteral("finepaper.createDesign"))) {
        createButton->click();
    }
    QApplication::processEvents();
}

void closeDiscarding(QWidget& window) {
    if (window.isWindowModified()) {
        chooseMessageBoxButton(QMessageBox::Discard);
    }
    window.close();
    QApplication::processEvents();
}

bool createNumberParameterPackage(const QString& sourceRoot,
                                  const QString& destinationRoot) {
    QFile sourceManifest(QDir(sourceRoot).filePath(QStringLiteral("package.json")));
    if (!sourceManifest.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(sourceManifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    QJsonObject package = document.object();
    package.insert(QStringLiteral("id"), QStringLiteral("test.number-parameter"));
    package.insert(QStringLiteral("name"), QStringLiteral("Number Parameter Fixture"));
    package.insert(QStringLiteral("parameters"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("frequencyScale")},
            {QStringLiteral("label"), QStringLiteral("Frequency scale")},
            {QStringLiteral("type"), QStringLiteral("number")},
            {QStringLiteral("default"), 1.25},
            {QStringLiteral("minimum"), 0.1},
            {QStringLiteral("maximum"), 10.0}
        }
    });

    if (!QDir().mkpath(QDir(destinationRoot).filePath(QStringLiteral("runtime/bin")))) {
        return false;
    }
    QFile destinationManifest(
        QDir(destinationRoot).filePath(QStringLiteral("package.json")));
    if (!destinationManifest.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || destinationManifest.write(QJsonDocument(package).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    destinationManifest.close();

    const QString sourceGenerator = QDir(sourceRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    const QString destinationGenerator = QDir(destinationRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    if (!QFile::copy(sourceGenerator, destinationGenerator)) {
        return false;
    }
    return QFile::setPermissions(destinationGenerator, QFile::permissions(sourceGenerator));
}

bool createDomainPresentationPackage(const QString& sourceRoot,
                                     const QString& destinationRoot) {
    QFile sourceManifest(QDir(sourceRoot).filePath(QStringLiteral("package.json")));
    if (!sourceManifest.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(sourceManifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    QJsonObject package = document.object();
    package.insert(QStringLiteral("formatVersion"), 2);
    package.insert(QStringLiteral("id"), QStringLiteral("test.domain-presentation"));
    package.insert(QStringLiteral("name"), QStringLiteral("Domain Presentation Fixture"));
    package.insert(QStringLiteral("domainTypes"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("security-zone")},
            {QStringLiteral("label"), QStringLiteral("Security zones")},
            {QStringLiteral("appliesTo"), QJsonArray{
                QStringLiteral("router"), QStringLiteral("endpoint")}},
            {QStringLiteral("cardinality"), QStringLiteral("multiple")},
            {QStringLiteral("required"), false},
            {QStringLiteral("properties"), QJsonArray{}},
            {QStringLiteral("relations"), QJsonArray{}},
            {QStringLiteral("crossingProperties"), QJsonArray{}}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("fabric-tier")},
            {QStringLiteral("label"), QStringLiteral("Fabric tier")},
            {QStringLiteral("appliesTo"), QJsonArray{QStringLiteral("router")}},
            {QStringLiteral("cardinality"), QStringLiteral("single")},
            {QStringLiteral("required"), false},
            {QStringLiteral("properties"), QJsonArray{}},
            {QStringLiteral("relations"), QJsonArray{}},
            {QStringLiteral("crossingProperties"), QJsonArray{}}
        }
    });
    package.insert(QStringLiteral("runtimeCapabilities"), QJsonObject{
        {QStringLiteral("domainConfiguration"), QJsonObject{
            {QStringLiteral("domains"), true},
            {QStringLiteral("memberships"), true},
            {QStringLiteral("relations"), true},
            {QStringLiteral("crossingPolicies"), true},
            {QStringLiteral("edgeOverrides"), true}
        }}
    });

    if (!QDir().mkpath(QDir(destinationRoot).filePath(QStringLiteral("runtime/bin")))) {
        return false;
    }
    QFile destinationManifest(
        QDir(destinationRoot).filePath(QStringLiteral("package.json")));
    if (!destinationManifest.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || destinationManifest.write(
               QJsonDocument(package).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    destinationManifest.close();

    const QString sourceGenerator = QDir(sourceRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    const QString destinationGenerator = QDir(destinationRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    if (!QFile::copy(sourceGenerator, destinationGenerator)) {
        return false;
    }
    return QFile::setPermissions(destinationGenerator, QFile::permissions(sourceGenerator));
}

bool createRequiredRelationPackage(const QString& sourceRoot,
                                   const QString& destinationRoot) {
    QFile sourceManifest(QDir(sourceRoot).filePath(QStringLiteral("package.json")));
    if (!sourceManifest.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(sourceManifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    QJsonObject package = document.object();
    package.insert(QStringLiteral("formatVersion"), 2);
    package.insert(QStringLiteral("id"), QStringLiteral("test.required-relation"));
    package.insert(QStringLiteral("name"), QStringLiteral("Required Relation Fixture"));
    package.insert(QStringLiteral("domainTypes"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("power")},
            {QStringLiteral("label"), QStringLiteral("Power domains")},
            {QStringLiteral("appliesTo"), QJsonArray{QStringLiteral("router")}},
            {QStringLiteral("cardinality"), QStringLiteral("single")},
            {QStringLiteral("required"), true},
            {QStringLiteral("properties"), QJsonArray{}},
            {QStringLiteral("relations"), QJsonArray{}},
            {QStringLiteral("crossingProperties"), QJsonArray{}}
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("clock")},
            {QStringLiteral("label"), QStringLiteral("Clock domains")},
            {QStringLiteral("appliesTo"), QJsonArray{QStringLiteral("router")}},
            {QStringLiteral("cardinality"), QStringLiteral("single")},
            {QStringLiteral("required"), true},
            {QStringLiteral("properties"), QJsonArray{}},
            {QStringLiteral("relations"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("poweredBy")},
                    {QStringLiteral("label"), QStringLiteral("Powered by")},
                    {QStringLiteral("targetTypes"), QJsonArray{
                        QStringLiteral("power")}},
                    {QStringLiteral("cardinality"), QStringLiteral("single")},
                    {QStringLiteral("required"), true},
                    {QStringLiteral("properties"), QJsonArray{}}
                }
            }},
            {QStringLiteral("crossingProperties"), QJsonArray{}}
        }
    });
    package.insert(QStringLiteral("runtimeCapabilities"), QJsonObject{
        {QStringLiteral("domainConfiguration"), QJsonObject{
            {QStringLiteral("domains"), true},
            {QStringLiteral("memberships"), true},
            {QStringLiteral("relations"), true},
            {QStringLiteral("crossingPolicies"), true},
            {QStringLiteral("edgeOverrides"), true}
        }}
    });

    if (!QDir().mkpath(QDir(destinationRoot).filePath(QStringLiteral("runtime/bin")))) {
        return false;
    }
    QFile destinationManifest(
        QDir(destinationRoot).filePath(QStringLiteral("package.json")));
    if (!destinationManifest.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || destinationManifest.write(
               QJsonDocument(package).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    destinationManifest.close();

    const QString sourceGenerator = QDir(sourceRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    const QString destinationGenerator = QDir(destinationRoot).filePath(
        QStringLiteral("runtime/bin/generate"));
    if (!QFile::copy(sourceGenerator, destinationGenerator)) {
        return false;
    }
    return QFile::setPermissions(destinationGenerator, QFile::permissions(sourceGenerator));
}

bool writeDomainPresentationDesign(const QString& path) {
    const QJsonObject design{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-design")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("id"), QStringLiteral("domain_presentation_design")},
        {QStringLiteral("name"), QStringLiteral("Domain Presentation Design")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.domain-presentation")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 1},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("parameters"), QJsonObject{
            {QStringLiteral("dataWidth"), 64},
            {QStringLiteral("flitWidth"), 128},
            {QStringLiteral("addrWidth"), 32}
        }},
        {QStringLiteral("endpoints"), QJsonArray{}},
        {QStringLiteral("domains"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("zone-a")},
                {QStringLiteral("type"), QStringLiteral("security-zone")},
                {QStringLiteral("name"), QStringLiteral("Trusted")},
                {QStringLiteral("properties"), QJsonObject{}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("zone-b")},
                {QStringLiteral("type"), QStringLiteral("security-zone")},
                {QStringLiteral("name"), QStringLiteral("Restricted")},
                {QStringLiteral("properties"), QJsonObject{}}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("tier-core")},
                {QStringLiteral("type"), QStringLiteral("fabric-tier")},
                {QStringLiteral("name"), QStringLiteral("Core")},
                {QStringLiteral("properties"), QJsonObject{}}
            }
        }},
        {QStringLiteral("domainMemberships"), QJsonArray{
            QJsonObject{
                {QStringLiteral("element"), QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("router")},
                    {QStringLiteral("id"), QStringLiteral("r-0-0")}
                }},
                {QStringLiteral("assignments"), QJsonObject{
                    {QStringLiteral("security-zone"), QJsonArray{
                        QStringLiteral("zone-a")}},
                    {QStringLiteral("fabric-tier"), QJsonArray{
                        QStringLiteral("tier-core")}}
                }}
            },
            QJsonObject{
                {QStringLiteral("element"), QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("router")},
                    {QStringLiteral("id"), QStringLiteral("r-1-0")}
                }},
                {QStringLiteral("assignments"), QJsonObject{
                    {QStringLiteral("security-zone"), QJsonArray{
                        QStringLiteral("zone-b")}},
                    {QStringLiteral("fabric-tier"), QJsonArray{
                        QStringLiteral("tier-core")}}
                }}
            }
        }},
        {QStringLiteral("domainRelations"), QJsonArray{}},
        {QStringLiteral("crossingPolicies"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("zone-transition")},
                {QStringLiteral("domainType"), QStringLiteral("security-zone")},
                {QStringLiteral("from"), QStringLiteral("zone-a")},
                {QStringLiteral("to"), QStringLiteral("zone-b")},
                {QStringLiteral("properties"), QJsonObject{}}
            }
        }},
        {QStringLiteral("edgeOverrides"), QJsonArray{}}
    };
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(QJsonDocument(design).toJson(QJsonDocument::Indented)) >= 0;
}

bool writeTopologyWorkspaceDesign(const QString& path) {
    const QJsonObject design{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-design")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("topology_workspace_persistence")},
        {QStringLiteral("name"), QStringLiteral("Topology Workspace Persistence")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("finepaper.noc")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("parameters"), QJsonObject{}},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("persisted_master")},
                {QStringLiteral("type"), QStringLiteral("master")},
                {QStringLiteral("parameters"), QJsonObject{}},
                {QStringLiteral("attachment"), QJsonObject{
                    {QStringLiteral("router"), QJsonObject{
                        {QStringLiteral("x"), 0},
                        {QStringLiteral("y"), 0}
                    }}
                }}
            }
        }}
    };
    const QByteArray payload = QJsonDocument(design).toJson(
        QJsonDocument::Indented);
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(payload) == payload.size();
}

QSet<QString> currentTopologyWorkspaceKeys() {
    QSettings settings;
    QSet<QString> keys;
    for (const QString& key : settings.allKeys()) {
        if (key.startsWith(QStringLiteral("workbench/workspaces/v1/"))
            && key.endsWith(QStringLiteral("/topology"))) {
            keys.insert(key);
        }
    }
    return keys;
}

bool writeMissingPackageDesign(const QString& path) {
    const QJsonObject design{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-design")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("missing_package_design")},
        {QStringLiteral("name"), QStringLiteral("Missing Package Design")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.never-loaded")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("parameters"), QJsonObject{}},
        {QStringLiteral("endpoints"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("client_0")},
                {QStringLiteral("type"), QStringLiteral("client")},
                {QStringLiteral("parameters"), QJsonObject{}},
                {QStringLiteral("attachment"), QJsonObject{
                    {QStringLiteral("router"), QJsonObject{
                        {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}}},
                    {QStringLiteral("slot"), QStringLiteral("local0")}
                }}
            },
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("client_1")},
                {QStringLiteral("type"), QStringLiteral("client")},
                {QStringLiteral("parameters"), QJsonObject{}},
                {QStringLiteral("attachment"), QJsonObject{
                    {QStringLiteral("router"), QJsonObject{
                        {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}}},
                    {QStringLiteral("slot"), QStringLiteral("local1")}
                }}
            }
        }}
    };
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(QJsonDocument(design).toJson(QJsonDocument::Indented)) >= 0;
}

} // namespace

int main(int argc, char** argv) {
    QTemporaryDir configRoot(QStringLiteral("/tmp/finepaper-gui-config-XXXXXX"));
    QTemporaryDir outputRoot(QStringLiteral("/tmp/finepaper-gui-output-XXXXXX"));
    check(configRoot.isValid(), QStringLiteral("temporary GUI settings root is available"));
    check(outputRoot.isValid(), QStringLiteral("temporary GUI output root is available"));
    qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());

    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    EndpointCreationAutoAccepter endpointCreationAutoAccepter;
    application.installEventFilter(&endpointCreationAutoAccepter);
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(QStringLiteral("finepaper-gui-smoke"));

    check(finepaper::workbench::designWorkspaceKey(
              QStringLiteral("a"), QStringLiteral("b:c"), QStringLiteral("d"))
              != finepaper::workbench::designWorkspaceKey(
                  QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c:d")),
          QStringLiteral("Workspace design keys remain unambiguous for arbitrary ids"));

    const QString projectRoot = QString::fromUtf8(FINEPAPER_SOURCE_DIR);
    finepaper::RuntimeLocations locations{
        QStringList{QDir(projectRoot).filePath(QStringLiteral("packages/finepaper-noc"))},
        outputRoot.path()};
    QSettings pollutedSettings;
    pollutedSettings.setValue(
        finepaper::workbench::packageRootsSetting,
        QStringList{
            locations.packageRoots.front(),
            QDir(configRoot.path()).filePath(QStringLiteral("missing-installed-package"))});
    finepaper::FinepaperMainWindow window(locations);
    pollutedSettings.remove(finepaper::workbench::packageRootsSetting);
    window.show();
    application.processEvents();

    auto* centerViews = qobject_cast<QTabWidget*>(window.centralWidget());
    check(centerViews && centerViews->count() == 5,
          QStringLiteral("central workbench exposes five switchable views"));
    if (centerViews) {
        check(centerViews->tabText(0) == QStringLiteral("NoC Editor"),
              QStringLiteral("NoC Editor is the default central view"));
        check(centerViews->tabText(1) == QStringLiteral("Domain Configuration")
                  && centerViews->widget(1)->objectName()
                      == QStringLiteral(
                          "finepaper.domainConfigurationWorkspace"),
              QStringLiteral(
                  "complete Domain configuration is a first-class persistent central Workspace"));
        check(centerViews->tabText(2) == QStringLiteral("Design Extensions")
                  && centerViews->widget(2)->objectName()
                      == QStringLiteral("finepaper.designExtensionsWorkspace"),
              QStringLiteral(
                  "Package-driven Design Extensions are a first-class central Workspace"));
        check(centerViews->tabText(3) == QStringLiteral("Performance Analysis"),
              QStringLiteral("performance analysis is a central view"));
        check(centerViews->tabText(4) == QStringLiteral("Problem Report"),
              QStringLiteral("problem report is a central view"));
    }
    QAction* designExtensionsViewAction = actionWithText(
        window, QStringLiteral("Design Extensions"));
    if (designExtensionsViewAction) {
        designExtensionsViewAction->trigger();
        application.processEvents();
    }
    check(designExtensionsViewAction && centerViews
              && centerViews->currentWidget()
                     ->objectName()
                     == QStringLiteral("finepaper.designExtensionsWorkspace"),
          QStringLiteral(
              "View menu can select the Design Extensions Workspace"));
    if (centerViews) {
        centerViews->setCurrentIndex(0);
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

    auto* activePackage = window.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackage"));
    auto* availablePackages = window.findChild<QLabel*>(
        QStringLiteral("finepaper.availablePackages"));
    auto* endpointPalette = window.findChild<QListWidget*>(QStringLiteral("finepaper.endpointPalette"));
    auto* initialDomainSelector = window.findChild<QComboBox*>(
        finepaper::workbench::domainLayerSelectorName);
    check(availablePackages
              && availablePackages->text().startsWith(QStringLiteral("1 NoC IP Package")),
          QStringLiteral("runtime NoC IP availability is summarized in the workbench"));
    check(activePackage
              && activePackage->text().contains(QStringLiteral("No design is open")),
          QStringLiteral("the workbench does not imply an active IP before design creation"));
    check(endpointPalette && endpointPalette->count() == 0,
          QStringLiteral("Endpoint types are shown only for the active design Package"));
    check(initialDomainSelector && initialDomainSelector->count() == 1
              && initialDomainSelector->itemData(0).toString().isEmpty()
              && !initialDomainSelector->isEnabled(),
          QStringLiteral("a Package without Domain schema exposes only a disabled None layer"));
    QAction* initialSaveAction = actionWithText(window, QStringLiteral("Save"));
    QAction* saveAsAction = actionWithText(window, QStringLiteral("Save As…"));
    QAction* initialValidateAction = actionWithText(
        window, QStringLiteral("Validate / DRC"));
    QAction* initialGenerateAction = actionWithText(window, QStringLiteral("Generate RTL"));
    auto* applyParameters = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
    auto* resizeMeshButton = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.resizeMesh"));
    QAction* resizeMeshAction = window.findChild<QAction*>(
        QStringLiteral("finepaper.resizeMeshAction"));
    check(endpointPalette && !endpointPalette->isEnabled(),
          QStringLiteral("Endpoint Palette is disabled until a design exists"));
    check(initialSaveAction && !initialSaveAction->isEnabled()
              && saveAsAction && !saveAsAction->isEnabled(),
          QStringLiteral("save actions are disabled without a design"));
    check(initialValidateAction && !initialValidateAction->isEnabled()
              && initialGenerateAction && !initialGenerateAction->isEnabled(),
          QStringLiteral("validation and generation are disabled without a design"));
    check(applyParameters && !applyParameters->isEnabled(),
          QStringLiteral("parameter application is disabled without a design"));
    check(resizeMeshButton && !resizeMeshButton->isEnabled()
              && resizeMeshAction && !resizeMeshAction->isEnabled(),
          QStringLiteral("Mesh resize entry points are disabled without a design"));

    auto* createButton = window.findChild<QPushButton*>(QStringLiteral("finepaper.createDesign"));
    check(createButton && createButton->isEnabled(),
          QStringLiteral("stale or overlapping installed roots do not block NoC IP selection"));
    createDesignThroughDialog(window, QStringLiteral("finepaper.noc@1.0.0"));
    check(window.isWindowModified(),
          QStringLiteral("creating a design marks the workbench dirty"));
    check(activePackage
              && activePackage->text().contains(QStringLiteral("finepaper.noc@1.0.0"))
              && endpointPalette && endpointPalette->count() == 2
              && endpointPalette->isEnabled()
              && initialSaveAction && initialSaveAction->isEnabled()
              && saveAsAction && saveAsAction->isEnabled()
              && initialValidateAction && initialValidateAction->isEnabled()
              && initialGenerateAction && initialGenerateAction->isEnabled(),
          QStringLiteral("active NoC IP, design actions and Endpoint Palette stay aligned"));
    check(applyParameters && !applyParameters->isEnabled(),
          QStringLiteral("Package defaults are editable but do not enable a no-op Apply"));
    check(resizeMeshButton && resizeMeshButton->isEnabled()
              && resizeMeshAction && resizeMeshAction->isEnabled(),
          QStringLiteral("Mesh resize entry points follow active Package metadata"));
    auto* designOverview = window.findChild<QLabel*>(
        QStringLiteral("finepaper.designOverview"));
    const QString overviewBeforeCancelledResize = designOverview
        ? designOverview->text() : QString();
    const bool modifiedBeforeCancelledResize = window.isWindowModified();
    if (resizeMeshButton) {
        respondToMeshResizeDialog(false);
        resizeMeshButton->click();
        application.processEvents();
    }
    check(designOverview
              && designOverview->text() == overviewBeforeCancelledResize
              && window.isWindowModified() == modifiedBeforeCancelledResize,
          QStringLiteral("cancelling Mesh resize leaves the current design unchanged"));

    auto* editorWidget = window.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor"));
    auto* nodeEditor = dynamic_cast<finepaper::NocNodeEditor*>(editorWidget);
    auto* endpointConfigurationPanel = dynamic_cast<finepaper::EndpointConfigurationPanel*>(
        window.findChild<QWidget*>(
            QStringLiteral("finepaper.endpointConfigurationPanel")));
    const QString endpointDraftDesignIdentity = QStringLiteral("design-session-1");
    auto* graphicsView = nodeEditor ? nodeEditor->findChild<QGraphicsView*>() : nullptr;
    auto* animatedView = dynamic_cast<finepaper::AnimatedGraphicsView*>(graphicsView);
    auto* graphicsScene = graphicsView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(graphicsView->scene())
        : nullptr;
    check(graphicsView && graphicsView->scene() && !graphicsView->scene()->items().isEmpty(),
          QStringLiteral("created Mesh is projected into the QtNodes editor"));
    QAction* selectCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::selectCanvasActionName);
    QAction* panCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::panCanvasActionName);
    check(selectCanvasAction && panCanvasAction && selectCanvasAction->isChecked()
              && nodeEditor
              && nodeEditor->canvasInteractionMode()
                  == finepaper::NocCanvasInteractionMode::Select
              && graphicsView->dragMode() == QGraphicsView::RubberBandDrag,
          QStringLiteral("canvas exposes an explicit, default box-selection mode"));
    if (panCanvasAction) {
        panCanvasAction->trigger();
        application.processEvents();
    }
    check(nodeEditor
              && nodeEditor->canvasInteractionMode()
                  == finepaper::NocCanvasInteractionMode::Pan
              && graphicsView->dragMode() == QGraphicsView::ScrollHandDrag,
          QStringLiteral("Pan mode explicitly restores empty-canvas viewport dragging"));
    if (selectCanvasAction) {
        selectCanvasAction->trigger();
        application.processEvents();
    }
    QAction* newAction = actionWithText(window, QStringLiteral("New NoC Design…"));
    if (newAction) {
        respondToNewDesignDialog(QStringLiteral("finepaper.noc@1.0.0"));
        chooseMessageBoxButton(QMessageBox::Cancel);
        newAction->trigger();
        application.processEvents();
    }
    check(window.isWindowModified()
              && graphicsScene && !graphicsScene->items().isEmpty(),
          QStringLiteral("cancelling the unsaved-changes prompt preserves the current design"));

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
    if (graphicsView && graphicsScene && router00 && router10
        && router01 && router11) {
        const QRectF topRowScene = graphicsScene->nodeGraphicsObject(*router00)
                                       ->sceneBoundingRect()
                                       .united(graphicsScene->nodeGraphicsObject(*router10)
                                                   ->sceneBoundingRect())
                                       .adjusted(-18.0, -18.0, 18.0, 18.0);
        const QRect topRowViewport = QRect(
            graphicsView->mapFromScene(topRowScene.topLeft()),
            graphicsView->mapFromScene(topRowScene.bottomRight())).normalized();
        graphicsScene->clearSelection();
        dragCanvasSelection(graphicsView,
                            topRowViewport.topLeft(),
                            topRowViewport.bottomRight());
        application.processEvents();
        check(graphicsScene->nodeGraphicsObject(*router00)->isSelected()
                  && graphicsScene->nodeGraphicsObject(*router10)->isSelected()
                  && !graphicsScene->nodeGraphicsObject(*router01)->isSelected()
                  && !graphicsScene->nodeGraphicsObject(*router11)->isSelected(),
              QStringLiteral("real empty-canvas drag box-selects the intended Mesh row"));
        graphicsScene->clearSelection();
        application.processEvents();
    }
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
        if (auto* eastConnection = graphicsScene->connectionGraphicsObject(eastLink)) {
            eastConnection->setSelected(true);
            if (QAction* deleteSelection = animatedView
                    ? animatedView->deleteSelectionAction() : nullptr) {
                deleteSelection->trigger();
                application.processEvents();
            }
        }
        check(graphicsScene->graphModel().connectionExists(eastLink),
              QStringLiteral("Delete cannot remove a derived Router topology link"));
        graphicsScene->clearSelection();
    }

    check(nodeEditor
              && nodeEditor->routerCollapsed(QStringLiteral("r-0-0"))
              && nodeEditor->routerCollapsed(QStringLiteral("r-1-0"))
              && nodeEditor->routerCollapsed(QStringLiteral("r-0-1"))
              && nodeEditor->routerCollapsed(QStringLiteral("r-1-1")),
          QStringLiteral("Routers are collapsed by default in a new workspace"));
    if (nodeEditor) {
        nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), false);
        nodeEditor->setRouterCollapsed(QStringLiteral("r-1-0"), false);
        nodeEditor->setRouterCollapsed(QStringLiteral("r-0-1"), false);
        nodeEditor->setRouterCollapsed(QStringLiteral("r-1-1"), false);
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
    if (routerNode && graphicsScene) {
        routerNode->setSelected(true);
        graphicsScene->nodeSelected(routerNode->nodeId());
        application.processEvents();
    }
    const QPointF movedRouterPosition(525.0, 415.0);
    const auto movableRouter00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    if (graphicsView && graphicsScene && movableRouter00) {
        auto* routerGraphics = graphicsScene->nodeGraphicsObject(*movableRouter00);
        if (!routerGraphics) {
            check(false, QStringLiteral("expanded Router projection is available for dragging"));
        } else {
        const QPointF currentRouterPosition = graphicsScene->graphModel().nodeData(
            *movableRouter00, QtNodes::NodeRole::Position).toPointF();
        const QPointF targetCursorPosition = routerGraphics->sceneBoundingRect().center()
            + movedRouterPosition - currentRouterPosition;
        dragNodeTo(graphicsView,
                   graphicsScene,
                   *movableRouter00,
                   graphicsView->mapFromScene(targetCursorPosition));
        application.processEvents();
        application.processEvents();
        }
    }
    const std::optional<QPointF> draggedRouterPosition = nodeEditor
        ? nodeEditor->routerVisualPosition(QStringLiteral("r-0-0")) : std::nullopt;
    check(draggedRouterPosition
              && QLineF(*draggedRouterPosition, movedRouterPosition).length() < 1.0,
          QStringLiteral("dragging a Router changes its user-arranged workspace position (%1)")
              .arg(draggedRouterPosition
                       ? QStringLiteral("actual %1,%2")
                             .arg(draggedRouterPosition->x())
                             .arg(draggedRouterPosition->y())
                       : QStringLiteral("missing")));
    auto* shortcutTextInput = window.findChild<QLineEdit*>(
        QStringLiteral("finepaper.outputRoot"));
    if (shortcutTextInput) {
        shortcutTextInput->setFocus();
        shortcutTextInput->setCursorPosition(shortcutTextInput->text().size());
        QKeyEvent pressR(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
        QKeyEvent releaseR(QEvent::KeyRelease, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
        QApplication::sendEvent(shortcutTextInput, &pressR);
        QApplication::sendEvent(shortcutTextInput, &releaseR);
        application.processEvents();
    }
    const std::optional<QPointF> routerAfterTextInput = nodeEditor
        ? nodeEditor->routerVisualPosition(QStringLiteral("r-0-0")) : std::nullopt;
    check(shortcutTextInput && shortcutTextInput->text().endsWith(QLatin1Char('r'))
              && routerAfterTextInput
              && QLineF(*routerAfterTextInput, movedRouterPosition).length() < 1.0,
          QStringLiteral("single-key canvas shortcuts do not fire while editing text"));
    const auto selectedMovedRouter = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    check(selectedMovedRouter
              && graphicsScene->nodeGraphicsObject(*selectedMovedRouter)->isSelected(),
          QStringLiteral("moving and rebuilding a Router preserves its selection"));

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
        check(animatedView && animatedView->endpointDragActive()
                  && animatedView->endpointDragOverRouter(),
              QStringLiteral("original animated canvas shows a Router attachment preview"));
        QDropEvent drop(QPointF(dropPosition),
                        Qt::CopyAction,
                        endpointMime.get(),
                        Qt::LeftButton,
                        Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &drop);
        application.processEvents();
        check(dragEnter.isAccepted() && drop.isAccepted(),
              QStringLiteral("Endpoint MIME can be dragged from the Palette onto a Router"));
        check(animatedView && !animatedView->endpointDragActive(),
              QStringLiteral("Endpoint drag preview ends after the drop"));
    }
    const std::size_t nodesAfterEndpoint = graphicsScene
        ? graphicsScene->graphModel().allNodeIds().size() : 0;
    check(nodesAfterEndpoint == nodesBeforeEndpoint + 1,
          QStringLiteral("dropping an Endpoint type attaches it to the target Router"));

    const auto attachedEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master_0"));
    const auto currentRouter00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    check(attachedEndpoint && currentRouter00
              && graphicsScene->graphModel().connectionExists(
                  {*attachedEndpoint,
                   finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
                   *currentRouter00,
                  finepaper::portIndex(finepaper::RouterInputPort::Endpoint)}),
          QStringLiteral("Endpoint uses the Router's dedicated EP attachment port"));
    const auto firstAttachmentPort = attachedEndpoint
        ? attachmentPortForEndpoint(graphicsScene, *attachedEndpoint) : std::nullopt;

    if (graphicsView && graphicsScene && currentRouter00 && endpointMime) {
        const QPoint dropPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*currentRouter00)
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
    }
    const auto secondAttachedEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master_1"));
    const auto secondAttachmentPort = secondAttachedEndpoint
        ? attachmentPortForEndpoint(graphicsScene, *secondAttachedEndpoint) : std::nullopt;
    check(firstAttachmentPort && secondAttachmentPort
              && *firstAttachmentPort != *secondAttachmentPort,
          QStringLiteral("multiple Endpoint attachments use distinct Package-defined EP ports"));
    bool attachedEndpointDeletionRequested = false;
    const auto originalEndpointDeletionRequested = nodeEditor
        ? nodeEditor->endpointDeletionRequested
        : std::function<bool(const QString&)>{};
    if (nodeEditor) {
        nodeEditor->endpointDeletionRequested = [&, originalEndpointDeletionRequested](
                                                    const QString& endpointId) {
            attachedEndpointDeletionRequested = true;
            return originalEndpointDeletionRequested
                ? originalEndpointDeletionRequested(endpointId) : false;
        };
    }
    if (graphicsView && graphicsScene && secondAttachedEndpoint) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*secondAttachedEndpoint)
                ->sceneBoundingRect().center());
        sendContextMenu(graphicsView, endpointPosition);
        application.processEvents();
    }
    auto* secondEndpointMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    QAction* deleteSecondEndpoint = secondEndpointMenu ? secondEndpointMenu->findChild<QAction*>(
        finepaper::workbench::deleteEndpointActionName) : nullptr;
    if (deleteSecondEndpoint) {
        deleteSecondEndpoint->trigger();
        if (secondEndpointMenu) {
            secondEndpointMenu->close();
        }
        application.processEvents();
    }
    if (nodeEditor) {
        nodeEditor->endpointDeletionRequested = originalEndpointDeletionRequested;
    }
    check(attachedEndpointDeletionRequested,
          QStringLiteral("attached Endpoint Delete uses the permanent-deletion callback"));

    const auto currentRouter00AfterSecond = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    if (graphicsView && graphicsScene && currentRouter00AfterSecond) {
        const QPoint routerPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*currentRouter00AfterSecond)
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

    const auto attachedEndpointForMenu = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master_0"));
    if (graphicsView && graphicsScene && attachedEndpointForMenu) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*attachedEndpointForMenu)
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
              && graphicsScene->graphModel().connectionPossible(pendingAttachment),
          QStringLiteral("Endpoint EP to Router EP is the only allowed manual port connection"));
    const auto routerForConnectionRule = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    check(routerForConnectionRule && connectionRouter
              && !graphicsScene->graphModel().connectionPossible({
                  *routerForConnectionRule,
                  finepaper::portIndex(finepaper::RouterOutputPort::East),
                  *connectionRouter,
                  finepaper::portIndex(finepaper::RouterInputPort::West)}),
          QStringLiteral("manual connection cannot edit derived Router topology links"));
    bool routerPortStartedDraft = false;
    if (routerForConnectionRule && connectionRouter) {
        routerPortStartedDraft = dragPortConnection(
            graphicsView,
            graphicsScene,
            *routerForConnectionRule,
            finepaper::portIndex(finepaper::RouterOutputPort::East),
            *connectionRouter,
            finepaper::portIndex(finepaper::RouterInputPort::West));
        application.processEvents();
    }
    check(!routerPortStartedDraft,
          QStringLiteral("Router direction ports do not start user wiring"));

    bool endpointToTopologyDraftStarted = false;
    std::optional<QPoint> eastOutputPosition = std::nullopt;
    if (pendingEndpoint && routerForConnectionRule && graphicsView
        && graphicsScene) {
        auto* routerGraphics = graphicsScene->nodeGraphicsObject(
            *routerForConnectionRule);
        if (routerGraphics) {
            eastOutputPosition = graphicsView->mapFromScene(
                routerGraphics->mapToScene(
                    graphicsScene->nodeGeometry().portPosition(
                        *routerForConnectionRule,
                        QtNodes::PortType::Out,
                        finepaper::portIndex(
                            finepaper::RouterOutputPort::East))));
        }
        endpointToTopologyDraftStarted = dragPortConnection(
            graphicsView,
            graphicsScene,
            *pendingEndpoint,
            finepaper::portIndex(
                finepaper::EndpointOutputPort::Attachment),
            *routerForConnectionRule,
            finepaper::portIndex(finepaper::RouterOutputPort::East),
            QtNodes::PortType::Out);
        application.processEvents();
    }
    check(endpointToTopologyDraftStarted
              && nodeIdWithCaptionPrefix(
                  graphicsScene,
                  QStringLiteral("Unattached\nMaster endpoint")),
          QStringLiteral(
              "Endpoint EP release on Router East stays unattached"));

    const std::size_t nodesBeforeTopologyDrop = graphicsScene
        ? graphicsScene->graphModel().allNodeIds().size() : 0;
    bool topologyDropBlockedPreview = false;
    bool topologyDropRejected = false;
    if (eastOutputPosition && graphicsView && endpointMime) {
        QDragEnterEvent dragEnter(
            *eastOutputPosition,
            Qt::CopyAction,
            endpointMime.get(),
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &dragEnter);
        topologyDropBlockedPreview = animatedView
            && animatedView->endpointDragBlocked();
        QDropEvent drop(
            QPointF(*eastOutputPosition),
            Qt::CopyAction,
            endpointMime.get(),
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(graphicsView->viewport(), &drop);
        topologyDropRejected = !drop.isAccepted();
        application.processEvents();
    }
    check(topologyDropBlockedPreview && topologyDropRejected
              && graphicsScene
              && graphicsScene->graphModel().allNodeIds().size()
                  == nodesBeforeTopologyDrop,
          QStringLiteral(
              "Palette preview and drop both reject Router topology outputs"));

    bool staleConnectionWasQueued = false;
    if (pendingEndpoint && connectionRouter && nodeEditor && graphicsScene) {
        graphicsScene->graphModel().addConnection(pendingAttachment);
        staleConnectionWasQueued = graphicsScene->graphModel().connectionExists(
            pendingAttachment);
        nodeEditor->regularizeLayout();
        application.processEvents();
        application.processEvents();
    }
    const auto pendingAfterProjectionRebuild = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("Unattached\nMaster endpoint"));
    check(staleConnectionWasQueued && pendingAfterProjectionRebuild
              && !nodeIdWithCaptionPrefix(
                  graphicsScene, QStringLiteral("master_")),
          QStringLiteral(
              "a projection rebuild discards a queued connection from its old graph"));
    const auto connectionRouterAfterProjectionRebuild = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-1-0"));
    bool endpointPortStartedDraft = false;
    if (pendingAfterProjectionRebuild
        && connectionRouterAfterProjectionRebuild) {
        endpointPortStartedDraft = dragPortToNodeBody(
            graphicsView,
            graphicsScene,
            *pendingAfterProjectionRebuild,
            QtNodes::PortType::Out,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *connectionRouterAfterProjectionRebuild);
        application.processEvents();
        application.processEvents();
    }
    check(endpointPortStartedDraft,
          QStringLiteral("Endpoint EP starts a visible draft line"));
    check(!nodeIdWithCaptionPrefix(
               graphicsScene, QStringLiteral("Unattached\nMaster endpoint")),
          QStringLiteral("Endpoint draft line can release on the Router body"));
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

    bool routerEndpointStartedDraft = false;
    const auto routerWithFreeAttachmentPort = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-1"));
    if (manuallyAttachedEndpoint && routerWithFreeAttachmentPort) {
        routerEndpointStartedDraft = dragPortToNodeBody(
            graphicsView,
            graphicsScene,
            *routerWithFreeAttachmentPort,
            QtNodes::PortType::In,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint),
            *manuallyAttachedEndpoint);
        application.processEvents();
        application.processEvents();
    }
    check(routerEndpointStartedDraft,
          QStringLiteral("Router EP starts a visible draft line toward an Endpoint body"));
    check(endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-1")).has_value(),
          QStringLiteral("Router EP reassigns the Endpoint through the application layer"));
    const std::optional<QPointF> endpointPositionBeforeDirectRewire = nodeEditor
        ? nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
        : std::nullopt;
    bool attachedEndpointStartedDraft = false;
    const auto endpointForDirectRewire = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    const auto routerForDirectRewire = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-1-0"));
    if (endpointForDirectRewire && routerForDirectRewire) {
        attachedEndpointStartedDraft = dragPortToNodeBody(
            graphicsView,
            graphicsScene,
            *endpointForDirectRewire,
            QtNodes::PortType::Out,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *routerForDirectRewire,
            QPoint(-10, 0));
        application.processEvents();
        application.processEvents();
    }
    check(attachedEndpointStartedDraft
              && endpointAttachedToRouter(
                  graphicsScene, QStringLiteral("r-1-0"))
              && nodeEditor
              && nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
                     == endpointPositionBeforeDirectRewire,
          QStringLiteral("an attached Endpoint can rewire from a forgiving EP hit area without moving visually"));

    const auto endpointForRouterOverlap = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    const auto unrelatedRouter = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    QPoint routerOverlapTarget;
    if (graphicsView && graphicsScene
        && endpointForRouterOverlap && unrelatedRouter) {
        routerOverlapTarget = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*unrelatedRouter)
                ->sceneBoundingRect().center());
        dragNodeTo(graphicsView, graphicsScene,
                   *endpointForRouterOverlap, routerOverlapTarget);
        application.processEvents();
        application.processEvents();
    }
    const auto endpointAfterRouterOverlap = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    check(endpointAfterRouterOverlap
              && QLineF(
                     graphicsScene->nodeGraphicsObject(*endpointAfterRouterOverlap)
                         ->sceneBoundingRect().center(),
                     graphicsView->mapToScene(routerOverlapTarget)).length() < 4.0
              && endpointAttachedToRouter(
                  graphicsScene, QStringLiteral("r-1-0")),
          QStringLiteral("dragging an Endpoint body over another Router changes only its workspace position"));

    const QPoint freeEndpointTarget = blankViewportPosition(graphicsView);
    const QPointF freeEndpointTargetScene = graphicsView
        ? graphicsView->mapToScene(freeEndpointTarget)
        : QPointF();
    const auto endpointForFreePlacement = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    if (endpointForFreePlacement) {
        dragNodeTo(graphicsView, graphicsScene,
                   *endpointForFreePlacement, freeEndpointTarget);
        application.processEvents();
        application.processEvents();
    }
    const std::optional<QPointF> freelyPlacedEndpoint = nodeEditor
        ? nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
        : std::nullopt;
    const auto selectedFreeEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    check(freelyPlacedEndpoint && selectedFreeEndpoint
              && QLineF(
                     graphicsScene->nodeGraphicsObject(*selectedFreeEndpoint)
                         ->sceneBoundingRect().center(),
                     freeEndpointTargetScene).length() < 4.0
              && endpointAttachedToRouter(graphicsScene, QStringLiteral("r-1-0")),
          QStringLiteral("moving an attached Endpoint freely reaches the requested canvas position without rewiring"));
    check(selectedFreeEndpoint
              && graphicsScene->nodeGraphicsObject(*selectedFreeEndpoint)->isSelected(),
          QStringLiteral("releasing a freely moved Endpoint keeps it selected"));
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

    QAction* regularizeAction = window.findChild<QAction*>(
        finepaper::workbench::regularizeActionName);
    check(regularizeAction != nullptr,
          QStringLiteral("toolbar exposes a topology regularization action"));
    if (regularizeAction) {
        regularizeAction->trigger();
        application.processEvents();
    }
    const QPointF regularRouter11(
        finepaper::nocEditorMetrics().routerHorizontalSpacing,
        finepaper::nocEditorMetrics().routerVerticalSpacing);
    const QPointF regularEndpointPosition(
        regularRouter11.x() - finepaper::nocEditorMetrics().endpointHorizontalOffset,
        regularRouter11.y() + finepaper::nocEditorMetrics().endpointTopOffset);
    check(nodeEditor
              && nodeEditor->routerVisualPosition(QStringLiteral("r-0-0"))
                     == std::optional<QPointF>(QPointF(0.0, 0.0))
              && nodeEditor->endpointVisualPosition(QStringLiteral("master_0"))
                     == std::optional<QPointF>(regularEndpointPosition),
          QStringLiteral("regularize restores topology positions instead of only fitting the view"));
    const auto regularizedEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master_0"));
    check(regularizedEndpoint
              && graphicsScene->nodeGraphicsObject(*regularizedEndpoint)->isSelected(),
          QStringLiteral("regularization preserves the selected Endpoint"));
    const QPoint selectionClearPosition = blankViewportPosition(graphicsView);
    if (graphicsView && graphicsView->viewport()) {
        QWidget* viewport = graphicsView->viewport();
        QMouseEvent press(QEvent::MouseButtonPress,
                          QPointF(selectionClearPosition),
                          QPointF(viewport->mapToGlobal(selectionClearPosition)),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        QMouseEvent release(QEvent::MouseButtonRelease,
                            QPointF(selectionClearPosition),
                            QPointF(viewport->mapToGlobal(selectionClearPosition)),
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        application.processEvents();
    }
    check(regularizedEndpoint
              && !graphicsScene->nodeGraphicsObject(*regularizedEndpoint)->isSelected(),
          QStringLiteral("selection changes only when another node or the canvas is clicked"));

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
    const auto bodyDropRouter = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-1"));
    QPoint bodyDropTarget;
    if (graphicsView && graphicsScene && contextPendingEndpoint && bodyDropRouter) {
        bodyDropTarget = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*bodyDropRouter)
                ->sceneBoundingRect().center());
        dragNodeTo(graphicsView, graphicsScene, *contextPendingEndpoint, bodyDropTarget);
        application.processEvents();
        application.processEvents();
    }
    const auto pendingAfterBodyDrop = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("Unattached\nMaster endpoint"));
    const std::optional<QPointF> pendingBodyPosition = pendingAfterBodyDrop
        ? std::optional<QPointF>(graphicsScene->graphModel().nodeData(
              *pendingAfterBodyDrop, QtNodes::NodeRole::Position).toPointF())
        : std::nullopt;
    check(pendingAfterBodyDrop
              && QLineF(
                     graphicsScene->nodeGraphicsObject(*pendingAfterBodyDrop)
                         ->sceneBoundingRect().center(),
                     graphicsView->mapToScene(bodyDropTarget)).length() < 4.0
              && !endpointAttachedToRouter(
                  graphicsScene, QStringLiteral("r-0-1")),
          QStringLiteral("moving an unattached Endpoint body onto a Router keeps placement and attachment separate"));

    bool pendingBodyConnectionStarted = false;
    const auto routerForPendingConnection = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-1"));
    if (pendingAfterBodyDrop && routerForPendingConnection) {
        pendingBodyConnectionStarted = dragPortToNodeBody(
            graphicsView,
            graphicsScene,
            *pendingAfterBodyDrop,
            QtNodes::PortType::Out,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *routerForPendingConnection,
            QPoint(-8, 0));
        application.processEvents();
        application.processEvents();
    }
    const auto bodyAttachedEndpoint = endpointAttachedToRouter(
        graphicsScene, QStringLiteral("r-0-1"));
    check(pendingBodyConnectionStarted && bodyAttachedEndpoint
              && pendingBodyPosition
              && graphicsScene->graphModel().nodeData(
                     *bodyAttachedEndpoint, QtNodes::NodeRole::Position).toPointF()
                     == *pendingBodyPosition,
          QStringLiteral("dragging the EP port creates the Router attachment without snapping the node"));

    const QPoint exposedAttachmentTarget = blankViewportPosition(graphicsView);
    const std::optional<QPointF> routerPositionBeforeOverlappingEndpointDrag =
        nodeEditor
        ? nodeEditor->routerVisualPosition(QStringLiteral("r-0-1"))
        : std::nullopt;
    if (bodyAttachedEndpoint) {
        dragNodeTo(graphicsView, graphicsScene,
                   *bodyAttachedEndpoint, exposedAttachmentTarget);
        application.processEvents();
        application.processEvents();
    }

    // Projection rebuilds deliberately replace QtNodes' transient NodeIds.
    // Resolve the Endpoint again through the design-facing Router identity
    // before exercising direct line targeting.
    const auto exposedBodyAttachedEndpoint = endpointAttachedToRouter(
        graphicsScene, QStringLiteral("r-0-1"));
    auto* exposedBodyAttachedGraphics = exposedBodyAttachedEndpoint
        ? graphicsScene->nodeGraphicsObject(*exposedBodyAttachedEndpoint)
        : nullptr;
    check(exposedBodyAttachedGraphics
              && graphicsView
              && QLineF(
                     exposedBodyAttachedGraphics->sceneBoundingRect().center(),
                     graphicsView->mapToScene(exposedAttachmentTarget)).length() < 4.0
              && nodeEditor
              && nodeEditor->routerVisualPosition(QStringLiteral("r-0-1"))
                     == routerPositionBeforeOverlappingEndpointDrag,
          QStringLiteral("an Endpoint stacked over its Router remains the direct drag target without moving the Router"));
    const auto bodyAttachmentConnection = exposedBodyAttachedEndpoint
        ? attachmentConnectionForEndpoint(
              graphicsScene, *exposedBodyAttachedEndpoint)
        : std::nullopt;
    const QString exposedEndpointId = exposedBodyAttachedEndpoint
        ? graphicsScene->graphModel().nodeData(
              *exposedBodyAttachedEndpoint,
              QtNodes::NodeRole::Caption).toString().section(
                  QLatin1Char('\n'), 0, 0)
        : QString();
    std::optional<QPoint> attachmentLinePoint;
    if (graphicsView && graphicsScene && bodyAttachmentConnection) {
        auto* connection = graphicsScene->connectionGraphicsObject(
            *bodyAttachmentConnection);
        attachmentLinePoint = viewportPointForConnection(
            graphicsView, connection);
        if (attachmentLinePoint) {
            sendContextMenu(graphicsView, *attachmentLinePoint);
            application.processEvents();
        }
    }
    auto* connectionMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::connectionContextMenuName) : nullptr;
    QAction* disconnectConnection = connectionMenu
        ? connectionMenu->findChild<QAction*>(
              finepaper::workbench::disconnectConnectionActionName)
        : nullptr;
    check(attachmentLinePoint.has_value(),
          QStringLiteral("Endpoint attachment line is directly targetable on the canvas"));
    check(disconnectConnection != nullptr,
          QStringLiteral("right-clicking an Endpoint attachment line exposes Disconnect"));
    if (disconnectConnection) {
        disconnectConnection->trigger();
        if (connectionMenu) {
            connectionMenu->close();
        }
        application.processEvents();
        application.processEvents();
    }
    auto detachedBodyEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("Unattached\nMaster endpoint"));
    check(!endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-1"))
              && detachedBodyEndpoint
              && nodeEditor
              && nodeEditor->detachedEndpointDraftIds()
                     == QStringList{exposedEndpointId},
          QStringLiteral("line-menu Disconnect removes the attachment and keeps the Endpoint draft"));
    bool sawDetachedEndpointSaveBlocker = false;
    if (saveAsAction) {
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(
                QApplication::activeModalWidget());
            auto* messageBox = qobject_cast<QMessageBox*>(dialog);
            sawDetachedEndpointSaveBlocker = messageBox
                && messageBox->objectName()
                    == QStringLiteral(
                        "finepaper.detachedEndpointSaveBlocker");
            if (messageBox) {
                if (QAbstractButton* ok = messageBox->button(QMessageBox::Ok)) {
                    ok->click();
                } else {
                    messageBox->reject();
                }
            } else if (dialog) {
                dialog->reject();
            }
        });
        saveAsAction->trigger();
        application.processEvents();
    }
    check(sawDetachedEndpointSaveBlocker
              && window.isWindowModified()
              && nodeEditor
              && nodeEditor->detachedEndpointDraftIds()
                     == QStringList{exposedEndpointId},
          QStringLiteral("saving is blocked while a durable Endpoint survives only as a detached draft"));

    const auto routerForDeleteKeyReconnect = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-1"));
    if (detachedBodyEndpoint && routerForDeleteKeyReconnect) {
        dragPortToNodeBody(
            graphicsView,
            graphicsScene,
            *detachedBodyEndpoint,
            QtNodes::PortType::Out,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *routerForDeleteKeyReconnect);
        application.processEvents();
        application.processEvents();
    }
    const auto endpointForDeleteKey = endpointAttachedToRouter(
        graphicsScene, QStringLiteral("r-0-1"));
    check(nodeEditor && nodeEditor->detachedEndpointDraftIds().isEmpty(),
          QStringLiteral("reconnecting a detached Endpoint commits and clears its recoverable draft"));
    if (graphicsScene && endpointForDeleteKey) {
        graphicsScene->clearSelection();
        graphicsScene->nodeGraphicsObject(*endpointForDeleteKey)->setSelected(true);
        graphicsScene->nodeSelected(*endpointForDeleteKey);
        application.processEvents();
    }
    auto* endpointDraftWidth = static_cast<finepaper::SchemaValueEditor*>(
        window.findChild<QWidget*>(
            QStringLiteral("finepaper.endpointParameter.dataWidth")));
    if (endpointDraftWidth) {
        endpointDraftWidth->setValue(QJsonValue(96));
        if (endpointDraftWidth->valueChanged) {
            endpointDraftWidth->valueChanged();
        }
        application.processEvents();
    }
    check(endpointConfigurationPanel && endpointDraftWidth
              && endpointConfigurationPanel->unappliedDraftEndpointIds(
                     endpointDraftDesignIdentity).contains(exposedEndpointId),
          QStringLiteral("editing an attached Endpoint creates an unapplied parameter draft"));

    bool disconnectRemovalRequested = false;
    bool disconnectPermanentDeletionRequested = false;
    bool detachedEndpointDeletionRequested = false;
    const auto originalDisconnectRemovalRequested = nodeEditor
        ? nodeEditor->endpointRemovalRequested
        : std::function<bool(const QString&)>{};
    const auto originalDisconnectDeletionRequested = nodeEditor
        ? nodeEditor->endpointDeletionRequested
        : std::function<bool(const QString&)>{};
    const auto originalDetachedDeletionRequested = nodeEditor
        ? nodeEditor->detachedEndpointDeletionRequested
        : std::function<void(const QString&)>{};
    if (nodeEditor) {
        nodeEditor->endpointRemovalRequested = [&, originalDisconnectRemovalRequested](
                                                   const QString& endpointId) {
            disconnectRemovalRequested = true;
            return originalDisconnectRemovalRequested
                ? originalDisconnectRemovalRequested(endpointId) : false;
        };
        nodeEditor->endpointDeletionRequested = [&, originalDisconnectDeletionRequested](
                                                    const QString& endpointId) {
            disconnectPermanentDeletionRequested = true;
            return originalDisconnectDeletionRequested
                ? originalDisconnectDeletionRequested(endpointId) : false;
        };
        nodeEditor->detachedEndpointDeletionRequested = [&, originalDetachedDeletionRequested](
                                                            const QString& endpointId) {
            detachedEndpointDeletionRequested = true;
            if (originalDetachedDeletionRequested) {
                originalDetachedDeletionRequested(endpointId);
            }
        };
    }
    const auto connectionForDeleteKey = endpointForDeleteKey
        ? attachmentConnectionForEndpoint(graphicsScene, *endpointForDeleteKey)
        : std::nullopt;
    QAction* deleteSelection = animatedView
        ? animatedView->deleteSelectionAction() : nullptr;
    if (graphicsScene && connectionForDeleteKey && deleteSelection) {
        graphicsScene->clearSelection();
        if (auto* connection = graphicsScene->connectionGraphicsObject(
                *connectionForDeleteKey)) {
            connection->setSelected(true);
            deleteSelection->trigger();
            application.processEvents();
            application.processEvents();
        }
    }
    detachedBodyEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("Unattached\nMaster endpoint"));
    check(deleteSelection && detachedBodyEndpoint
              && !endpointAttachedToRouter(
                  graphicsScene, QStringLiteral("r-0-1"))
              && disconnectRemovalRequested
              && !disconnectPermanentDeletionRequested
              && endpointConfigurationPanel
              && endpointConfigurationPanel->unappliedDraftEndpointIds(
                     endpointDraftDesignIdentity).contains(exposedEndpointId),
          QStringLiteral("Delete on a selected Endpoint attachment line performs the same durable disconnect"));
    if (graphicsView && graphicsScene && detachedBodyEndpoint) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            graphicsScene->nodeGraphicsObject(*detachedBodyEndpoint)
                ->sceneBoundingRect().center());
        sendContextMenu(graphicsView, endpointPosition);
        application.processEvents();
    }
    auto* detachedEndpointMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    QAction* deleteDetachedEndpoint = detachedEndpointMenu ? detachedEndpointMenu->findChild<QAction*>(
        finepaper::workbench::deleteEndpointActionName) : nullptr;
    if (deleteDetachedEndpoint) {
        deleteDetachedEndpoint->trigger();
        if (detachedEndpointMenu) {
            detachedEndpointMenu->close();
        }
        application.processEvents();
    }
    if (nodeEditor) {
        nodeEditor->endpointRemovalRequested = originalDisconnectRemovalRequested;
        nodeEditor->endpointDeletionRequested = originalDisconnectDeletionRequested;
        nodeEditor->detachedEndpointDeletionRequested =
            originalDetachedDeletionRequested;
    }
    check(!nodeIdWithCaptionPrefix(
               graphicsScene, QStringLiteral("Unattached\nMaster endpoint"))
              && detachedEndpointDeletionRequested
              && endpointConfigurationPanel
              && !endpointConfigurationPanel->unappliedDraftEndpointIds(
                      endpointDraftDesignIdentity).contains(exposedEndpointId),
          QStringLiteral("permanently deleting an unattached Endpoint clears its parameter draft"));

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
        check(waitUntil([&window] { return !window.operationBusy(); }),
              QStringLiteral("GUI validation finishes asynchronously without blocking the event loop"));
    }

    auto* outputPath = window.findChild<QLineEdit*>(QStringLiteral("finepaper.outputRoot"));
    if (outputPath) {
        outputPath->setText(outputRoot.path());
    }
    QAction* generateAction = actionWithText(window, QStringLiteral("Generate RTL"));
    check(generateAction != nullptr, QStringLiteral("shared RTL generation action is available"));
    if (generateAction) {
        generateAction->trigger();
        check(waitUntil([&window] { return !window.operationBusy(); }),
              QStringLiteral("GUI generation finishes asynchronously without blocking the event loop"));
    }

    auto* artifacts = window.findChild<QTableWidget*>(QStringLiteral("finepaper.artifactTable"));
    check(artifacts && artifacts->rowCount() > 0,
          QStringLiteral("GUI generation flow reports real RTL artifacts"));
    if (artifacts && artifacts->rowCount() > 0) {
        const QString artifactPath = artifacts->item(0, 2)->text();
        check(!artifactPath.isEmpty(), QStringLiteral("reported GUI artifact has a path"));
    }

    finepaper::NocEditorSelectionSet observedSemanticSelection;
    int semanticSelectionCallbacks = 0;
    std::function<void(const finepaper::NocEditorSelectionSet&)>
        mainWindowSelectionHandler;
    if (nodeEditor) {
        mainWindowSelectionHandler = nodeEditor->semanticSelectionChanged;
        nodeEditor->semanticSelectionChanged =
            [&, mainWindowSelectionHandler](
                const finepaper::NocEditorSelectionSet& selection) {
                observedSemanticSelection = selection;
                ++semanticSelectionCallbacks;
                if (mainWindowSelectionHandler) {
                    mainWindowSelectionHandler(selection);
                }
            };
    }
    const auto semanticRouter = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    const auto semanticEndpoint = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("master"));
    if (graphicsScene && semanticRouter && semanticEndpoint) {
        graphicsScene->clearSelection();
        graphicsScene->nodeGraphicsObject(*semanticRouter)->setSelected(true);
        graphicsScene->nodeGraphicsObject(*semanticEndpoint)->setSelected(true);
        application.processEvents();
    }
    check(observedSemanticSelection.size() == 2
              && observedSemanticSelection.elements().size() == 2,
          QStringLiteral("Router and Endpoint multi-selection is reported as stable semantic references"));

    std::optional<QtNodes::ConnectionId> semanticRouterLink;
    std::optional<QtNodes::ConnectionId> semanticAttachment;
    if (graphicsScene) {
        for (const QtNodes::ConnectionId& connection
             : sceneConnectionIds(graphicsScene)) {
            const QString sourceCaption = graphicsScene->graphModel().nodeData(
                connection.outNodeId, QtNodes::NodeRole::Caption).toString();
            if (sourceCaption.startsWith(QStringLiteral("r-"))) {
                if (!semanticRouterLink) {
                    semanticRouterLink = connection;
                }
            } else if (!semanticAttachment) {
                semanticAttachment = connection;
            }
        }
    }
    if (graphicsScene && semanticRouterLink) {
        graphicsScene->clearSelection();
        graphicsScene->connectionGraphicsObject(*semanticRouterLink)
            ->setSelected(true);
        application.processEvents();
    }
    auto* semanticInspectorGroup = window.findChild<QGroupBox*>(
        finepaper::workbench::selectionInspectorName);
    auto* semanticInspector = semanticInspectorGroup
        ? semanticInspectorGroup->findChild<QLabel*>() : nullptr;
    check(observedSemanticSelection.size() == 1
              && observedSemanticSelection.items.front().kind
                  == finepaper::NocEditorSelection::Kind::RouterLink
              && semanticInspector
              && semanticInspector->text().contains(QStringLiteral("fixed Mesh")),
          QStringLiteral("a Router Link is selectable by semantic id while remaining fixed by the Mesh"));

    if (graphicsScene && semanticAttachment) {
        graphicsScene->clearSelection();
        graphicsScene->connectionGraphicsObject(*semanticAttachment)
            ->setSelected(true);
        application.processEvents();
    }
    check(observedSemanticSelection.size() == 1
              && observedSemanticSelection.items.front().kind
                  == finepaper::NocEditorSelection::Kind::EndpointAttachment
              && !observedSemanticSelection.items.front().id.isEmpty()
              && semanticSelectionCallbacks > 0,
          QStringLiteral("an Endpoint attachment line is exposed as a distinct semantic selection"));
    if (nodeEditor) {
        nodeEditor->semanticSelectionChanged = mainWindowSelectionHandler;
    }

    if (packagePanelAction) {
        packagePanelAction->trigger();
        application.processEvents();
    }
    if (nodeEditor) {
        nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), true);
    }
    closeDiscarding(window);

    finepaper::FinepaperMainWindow restoredWindow(locations);
    restoredWindow.show();
    application.processEvents();
    auto* restoredPackageDock = restoredWindow.findChild<QDockWidget*>(
        finepaper::workbench::packageDockName);
    check(restoredPackageDock && !restoredPackageDock->isVisible(),
          QStringLiteral("collapsed panel state is restored in the next workbench session"));
    auto* restoredCreateButton = restoredWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    check(restoredCreateButton != nullptr,
          QStringLiteral("restored workbench keeps New NoC Design available"));
    createDesignThroughDialog(
        restoredWindow, QStringLiteral("finepaper.noc@1.0.0"));
    auto* restoredEditorWidget = restoredWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.nodeEditor"));
    auto* restoredNodeEditor = dynamic_cast<finepaper::NocNodeEditor*>(restoredEditorWidget);
    check(restoredNodeEditor
              && restoredNodeEditor->routerVisualPosition(QStringLiteral("r-0-0"))
                     == std::optional<QPointF>(QPointF(0.0, 0.0)),
          QStringLiteral("regularized Router placement is restored in the next session"));
    check(restoredNodeEditor
              && restoredNodeEditor->routerCollapsed(QStringLiteral("r-0-0")),
          QStringLiteral("Router collapsed state is restored in the next session"));
    closeDiscarding(restoredWindow);

    const QString topologyWorkspaceDesignPath = outputRoot.filePath(
        QStringLiteral("topology-workspace-persistence.fpnoc"));
    check(writeTopologyWorkspaceDesign(topologyWorkspaceDesignPath),
          QStringLiteral("topology workspace persistence design is writable"));
    const finepaper::TopologyWorkspaceIdentity topologyWorkspaceIdentity{
        QStringLiteral("finepaper.noc"),
        QStringLiteral("1.0.0"),
        QStringLiteral("topology_workspace_persistence"),
    };
    finepaper::TopologyWorkspaceState damagedWorkspaceFixture;
    damagedWorkspaceFixture.collapsedRouterIds.emplace();
    const QSet<QString> workspaceKeysBeforeDamage =
        currentTopologyWorkspaceKeys();
    finepaper::TopologyWorkspaceStore topologyWorkspaceWriter;
    check(topologyWorkspaceWriter.save(
              topologyWorkspaceIdentity, damagedWorkspaceFixture).success,
          QStringLiteral("topology workspace damage fixture is writable"));
    const QSet<QString> newWorkspaceKeys = currentTopologyWorkspaceKeys()
        - workspaceKeysBeforeDamage;
    check(newWorkspaceKeys.size() == 1,
          QStringLiteral("topology workspace damage fixture has one isolated record"));
    if (newWorkspaceKeys.size() == 1) {
        QSettings rawWorkspaceSettings;
        rawWorkspaceSettings.setValue(
            *newWorkspaceKeys.cbegin(), QStringLiteral("damaged-record"));
        rawWorkspaceSettings.sync();
    }
    finepaper::FinepaperMainWindow positionWindow(locations);
    positionWindow.show();
    application.processEvents();
    check(positionWindow.openDesignFile(topologyWorkspaceDesignPath),
          QStringLiteral("topology workspace persistence design opens"));
    check(positionWindow.statusBar()->currentMessage().contains(
              QStringLiteral("is damaged"), Qt::CaseInsensitive),
          QStringLiteral("a damaged canvas layout is reported non-modally"));
    QAction* repairWorkspaceAction = positionWindow.findChild<QAction*>(
        finepaper::workbench::regularizeActionName);
    if (repairWorkspaceAction) {
        repairWorkspaceAction->trigger();
        application.processEvents();
    }
    const finepaper::TopologyWorkspaceLoadResult repairedWorkspace =
        topologyWorkspaceWriter.load(topologyWorkspaceIdentity);
    check(repairWorkspaceAction && repairedWorkspace.ok()
              && repairedWorkspace.state
              && positionWindow.statusBar()->currentMessage().contains(
                  QStringLiteral("repaired"), Qt::CaseInsensitive),
          QStringLiteral(
              "Regularize Layout explicitly repairs damaged workspace storage"));
    auto* positionEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        positionWindow.findChild<QWidget*>(
            QStringLiteral("finepaper.nodeEditor")));
    if (positionEditor) {
        positionEditor->setRouterCollapsed(QStringLiteral("r-0-0"), false);
    }
    auto* positionView = positionEditor
        ? positionEditor->findChild<QGraphicsView*>() : nullptr;
    auto* positionScene = positionView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(positionView->scene())
        : nullptr;
    const auto positionEndpoint = nodeIdWithCaptionPrefix(
        positionScene, QStringLiteral("persisted_master"));
    const std::optional<QPointF> initialEndpointPosition = positionEditor
        ? positionEditor->endpointVisualPosition(
              QStringLiteral("persisted_master"))
        : std::nullopt;
    if (positionView && positionScene && positionEndpoint) {
        dragNodeTo(positionView, positionScene, *positionEndpoint,
                   blankViewportPosition(positionView));
        application.processEvents();
        application.processEvents();
    }
    const std::optional<QPointF> persistedEndpointPosition = positionEditor
        ? positionEditor->endpointVisualPosition(
              QStringLiteral("persisted_master"))
        : std::nullopt;
    check(persistedEndpointPosition.has_value(),
          QStringLiteral("free Endpoint position is captured before restart"));
    check(initialEndpointPosition && persistedEndpointPosition
              && QLineF(*initialEndpointPosition, *persistedEndpointPosition)
                     .length() >= 4.0,
          QStringLiteral("the Endpoint actually moves before persistence is tested"));
    check(positionEditor
              && !positionEditor->setRouterVisualPosition(
                  QStringLiteral("r-0-0"),
                  QPointF(std::numeric_limits<qreal>::infinity(), 0.0)),
          QStringLiteral("the canvas rejects non-finite Router positions"));
    QString settingsFile;
    {
        QSettings settings;
        settings.sync();
        settingsFile = settings.fileName();
    }
    const QString settingsBackup = outputRoot.filePath(
        QStringLiteral("workspace-save-recovery-settings.backup"));
    const bool settingsCopied = QFile::copy(settingsFile, settingsBackup);
    const bool settingsRemoved = settingsCopied && QFile::remove(settingsFile);
    const bool settingsBlocked = settingsRemoved
        && QDir().mkdir(settingsFile);
    check(settingsBlocked,
          QStringLiteral("workspace save failure fixture replaces the settings file"));
    auto* positionActivityLog = positionWindow.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.activityLog"));
    const int activityBlocksBeforeFailure = positionActivityLog
        ? positionActivityLog->document()->blockCount() : 0;
    if (settingsBlocked && positionEditor) {
        positionEditor->setRouterVisualPosition(
            QStringLiteral("r-0-0"), QPointF(440.0, 210.0));
        positionEditor->setRouterVisualPosition(
            QStringLiteral("r-0-0"), QPointF(460.0, 230.0));
    }
    const int activityBlocksAfterFailure = positionActivityLog
        ? positionActivityLog->document()->blockCount() : 0;
    check(settingsBlocked && positionActivityLog
              && activityBlocksAfterFailure
                  == activityBlocksBeforeFailure + 1
              && positionWindow.statusBar()->currentMessage().contains(
                  QStringLiteral("could not be saved"), Qt::CaseInsensitive),
          QStringLiteral(
              "repeated Workspace save failures produce one persistent UI diagnostic"));
    const bool settingsBlockRemoved = settingsBlocked
        && QDir().rmdir(settingsFile);
    const bool settingsRestored = settingsBlockRemoved
        && QFile::copy(settingsBackup, settingsFile);
    check(settingsRestored,
          QStringLiteral("workspace save failure fixture restores settings storage"));
    if (settingsRestored && positionEditor) {
        positionEditor->setRouterVisualPosition(
            QStringLiteral("r-0-0"), QPointF(480.0, 250.0));
    }
    const int activityBlocksAfterRecovery = positionActivityLog
        ? positionActivityLog->document()->blockCount() : 0;
    check(settingsRestored && positionActivityLog
              && activityBlocksAfterRecovery
                  == activityBlocksAfterFailure + 1
              && positionWindow.statusBar()->currentMessage().contains(
                  QStringLiteral("available again"), Qt::CaseInsensitive),
          QStringLiteral(
              "a successful retry clears the persistent save failure state"));
    closeDiscarding(positionWindow);

    finepaper::FinepaperMainWindow positionRestoredWindow(locations);
    positionRestoredWindow.show();
    application.processEvents();
    check(positionRestoredWindow.openDesignFile(topologyWorkspaceDesignPath),
          QStringLiteral("the same topology design reopens in a new session"));
    auto* positionRestoredEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        positionRestoredWindow.findChild<QWidget*>(
            QStringLiteral("finepaper.nodeEditor")));
    if (positionRestoredEditor) {
        positionRestoredEditor->setRouterCollapsed(
            QStringLiteral("r-0-0"), false);
    }
    check(positionRestoredEditor && persistedEndpointPosition
              && positionRestoredEditor->endpointVisualPosition(
                     QStringLiteral("persisted_master"))
                     == persistedEndpointPosition,
          QStringLiteral("free Endpoint placement is restored for the reopened design"));
    closeDiscarding(positionRestoredWindow);

    finepaper::NocDesign reloadDesign = {};
    reloadDesign.formatVersion = 1;
    reloadDesign.id = QStringLiteral("same-editor-workspace-reload");
    reloadDesign.name = QStringLiteral("Same Editor Workspace Reload");
    reloadDesign.package = {
        QStringLiteral("finepaper.noc"), QStringLiteral("1.0.0")};
    reloadDesign.topology = {QStringLiteral("mesh"), 1, 1};
    reloadDesign.endpoints = {
        finepaper::EndpointInstance{
            QStringLiteral("reload_endpoint"),
            QStringLiteral("master"),
            finepaper::EndpointAttachment{
                finepaper::RouterPosition{0, 0}, std::nullopt},
            {},
        },
    };
    const finepaper::TopologyWorkspaceIdentity reloadWorkspaceIdentity = {
        reloadDesign.package.id,
        reloadDesign.package.version,
        reloadDesign.id,
    };
    finepaper::TopologyWorkspaceState firstReloadState;
    firstReloadState.endpointPositionOverrides.insert(
        QStringLiteral("reload_endpoint"), QPointF(121.5, -87.25));
    firstReloadState.collapsedRouterIds.emplace();
    finepaper::TopologyWorkspaceStore firstReloadWriter;
    check(firstReloadWriter.save(
              reloadWorkspaceIdentity, firstReloadState).success,
          QStringLiteral("same-editor reload fixture saves its first state"));

    finepaper::NocNodeEditor reloadEditor;
    reloadEditor.beginDocumentSession(QStringLiteral("reload-session-a"));
    reloadEditor.setDesign(&reloadDesign);
    check(reloadEditor.endpointVisualPosition(
              QStringLiteral("reload_endpoint"))
              == firstReloadState.endpointPositionOverrides.value(
                  QStringLiteral("reload_endpoint")),
          QStringLiteral("same-editor reload fixture loads its first state"));

    finepaper::TopologyWorkspaceState secondReloadState = firstReloadState;
    secondReloadState.endpointPositionOverrides.insert(
        QStringLiteral("reload_endpoint"), QPointF(-302.75, 411.125));
    finepaper::TopologyWorkspaceStore secondReloadWriter;
    check(secondReloadWriter.save(
              reloadWorkspaceIdentity, secondReloadState).success,
          QStringLiteral("an independent writer updates the same Workspace"));
    reloadEditor.beginDocumentSession(QStringLiteral("reload-session-b"));
    reloadEditor.setDesign(&reloadDesign);
    check(reloadEditor.endpointVisualPosition(
              QStringLiteral("reload_endpoint"))
              == secondReloadState.endpointPositionOverrides.value(
                  QStringLiteral("reload_endpoint")),
          QStringLiteral(
              "a new document session reloads an externally updated Workspace "
              "even when its identity is unchanged"));
    reloadEditor.close();

    finepaper::NocDesign sessionDesign = {};
    sessionDesign.formatVersion = 1;
    sessionDesign.id = QStringLiteral("same-storage-identity");
    sessionDesign.name = QStringLiteral("Document Session Separation");
    sessionDesign.package = {
        QStringLiteral("finepaper.noc"), QStringLiteral("1.0.0")};
    sessionDesign.topology = {QStringLiteral("mesh"), 1, 1};
    sessionDesign.endpoints = {
        finepaper::EndpointInstance{
            QStringLiteral("session_endpoint"),
            QStringLiteral("master"),
            finepaper::EndpointAttachment{
                finepaper::RouterPosition{0, 0}, std::nullopt},
            {},
        },
    };
    finepaper::NocNodeEditor sessionEditor;
    sessionEditor.setObjectName(QStringLiteral("finepaper.sessionEditor"));
    sessionEditor.setEndpointTypes({{
        QStringLiteral("master"), QStringLiteral("Master endpoint")}});
    sessionEditor.beginDocumentSession(QStringLiteral("document-session-a"));
    sessionEditor.setDesign(&sessionDesign);
    sessionEditor.show();
    sessionEditor.setRouterCollapsed(QStringLiteral("r-0-0"), false);
    application.processEvents();
    auto* sessionView = sessionEditor.findChild<QGraphicsView*>();
    auto* sessionAnimatedView = dynamic_cast<finepaper::AnimatedGraphicsView*>(
        sessionView);
    auto* sessionScene = sessionView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(sessionView->scene())
        : nullptr;
    const auto sessionEndpoint = nodeIdWithCaptionPrefix(
        sessionScene, QStringLiteral("session_endpoint"));
    const auto sessionAttachment = sessionEndpoint
        ? attachmentConnectionForEndpoint(sessionScene, *sessionEndpoint)
        : std::nullopt;
    sessionEditor.endpointRemovalRequested = [&](const QString& endpointId) {
        if (endpointId != QStringLiteral("session_endpoint")) {
            return false;
        }
        sessionDesign.endpoints.clear();
        sessionEditor.syncDesignState(sessionDesign);
        return true;
    };
    const bool staleDeletionWasQueued = sessionScene && sessionAttachment
        && sessionScene->graphModel().deleteConnection(*sessionAttachment);
    sessionEditor.regularizeLayout();
    const auto sessionEndpointAfterProjectionRebuild = nodeIdWithCaptionPrefix(
        sessionScene, QStringLiteral("session_endpoint"));
    const auto sessionAttachmentAfterProjectionRebuild =
        sessionEndpointAfterProjectionRebuild
        ? attachmentConnectionForEndpoint(
              sessionScene, *sessionEndpointAfterProjectionRebuild)
        : std::nullopt;
    if (sessionScene && sessionAttachmentAfterProjectionRebuild
        && sessionAnimatedView) {
        sessionScene->clearSelection();
        auto* sessionConnection = sessionScene->connectionGraphicsObject(
            *sessionAttachmentAfterProjectionRebuild);
        if (sessionConnection) {
            sessionConnection->setSelected(true);
        }
        sessionAnimatedView->deleteSelectionAction()->trigger();
        application.processEvents();
        application.processEvents();
    }
    check(staleDeletionWasQueued
              && sessionEditor.detachedEndpointDraftIds()
              == QStringList{QStringLiteral("session_endpoint")},
          QStringLiteral(
              "a canceled old deletion does not block a new disconnect, which "
              "creates a transient Endpoint draft"));
    sessionEditor.setDesign(&sessionDesign);
    check(sessionEditor.detachedEndpointDraftIds()
              == QStringList{QStringLiteral("session_endpoint")},
          QStringLiteral("same-session projection refresh retains the detached Endpoint draft"));
    sessionEditor.beginDocumentSession(QStringLiteral("document-session-b"));
    sessionEditor.setDesign(&sessionDesign);
    check(sessionEditor.detachedEndpointDraftIds().isEmpty()
              && !nodeIdWithCaptionPrefix(
                  sessionScene, QStringLiteral("Unattached\nMaster endpoint")),
          QStringLiteral("a new document session clears transient state despite the same storage identity"));
    sessionEditor.close();

    finepaper::NocDesign collapsedOccupancyDesign;
    collapsedOccupancyDesign.id = QStringLiteral("collapsed-occupancy");
    collapsedOccupancyDesign.topology = {QStringLiteral("mesh"), 1, 1};
    collapsedOccupancyDesign.endpoints = {
        finepaper::EndpointInstance{
            QStringLiteral("hidden_endpoint"),
            QStringLiteral("master"),
            finepaper::EndpointAttachment{
                finepaper::RouterPosition{0, 0}, std::nullopt},
            {},
        },
    };
    finepaper::AttachmentDefinition singleAttachment;
    singleAttachment.maxPerRouter = 1;
    singleAttachment.slotMode = finepaper::AttachmentSlotMode::Automatic;
    finepaper::NocNodeEditor collapsedOccupancyEditor;
    collapsedOccupancyEditor.setAttachmentPolicy(
        finepaper::attachment::policyFromPackage(singleAttachment));
    collapsedOccupancyEditor.beginDocumentSession(
        QStringLiteral("collapsed-occupancy-session"));
    collapsedOccupancyEditor.setDesign(&collapsedOccupancyDesign);
    collapsedOccupancyEditor.show();
    collapsedOccupancyEditor.setRouterCollapsed(
        QStringLiteral("r-0-0"), true);
    application.processEvents();
    auto* collapsedOccupancyView =
        collapsedOccupancyEditor.findChild<QGraphicsView*>();
    auto* collapsedOccupancyScene = collapsedOccupancyView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(
              collapsedOccupancyView->scene())
        : nullptr;
    const auto fullCollapsedRouter = nodeIdWithCaption(
        collapsedOccupancyScene, QStringLiteral("r-0-0"));
    const bool collapsedPortAcceptsDraft = fullCollapsedRouter
        && collapsedOccupancyScene
        && collapsedOccupancyScene->graphModel().connectionPossible({
            QtNodes::InvalidNodeId,
            finepaper::portIndex(
                finepaper::EndpointOutputPort::Attachment),
            *fullCollapsedRouter,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint),
        });
    check(fullCollapsedRouter
              && !nodeIdWithCaptionPrefix(
                  collapsedOccupancyScene,
                  QStringLiteral("hidden_endpoint"))
              && !collapsedPortAcceptsDraft,
          QStringLiteral(
              "a collapsed full Router keeps its hidden Endpoint port occupied"));
    collapsedOccupancyEditor.close();

    finepaper::NocDesign capacityProjectionDesign;
    capacityProjectionDesign.id = QStringLiteral("capacity-projection");
    capacityProjectionDesign.topology = {QStringLiteral("mesh"), 1, 1};
    capacityProjectionDesign.endpoints = {
        finepaper::EndpointInstance{
            QStringLiteral("capacity_endpoint"),
            QStringLiteral("master"),
            finepaper::EndpointAttachment{
                finepaper::RouterPosition{0, 0}, QStringLiteral("local0")},
            {},
        },
    };
    finepaper::AttachmentDefinition capacityAttachment;
    capacityAttachment.maxPerRouter = 1;
    capacityAttachment.slotMode = finepaper::AttachmentSlotMode::Explicit;
    capacityAttachment.positions = {
        {QStringLiteral("local0"), QStringLiteral("Local 0")},
        {QStringLiteral("local1"), QStringLiteral("Local 1")},
    };
    finepaper::NocNodeEditor capacityProjectionEditor;
    capacityProjectionEditor.setAttachmentPolicy(
        finepaper::attachment::policyFromPackage(capacityAttachment));
    capacityProjectionEditor.beginDocumentSession(
        QStringLiteral("capacity-projection-session"));
    capacityProjectionEditor.setDesign(&capacityProjectionDesign);
    capacityProjectionEditor.show();
    capacityProjectionEditor.setRouterCollapsed(
        QStringLiteral("r-0-0"), false);
    application.processEvents();
    auto* capacityProjectionView =
        capacityProjectionEditor.findChild<QGraphicsView*>();
    auto* capacityProjectionScene = capacityProjectionView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(
              capacityProjectionView->scene())
        : nullptr;
    const auto capacityRouter = nodeIdWithCaption(
        capacityProjectionScene, QStringLiteral("r-0-0"));
    const bool sparePortAccepted = capacityRouter && capacityProjectionScene
        && capacityProjectionScene->graphModel().connectionPossible({
            QtNodes::InvalidNodeId,
            finepaper::portIndex(
                finepaper::EndpointOutputPort::Attachment),
            *capacityRouter,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint) + 1U,
        });
    finepaper::attachment::Policy invalidProjectionPolicy =
        finepaper::attachment::policyFromPackage(singleAttachment);
    invalidProjectionPolicy.maxPerRouter = 2;
    capacityProjectionEditor.setAttachmentPolicy(invalidProjectionPolicy);
    application.processEvents();
    const auto invalidPolicyRouter = nodeIdWithCaption(
        capacityProjectionScene, QStringLiteral("r-0-0"));
    const bool invalidPolicyAccepted = invalidPolicyRouter
        && capacityProjectionScene
        && capacityProjectionScene->graphModel().connectionPossible({
            QtNodes::InvalidNodeId,
            finepaper::portIndex(
                finepaper::EndpointOutputPort::Attachment),
            *invalidPolicyRouter,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint),
        });
    check(!sparePortAccepted && !invalidPolicyAccepted,
          QStringLiteral(
              "QtNodes preflight enforces Package capacity and policy invariants"));
    capacityProjectionEditor.close();

    finepaper::NocDesign readOnlyRaceDesign;
    readOnlyRaceDesign.id = QStringLiteral("disconnect-read-only-race");
    readOnlyRaceDesign.topology = {QStringLiteral("mesh"), 1, 1};
    readOnlyRaceDesign.endpoints = {
        finepaper::EndpointInstance{
            QStringLiteral("race_endpoint"),
            QStringLiteral("master"),
            finepaper::EndpointAttachment{
                finepaper::RouterPosition{0, 0}, std::nullopt},
            {},
        },
    };
    finepaper::NocNodeEditor readOnlyRaceEditor;
    readOnlyRaceEditor.setAttachmentPolicy(
        finepaper::attachment::policyFromPackage(singleAttachment));
    readOnlyRaceEditor.beginDocumentSession(
        QStringLiteral("disconnect-read-only-race-session"));
    readOnlyRaceEditor.setDesign(&readOnlyRaceDesign);
    readOnlyRaceEditor.show();
    readOnlyRaceEditor.setRouterCollapsed(QStringLiteral("r-0-0"), false);
    application.processEvents();
    auto* readOnlyRaceView = readOnlyRaceEditor.findChild<QGraphicsView*>();
    auto* readOnlyRaceScene = readOnlyRaceView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(
              readOnlyRaceView->scene())
        : nullptr;
    const auto readOnlyRaceEndpoint = nodeIdWithCaptionPrefix(
        readOnlyRaceScene, QStringLiteral("race_endpoint"));
    const auto readOnlyRaceAttachment = readOnlyRaceEndpoint
        ? attachmentConnectionForEndpoint(
              readOnlyRaceScene, *readOnlyRaceEndpoint)
        : std::nullopt;
    bool readOnlyRaceRemovalCalled = false;
    readOnlyRaceEditor.endpointRemovalRequested = [&](const QString&) {
        readOnlyRaceRemovalCalled = true;
        return true;
    };
    const bool readOnlyRaceDeletionQueued = readOnlyRaceScene
        && readOnlyRaceAttachment
        && readOnlyRaceScene->graphModel().deleteConnection(
            *readOnlyRaceAttachment);
    readOnlyRaceEditor.setEditingEnabled(false);
    application.processEvents();
    application.processEvents();
    const auto restoredRaceEndpoint = nodeIdWithCaptionPrefix(
        readOnlyRaceScene, QStringLiteral("race_endpoint"));
    const auto restoredRaceAttachment = restoredRaceEndpoint
        ? attachmentConnectionForEndpoint(
              readOnlyRaceScene, *restoredRaceEndpoint)
        : std::nullopt;
    check(readOnlyRaceDeletionQueued && !readOnlyRaceRemovalCalled
              && restoredRaceAttachment,
          QStringLiteral(
              "switching to read-only during deferred disconnect restores the projection"));
    readOnlyRaceEditor.close();

    finepaper::RuntimeLocations explicitLocations = {
        QStringList{QDir(projectRoot).filePath(
            QStringLiteral("tests/fixtures/explicit-package"))},
        outputRoot.path()};
    finepaper::FinepaperMainWindow explicitWindow(explicitLocations);
    explicitWindow.show();
    application.processEvents();
    auto* explicitAvailablePackages = explicitWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.availablePackages"));
    check(explicitAvailablePackages
              && explicitAvailablePackages->text().startsWith(
                  QStringLiteral("1 NoC IP Package")),
          QStringLiteral("explicit-slot Package fixture loads at runtime"));
    auto* explicitCreate = explicitWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    check(explicitCreate != nullptr,
          QStringLiteral("explicit-slot workbench can open the creation dialog"));
    createDesignThroughDialog(
        explicitWindow, QStringLiteral("test.explicit-slots@1.0.0"));
    auto* explicitEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        explicitWindow.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor")));
    auto* explicitView = explicitEditor
        ? explicitEditor->findChild<QGraphicsView*>() : nullptr;
    auto* explicitScene = explicitView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(explicitView->scene()) : nullptr;
    auto* explicitPalette = explicitWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointPalette"));
    if (explicitEditor) {
        explicitEditor->setRouterCollapsed(QStringLiteral("r-0-0"), false);
        explicitEditor->setRouterCollapsed(QStringLiteral("r-1-0"), false);
    }
    const auto explicitRouter00 = nodeIdWithCaption(
        explicitScene, QStringLiteral("r-0-0"));
    if (explicitScene && explicitRouter00) {
        explicitScene->nodeGraphicsObject(*explicitRouter00)->setSelected(true);
        explicitScene->nodeSelected(*explicitRouter00);
        application.processEvents();
    }
    chooseAttachmentSlot(1);
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

    chooseAttachmentSlot(0);
    const bool explicitMove = explicitEditor && explicitEditor->endpointMoveRequested
        ? explicitEditor->endpointMoveRequested(
              QStringLiteral("device_0"),
              finepaper::NocAttachmentTarget{
                  finepaper::RouterPosition{1, 0}, std::nullopt})
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

    chooseAttachmentSlot(1);
    bool explicitSameRouterBodyDraftStarted = false;
    if (movedExplicitEndpoint && explicitRouter10) {
        explicitSameRouterBodyDraftStarted = dragPortToNodeBody(
            explicitView,
            explicitScene,
            *movedExplicitEndpoint,
            QtNodes::PortType::Out,
            finepaper::portIndex(
                finepaper::EndpointOutputPort::Attachment),
            *explicitRouter10);
        application.processEvents();
        application.processEvents();
    }
    const auto sameRouterMovedEndpoint = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    const auto sameRouterMovedPort = sameRouterMovedEndpoint
        ? attachmentPortForEndpoint(explicitScene, *sameRouterMovedEndpoint)
        : std::nullopt;
    check(explicitSameRouterBodyDraftStarted && sameRouterMovedEndpoint
              && sameRouterMovedPort
              && *sameRouterMovedPort
                  == finepaper::portIndex(
                         finepaper::RouterInputPort::Endpoint) + 1U
              && explicitScene->graphModel().nodeData(
                     *sameRouterMovedEndpoint, QtNodes::NodeRole::Caption)
                     .toString().contains(QStringLiteral("slot local1")),
          QStringLiteral(
              "explicit same-Router body reconnect still asks for and applies a slot"));

    std::unique_ptr<QMimeData> explicitMime;
    if (explicitPalette && explicitPalette->count() > 0) {
        explicitMime.reset(explicitPalette->model()->mimeData(
            {explicitPalette->model()->index(0, 0)}));
    }
    const QPoint explicitBlankPosition = blankViewportPosition(explicitView);
    if (explicitView && explicitMime) {
        QDragEnterEvent dragEnter(explicitBlankPosition,
                                  Qt::CopyAction,
                                  explicitMime.get(),
                                  Qt::LeftButton,
                                  Qt::NoModifier);
        QApplication::sendEvent(explicitView->viewport(), &dragEnter);
        QDropEvent drop(QPointF(explicitBlankPosition),
                        Qt::CopyAction,
                        explicitMime.get(),
                        Qt::LeftButton,
                        Qt::NoModifier);
        QApplication::sendEvent(explicitView->viewport(), &drop);
        application.processEvents();
    }
    const auto explicitPending = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("Unattached\nDevice endpoint"));
    const auto exactSlotRouter = nodeIdWithCaption(
        explicitScene, QStringLiteral("r-0-0"));
    bool exactSlotDraftStarted = false;
    if (explicitPending && exactSlotRouter) {
        exactSlotDraftStarted = dragPortConnection(
            explicitView,
            explicitScene,
            *explicitPending,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *exactSlotRouter,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint) + 1U);
        application.processEvents();
        application.processEvents();
    }
    const auto exactSlotEndpoint = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_1"));
    const auto exactSlotPort = exactSlotEndpoint
        ? attachmentPortForEndpoint(explicitScene, *exactSlotEndpoint)
        : std::nullopt;
    check(exactSlotDraftStarted && exactSlotEndpoint && exactSlotPort
              && *exactSlotPort
                     == finepaper::portIndex(finepaper::RouterInputPort::Endpoint) + 1U
              && explicitScene->graphModel().nodeData(
                     *exactSlotEndpoint, QtNodes::NodeRole::Caption)
                     .toString().contains(QStringLiteral("slot local1")),
          QStringLiteral("dragging to an explicit Router port commits that exact slot without a second dialog"));

    const auto endpointBeforeRejectedDetach = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    const auto routerBeforeRejectedDetach = nodeIdWithCaption(
        explicitScene, QStringLiteral("r-1-0"));
    const auto originalRemovalCallback = explicitEditor
        ? explicitEditor->endpointRemovalRequested
        : std::function<bool(const QString&)>{};
    if (explicitEditor) {
        explicitEditor->endpointRemovalRequested = [](const QString&) { return false; };
    }
    if (explicitView && explicitScene && endpointBeforeRejectedDetach) {
        const QPoint endpointPosition = explicitView->mapFromScene(
            explicitScene->nodeGraphicsObject(*endpointBeforeRejectedDetach)
                ->sceneBoundingRect().center());
        sendContextMenu(explicitView, endpointPosition);
        application.processEvents();
    }
    auto* rejectedDetachMenu = explicitEditor ? explicitEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    QAction* rejectedDetach = rejectedDetachMenu ? rejectedDetachMenu->findChild<QAction*>(
        finepaper::workbench::detachEndpointActionName) : nullptr;
    if (rejectedDetach) {
        rejectedDetach->trigger();
        rejectedDetachMenu->close();
        application.processEvents();
    }
    if (explicitEditor) {
        explicitEditor->endpointRemovalRequested = originalRemovalCallback;
    }
    const auto endpointAfterRejectedDetach = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    const auto attachmentAfterRejectedDetach = endpointAfterRejectedDetach
        ? attachmentConnectionForEndpoint(
              explicitScene, *endpointAfterRejectedDetach)
        : std::nullopt;
    check(endpointBeforeRejectedDetach && routerBeforeRejectedDetach
              && attachmentAfterRejectedDetach
              && !nodeIdWithCaptionPrefix(
                  explicitScene, QStringLiteral("Unattached\nDevice endpoint")),
          QStringLiteral("a rejected disconnect keeps the durable attachment and creates no pending draft"));

    const auto endpointBeforeRejectedLineDelete = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    const auto rejectedLineConnection = endpointBeforeRejectedLineDelete
        ? attachmentConnectionForEndpoint(
              explicitScene, *endpointBeforeRejectedLineDelete)
        : std::nullopt;
    auto* explicitAnimatedView = dynamic_cast<finepaper::AnimatedGraphicsView*>(
        explicitView);
    QAction* explicitDeleteSelection = explicitAnimatedView
        ? explicitAnimatedView->deleteSelectionAction() : nullptr;
    if (explicitEditor) {
        explicitEditor->endpointRemovalRequested = [](const QString&) { return false; };
    }
    if (explicitScene && rejectedLineConnection && explicitDeleteSelection) {
        explicitScene->clearSelection();
        if (auto* connection = explicitScene->connectionGraphicsObject(
                *rejectedLineConnection)) {
            connection->setSelected(true);
            explicitDeleteSelection->trigger();
            application.processEvents();
            application.processEvents();
        }
    }
    if (explicitEditor) {
        explicitEditor->endpointRemovalRequested = originalRemovalCallback;
    }
    const auto endpointAfterRejectedLineDelete = nodeIdWithCaptionPrefix(
        explicitScene, QStringLiteral("device_0"));
    check(explicitDeleteSelection && endpointAfterRejectedLineDelete
              && attachmentConnectionForEndpoint(
                     explicitScene, *endpointAfterRejectedLineDelete)
              && !nodeIdWithCaptionPrefix(
                  explicitScene, QStringLiteral("Unattached\nDevice endpoint")),
          QStringLiteral("a rejected line deletion restores the projected attachment"));
    closeDiscarding(explicitWindow);

    QTemporaryDir numberPackageRoot(
        QStringLiteral("/tmp/finepaper-number-package-XXXXXX"));
    check(numberPackageRoot.isValid(),
          QStringLiteral("temporary number-parameter Package root is available"));
    const QString standardPackageRoot = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc"));
    check(numberPackageRoot.isValid()
              && createNumberParameterPackage(
                  standardPackageRoot, numberPackageRoot.path()),
          QStringLiteral("number-parameter Package fixture is created at runtime"));

    finepaper::RuntimeLocations multiPackageLocations{
        QStringList{standardPackageRoot, numberPackageRoot.path()}, outputRoot.path()};
    finepaper::FinepaperMainWindow multiPackageWindow(multiPackageLocations);
    multiPackageWindow.show();
    application.processEvents();
    auto* multiAvailablePackages = multiPackageWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.availablePackages"));
    auto* multiActivePackage = multiPackageWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackage"));
    auto* multiCreate = multiPackageWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    check(multiAvailablePackages
              && multiAvailablePackages->text().startsWith(
                  QStringLiteral("2 NoC IP Package"))
              && multiPackageWindow.findChild<QComboBox*>(
                     QStringLiteral("finepaper.packageSelector")) == nullptr,
          QStringLiteral("workbench separates Package availability from active design state"));
    createDesignThroughDialog(
        multiPackageWindow,
        QStringLiteral("test.number-parameter@1.0.0"),
        QStringLiteral("number_design"),
        3,
        4);
    check(multiActivePackage
              && multiActivePackage->text().contains(
                  QStringLiteral("test.number-parameter@1.0.0"))
              && multiPackageWindow.findChild<QWidget*>(
                  QStringLiteral("finepaper.parameter.frequencyScale")),
          QStringLiteral("creation dialog selects the NoC IP that owns the active design"));

    respondToNewDesignDialog(
        QStringLiteral("finepaper.noc@1.0.0"), {}, std::nullopt, std::nullopt, false);
    if (multiCreate) {
        multiCreate->click();
        application.processEvents();
    }
    check(multiActivePackage
              && multiActivePackage->text().contains(
                  QStringLiteral("test.number-parameter@1.0.0")),
          QStringLiteral("cancelling creation leaves the active design Package unchanged"));

    respondToNewDesignDialog(QStringLiteral("finepaper.noc@1.0.0"));
    chooseMessageBoxButton(QMessageBox::Discard);
    if (multiCreate) {
        multiCreate->click();
        application.processEvents();
    }
    check(multiActivePackage
              && multiActivePackage->text().contains(
                  QStringLiteral("finepaper.noc@1.0.0"))
              && !multiPackageWindow.findChild<QWidget*>(
                  QStringLiteral("finepaper.parameter.frequencyScale")),
          QStringLiteral("confirmed replacement switches active Package without split-brain UI"));
    closeDiscarding(multiPackageWindow);

    finepaper::RuntimeLocations numberLocations{
        QStringList{numberPackageRoot.path()}, outputRoot.path()};
    finepaper::FinepaperMainWindow numberWindow(numberLocations);
    numberWindow.show();
    application.processEvents();
    auto* numberCreate = numberWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    check(numberCreate != nullptr,
          QStringLiteral("number-parameter Package can start design creation"));
    createDesignThroughDialog(
        numberWindow, QStringLiteral("test.number-parameter@1.0.0"));
    auto* numberEditor = static_cast<finepaper::SchemaValueEditor*>(
        numberWindow.findChild<QWidget*>(
            QStringLiteral("finepaper.parameter.frequencyScale")));
    check(numberEditor && numberEditor->value()
              && qAbs(numberEditor->value()->toDouble() - 1.25) < 0.000001,
          QStringLiteral("number Package parameters use the shared schema editor"));
    if (numberEditor) {
        numberEditor->setValue(QJsonValue(2.75));
        if (numberEditor->valueChanged) {
            numberEditor->valueChanged();
        }
    }
    auto* numberApply = numberWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
    if (numberApply) {
        numberApply->click();
        application.processEvents();
    }
    numberEditor = static_cast<finepaper::SchemaValueEditor*>(
        numberWindow.findChild<QWidget*>(
            QStringLiteral("finepaper.parameter.frequencyScale")));
    check(numberEditor && numberEditor->value()
              && qAbs(numberEditor->value()->toDouble() - 2.75) < 0.000001,
          QStringLiteral("number Package parameters round-trip as JSON numbers"));

    auto* numberResultsDock = numberWindow.findChild<QDockWidget*>(
        finepaper::workbench::resultsDockName);
    if (numberResultsDock) {
        numberResultsDock->hide();
    }
    const QString unavailablePackagePath = numberPackageRoot.path()
        + QStringLiteral("-unavailable");
    const bool packageMoved = QDir().rename(
        numberPackageRoot.path(), unavailablePackagePath);
    check(packageMoved,
          QStringLiteral("runtime Package can be made unavailable for reload testing"));
    QAction* reloadPackagesAction = actionWithText(
        numberWindow, QStringLiteral("Reload Packages"));
    if (reloadPackagesAction) {
        reloadPackagesAction->trigger();
        application.processEvents();
        application.processEvents();
    }

    auto* retainedActivePackage = numberWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackage"));
    auto* retainedAvailablePackages = numberWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.availablePackages"));
    auto* retainedPalette = numberWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointPalette"));
    auto* retainedApply = numberWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
    auto* reloadErrorDrc = numberWindow.findChild<QTableWidget*>(
        QStringLiteral("finepaper.drcTable"));
    QAction* retainedValidate = actionWithText(
        numberWindow, QStringLiteral("Validate / DRC"));
    QAction* retainedGenerate = actionWithText(
        numberWindow, QStringLiteral("Generate RTL"));
    QAction* retainedNew = actionWithText(
        numberWindow, QStringLiteral("New NoC Design…"));
    check(retainedActivePackage
              && retainedActivePackage->text().contains(
                  QStringLiteral("test.number-parameter@1.0.0"))
              && retainedActivePackage->text().contains(
                  QStringLiteral("runtime unavailable"))
              && retainedAvailablePackages
              && retainedAvailablePackages->text().startsWith(
                  QStringLiteral("No runnable NoC IP Package"))
              && retainedPalette && retainedPalette->isEnabled()
              && retainedApply && !retainedApply->isEnabled()
              && retainedValidate && !retainedValidate->isEnabled()
              && retainedGenerate && !retainedGenerate->isEnabled()
              && retainedNew && !retainedNew->isEnabled()
              && numberCreate && !numberCreate->isEnabled(),
          QStringLiteral("failed reload retains editing metadata without enabling a clean no-op or claiming runtime availability"));
    check(numberResultsDock && numberResultsDock->isVisible()
              && reloadErrorDrc && reloadErrorDrc->rowCount() > 0,
          QStringLiteral("Package reload errors automatically expose structured diagnostics"));
    check(numberWindow.statusBar()->currentMessage().startsWith(
              QStringLiteral("Package reload failed:")),
          QStringLiteral("Package reload errors produce a clear status summary"));

    const bool recoveredPackage = packageMoved
        && numberWindow.installPackageDirectory(unavailablePackagePath);
    application.processEvents();
    const QStringList installedPackageRoots = QSettings()
        .value(finepaper::workbench::packageRootsSetting)
        .toStringList();
    check(recoveredPackage && installedPackageRoots.contains(unavailablePackagePath),
          QStringLiteral("a stale same-key Package can be transactionally rebound to a valid directory"));
    check(reloadErrorDrc && reloadErrorDrc->rowCount() == 0
              && numberWindow.statusBar()->currentMessage().startsWith(
                  QStringLiteral("Installed NoC IP test.number-parameter@1.0.0"))
              && retainedActivePackage
              && retainedActivePackage->text().contains(
                  QStringLiteral("test.number-parameter@1.0.0"))
              && !retainedActivePackage->text().contains(
                  QStringLiteral("runtime unavailable"))
              && retainedAvailablePackages
              && retainedAvailablePackages->text().startsWith(
                  QStringLiteral("1 NoC IP Package"))
              && retainedValidate && retainedValidate->isEnabled()
              && retainedGenerate && retainedGenerate->isEnabled()
              && retainedNew && retainedNew->isEnabled()
              && numberCreate && numberCreate->isEnabled(),
          QStringLiteral("successful Package repair clears stale errors and restores runtime actions"));
    closeDiscarding(numberWindow);
    QSettings().remove(finepaper::workbench::packageRootsSetting);
    if (packageMoved) {
        check(QDir().rename(unavailablePackagePath, numberPackageRoot.path()),
              QStringLiteral("runtime Package fixture is restored after repair testing"));
    }

    QTemporaryDir requiredRelationPackageRoot(
        QStringLiteral("/tmp/finepaper-required-relation-package-XXXXXX"));
    check(requiredRelationPackageRoot.isValid()
              && createRequiredRelationPackage(
                  standardPackageRoot, requiredRelationPackageRoot.path()),
          QStringLiteral("a Package V2 fixture with a required cross-Domain relation is created"));
    finepaper::RuntimeLocations requiredRelationLocations{
        QStringList{requiredRelationPackageRoot.path()}, outputRoot.path()};
    finepaper::FinepaperMainWindow requiredRelationWindow(
        requiredRelationLocations);
    requiredRelationWindow.show();
    application.processEvents();

    bool sawRequiredConfigurationRecovery = false;
    bool completedRequiredRelation = false;
    bool appliedRequiredConfiguration = false;
    std::function<void(int)> applyRequiredConfigurationWhenReady;
    applyRequiredConfigurationWhenReady = [&](int attempts) {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* apply = dialog
            && dialog->objectName()
                == QStringLiteral("finepaper.domainConfigurationDialog")
            ? dialog->findChild<QPushButton*>(
                  QStringLiteral("finepaper.domainConfiguration.apply"))
            : nullptr;
        if (apply && apply->isEnabled()) {
            appliedRequiredConfiguration = true;
            apply->click();
            return;
        }
        if (attempts > 0) {
            QTimer::singleShot(20, [&, attempts] {
                applyRequiredConfigurationWhenReady(attempts - 1);
            });
        } else if (dialog) {
            dialog->reject();
        }
    };
    std::function<void(int)> configureRequiredRelation;
    configureRequiredRelation = [&](int attempts) {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog
            || dialog->objectName()
                != QStringLiteral("finepaper.domainConfigurationDialog")) {
            if (attempts > 0) {
                QTimer::singleShot(20, [&, attempts] {
                    configureRequiredRelation(attempts - 1);
                });
            }
            return;
        }

        auto* domains = dialog->findChild<QTableWidget*>(
            QStringLiteral("finepaper.domainConfiguration.domains"));
        auto* memberships = dialog->findChild<QTableWidget*>(
            QStringLiteral("finepaper.domainConfiguration.memberships"));
        auto* relations = dialog->findChild<QTableWidget*>(
            QStringLiteral("finepaper.domainConfiguration.relations"));
        auto* addRelation = dialog->findChild<QPushButton*>(
            QStringLiteral("finepaper.domainConfiguration.relations.add"));
        sawRequiredConfigurationRecovery = domains && memberships && relations
            && domains->rowCount() == 2
            && memberships->rowCount() == 1
            && relations->rowCount() == 0
            && addRelation;
        if (!addRelation) {
            dialog->reject();
            return;
        }

        QTimer::singleShot(0, [&] {
            auto* relationEditor = qobject_cast<QDialog*>(
                QApplication::activeModalWidget());
            if (!relationEditor
                || relationEditor->objectName()
                    != QStringLiteral(
                        "finepaper.domainConfiguration.relationDialog")) {
                if (relationEditor) {
                    relationEditor->reject();
                }
                return;
            }
            auto* from = relationEditor->findChild<QComboBox*>(
                QStringLiteral(
                    "finepaper.domainConfiguration.relationDialog.from"));
            auto* type = relationEditor->findChild<QComboBox*>(
                QStringLiteral(
                    "finepaper.domainConfiguration.relationDialog.type"));
            auto* to = relationEditor->findChild<QComboBox*>(
                QStringLiteral(
                    "finepaper.domainConfiguration.relationDialog.to"));
            const bool selected = chooseComboTextContaining(
                                      from, QStringLiteral("Clock domains"))
                && chooseComboData(type, QStringLiteral("poweredBy"))
                && chooseComboTextContaining(
                    to, QStringLiteral("Power domains"));
            auto* buttons = relationEditor->findChild<QDialogButtonBox*>();
            QPushButton* save = buttons
                ? buttons->button(QDialogButtonBox::Ok) : nullptr;
            if (selected && save && save->isEnabled()) {
                completedRequiredRelation = true;
                save->click();
            } else {
                relationEditor->reject();
            }
        });
        addRelation->click();
        applyRequiredConfigurationWhenReady(100);
    };

    respondToNewDesignDialog(
        QStringLiteral("test.required-relation@1.0.0"),
        QStringLiteral("required_relation_design"), 1, 1, true);
    configureRequiredRelation(100);
    auto* requiredRelationCreate = requiredRelationWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    if (requiredRelationCreate) {
        requiredRelationCreate->click();
        application.processEvents();
    }
    auto* requiredRelationActivePackage = requiredRelationWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackage"));
    check(requiredRelationCreate
              && sawRequiredConfigurationRecovery
              && completedRequiredRelation
              && appliedRequiredConfiguration
              && requiredRelationActivePackage
              && requiredRelationActivePackage->text().contains(
                  QStringLiteral("test.required-relation@1.0.0"))
              && requiredRelationWindow.isWindowModified(),
          QStringLiteral("New Design recovers Package-required Domain relations through the same atomic complete editor"));

    auto* requiredRelationComplete = requiredRelationWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.completeConfiguration"));
    if (requiredRelationComplete) {
        requiredRelationComplete->click();
        application.processEvents();
    }
    auto* requiredRelationCenter = qobject_cast<QTabWidget*>(
        requiredRelationWindow.centralWidget());
    auto* requiredRelationWorkspace = requiredRelationWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.domainConfigurationWorkspace"));
    auto* requiredRelations = requiredRelationWorkspace
        ? requiredRelationWorkspace->findChild<QTableWidget*>(
              QStringLiteral("finepaper.domainConfiguration.relations"))
        : nullptr;
    check(requiredRelationComplete && requiredRelationCenter
              && requiredRelationCenter->currentWidget()
                  == requiredRelationWorkspace
              && requiredRelations && requiredRelations->rowCount() == 1
              && requiredRelations->item(0, 0)->text()
                  == QStringLiteral("poweredBy"),
          QStringLiteral(
              "the relation accepted during creation becomes active data in the persistent Domain Workspace"));
    closeDiscarding(requiredRelationWindow);

    QTemporaryDir domainPackageRoot(
        QStringLiteral("/tmp/finepaper-domain-package-XXXXXX"));
    QTemporaryDir domainDesignRoot(
        QStringLiteral("/tmp/finepaper-domain-design-XXXXXX"));
    check(domainPackageRoot.isValid() && domainDesignRoot.isValid(),
          QStringLiteral("temporary generic Domain GUI fixtures are available"));
    check(createDomainPresentationPackage(
              standardPackageRoot, domainPackageRoot.path()),
          QStringLiteral("a Package V2 fixture declares arbitrary Domain types"));
    const QString domainDesignPath = QDir(domainDesignRoot.path()).filePath(
        QStringLiteral("domain-presentation.fpnoc"));
    check(writeDomainPresentationDesign(domainDesignPath),
          QStringLiteral("a Design V2 fixture carries generic Domain assignments"));

    finepaper::RuntimeLocations domainLocations{
        QStringList{domainPackageRoot.path()}, outputRoot.path()};
    finepaper::FinepaperMainWindow domainWindow(domainLocations);
    domainWindow.show();
    application.processEvents();
    check(domainWindow.openDesignFile(domainDesignPath),
          QStringLiteral("the Package-driven Domain design opens in the workbench"));
    application.processEvents();

    auto* domainSelector = domainWindow.findChild<QComboBox*>(
        finepaper::workbench::domainLayerSelectorName);
    check(domainSelector && domainSelector->isEnabled()
              && domainSelector->count() == 3
              && domainSelector->itemText(0) == QStringLiteral("None")
              && domainSelector->itemData(0).toString().isEmpty()
              && domainSelector->findData(QStringLiteral("security-zone")) > 0
              && domainSelector->findData(QStringLiteral("fabric-tier")) > 0,
          QStringLiteral("Color by lists Package labels backed by arbitrary stable Domain type ids"));

    auto* domainManagerDock = domainWindow.findChild<QDockWidget*>(
        finepaper::workbench::domainManagerDockName);
    QAction* domainManagerToggle = domainWindow.findChild<QAction*>(
        finepaper::workbench::domainManagerToggleActionName);
    if (domainManagerToggle && !domainManagerToggle->isChecked()) {
        domainManagerToggle->trigger();
        application.processEvents();
    }
    check(domainManagerDock && domainManagerToggle
              && domainWindow.dockWidgetArea(domainManagerDock)
                  == Qt::RightDockWidgetArea
              && domainManagerDock->isVisible()
              && domainManagerToggle->isChecked(),
          QStringLiteral("the Package-driven Domain Manager is a toggleable right-side Dock"));
    if (domainManagerDock && domainManagerToggle) {
        domainManagerToggle->trigger();
        application.processEvents();
        check(!domainManagerDock->isVisible() && !domainManagerToggle->isChecked(),
              QStringLiteral("the Domain Manager toggle hides its Dock"));
        domainManagerToggle->trigger();
        domainManagerDock->raise();
        application.processEvents();
        check(domainManagerDock->isVisible() && domainManagerToggle->isChecked()
                  && !domainWindow.isWindowModified(),
              QStringLiteral("restoring the Domain Manager is Workspace-only and does not dirty the design"));
    }

    auto* domainManagerType = domainWindow.findChild<QComboBox*>(
        QStringLiteral("finepaper.domainManager.typeSelector"));
    auto* domainManagerInstances = domainWindow.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainManager.instanceView"));
    auto* domainManagerTabs = domainWindow.findChild<QTabWidget*>(
        QStringLiteral("finepaper.domainManager.tabs"));
    auto* domainAssignmentState = domainWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.domainManager.assignmentState"));
    auto* domainMultipleAssignment = domainWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.domainManager.assignmentEditor.multiple"));
    auto* domainApplyAssignment = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.applyAssignment"));
    auto* domainCompleteConfiguration = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.completeConfiguration"));
    auto* domainSelectMembers = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.selectMembers"));
    auto* domainSelectAllEligible = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.selectAllEligible"));
    if (domainManagerType) {
        domainManagerType->setCurrentIndex(
            domainManagerType->findData(QStringLiteral("security-zone")));
        application.processEvents();
    }
    const auto domainInstanceRow = [domainManagerInstances](const QString& id) {
        if (!domainManagerInstances) {
            return -1;
        }
        for (int row = 0; row < domainManagerInstances->rowCount(); ++row) {
            QTableWidgetItem* item = domainManagerInstances->item(row, 2);
            if (item && item->data(finepaper::domainManagerDomainIdRole).toString()
                    == id) {
                return row;
            }
        }
        return -1;
    };
    const int zoneARow = domainInstanceRow(QStringLiteral("zone-a"));
    const int zoneBRow = domainInstanceRow(QStringLiteral("zone-b"));
    check(domainManagerType && domainManagerType->count() == 2
              && domainManagerType->findData(QStringLiteral("security-zone")) >= 0
              && domainManagerType->findData(QStringLiteral("fabric-tier")) >= 0
              && domainManagerInstances && domainManagerInstances->rowCount() == 2
              && zoneARow >= 0 && zoneBRow >= 0,
          QStringLiteral("Domain Manager lists arbitrary Package types and their instances"));
    check(zoneARow >= 0 && zoneBRow >= 0
              && domainManagerInstances->item(zoneARow, 1)->text()
                  == QStringLiteral("Trusted")
              && domainManagerInstances->item(zoneBRow, 1)->text()
                  == QStringLiteral("Restricted")
              && domainManagerInstances->item(zoneARow, 3)->data(
                     finepaper::domainManagerMemberCountRole).toInt() == 1
              && domainManagerInstances->item(zoneBRow, 3)->data(
                     finepaper::domainManagerMemberCountRole).toInt() == 1
              && domainManagerInstances->item(zoneARow, 4)->data(
                     finepaper::domainManagerCrossingCountRole).toInt() == 1
              && domainManagerInstances->item(zoneBRow, 4)->data(
                     finepaper::domainManagerCrossingCountRole).toInt() == 1
              && domainManagerInstances->item(zoneARow, 0)->data(
                     finepaper::domainManagerColorRole).value<QColor>().isValid()
              && domainManagerInstances->item(zoneBRow, 0)->data(
                     finepaper::domainManagerColorRole).value<QColor>().isValid(),
          QStringLiteral("Domain Manager instances double as a deterministic color/member/crossing Legend"));

    auto* domainEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        domainWindow.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor")));
    auto* domainView = domainEditor
        ? domainEditor->findChild<QGraphicsView*>() : nullptr;
    auto* domainScene = domainView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(domainView->scene()) : nullptr;
    const auto domainRouter = nodeIdWithCaption(
        domainScene, QStringLiteral("r-0-0"));
    const auto domainRouter1 = nodeIdWithCaption(
        domainScene, QStringLiteral("r-1-0"));
    QtNodes::NodeGraphicsObject* domainRouterGraphics =
        domainScene && domainRouter
        ? domainScene->nodeGraphicsObject(*domainRouter) : nullptr;
    QtNodes::NodeGraphicsObject* domainRouter1Graphics =
        domainScene && domainRouter1
        ? domainScene->nodeGraphicsObject(*domainRouter1) : nullptr;
    const qsizetype domainNodeCount = domainScene
        ? static_cast<qsizetype>(domainScene->graphModel().allNodeIds().size()) : 0;
    const qsizetype domainConnectionCount = static_cast<qsizetype>(
        sceneConnectionIds(domainScene).size());
    if (domainManagerInstances && zoneARow >= 0) {
        domainManagerInstances->selectRow(zoneARow);
    }
    if (domainSelectMembers) {
        domainSelectMembers->click();
        application.processEvents();
    }
    check(domainSelectMembers && domainRouterGraphics && domainRouter1Graphics
              && domainRouterGraphics->isSelected()
              && !domainRouter1Graphics->isSelected(),
          QStringLiteral("Domain instance selection helper selects its exact canvas members"));
    if (domainSelectAllEligible) {
        domainSelectAllEligible->click();
        application.processEvents();
    }
    check(domainSelectAllEligible && domainRouterGraphics && domainRouter1Graphics
              && domainRouterGraphics->isSelected()
              && domainRouter1Graphics->isSelected(),
          QStringLiteral("Domain all-eligible helper bridges to one semantic canvas selection"));
    if (domainScene && domainRouterGraphics) {
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        application.processEvents();
    }
    int domainLayerSelectionCallbacks = 0;
    std::function<void(const finepaper::NocEditorSelectionSet&)>
        domainMainWindowSelectionHandler;
    if (domainEditor) {
        domainMainWindowSelectionHandler = domainEditor->semanticSelectionChanged;
        domainEditor->semanticSelectionChanged =
            [&, domainMainWindowSelectionHandler](
                const finepaper::NocEditorSelectionSet& selection) {
                ++domainLayerSelectionCallbacks;
                if (domainMainWindowSelectionHandler) {
                    domainMainWindowSelectionHandler(selection);
                }
            };
    }
    if (domainSelector) {
        domainSelector->setCurrentIndex(
            domainSelector->findData(QStringLiteral("security-zone")));
        application.processEvents();
    }

    QtNodes::ConnectionGraphicsObject* domainLinkGraphics = nullptr;
    const auto domainConnections = sceneConnectionIds(domainScene);
    if (domainScene && !domainConnections.empty()) {
        domainLinkGraphics = domainScene->connectionGraphicsObject(
            *domainConnections.begin());
    }
    check(domainEditor
              && domainEditor->domainPresentation().activeDomainType
                  == QStringLiteral("security-zone")
              && domainEditor->domainPresentation().legend.size() == 2,
          QStringLiteral("the selected generic Domain type produces a presentation snapshot"));
    check(domainScene && domainRouter && domainRouterGraphics
              && domainScene->nodeGraphicsObject(*domainRouter)
                  == domainRouterGraphics
              && domainScene->graphModel().allNodeIds().size()
                  == static_cast<size_t>(domainNodeCount)
              && sceneConnectionIds(domainScene).size()
                  == static_cast<size_t>(domainConnectionCount)
              && domainRouterGraphics->isSelected()
              && domainLayerSelectionCallbacks == 0
              && !domainWindow.isWindowModified(),
          QStringLiteral("switching Domain layers repaints in place without rebuilding, changing selection, or dirtying the design"));
    check(domainRouterGraphics
              && static_cast<finepaper::DomainAssignmentDisplayState>(
                     domainRouterGraphics->data(
                         finepaper::domainAssignmentStateDataRole).toInt())
                  == finepaper::DomainAssignmentDisplayState::Assigned
              && domainRouterGraphics->data(
                     finepaper::domainColorsDataRole).toList().size() == 1
              && domainLinkGraphics
              && domainLinkGraphics->data(
                     finepaper::domainCrossingDataRole).toBool(),
          QStringLiteral("nodes and derived Mesh crossings receive generic Domain presentation roles"));

    if (domainScene && domainLinkGraphics) {
        domainScene->clearSelection();
        domainLinkGraphics->setSelected(true);
        application.processEvents();
    }
    auto* domainSelectionInspectorGroup = domainWindow.findChild<QGroupBox*>(
        finepaper::workbench::selectionInspectorName);
    auto* domainSelectionInspector = domainSelectionInspectorGroup
        ? domainSelectionInspectorGroup->findChild<QLabel*>() : nullptr;
    check(domainSelectionInspector
              && domainSelectionInspector->text().contains(
                  QStringLiteral("Color-by Domain crossing"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("From set: { zone-a }"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("To set: { zone-b }"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("zone-transition"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("Edge override"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("default applies unchanged")),
          QStringLiteral("Inspector separates singleton crossing sets, the resolved default, and the absent edge override"));

    const QString domainWorkspaceKey = finepaper::workbench::designWorkspaceKey(
        QStringLiteral("test.domain-presentation"),
        QStringLiteral("1.0.0"),
        QStringLiteral("domain_presentation_design"));
    check(QSettings().value(
              finepaper::workbench::domainLayerSelectionsSetting).toMap()
              .value(domainWorkspaceKey).toString()
              == QStringLiteral("security-zone"),
          QStringLiteral("the active Domain layer is saved as per-design Workspace state"));

    const int callbacksBeforeCompleteConfiguration =
        domainLayerSelectionCallbacks;
    if (domainCompleteConfiguration) {
        domainCompleteConfiguration->click();
        application.processEvents();
    }
    auto* domainCenterViews = qobject_cast<QTabWidget*>(
        domainWindow.centralWidget());
    auto* domainConfigurationWorkspace = domainWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.domainConfigurationWorkspace"));
    auto* domainWorkspaceDomains = domainConfigurationWorkspace
        ? domainConfigurationWorkspace->findChild<QTableWidget*>(
              QStringLiteral("finepaper.domainConfiguration.domains"))
        : nullptr;
    auto* domainWorkspaceTabs = domainConfigurationWorkspace
        ? domainConfigurationWorkspace->findChild<QTabWidget*>(
              QStringLiteral("finepaper.domainConfiguration.tabs"))
        : nullptr;
    auto* domainWorkspaceCapabilities = domainConfigurationWorkspace
        ? domainConfigurationWorkspace->findChild<QLabel*>(
              QStringLiteral(
                  "finepaper.domainConfigurationWorkspace.runtimeCapabilities"))
        : nullptr;
    check(domainCompleteConfiguration && domainCenterViews
              && domainCenterViews->currentWidget()
                  == domainConfigurationWorkspace
              && domainWorkspaceTabs && domainWorkspaceTabs->count() == 5
              && domainWorkspaceDomains
              && domainWorkspaceDomains->rowCount() == 3
              && domainWorkspaceCapabilities
              && domainWorkspaceCapabilities->text().contains(
                  QStringLiteral("all five"))
              && QApplication::activeModalWidget() == nullptr
              && !domainWindow.isWindowModified()
              && domainView && domainView->scene() == domainScene
              && domainScene && domainRouter
              && domainScene->nodeGraphicsObject(*domainRouter)
                  == domainRouterGraphics
              && domainLinkGraphics && domainLinkGraphics->isSelected()
              && domainLayerSelectionCallbacks
                  == callbacksBeforeCompleteConfiguration
              && domainManagerInstances
              && domainManagerInstances->rowCount() == 2,
          QStringLiteral(
              "Domain Manager switches to the persistent five-page Workspace without mutating the design, scene, or selection"));

    bool sawCompleteConfiguration = domainConfigurationWorkspace
        && domainCenterViews
        && domainCenterViews->currentWidget() == domainConfigurationWorkspace;
    bool sawCompleteDomainEditor = false;
    bool submittedCompleteConfiguration = false;
    std::function<void(int)> applyCompleteConfigurationWhenReady;
    applyCompleteConfigurationWhenReady = [&](int attempts) {
        auto* apply = domainConfigurationWorkspace
            ? domainConfigurationWorkspace->findChild<QPushButton*>(
                  QStringLiteral("finepaper.domainConfiguration.apply"))
            : nullptr;
        if (apply && apply->isEnabled()) {
            submittedCompleteConfiguration = true;
            apply->click();
            return;
        }
        if (attempts > 0) {
            QTimer::singleShot(20, [&, attempts] {
                applyCompleteConfigurationWhenReady(attempts - 1);
            });
        }
    };
    QTimer::singleShot(0, [&] {
        sawCompleteConfiguration = domainConfigurationWorkspace
            && domainCenterViews
            && domainCenterViews->currentWidget()
                == domainConfigurationWorkspace;
        auto* addDomain = domainConfigurationWorkspace
            ? domainConfigurationWorkspace->findChild<QPushButton*>(
                  QStringLiteral(
                      "finepaper.domainConfiguration.domains.add"))
            : nullptr;
        if (!addDomain) {
            return;
        }
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<QDialog*>(
                QApplication::activeModalWidget());
            sawCompleteDomainEditor = editor
                && editor->objectName()
                    == QStringLiteral(
                        "finepaper.domainConfiguration.domainDialog");
            auto* type = editor ? editor->findChild<QComboBox*>(
                                      QStringLiteral(
                                          "finepaper.domainConfiguration.domainDialog.type"))
                                : nullptr;
            auto* id = editor ? editor->findChild<QLineEdit*>(
                                    QStringLiteral(
                                        "finepaper.domainConfiguration.domainDialog.id"))
                              : nullptr;
            auto* name = editor ? editor->findChild<QLineEdit*>(
                                      QStringLiteral(
                                          "finepaper.domainConfiguration.domainDialog.name"))
                                : nullptr;
            if (type) {
                type->setCurrentIndex(
                    type->findData(QStringLiteral("security-zone")));
            }
            if (id) {
                id->setText(QStringLiteral("zone-c"));
            }
            if (name) {
                name->setText(QStringLiteral("Observed"));
            }
            auto* save = editor ? editor->findChild<QPushButton*>(
                                      QStringLiteral(
                                          "finepaper.domainConfiguration.domainDialog.save"))
                                : nullptr;
            if (save && save->isEnabled()) {
                save->click();
            } else if (editor) {
                editor->reject();
            }
        });
        addDomain->click();
        applyCompleteConfigurationWhenReady(100);
    });
    if (domainCompleteConfiguration) {
        domainCompleteConfiguration->click();
        waitUntil([&submittedCompleteConfiguration] {
            return submittedCompleteConfiguration;
        }, 4000);
        application.processEvents();
    }

    const int zoneCRow = domainInstanceRow(QStringLiteral("zone-c"));
    check(sawCompleteConfiguration,
          QStringLiteral("the persistent Domain Workspace remains selected for complete editing"));
    check(sawCompleteDomainEditor,
          QStringLiteral("Workspace Domain Add opens the existing schema-driven record editor"));
    check(submittedCompleteConfiguration,
          QStringLiteral("the complete Workspace draft becomes valid and enables atomic Apply"));
    check(domainWindow.isWindowModified() && zoneCRow >= 0
              && domainManagerInstances
              && domainManagerInstances->rowCount() == 3
              && domainManagerInstances->item(zoneCRow, 1)->text()
                  == QStringLiteral("Observed"),
          QStringLiteral("a legal complete working-copy change is applied atomically and refreshes the Domain Manager"));
    check(domainView && domainView->scene() == domainScene
              && domainScene && domainRouter && domainRouter1
              && domainScene->nodeGraphicsObject(*domainRouter)
                  == domainRouterGraphics
              && domainScene->nodeGraphicsObject(*domainRouter1)
                  == domainRouter1Graphics
              && domainScene->graphModel().allNodeIds().size()
                  == static_cast<size_t>(domainNodeCount)
              && sceneConnectionIds(domainScene).size()
                  == static_cast<size_t>(domainConnectionCount)
              && domainLinkGraphics && domainLinkGraphics->isSelected()
              && domainLayerSelectionCallbacks
                  == callbacksBeforeCompleteConfiguration,
          QStringLiteral("complete Domain Apply repaints the existing scene without rebuilding or disturbing selection"));

    QAction* domainCompleteSaveAction = actionWithText(
        domainWindow, QStringLiteral("Save"));
    if (domainCompleteSaveAction) {
        domainCompleteSaveAction->trigger();
        application.processEvents();
    }
    QFile savedCompleteDomainFile(domainDesignPath);
    QJsonDocument savedCompleteDomainDocument;
    if (savedCompleteDomainFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        savedCompleteDomainDocument = QJsonDocument::fromJson(
            savedCompleteDomainFile.readAll(), &error);
        check(error.error == QJsonParseError::NoError,
              QStringLiteral("the complete Domain configuration saves as valid JSON"));
    }
    const QJsonObject savedCompleteDomain =
        savedCompleteDomainDocument.object();
    const QJsonArray savedDomains = savedCompleteDomain.value(
        QStringLiteral("domains")).toArray();
    const QJsonArray savedCrossingPolicies = savedCompleteDomain.value(
        QStringLiteral("crossingPolicies")).toArray();
    bool savedZoneC = false;
    for (const QJsonValue& value : savedDomains) {
        savedZoneC = savedZoneC
            || (value.isObject()
                && value.toObject().value(QStringLiteral("id")).toString()
                    == QStringLiteral("zone-c"));
    }
    bool savedZoneTransition = false;
    for (const QJsonValue& value : savedCrossingPolicies) {
        const QJsonObject policy = value.toObject();
        savedZoneTransition = savedZoneTransition
            || (policy.value(QStringLiteral("id")).toString()
                    == QStringLiteral("zone-transition")
                && policy.value(QStringLiteral("domainType")).toString()
                    == QStringLiteral("security-zone")
                && policy.value(QStringLiteral("from")).toString()
                    == QStringLiteral("zone-a")
                && policy.value(QStringLiteral("to")).toString()
                    == QStringLiteral("zone-b"));
    }
    check(domainCompleteSaveAction && !domainWindow.isWindowModified()
              && savedCompleteDomain.value(QStringLiteral("domains")).isArray()
              && savedCompleteDomain.value(
                     QStringLiteral("domainMemberships")).isArray()
              && savedCompleteDomain.value(
                     QStringLiteral("domainRelations")).isArray()
              && savedCompleteDomain.value(
                     QStringLiteral("crossingPolicies")).isArray()
              && savedCompleteDomain.value(
                     QStringLiteral("edgeOverrides")).isArray()
              && savedDomains.size() == 4 && savedZoneC
              && savedCompleteDomain.value(
                     QStringLiteral("domainMemberships")).toArray().size() == 2
              && savedCompleteDomain.value(
                     QStringLiteral("domainRelations")).toArray().isEmpty()
              && savedCrossingPolicies.size() == 1
              && savedZoneTransition
              && savedCompleteDomain.value(
                     QStringLiteral("edgeOverrides")).toArray().isEmpty(),
          QStringLiteral("saving complete Domain configuration persists all five arrays, the added optional Domain, and the existing default policy"));

    int workspaceZoneCRow = -1;
    if (domainWorkspaceDomains) {
        for (int row = 0; row < domainWorkspaceDomains->rowCount(); ++row) {
            QTableWidgetItem* id = domainWorkspaceDomains->item(row, 2);
            if (id && id->text() == QStringLiteral("zone-c")) {
                workspaceZoneCRow = row;
                break;
            }
        }
    }
    auto* deleteWorkspaceDomain = domainConfigurationWorkspace
        ? domainConfigurationWorkspace->findChild<QPushButton*>(
              QStringLiteral("finepaper.domainConfiguration.domains.delete"))
        : nullptr;
    auto* revertWorkspaceDraft = domainConfigurationWorkspace
        ? domainConfigurationWorkspace->findChild<QPushButton*>(
              QStringLiteral("finepaper.domainConfiguration.revert"))
        : nullptr;
    if (domainWorkspaceDomains && deleteWorkspaceDomain
        && workspaceZoneCRow >= 0) {
        domainWorkspaceDomains->selectRow(workspaceZoneCRow);
        deleteWorkspaceDomain->click();
        application.processEvents();
    }
    check(workspaceZoneCRow >= 0 && revertWorkspaceDraft
              && revertWorkspaceDraft->isEnabled()
              && domainWorkspaceDomains
              && domainWorkspaceDomains->rowCount() == 3
              && domainManagerInstances
              && domainManagerInstances->rowCount() == 3
              && !domainWindow.isWindowModified(),
          QStringLiteral(
              "a persistent complete-configuration draft remains separate from durable quick-Manager data"));
    if (domainCenterViews) {
        domainCenterViews->setCurrentIndex(0);
        application.processEvents();
    }

    if (domainManagerTabs) {
        domainManagerTabs->setCurrentIndex(1);
    }
    if (domainScene && domainRouterGraphics && domainRouter1Graphics) {
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        domainRouter1Graphics->setSelected(true);
        application.processEvents();
    }
    const auto domainAssignmentItem = [domainMultipleAssignment](const QString& id) {
        if (!domainMultipleAssignment) {
            return static_cast<QListWidgetItem*>(nullptr);
        }
        for (int row = 0; row < domainMultipleAssignment->count(); ++row) {
            QListWidgetItem* item = domainMultipleAssignment->item(row);
            if (item && item->data(finepaper::domainManagerDomainIdRole).toString()
                    == id) {
                return item;
            }
        }
        return static_cast<QListWidgetItem*>(nullptr);
    };
    QListWidgetItem* zoneAAssignment = domainAssignmentItem(
        QStringLiteral("zone-a"));
    QListWidgetItem* zoneBAssignment = domainAssignmentItem(
        QStringLiteral("zone-b"));
    check(domainAssignmentState
              && domainAssignmentState->property("assignmentState").toString()
                  == QStringLiteral("mixed")
              && domainAssignmentState->text().contains(QStringLiteral("Mixed"))
              && zoneAAssignment && zoneBAssignment
              && zoneAAssignment->checkState() == Qt::PartiallyChecked
              && zoneBAssignment->checkState() == Qt::PartiallyChecked,
          QStringLiteral("two Routers with different generic multiple assignments expose Mixed tri-state choices"));

    int callbacksBeforeDomainApply = domainLayerSelectionCallbacks;
    if (zoneAAssignment && zoneBAssignment) {
        zoneAAssignment->setCheckState(Qt::Checked);
        zoneBAssignment->setCheckState(Qt::Unchecked);
        application.processEvents();
    }
    check(domainApplyAssignment && domainApplyAssignment->isEnabled()
              && !domainWindow.isWindowModified(),
          QStringLiteral("editing Mixed choices stages an explicit atomic assignment without mutating the design"));

    auto* domainEndpointPalette = domainWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointPalette"));
    if (domainScene && domainRouter && domainRouterGraphics
        && domainEndpointPalette && domainEndpointPalette->count() > 0) {
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        domainScene->nodeSelected(*domainRouter);
        application.processEvents();
        endpointCreationAutoAccepter.setEnabled(false);
        chooseMessageBoxButton(QMessageBox::Discard);
        rejectEndpointCreationDialog();
        domainEndpointPalette->itemDoubleClicked(
            domainEndpointPalette->item(0));
        application.processEvents();
        endpointCreationAutoAccepter.setEnabled(true);
    }
    zoneAAssignment = domainAssignmentItem(QStringLiteral("zone-a"));
    zoneBAssignment = domainAssignmentItem(QStringLiteral("zone-b"));
    check(domainEndpointPalette && domainEndpointPalette->count() > 0
              && domainApplyAssignment && domainApplyAssignment->isEnabled()
              && zoneAAssignment
              && zoneAAssignment->checkState() == Qt::Checked
              && zoneBAssignment
              && zoneBAssignment->checkState() == Qt::Unchecked
              && revertWorkspaceDraft && revertWorkspaceDraft->isEnabled()
              && domainWorkspaceDomains
              && domainWorkspaceDomains->rowCount() == 3
              && !domainWindow.isWindowModified(),
          QStringLiteral(
              "authorizing discard before Endpoint creation keeps both quick and complete Domain drafts when the later Endpoint dialog is cancelled"));
    if (domainScene && domainRouterGraphics && domainRouter1Graphics) {
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        domainRouter1Graphics->setSelected(true);
        application.processEvents();
    }
    callbacksBeforeDomainApply = domainLayerSelectionCallbacks;

    QAction* pendingDomainSave = actionWithText(
        domainWindow, QStringLiteral("Save"));
    QAction* pendingDomainValidate = actionWithText(
        domainWindow, QStringLiteral("Validate / DRC"));
    QAction* pendingDomainGenerate = actionWithText(
        domainWindow, QStringLiteral("Generate RTL"));
    for (QAction* action
         : {pendingDomainSave, pendingDomainValidate, pendingDomainGenerate}) {
        if (!action) {
            continue;
        }
        chooseMessageBoxButton(QMessageBox::Cancel);
        action->trigger();
        application.processEvents();
    }
    check(pendingDomainSave && pendingDomainValidate && pendingDomainGenerate
              && domainApplyAssignment->isEnabled()
              && revertWorkspaceDraft && revertWorkspaceDraft->isEnabled()
              && !domainWindow.operationBusy()
              && !domainWindow.isWindowModified(),
          QStringLiteral(
              "Save, Validate and Generate cannot silently ignore quick or complete Domain drafts"));

    if (domainApplyAssignment) {
        chooseMessageBoxButton(QMessageBox::Cancel);
        domainApplyAssignment->click();
        application.processEvents();
    }
    check(domainApplyAssignment && domainApplyAssignment->isEnabled()
              && revertWorkspaceDraft && revertWorkspaceDraft->isEnabled()
              && domainWorkspaceDomains
              && domainWorkspaceDomains->rowCount() == 3
              && domainManagerInstances
              && domainManagerInstances->rowCount() == 3,
          QStringLiteral(
              "cancelling quick Manager Apply preserves both its assignment draft and the complete Workspace draft"));
    if (domainApplyAssignment) {
        chooseMessageBoxButton(QMessageBox::Discard);
        domainApplyAssignment->click();
        application.processEvents();
        application.processEvents();
    }

    const finepaper::DomainElementPresentation* assignedRouter0 = domainEditor
        ? domainEditor->domainPresentation().element(
              finepaper::ElementRef{
                  finepaper::ElementKind::Router, QStringLiteral("r-0-0")})
        : nullptr;
    const finepaper::DomainElementPresentation* assignedRouter1 = domainEditor
        ? domainEditor->domainPresentation().element(
              finepaper::ElementRef{
                  finepaper::ElementKind::Router, QStringLiteral("r-1-0")})
        : nullptr;
    zoneAAssignment = domainAssignmentItem(QStringLiteral("zone-a"));
    zoneBAssignment = domainAssignmentItem(QStringLiteral("zone-b"));
    check(domainWindow.isWindowModified()
              && assignedRouter0
              && assignedRouter0->domainIds
                  == QStringList{QStringLiteral("zone-a")}
              && assignedRouter1
              && assignedRouter1->domainIds
                  == QStringList{QStringLiteral("zone-a")}
              && domainAssignmentState
              && domainAssignmentState->property("assignmentState").toString()
                  == QStringLiteral("common")
              && zoneAAssignment && zoneAAssignment->checkState() == Qt::Checked
              && zoneBAssignment && zoneBAssignment->checkState() == Qt::Unchecked
              && revertWorkspaceDraft && !revertWorkspaceDraft->isEnabled()
              && domainWorkspaceDomains
              && domainWorkspaceDomains->rowCount() == 4,
          QStringLiteral(
              "one quick Apply atomically converges the selected Routers and discards the separately authorized complete Workspace draft only after success"));
    check(domainView && domainView->scene() == domainScene
              && domainScene && domainRouter && domainRouter1
              && domainScene->nodeGraphicsObject(*domainRouter)
                  == domainRouterGraphics
              && domainScene->nodeGraphicsObject(*domainRouter1)
                  == domainRouter1Graphics
              && domainScene->graphModel().allNodeIds().size()
                  == static_cast<size_t>(domainNodeCount)
              && sceneConnectionIds(domainScene).size()
                  == static_cast<size_t>(domainConnectionCount)
              && domainRouterGraphics->isSelected()
              && domainRouter1Graphics->isSelected()
              && domainLayerSelectionCallbacks == callbacksBeforeDomainApply
              && domainLinkGraphics
              && !domainLinkGraphics->data(
                  finepaper::domainCrossingDataRole).toBool(),
          QStringLiteral("Domain-only assignment refresh repaints the existing scene without rebuilding or disturbing selection"));

    const int updatedZoneARow = domainInstanceRow(QStringLiteral("zone-a"));
    const int updatedZoneBRow = domainInstanceRow(QStringLiteral("zone-b"));
    check(updatedZoneARow >= 0 && updatedZoneBRow >= 0
              && domainManagerInstances->item(updatedZoneARow, 3)->data(
                     finepaper::domainManagerMemberCountRole).toInt() == 2
              && domainManagerInstances->item(updatedZoneBRow, 3)->data(
                     finepaper::domainManagerMemberCountRole).toInt() == 0
              && domainManagerInstances->item(updatedZoneARow, 4)->data(
                     finepaper::domainManagerCrossingCountRole).toInt() == 0
              && domainManagerInstances->item(updatedZoneBRow, 4)->data(
                     finepaper::domainManagerCrossingCountRole).toInt() == 0,
          QStringLiteral("Legend membership and crossing counts refresh with the Domain-only mutation"));

    QAction* domainSaveAction = actionWithText(domainWindow, QStringLiteral("Save"));
    check(domainSaveAction && domainSaveAction->isEnabled(),
          QStringLiteral("a Domain assignment mutation enables Save"));
    if (domainSaveAction) {
        domainSaveAction->trigger();
        application.processEvents();
    }
    finepaper::FinepaperApplication persistedDomainApplication;
    persistedDomainApplication.reloadPackages(
        QStringList{domainPackageRoot.path()});
    const finepaper::DesignResult persistedDomainResult =
        persistedDomainApplication.loadDesignFile(domainDesignPath);
    const auto persistedAssignment = [&persistedDomainResult](const QString& routerId) {
        for (const finepaper::DomainMembership& membership
             : persistedDomainResult.design.domainMemberships) {
            if (membership.element.kind == finepaper::ElementKind::Router
                && membership.element.id == routerId) {
                return membership.assignments.value(
                    QStringLiteral("security-zone"));
            }
        }
        return QStringList{};
    };
    check(!domainWindow.isWindowModified()
              && persistedDomainResult.success
              && persistedAssignment(QStringLiteral("r-0-0"))
                  == QStringList{QStringLiteral("zone-a")}
              && persistedAssignment(QStringLiteral("r-1-0"))
                  == QStringList{QStringLiteral("zone-a")},
          QStringLiteral("saving clears dirty state and persists the atomic generic Domain assignment"));

    const auto stageCompleteDomainDraft = [&domainWindow]() {
        auto* workspace = domainWindow.findChild<QWidget*>(
            QStringLiteral("finepaper.domainConfigurationWorkspace"));
        auto* domains = workspace ? workspace->findChild<QTableWidget*>(
                                        QStringLiteral(
                                            "finepaper.domainConfiguration.domains"))
                                  : nullptr;
        auto* remove = workspace ? workspace->findChild<QPushButton*>(
                                       QStringLiteral(
                                           "finepaper.domainConfiguration.domains.delete"))
                                 : nullptr;
        int row = -1;
        if (domains) {
            for (int index = 0; index < domains->rowCount(); ++index) {
                QTableWidgetItem* id = domains->item(index, 2);
                if (id && id->text() == QStringLiteral("zone-c")) {
                    row = index;
                    break;
                }
            }
        }
        if (domains && remove && row >= 0) {
            domains->selectRow(row);
            remove->click();
            QApplication::processEvents();
        }
        auto* revert = workspace ? workspace->findChild<QPushButton*>(
                                       QStringLiteral(
                                           "finepaper.domainConfiguration.revert"))
                                 : nullptr;
        return row >= 0 && revert && revert->isEnabled();
    };
    const auto completeDomainDraftPending = [&domainWindow] {
        auto* revert = domainWindow.findChild<QPushButton*>(
            QStringLiteral("finepaper.domainConfiguration.revert"));
        return revert && revert->isEnabled();
    };

    check(stageCompleteDomainDraft(),
          QStringLiteral("a complete Domain draft can be staged before Validate"));
    chooseMessageBoxButton(QMessageBox::Discard);
    if (pendingDomainValidate) {
        pendingDomainValidate->trigger();
        application.processEvents();
    }
    const bool validateDiscardedAtStart = pendingDomainValidate
        && !completeDomainDraftPending();
    waitUntil([&domainWindow] { return !domainWindow.operationBusy(); });
    check(validateDiscardedAtStart,
          QStringLiteral(
              "Validate discards an authorized complete Domain draft only when the async operation actually starts"));

    check(stageCompleteDomainDraft(),
          QStringLiteral("a complete Domain draft can be staged before Generate"));
    chooseMessageBoxButton(QMessageBox::Discard);
    QTimer::singleShot(250, [] {
        chooseMessageBoxButton(QMessageBox::Ok);
    });
    if (pendingDomainGenerate) {
        pendingDomainGenerate->trigger();
        application.processEvents();
    }
    const bool generateDiscardedAtStart = pendingDomainGenerate
        && !completeDomainDraftPending();
    waitUntil([&domainWindow] { return !domainWindow.operationBusy(); });
    check(generateDiscardedAtStart,
          QStringLiteral(
              "Generate discards an authorized complete Domain draft only when the async operation actually starts"));

    check(stageCompleteDomainDraft(),
          QStringLiteral("a complete Domain draft can be staged before Mesh resize"));
    QAction* domainResizeAction = domainWindow.findChild<QAction*>(
        QStringLiteral("finepaper.resizeMeshAction"));
    respondToMeshResizeDialog(true, 1, 3);
    chooseMessageBoxButton(QMessageBox::Discard);
    if (domainResizeAction) {
        domainResizeAction->trigger();
        application.processEvents();
    }
    check(domainResizeAction && !completeDomainDraftPending()
              && domainWindow.isWindowModified(),
          QStringLiteral(
              "successful Mesh resize commits the previously authorized Domain-draft discard"));
    if (domainSaveAction) {
        domainSaveAction->trigger();
        application.processEvents();
    }

    check(stageCompleteDomainDraft(),
          QStringLiteral("a complete Domain draft can be staged before Open"));
    chooseMessageBoxButton(QMessageBox::Discard);
    const bool reopenedDomainDesign = domainWindow.openDesignFile(
        domainDesignPath);
    application.processEvents();
    check(reopenedDomainDesign && !completeDomainDraftPending()
              && !domainWindow.isWindowModified(),
          QStringLiteral(
              "successful Open commits the authorized Domain-draft discard and starts a new design session"));

    check(stageCompleteDomainDraft(),
          QStringLiteral("a complete Domain draft can be staged before New Design"));
    chooseMessageBoxButton(QMessageBox::Discard);
    createDesignThroughDialog(
        domainWindow,
        QStringLiteral("test.domain-presentation@1.0.0"),
        QStringLiteral("replacement_domain_design"),
        1,
        2);
    check(!completeDomainDraftPending() && domainWindow.isWindowModified(),
          QStringLiteral(
              "successful New Design commits the authorized Domain-draft discard and starts a new design session"));
    if (domainEditor) {
        domainEditor->semanticSelectionChanged = domainMainWindowSelectionHandler;
    }
    closeDiscarding(domainWindow);

    finepaper::FinepaperMainWindow restoredDomainWindow(domainLocations);
    restoredDomainWindow.show();
    application.processEvents();
    check(restoredDomainWindow.openDesignFile(domainDesignPath),
          QStringLiteral("the Domain design reopens in a fresh workbench"));
    application.processEvents();
    auto* restoredDomainSelector = restoredDomainWindow.findChild<QComboBox*>(
        finepaper::workbench::domainLayerSelectorName);
    check(restoredDomainSelector
              && restoredDomainSelector->currentData().toString()
                  == QStringLiteral("security-zone")
              && !restoredDomainWindow.isWindowModified(),
          QStringLiteral("the per-design Domain layer restores without dirtying the design"));
    closeDiscarding(restoredDomainWindow);

    QTemporaryDir missingPackageRoot(
        QStringLiteral("/tmp/finepaper-missing-package-XXXXXX"));
    check(missingPackageRoot.isValid(),
          QStringLiteral("temporary missing-Package workspace is available"));
    const QString missingDesignPath = QDir(missingPackageRoot.path()).filePath(
        QStringLiteral("missing-package.fpnoc"));
    check(writeMissingPackageDesign(missingDesignPath),
          QStringLiteral("design referencing a never-loaded Package is created"));
    finepaper::RuntimeLocations zeroPackageLocations{
        QStringList{missingPackageRoot.path()}, outputRoot.path()};
    finepaper::FinepaperMainWindow zeroPackageWindow(zeroPackageLocations);
    zeroPackageWindow.show();
    application.processEvents();
    auto* missingCreate = zeroPackageWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    QAction* missingNew = actionWithText(
        zeroPackageWindow, QStringLiteral("New NoC Design…"));
    check(missingCreate && !missingCreate->isEnabled()
              && missingNew && !missingNew->isEnabled(),
          QStringLiteral("zero-Package startup disables every New Design entry point consistently"));
    closeDiscarding(zeroPackageWindow);

    finepaper::RuntimeLocations missingLocations{
        QStringList{standardPackageRoot}, outputRoot.path()};
    finepaper::FinepaperMainWindow missingWindow(missingLocations);
    missingWindow.show();
    application.processEvents();
    check(missingWindow.openDesignFile(missingDesignPath),
          QStringLiteral("missing-Package design opens through the reusable file operation"));
    application.processEvents();
    application.processEvents();

    auto* missingActivePackage = missingWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackage"));
    auto* missingAvailablePackages = missingWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.availablePackages"));
    auto* missingPalette = missingWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointPalette"));
    auto* missingApply = missingWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
    QAction* missingValidate = actionWithText(
        missingWindow, QStringLiteral("Validate / DRC"));
    QAction* missingGenerate = actionWithText(
        missingWindow, QStringLiteral("Generate RTL"));
    QAction* missingSaveAs = actionWithText(
        missingWindow, QStringLiteral("Save As…"));
    auto* missingEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        missingWindow.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor")));
    auto* missingView = missingEditor
        ? missingEditor->findChild<QGraphicsView*>() : nullptr;
    auto* missingScene = missingView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(missingView->scene()) : nullptr;
    check(missingActivePackage
              && missingActivePackage->text().contains(
                  QStringLiteral("test.never-loaded@1.0.0"))
              && missingActivePackage->text().contains(
                  QStringLiteral("Package not loaded"))
              && missingAvailablePackages
              && missingAvailablePackages->text().startsWith(
                  QStringLiteral("1 NoC IP Package")),
          QStringLiteral("missing active Package is not replaced by an unrelated available IP"));
    check(missingPalette && missingPalette->count() == 0
              && !missingPalette->isEnabled(),
          QStringLiteral("never-loaded design Package clears and disables the Endpoint Palette"));
    check(missingApply && !missingApply->isEnabled()
              && missingValidate && !missingValidate->isEnabled()
              && missingGenerate && !missingGenerate->isEnabled(),
          QStringLiteral("never-loaded design Package disables persistent editing and run actions"));
    check(missingSaveAs && missingSaveAs->isEnabled(),
          QStringLiteral("missing-Package design can still be saved elsewhere"));
    check(missingEditor && missingEditor->isEnabled()
              && !missingEditor->editingEnabled(),
          QStringLiteral("missing-Package design keeps the canvas browsable while disabling mutations"));
    if (missingEditor) {
        missingEditor->setRouterCollapsed(QStringLiteral("r-0-0"), false);
    }
    const auto readOnlyEndpoint0 = nodeIdWithCaptionPrefix(
        missingScene, QStringLiteral("client_0"));
    const auto readOnlyEndpoint1 = nodeIdWithCaptionPrefix(
        missingScene, QStringLiteral("client_1"));
    const auto readOnlyRouter = nodeIdWithCaption(
        missingScene, QStringLiteral("r-0-0"));
    const auto readOnlyPort0 = readOnlyEndpoint0
        ? attachmentPortForEndpoint(missingScene, *readOnlyEndpoint0) : std::nullopt;
    const auto readOnlyPort1 = readOnlyEndpoint1
        ? attachmentPortForEndpoint(missingScene, *readOnlyEndpoint1) : std::nullopt;
    check(readOnlyEndpoint0 && readOnlyEndpoint1 && readOnlyRouter
              && readOnlyPort0 && readOnlyPort1 && *readOnlyPort0 != *readOnlyPort1,
          QStringLiteral("read-only projection preserves all persisted Endpoint attachments without Package metadata"));
    bool readOnlyDraftStarted = false;
    if (readOnlyEndpoint0 && readOnlyRouter) {
        readOnlyDraftStarted = dragPortConnection(
            missingView,
            missingScene,
            *readOnlyEndpoint0,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *readOnlyRouter,
            finepaper::portIndex(finepaper::RouterInputPort::Endpoint));
        application.processEvents();
    }
    check(!readOnlyDraftStarted && !hasDraftConnection(missingScene),
          QStringLiteral("read-only canvas rejects connection drafts while remaining interactive"));
    if (missingView && missingScene && readOnlyEndpoint0) {
        const QPoint endpointPosition = missingView->mapFromScene(
            missingScene->nodeGraphicsObject(*readOnlyEndpoint0)
                ->sceneBoundingRect().center());
        sendContextMenu(missingView, endpointPosition);
        application.processEvents();
    }
    check(missingEditor && !missingEditor->findChild<QMenu*>(
                               finepaper::workbench::endpointContextMenuName),
          QStringLiteral("read-only Endpoint context menus expose no mutating actions"));
    check(missingWindow.statusBar()->currentMessage().startsWith(
              QStringLiteral("Read-only design:")),
          QStringLiteral("missing-Package design reports a clear read-only status"));
    closeDiscarding(missingWindow);

    QTextStream(stdout) << (failures == 0 ? "finepaper-gui-smoke passed"
                                          : "finepaper-gui-smoke failed")
                        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
