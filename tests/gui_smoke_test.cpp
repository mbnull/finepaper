#include "gui/main_window.h"
#include "gui/animated_graphics_view.h"
#include "gui/noc_editor_style.h"
#include "gui/noc_node_editor.h"
#include "gui/workbench_config.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsView>
#include <QGraphicsPathItem>
#include <QGroupBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
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
#include <memory>

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
        for (QGraphicsItem* hitItem : view->items(viewportPosition)) {
            QGraphicsItem* item = hitItem;
            while (item) {
                if (item == connection) {
                    return viewportPosition;
                }
                item = item->parentItem();
            }
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
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(QStringLiteral("finepaper-gui-smoke"));

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

    auto* activePackage = window.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackage"));
    auto* availablePackages = window.findChild<QLabel*>(
        QStringLiteral("finepaper.availablePackages"));
    auto* endpointPalette = window.findChild<QListWidget*>(QStringLiteral("finepaper.endpointPalette"));
    check(availablePackages
              && availablePackages->text().startsWith(QStringLiteral("1 NoC IP Package")),
          QStringLiteral("runtime NoC IP availability is summarized in the workbench"));
    check(activePackage
              && activePackage->text().contains(QStringLiteral("No design is open")),
          QStringLiteral("the workbench does not imply an active IP before design creation"));
    check(endpointPalette && endpointPalette->count() == 0,
          QStringLiteral("Endpoint types are shown only for the active design Package"));
    QAction* initialSaveAction = actionWithText(window, QStringLiteral("Save"));
    QAction* saveAsAction = actionWithText(window, QStringLiteral("Save As…"));
    QAction* initialValidateAction = actionWithText(
        window, QStringLiteral("Validate / DRC"));
    QAction* initialGenerateAction = actionWithText(window, QStringLiteral("Generate RTL"));
    auto* applyParameters = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
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
    check(applyParameters && applyParameters->isEnabled(),
          QStringLiteral("Package parameters become editable with a design"));

    auto* editorWidget = window.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor"));
    auto* nodeEditor = dynamic_cast<finepaper::NocNodeEditor*>(editorWidget);
    auto* graphicsView = nodeEditor ? nodeEditor->findChild<QGraphicsView*>() : nullptr;
    auto* animatedView = dynamic_cast<finepaper::AnimatedGraphicsView*>(graphicsView);
    auto* graphicsScene = graphicsView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(graphicsView->scene())
        : nullptr;
    check(graphicsView && graphicsView->scene() && !graphicsView->scene()->items().isEmpty(),
          QStringLiteral("created Mesh is projected into the QtNodes editor"));
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
              && QLineF(*draggedRouterPosition, movedRouterPosition).length() < 0.5,
          QStringLiteral("dragging a Router changes its user-arranged workspace position"));
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
              && QLineF(*routerAfterTextInput, movedRouterPosition).length() < 0.5,
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
    bool endpointPortStartedDraft = false;
    if (pendingEndpoint && connectionRouter) {
        endpointPortStartedDraft = dragPortToNodeBody(
            graphicsView,
            graphicsScene,
            *pendingEndpoint,
            QtNodes::PortType::Out,
            finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
            *connectionRouter);
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
    if (bodyAttachedEndpoint) {
        dragNodeTo(graphicsView, graphicsScene,
                   *bodyAttachedEndpoint, exposedAttachmentTarget);
        application.processEvents();
        application.processEvents();
    }

    const auto bodyAttachmentConnection = bodyAttachedEndpoint
        ? attachmentConnectionForEndpoint(graphicsScene, *bodyAttachedEndpoint)
        : std::nullopt;
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
              && detachedBodyEndpoint,
          QStringLiteral("line-menu Disconnect removes the attachment and keeps the Endpoint draft"));

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
                  graphicsScene, QStringLiteral("r-0-1")),
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
    check(!nodeIdWithCaptionPrefix(
               graphicsScene, QStringLiteral("Unattached\nMaster endpoint")),
          QStringLiteral("an unattached Endpoint draft can still be deleted"));

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

    finepaper::RuntimeLocations explicitLocations{
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
    check(endpointBeforeRejectedDetach && routerBeforeRejectedDetach
              && explicitScene->graphModel().connectionExists({
                  *endpointBeforeRejectedDetach,
                  finepaper::portIndex(finepaper::EndpointOutputPort::Attachment),
                  *routerBeforeRejectedDetach,
                  finepaper::portIndex(finepaper::RouterInputPort::Endpoint)})
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
              && multiPackageWindow.findChild<QDoubleSpinBox*>(
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
              && !multiPackageWindow.findChild<QDoubleSpinBox*>(
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
    auto* numberEditor = numberWindow.findChild<QDoubleSpinBox*>(
        QStringLiteral("finepaper.parameter.frequencyScale"));
    check(numberEditor && qAbs(numberEditor->value() - 1.25) < 0.000001,
          QStringLiteral("number Package parameters use QDoubleSpinBox"));
    if (numberEditor) {
        numberEditor->setValue(2.75);
    }
    auto* numberApply = numberWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
    if (numberApply) {
        numberApply->click();
        application.processEvents();
    }
    numberEditor = numberWindow.findChild<QDoubleSpinBox*>(
        QStringLiteral("finepaper.parameter.frequencyScale"));
    check(numberEditor && qAbs(numberEditor->value() - 2.75) < 0.000001,
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
              && retainedApply && retainedApply->isEnabled()
              && retainedValidate && !retainedValidate->isEnabled()
              && retainedGenerate && !retainedGenerate->isEnabled()
              && retainedNew && !retainedNew->isEnabled()
              && numberCreate && !numberCreate->isEnabled(),
          QStringLiteral("failed reload retains editing metadata without claiming runtime availability"));
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
