#include "gui/main_window.h"
#include "features/topology/animated_graphics_view.h"
#include "features/domain/domain_manager_panel.h"
#include "gui/element_configuration_panel.h"
#include "gui/endpoint_configuration_panel.h"
#include "features/topology/noc_editor_style.h"
#include "features/topology/noc_node_editor.h"
#include "features/topology/topology_workspace_store.h"
#include "ui/common/schema_value_editor.h"
#include "ui/theme/ui_tokens.h"
#include "ui/theme/workbench_style.h"
#include "ui/workbench/workbench_config.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsView>
#include <QGraphicsPathItem>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
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
#include <QMetaObject>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QSize>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QUndoStack>
#include <QVariantAnimation>
#include <QWidget>

#include <QtTest/QTest>

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <optional>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_set>

namespace finepaper {

struct FinepaperMainWindowSmokeAccess final {
    static void presentRetainedFileCancellation(
        FinepaperMainWindow& window,
        const QVector<Diagnostic>& diagnostics,
        const QStringList& retainedRuntimePaths) {
        FinepaperMainWindow::OperationCompletion completion;
        window.presentOperationCancellation(
            completion,
            true,
            false,
            diagnostics,
            retainedRuntimePaths,
            QStringLiteral("Validation"),
            QStringLiteral(
                "Cancelled — previous validation results remain available."),
            QStringLiteral(
                "Validation cancelled; previous results were preserved."),
            {});
    }

    static bool processCleanupIsUnresolved(
        const FinepaperMainWindow& window) {
        return window.m_processCleanupUnresolved;
    }

    static void presentCleanupFailure(
        FinepaperMainWindow& window,
        const QString& operationName,
        const QVector<Diagnostic>& diagnostics,
        const QStringList& retainedRuntimePaths) {
        window.presentProcessCleanupFailure(
            operationName, diagnostics, retainedRuntimePaths, {});
    }

    static quint64 runtimePackageProbeGeneration(
        const FinepaperMainWindow& window) {
        return window.m_runtimePackageCache.probeGeneration();
    }

    static void refreshDesignProjection(FinepaperMainWindow& window) {
        window.refreshDesignViews();
    }

    static bool endpointCanvasDraftOperationAllowed(
        FinepaperMainWindow& window,
        const QString& operation) {
        return window.ensureEndpointCanvasDraftsResolved(operation);
    }
};

} // namespace finepaper

namespace {

int failures = 0;
constexpr int kMaximumSmokeDimension = 4096;

void check(bool condition, const QString& message) {
    if (!condition) {
        QTextStream(stderr) << "FAILED: " << message << Qt::endl;
        ++failures;
    }
}

QSize requestedSmokeSize() {
    QString text = qEnvironmentVariable("FINEPAPER_GUI_SMOKE_SIZE")
                       .trimmed()
                       .toLower();
    if (text.isEmpty()) {
        return {};
    }
    const QStringList parts = text.split(QLatin1Char('x'));
    if (parts.size() != 2) {
        check(false, QStringLiteral(
            "FINEPAPER_GUI_SMOKE_SIZE must use WIDTHxHEIGHT."));
        return {};
    }
    bool widthOk = false;
    bool heightOk = false;
    const int width = parts.at(0).toInt(&widthOk);
    const int height = parts.at(1).toInt(&heightOk);
    const bool valid = widthOk && heightOk
        && width >= 640 && height >= 480
        && width <= kMaximumSmokeDimension
        && height <= kMaximumSmokeDimension;
    check(valid,
          QStringLiteral(
              "FINEPAPER_GUI_SMOKE_SIZE must be between 640x480 and %1x%1.")
              .arg(kMaximumSmokeDimension));
    return valid ? QSize(width, height) : QSize{};
}

double requestedFontScale() {
    const QString text = qEnvironmentVariable(
        "FINEPAPER_GUI_SMOKE_FONT_SCALE").trimmed();
    if (text.isEmpty()) {
        return 1.0;
    }
    bool ok = false;
    const double scale = text.toDouble(&ok);
    const bool valid = ok && scale >= 1.0 && scale <= 2.0;
    check(valid,
          QStringLiteral(
              "FINEPAPER_GUI_SMOKE_FONT_SCALE must be between 1.0 and 2.0."));
    return valid ? scale : 1.0;
}

void applyRequestedSmokePalette(QApplication& application,
                                const QString& theme) {
    if (theme.isEmpty()) {
        return;
    }
    if (theme != QStringLiteral("light")
        && theme != QStringLiteral("dark")) {
        check(false, QStringLiteral(
            "FINEPAPER_GUI_SMOKE_THEME must be light or dark."));
        return;
    }

    const bool dark = theme == QStringLiteral("dark");
    const QColor window = dark ? QColor(QStringLiteral("#202124"))
                               : QColor(QStringLiteral("#f5f6f8"));
    const QColor base = dark ? QColor(QStringLiteral("#17181a"))
                             : QColor(QStringLiteral("#ffffff"));
    const QColor raised = dark ? QColor(QStringLiteral("#292b2f"))
                               : QColor(QStringLiteral("#ffffff"));
    const QColor text = dark ? QColor(QStringLiteral("#f1f3f4"))
                             : QColor(QStringLiteral("#202124"));
    const QColor muted = dark ? QColor(QStringLiteral("#aeb4bc"))
                              : QColor(QStringLiteral("#5f6368"));
    const QColor accent = dark ? QColor(QStringLiteral("#8ab4f8"))
                               : QColor(QStringLiteral("#1967d2"));

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, raised);
    palette.setColor(QPalette::ToolTipBase, raised);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText,
                     dark ? QColor(QStringLiteral("#101216")) : Qt::white);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::LinkVisited, accent);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    palette.setColor(QPalette::Disabled, QPalette::Text, muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    application.setPalette(palette);
}

QString smokeScaleToken(double scale) {
    QString token = QString::number(scale, 'f', 2);
    token.replace(QLatin1Char('.'), QLatin1Char('p'));
    return token;
}

QString smokeVariantName(const QString& requestedTheme,
                         const QSize& windowSize,
                         double fontScale,
                         double devicePixelRatio) {
    const QString theme = requestedTheme == QStringLiteral("light")
            || requestedTheme == QStringLiteral("dark")
        ? requestedTheme : QStringLiteral("system");
    return QStringLiteral("%1_%2x%3_font%4_dpr%5")
        .arg(theme)
        .arg(windowSize.width())
        .arg(windowSize.height())
        .arg(smokeScaleToken(fontScale),
             smokeScaleToken(devicePixelRatio));
}

void captureSmokeScreenshot(QWidget& window,
                            const QString& phase,
                            const QString& requestedTheme) {
    const QString screenshotRoot =
        qEnvironmentVariable("FINEPAPER_GUI_SMOKE_SCREENSHOT_DIR").trimmed();
    if (screenshotRoot.isEmpty()) {
        return;
    }
    if (!QDir(screenshotRoot).exists()) {
        check(false, QStringLiteral(
            "FINEPAPER_GUI_SMOKE_SCREENSHOT_DIR does not exist: %1")
            .arg(screenshotRoot));
        return;
    }
    if (phase != QStringLiteral("no-design")
        && phase != QStringLiteral("endpoint-draft")) {
        auto* editor = dynamic_cast<finepaper::NocNodeEditor*>(
            window.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor")));
        if (editor) {
            editor->zoomToFit();
        }
    }
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    const QString fileName = QStringLiteral("finepaper_%1_%2.png")
        .arg(phase,
             smokeVariantName(
                 requestedTheme,
                 window.size(),
                 requestedFontScale(),
                 window.devicePixelRatioF()));
    const QString path = QDir(screenshotRoot).filePath(fileName);
    check(window.grab().save(path, "PNG"),
          QStringLiteral("%1 screenshot is saved to %2").arg(phase, path));
}

bool focusIsWithin(QWidget* target) {
    QWidget* focus = QApplication::focusWidget();
    QWidget* targetWindow = target ? target->window() : nullptr;
    if (targetWindow
        && (!focus || focus->window() != targetWindow)) {
        focus = targetWindow->focusWidget();
    }
    return target && focus
        && (focus == target || target->isAncestorOf(focus));
}

bool widgetIsFullyVisibleWithin(QWidget* ancestor, QWidget* widget) {
    if (!ancestor || !widget || !widget->isVisibleTo(ancestor)) {
        return false;
    }
    const QRect widgetRect(
        widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
    return ancestor->rect().contains(widgetRect);
}

bool widgetIntersectsScrollViewport(QScrollArea* scroll, QWidget* widget) {
    if (!scroll || !widget || !widget->isVisibleTo(scroll)) {
        return false;
    }
    const QRect widgetRect(
        widget->mapTo(scroll->viewport(), QPoint(0, 0)), widget->size());
    return scroll->viewport()->rect().intersects(widgetRect);
}

bool widgetIsFullyVisibleInScrollViewport(
    QScrollArea* scroll, QWidget* widget) {
    if (!scroll || !widget || !widget->isVisibleTo(scroll)) {
        return false;
    }
    const QRect widgetRect(
        widget->mapTo(scroll->viewport(), QPoint(0, 0)), widget->size());
    return scroll->viewport()->rect().contains(widgetRect);
}

bool visibleSiblingLabelsDoNotOverlap(QWidget* root,
                                      QString* collision = nullptr) {
    if (!root) {
        return false;
    }
    const QList<QLabel*> labels = root->findChildren<QLabel*>();
    for (qsizetype firstIndex = 0; firstIndex < labels.size(); ++firstIndex) {
        QLabel* first = labels.at(firstIndex);
        if (!first->isVisibleTo(root) || first->text().trimmed().isEmpty()) {
            continue;
        }
        for (qsizetype secondIndex = firstIndex + 1;
             secondIndex < labels.size(); ++secondIndex) {
            QLabel* second = labels.at(secondIndex);
            if (first->parentWidget() != second->parentWidget()
                || !second->isVisibleTo(root)
                || second->text().trimmed().isEmpty()) {
                continue;
            }
            const QRect overlap = first->geometry().intersected(second->geometry());
            if (overlap.width() > 1 && overlap.height() > 1) {
                if (collision) {
                    *collision = QStringLiteral("\"%1\" overlaps \"%2\"")
                        .arg(first->text().left(40), second->text().left(40));
                }
                return false;
            }
        }
    }
    return true;
}

bool waitUntil(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout = std::chrono::seconds(20)) {
    const QDeadlineTimer deadline(timeout);
    while (!predicate() && !deadline.hasExpired()) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(1);
    }
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

bool activateWindowForKeyboard(QWidget& window) {
    window.raise();
    window.activateWindow();
    return waitUntil(
        [&window] { return window.isActiveWindow(); },
        std::chrono::seconds(2));
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

QtNodes::NodeGraphicsObject* nodeGraphicsWithCaptionPrefix(
    QtNodes::BasicGraphicsScene* scene,
    const QString& prefix) {
    if (!scene) {
        return nullptr;
    }
    for (QGraphicsItem* item : scene->items()) {
        auto* node = qgraphicsitem_cast<QtNodes::NodeGraphicsObject*>(item);
        if (node && node->graphModel().nodeData(
                        node->nodeId(), QtNodes::NodeRole::Caption)
                        .toString()
                        .startsWith(prefix)) {
            return node;
        }
    }
    return nullptr;
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
            if (view->items(QPoint(x, y)).isEmpty()) {
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
            if (!messageBox || !messageBox->isVisible()) {
                continue;
            }
            if (QAbstractButton* target = messageBox->button(button)) {
                target->click();
                return;
            }
        }
        if (attempts > 1) {
            QTimer::singleShot(
                10, [button, attempts] { chooseMessageBoxButton(button, attempts - 1); });
        }
    });
}

void chooseVisibleButtonByObjectName(
    const QString& objectName,
    int attempts = 100) {
    QTimer::singleShot(0, [objectName, attempts] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (!widget || !widget->isVisible()) {
                continue;
            }
            if (auto* target = widget->findChild<QPushButton*>(objectName)) {
                target->click();
                return;
            }
        }
        if (attempts > 1) {
            QTimer::singleShot(
                10,
                [objectName, attempts] {
                    chooseVisibleButtonByObjectName(
                        objectName, attempts - 1);
                });
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
    std::optional<int> columns = std::nullopt,
    int attempts = 100) {
    QTimer::singleShot(0, [accept, rows, columns, attempts] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog
                || !dialog->isVisible()
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
                    if (button->isEnabled()) {
                        button->click();
                        return;
                    }
                }
            }
        }
        if (attempts > 1) {
            QTimer::singleShot(10, [accept, rows, columns, attempts] {
                respondToMeshResizeDialog(
                    accept, rows, columns, attempts - 1);
            });
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

bool createSlowOperationPackage(const QString& sourceRoot,
                                const QString& destinationRoot) {
    QFile sourceManifest(
        QDir(sourceRoot).filePath(QStringLiteral("package.json")));
    if (!sourceManifest.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        sourceManifest.readAll(), &error);
    if (error.error != QJsonParseError::NoError
        || !document.isObject()) {
        return false;
    }

    QJsonObject package = document.object();
    package.insert(
        QStringLiteral("id"), QStringLiteral("test.slow-operation"));
    package.insert(
        QStringLiteral("name"), QStringLiteral("Slow Operation Fixture"));
    QJsonObject generator = package.value(
        QStringLiteral("generator")).toObject();
    generator.insert(
        QStringLiteral("name"), QStringLiteral("slow-operation-fixture"));
    generator.insert(QStringLiteral("timeoutSeconds"), 30);
    package.insert(QStringLiteral("generator"), generator);

    const QString runtimeDirectory = QDir(destinationRoot).filePath(
        QStringLiteral("runtime/bin"));
    if (!QDir().mkpath(runtimeDirectory)) {
        return false;
    }
    QFile destinationManifest(
        QDir(destinationRoot).filePath(QStringLiteral("package.json")));
    const QByteArray manifestPayload = QJsonDocument(package).toJson(
        QJsonDocument::Indented);
    if (!destinationManifest.open(
            QIODevice::WriteOnly | QIODevice::Truncate)
        || destinationManifest.write(manifestPayload)
               != manifestPayload.size()) {
        return false;
    }
    destinationManifest.close();

    QFile executable(
        QDir(runtimeDirectory).filePath(QStringLiteral("generate")));
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "trap '' TERM\n"
        "while :; do\n"
        "  sleep 1\n"
        "done\n");
    if (!executable.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || executable.write(script) != script.size()) {
        return false;
    }
    executable.close();
    return QFile::setPermissions(
        executable.fileName(),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner | QFileDevice::ReadGroup
            | QFileDevice::ExeGroup | QFileDevice::ReadOther
            | QFileDevice::ExeOther);
}

bool writeSlowOperationDesign(const QString& path) {
    const QJsonObject design{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-design")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("slow_operation_design")},
        {QStringLiteral("name"), QStringLiteral("Slow Operation Design")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test.slow-operation")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("parameters"), QJsonObject{
            {QStringLiteral("dataWidth"), 64},
            {QStringLiteral("flitWidth"), 128},
            {QStringLiteral("addrWidth"), 32}
        }},
        {QStringLiteral("endpoints"), QJsonArray{}}
    };
    const QByteArray payload = QJsonDocument(design).toJson(
        QJsonDocument::Indented);
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(payload) == payload.size();
}

bool makeSlowOperationPackageCompleteNaturally(
    const QString& packageRoot) {
    QFile executable(QDir(packageRoot).filePath(
        QStringLiteral("runtime/bin/generate")));
    const QFileDevice::Permissions permissions = QFile::permissions(
        executable.fileName());
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "shift\n"
        "result=''\n"
        "while [ \"$#\" -gt 0 ]; do\n"
        "  case \"$1\" in\n"
        "    --result) result=\"$2\"; shift 2 ;;\n"
        "    *) shift ;;\n"
        "  esac\n"
        "done\n"
        "sleep 0.1\n"
        "printf '%s\\n' '{\"success\":true,\"diagnostics\":[]}' > \"$result\"\n");
    if (!executable.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || executable.write(script) != script.size()) {
        return false;
    }
    executable.close();
    return QFile::setPermissions(executable.fileName(), permissions);
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

bool writeEndpointDraftLifecycleDesign(const QString& path) {
    const QJsonObject design{
        {QStringLiteral("format"), QStringLiteral("finepaper.noc-design")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("endpoint_draft_lifecycle")},
        {QStringLiteral("name"), QStringLiteral("Endpoint Draft Lifecycle")},
        {QStringLiteral("package"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("finepaper.noc")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")}
        }},
        {QStringLiteral("topology"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mesh")},
            {QStringLiteral("rows"), 2},
            {QStringLiteral("columns"), 2}
        }},
        {QStringLiteral("parameters"), QJsonObject{
            {QStringLiteral("dataWidth"), 64},
            {QStringLiteral("flitWidth"), 128},
            {QStringLiteral("addrWidth"), 32}
        }},
        {QStringLiteral("endpoints"), QJsonArray{}}
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
    const bool maximizeWindow =
        qEnvironmentVariableIsSet("FINEPAPER_GUI_SMOKE_MAXIMIZED");
    const QSize requestedWindowSize = requestedSmokeSize();
    const double fontScale = requestedFontScale();
    const QString requestedTheme =
        qEnvironmentVariable("FINEPAPER_GUI_SMOKE_THEME")
            .trimmed()
            .toLower();
    const QString requestedScope =
        qEnvironmentVariable("FINEPAPER_GUI_SMOKE_SCOPE")
            .trimmed()
            .toLower();
    check(requestedScope.isEmpty()
              || requestedScope == QStringLiteral("full")
              || requestedScope == QStringLiteral("workbench"),
          QStringLiteral(
              "FINEPAPER_GUI_SMOKE_SCOPE must be full or workbench."));
    QTemporaryDir configRoot(QStringLiteral("/tmp/finepaper-gui-config-XXXXXX"));
    QTemporaryDir outputRoot(QStringLiteral("/tmp/finepaper-gui-output-XXXXXX"));
    check(configRoot.isValid(), QStringLiteral("temporary GUI settings root is available"));
    check(outputRoot.isValid(), QStringLiteral("temporary GUI output root is available"));
    qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());

    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    if (fontScale > 1.0) {
        QFont scaledFont = application.font();
        if (scaledFont.pointSizeF() > 0.0) {
            scaledFont.setPointSizeF(
                scaledFont.pointSizeF() * fontScale);
        } else if (scaledFont.pixelSize() > 0) {
            scaledFont.setPixelSize(qRound(
                static_cast<double>(scaledFont.pixelSize()) * fontScale));
        }
        application.setFont(scaledFont);
    }
    EndpointCreationAutoAccepter endpointCreationAutoAccepter;
    application.installEventFilter(&endpointCreationAutoAccepter);
    QCoreApplication::setOrganizationName(QStringLiteral("FinepaperTest"));
    QCoreApplication::setApplicationName(QStringLiteral("finepaper-gui-smoke"));
    applyRequestedSmokePalette(application, requestedTheme);
    finepaper::ui::applyWorkbenchStyle(application);
    const QString requiredPlatform =
        qEnvironmentVariable("FINEPAPER_GUI_SMOKE_REQUIRE_PLATFORM")
            .trimmed();
    check(requiredPlatform.isEmpty()
              || QGuiApplication::platformName() == requiredPlatform,
          QStringLiteral("GUI smoke uses the requested native platform plugin"));

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
    if (requestedWindowSize.isValid()) {
        window.resize(requestedWindowSize);
    }
    if (maximizeWindow) {
        window.showMaximized();
    } else {
        window.show();
    }
    application.processEvents();
    const auto restoreRequestedClientSize = [&] {
        if (!maximizeWindow && requestedWindowSize.isValid()
            && window.size() != requestedWindowSize) {
            window.resize(requestedWindowSize);
            application.processEvents();
        }
    };
    // The offscreen platform can polish a top-level window to its initial
    // sizeHint on first show. Reapply the requested client size after polish,
    // matching a compositor configure or a user resize.
    restoreRequestedClientSize();
    check(!maximizeWindow || window.isMaximized(),
          QStringLiteral(
              "the native-compositor regression run enters maximized state"));
    check(maximizeWindow || !requestedWindowSize.isValid()
              || window.size() == requestedWindowSize,
          QStringLiteral("requested smoke-test window size is honored (%1x%2)")
              .arg(window.width())
              .arg(window.height()));
    if (requestedTheme == QStringLiteral("light")
        || requestedTheme == QStringLiteral("dark")) {
        const int surfaceLightness = application.palette()
                                         .color(QPalette::Window)
                                         .lightness();
        const int textLightness = application.palette()
                                      .color(QPalette::WindowText)
                                      .lightness();
        check(!application.styleSheet().isEmpty()
                  && ((requestedTheme == QStringLiteral("dark")
                       && surfaceLightness < textLightness)
                      || (requestedTheme == QStringLiteral("light")
                          && surfaceLightness > textLightness)),
              QStringLiteral(
                  "requested %1 palette is applied before the workbench stylesheet")
                  .arg(requestedTheme));
    }

    auto* centerViews = qobject_cast<QTabWidget*>(window.centralWidget());
    check(centerViews && centerViews->count() == 5,
          QStringLiteral("central workbench exposes five switchable views"));
    if (centerViews) {
        check(centerViews->objectName()
                      == QStringLiteral("finepaper.centerViews")
                  && !centerViews->accessibleName().trimmed().isEmpty(),
              QStringLiteral(
                  "central workbench tabs expose a stable automation id and accessible name"));
        check(centerViews->tabText(0) == QStringLiteral("Editor"),
              QStringLiteral("Editor is the concise default central view"));
        check(centerViews->tabText(1) == QStringLiteral("Domains")
                  && centerViews->widget(1)->objectName()
                      == QStringLiteral(
                          "finepaper.domainConfigurationWorkspace"),
              QStringLiteral(
                  "complete Domain configuration is a first-class persistent central Workspace"));
        check(centerViews->tabText(2) == QStringLiteral("Extensions")
                  && centerViews->widget(2)->objectName()
                      == QStringLiteral("finepaper.designExtensionsWorkspace"),
              QStringLiteral(
                  "Package-driven Design Extensions are a first-class central Workspace"));
        check(centerViews->tabText(3) == QStringLiteral("Performance"),
              QStringLiteral("performance analysis is a central view"));
        check(centerViews->tabText(4) == QStringLiteral("Problems"),
              QStringLiteral("problem report is a central view"));
        const QStringList completeTitles = {
            QStringLiteral("NoC Editor"),
            QStringLiteral("Domain Configuration"),
            QStringLiteral("Design Extensions"),
            QStringLiteral("Performance Analysis"),
            QStringLiteral("Problem Report")};
        bool completeTabToolTips =
            centerViews->count() == completeTitles.size();
        if (completeTabToolTips) {
            for (int index = 0; index < centerViews->count(); ++index) {
                completeTabToolTips = completeTabToolTips
                    && centerViews->tabToolTip(index)
                        == completeTitles.at(index);
            }
        }
        check(completeTabToolTips,
              QStringLiteral(
                  "concise central tabs expose their complete title as a tooltip"));
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
    auto* domainDock = window.findChild<QDockWidget*>(
        finepaper::workbench::domainManagerDockName);
    auto* resultsDock = window.findChild<QDockWidget*>(finepaper::workbench::resultsDockName);
    auto* inspectorScroll = window.findChild<QScrollArea*>(
        QStringLiteral("finepaper.inspectorScroll"));
    auto* endpointConfigurationGroup = window.findChild<QWidget*>(
        QStringLiteral("finepaper.endpointConfigurationGroup"));
    auto* elementConfigurationGroup = window.findChild<QWidget*>(
        QStringLiteral("finepaper.elementConfigurationGroup"));
    check(packageDock && window.dockWidgetArea(packageDock) == Qt::LeftDockWidgetArea,
          QStringLiteral("Package and Endpoint library is docked on the left"));
    check(inspectorDock && window.dockWidgetArea(inspectorDock) == Qt::RightDockWidgetArea,
          QStringLiteral("Inspector is docked on the right"));
    check(resultsDock && window.dockWidgetArea(resultsDock) == Qt::BottomDockWidgetArea,
          QStringLiteral("diagnostics and outputs are docked at the bottom"));
    check(resultsDock
              && resultsDock->windowTitle()
                  == QStringLiteral("Diagnostics and Output")
              && resultsDock->accessibleName()
                  == QStringLiteral("Diagnostics and Output"),
          QStringLiteral(
              "the Results dock renders its complete text title and exposes a spoken name"));
    const auto hasTextDockControls = [](QDockWidget* dock) {
        if (!dock || !dock->titleBarWidget()) {
            return false;
        }
        const auto* floatButton = dock->titleBarWidget()
            ->findChild<QToolButton*>(
                QStringLiteral("finepaper.dockTitleBar.floatButton"));
        const auto* closeButton = dock->titleBarWidget()
            ->findChild<QToolButton*>(
                QStringLiteral("finepaper.dockTitleBar.closeButton"));
        return floatButton
            && (floatButton->text() == QStringLiteral("Float")
                || floatButton->text() == QStringLiteral("Dock"))
            && !floatButton->accessibleName().isEmpty()
            && closeButton
            && closeButton->text() == QStringLiteral("Close")
            && !closeButton->accessibleName().isEmpty();
    };
    const QList<QDockWidget*> workbenchDocks = {
        packageDock, inspectorDock, domainDock, resultsDock};
    check(std::all_of(
              workbenchDocks.cbegin(), workbenchDocks.cend(),
              hasTextDockControls),
          QStringLiteral(
              "all workbench Docks expose readable Float and Close text controls"));
    check(inspectorDock && inspectorScroll
              && inspectorDock->widget() == inspectorScroll
              && inspectorScroll->widgetResizable()
              && inspectorScroll->horizontalScrollBarPolicy()
                     == Qt::ScrollBarAlwaysOff,
          QStringLiteral(
              "the complete Inspector scrolls inside its dock instead of resizing the top-level window"));
    check(inspectorScroll && inspectorScroll->widget()
              && inspectorScroll->widget()
                     ->findChildren<QScrollArea*>()
                     .isEmpty(),
          QStringLiteral(
              "the Inspector has one vertical scroll owner and no nested scroll areas"));

    auto* mainToolbar = window.findChild<QToolBar*>(
        finepaper::workbench::mainToolbarName);
    auto* workspaceToolbar = window.findChild<QToolBar*>(
        finepaper::workbench::workspaceToolbarName);
    auto* workspaceSelector = window.findChild<QComboBox*>(
        finepaper::workbench::workspaceSelectorName);
    auto* canvasControlsButton = window.findChild<QToolButton*>(
        finepaper::workbench::canvasControlsButtonName);
    auto* wideCanvasControls = window.findChild<QWidget*>(
        QStringLiteral("finepaper.wideCanvasControls"));
    auto* canvasModeControl = window.findChild<QWidget*>(
        QStringLiteral("finepaper.canvasModeControl"));
    auto* runControls = window.findChild<QWidget*>(
        QStringLiteral("finepaper.runControls"));
    auto* panelNavigationButton = window.findChild<QToolButton*>(
        finepaper::workbench::panelNavigationButtonName);
    QAction* packagePanelAction = window.findChild<QAction*>(
        finepaper::workbench::packageToggleActionName);
    QAction* inspectorPanelAction = window.findChild<QAction*>(
        finepaper::workbench::inspectorToggleActionName);
    QAction* resultsPanelAction = window.findChild<QAction*>(
        finepaper::workbench::resultsToggleActionName);
    QAction* canvasFocusAction = window.findChild<QAction*>(
        finepaper::workbench::canvasFocusActionName);
    QAction* fitCanvasAction = window.findChild<QAction*>(
        QStringLiteral("finepaper.fitCanvasAction"));
    QAction* regularizeCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::regularizeActionName);
    const bool toolbarActionsUseText = mainToolbar
        && std::all_of(
            mainToolbar->actions().cbegin(), mainToolbar->actions().cend(),
            [](const QAction* action) {
                return action->isSeparator() || action->icon().isNull();
            });
    check(mainToolbar
              && mainToolbar->toolButtonStyle() == Qt::ToolButtonTextOnly
              && toolbarActionsUseText
              && panelNavigationButton
              && panelNavigationButton->text() == QStringLiteral("Panels")
              && panelNavigationButton->menu(),
          QStringLiteral(
              "the main toolbar and Panels menu use visible text instead of icon-only navigation"));
    const QString responsiveMode = window.property(
        finepaper::workbench::workbenchWidthModeProperty).toString();
    const bool compactPresentation = responsiveMode != QStringLiteral("wide");
    const bool workspaceTitlesComplete = workspaceSelector
        && workspaceSelector->count() == 5
        && workspaceSelector->itemText(0)
            == QStringLiteral("NoC Editor")
        && workspaceSelector->itemText(1)
            == QStringLiteral("Domain Configuration")
        && workspaceSelector->itemText(2)
            == QStringLiteral("Design Extensions")
        && workspaceSelector->itemText(3)
            == QStringLiteral("Performance Analysis")
        && workspaceSelector->itemText(4)
            == QStringLiteral("Problem Report");
    const QList<QAction*> mainToolbarActions = mainToolbar
        ? mainToolbar->actions() : QList<QAction*>{};
    const bool visibleToolbarWidgetsFit = mainToolbar
        && std::all_of(
            mainToolbarActions.cbegin(),
            mainToolbarActions.cend(),
            [mainToolbar](QAction* action) {
                if (!action || !action->isVisible()
                    || action->isSeparator()) {
                    return true;
                }
                QWidget* widget = mainToolbar->widgetForAction(action);
                if (!widget || !widget->isVisibleTo(mainToolbar)) {
                    return false;
                }
                const QRect widgetRect(
                    widget->mapTo(mainToolbar, QPoint(0, 0)),
                    widget->size());
                return mainToolbar->contentsRect().contains(widgetRect);
            });
    const bool compactCanvasMenuComplete = canvasControlsButton
        && canvasControlsButton->menu()
        && canvasControlsButton->menu()->actions().contains(
            window.findChild<QAction*>(
                finepaper::workbench::selectCanvasActionName))
        && canvasControlsButton->menu()->actions().contains(
            window.findChild<QAction*>(
                finepaper::workbench::panCanvasActionName))
        && canvasControlsButton->menu()->actions().contains(canvasFocusAction);
    check(workspaceToolbar && workspaceSelector && workspaceTitlesComplete
              && centerViews
              && centerViews->tabBar()->isVisible() == !compactPresentation
              && workspaceToolbar->isVisible() == compactPresentation
              && runControls && !runControls->isVisible()
              && canvasControlsButton
              && !canvasControlsButton->isVisible()
              && wideCanvasControls
              && !wideCanvasControls->isVisible()
              && canvasModeControl && !canvasModeControl->isVisible()
              && compactCanvasMenuComplete
              && visibleToolbarWidgetsFit,
          QStringLiteral(
              "responsive chrome keeps complete text navigation while hiding "
              "irrelevant design commands (mode=%1, tabs=%2, workspace=%3, "
              "canvas=%4, widgetsFit=%5)")
              .arg(responsiveMode)
              .arg(centerViews && centerViews->tabBar()->isVisible())
              .arg(workspaceToolbar && workspaceToolbar->isVisible())
              .arg(canvasControlsButton && canvasControlsButton->isVisible())
              .arg(visibleToolbarWidgetsFit));
    QAction* packageNavigation = window.findChild<QAction*>(
        finepaper::workbench::packageNavigationActionName);
    QAction* inspectorNavigation = window.findChild<QAction*>(
        finepaper::workbench::inspectorNavigationActionName);
    QAction* domainNavigation = window.findChild<QAction*>(
        finepaper::workbench::domainNavigationActionName);
    QAction* resultsNavigation = window.findChild<QAction*>(
        finepaper::workbench::resultsNavigationActionName);
    check(packagePanelAction && packagePanelAction->shortcut().isEmpty()
              && packageNavigation && !packageNavigation->isCheckable()
              && packageNavigation->shortcut()
                  == QKeySequence(QStringLiteral("Ctrl+B")),
          QStringLiteral(
              "Ctrl+B is a dedicated Package navigation command, not a responsive preference toggle"));
    check(inspectorPanelAction && inspectorPanelAction->shortcut().isEmpty()
              && inspectorNavigation && !inspectorNavigation->isCheckable()
              && inspectorNavigation->shortcut()
                  == QKeySequence(QStringLiteral("Ctrl+Shift+B")),
          QStringLiteral(
              "Ctrl+Shift+B is a dedicated Inspector navigation command"));
    check(domainNavigation && !domainNavigation->isCheckable()
              && domainNavigation->shortcut()
                  == QKeySequence(QStringLiteral("Ctrl+Shift+D")),
          QStringLiteral(
              "Ctrl+Shift+D is a dedicated Domain navigation command"));
    check(resultsPanelAction && resultsPanelAction->shortcut().isEmpty()
              && resultsNavigation && !resultsNavigation->isCheckable()
              && resultsNavigation->shortcut()
                  == QKeySequence(QStringLiteral("Ctrl+J")),
          QStringLiteral(
              "Ctrl+J is a dedicated Results navigation command"));
    check(canvasFocusAction
              && canvasFocusAction->isCheckable()
              && canvasFocusAction->shortcut()
                  == QKeySequence(QStringLiteral("Ctrl+Shift+F"))
              && canvasFocusAction->text() == QStringLiteral("Focus Canvas")
              && !canvasFocusAction->isEnabled()
              && !canvasFocusAction->isVisible(),
          QStringLiteral(
              "Canvas Focus is explicit but suppressed until a canvas context exists"));

    if (packagePanelAction && packageDock) {
        packageDock->show();
        application.processEvents();
        packagePanelAction->trigger();
        application.processEvents();
        check(!packageDock->isVisible() && !packagePanelAction->isChecked(),
              QStringLiteral("View menu toggle hides the left Package panel"));
        if (centerViews) {
            centerViews->setFocus(Qt::OtherFocusReason);
        }
        application.processEvents();
        const QPointer<QWidget> focusBeforePackageRestore =
            QApplication::focusWidget();
        const bool focusStartedOutsidePackage =
            !focusBeforePackageRestore
            || (focusBeforePackageRestore != packageDock
                && !packageDock->isAncestorOf(
                    focusBeforePackageRestore));
        packagePanelAction->trigger();
        application.processEvents();
        const bool packageExposureMatchesResponsiveMode =
            responsiveMode != QStringLiteral("wide")
            || packageDock->isVisible();
        QWidget* focusAfterPackageRestore = QApplication::focusWidget();
        const bool focusRemainedOutsidePackage =
            !focusAfterPackageRestore
            || (focusAfterPackageRestore != packageDock
                && !packageDock->isAncestorOf(
                    focusAfterPackageRestore));
        check(packageExposureMatchesResponsiveMode
                  && packagePanelAction->isChecked()
                  && focusStartedOutsidePackage
                  && focusRemainedOutsidePackage,
              QStringLiteral(
                  "View restores the Package preference without stealing workspace focus; constrained layouts may defer exposure "
                  "(mode=%1, visible=%2, preferred=%3, focus before=%4, after=%5)")
                  .arg(responsiveMode)
                  .arg(packageDock->isVisible())
                  .arg(packagePanelAction->isChecked())
                  .arg(focusBeforePackageRestore
                           ? focusBeforePackageRestore->objectName()
                           : QStringLiteral("none"))
                  .arg(focusAfterPackageRestore
                           ? focusAfterPackageRestore->objectName()
                           : QStringLiteral("none")));
    }
    if (inspectorPanelAction && inspectorDock) {
        inspectorDock->show();
        inspectorDock->raise();
        application.processEvents();
        inspectorPanelAction->trigger();
        application.processEvents();
        check(!inspectorDock->isVisible(),
              QStringLiteral("View menu toggle hides the right Inspector panel"));
        inspectorPanelAction->trigger();
        application.processEvents();
        check(inspectorDock->isVisible(),
              QStringLiteral("View menu toggle restores the right Inspector panel"));
    }
    if (resultsPanelAction && resultsDock) {
        resultsDock->show();
        application.processEvents();
        resultsPanelAction->trigger();
        application.processEvents();
        check(!resultsDock->isVisible(),
              QStringLiteral("View menu toggle hides the bottom diagnostics panel"));
        resultsPanelAction->trigger();
        application.processEvents();
        check(resultsDock->isVisible(),
              QStringLiteral("View menu toggle restores the bottom diagnostics panel"));
    }

    auto* creationPackageSelector = window.findChild<QComboBox*>(
        QStringLiteral("finepaper.packageSelector"));
    auto* inspectorEmptyFocusTarget = window.findChild<QLabel*>(
        QStringLiteral("finepaper.designOverview"));
    auto* domainEmptyFocusTarget = window.findChild<QLabel*>(
        QStringLiteral("finepaper.domainManager.status"));
    auto* initialDiagnosticsStatus = window.findChild<QLabel*>(
        QStringLiteral("finepaper.diagnosticsStatus"));
    auto* initialDrcTable = window.findChild<QTableWidget*>(
        QStringLiteral("finepaper.drcTable"));
    check(packageNavigation && inspectorNavigation && domainNavigation
              && resultsNavigation,
          QStringLiteral(
              "the Panels menu exposes dedicated text actions for every workbench panel"));
    if (packageNavigation && packageDock) {
        packageDock->hide();
        packageNavigation->trigger();
        application.processEvents();
        check(packageDock->isVisible()
                  && focusIsWithin(creationPackageSelector),
              QStringLiteral(
                  "Package navigation shows the dock and focuses the new-design Package selector"));
    }
    if (inspectorNavigation && inspectorDock) {
        inspectorDock->hide();
        inspectorNavigation->trigger();
        application.processEvents();
        check(inspectorDock->isVisible()
                  && QApplication::focusWidget()
                      == inspectorEmptyFocusTarget,
              QStringLiteral(
                  "Inspector navigation focuses its readable empty-state context"));
    }
    if (domainNavigation && domainDock) {
        domainDock->hide();
        domainNavigation->trigger();
        application.processEvents();
        check(domainDock->isVisible()
                  && QApplication::focusWidget() == domainEmptyFocusTarget,
              QStringLiteral(
                  "Domain navigation focuses its readable empty-state status"));
    }
    if (resultsNavigation && resultsDock) {
        resultsDock->hide();
        resultsNavigation->trigger();
        application.processEvents();
        QWidget* expectedDiagnosticsFocus = initialDrcTable
                && initialDrcTable->rowCount() > 0
            ? static_cast<QWidget*>(initialDrcTable)
            : static_cast<QWidget*>(initialDiagnosticsStatus);
        check(resultsDock->isVisible()
                  && QApplication::focusWidget()
                      == expectedDiagnosticsFocus,
              QStringLiteral(
                  "Results navigation focuses the active diagnostics content "
                  "(actual %1)")
                  .arg(QApplication::focusWidget()
                           ? QApplication::focusWidget()->objectName()
                           : QStringLiteral("none")));
    }

    if (centerViews && packageDock && inspectorDock && domainDock
        && resultsDock && packageNavigation && inspectorNavigation
        && domainNavigation && resultsNavigation) {
        packageDock->hide();
        centerViews->setFocus(Qt::ShortcutFocusReason);
        QTest::keyClick(centerViews, Qt::Key_B, Qt::ControlModifier);
        application.processEvents();
        application.processEvents();
        check(packageDock->isVisible()
                  && focusIsWithin(creationPackageSelector)
                  && packagePanelAction->isChecked()
                  && inspectorPanelAction->isChecked(),
              QStringLiteral(
                  "Ctrl+B reveals and focuses an automatically hidden Package panel without erasing other panel preferences"));

        centerViews->setFocus(Qt::ShortcutFocusReason);
        QTest::keyClick(
            centerViews,
            Qt::Key_B,
            Qt::ControlModifier | Qt::ShiftModifier);
        application.processEvents();
        application.processEvents();
        check(inspectorDock->isVisible()
                  && focusIsWithin(inspectorEmptyFocusTarget)
                  && packagePanelAction->isChecked()
                  && inspectorPanelAction->isChecked(),
              QStringLiteral(
                  "Ctrl+Shift+B routes to Inspector while preserving the responsive Package preference"));

        centerViews->setFocus(Qt::ShortcutFocusReason);
        QTest::keyClick(
            centerViews,
            Qt::Key_D,
            Qt::ControlModifier | Qt::ShiftModifier);
        application.processEvents();
        application.processEvents();
        check(domainDock->isVisible()
                  && focusIsWithin(domainEmptyFocusTarget),
              QStringLiteral(
                  "Ctrl+Shift+D raises the Domain tab and focuses its current task"));

        resultsDock->hide();
        centerViews->setFocus(Qt::ShortcutFocusReason);
        QTest::keyClick(centerViews, Qt::Key_J, Qt::ControlModifier);
        application.processEvents();
        application.processEvents();
        check(resultsDock->isVisible()
                  && (focusIsWithin(initialDiagnosticsStatus)
                      || focusIsWithin(initialDrcTable)),
              QStringLiteral(
                  "Ctrl+J opens Results and focuses its active readable content"));
    }

    QAction* resetWorkbenchLayoutAction = window.findChild<QAction*>(
        finepaper::workbench::resetWorkbenchLayoutActionName);
    if (centerViews) {
        centerViews->setCurrentIndex(2);
    }
    if (packageDock) {
        packageDock->hide();
    }
    if (inspectorDock) {
        inspectorDock->hide();
    }
    if (resultsDock) {
        resultsDock->show();
    }
    if (resetWorkbenchLayoutAction) {
        resetWorkbenchLayoutAction->trigger();
        application.processEvents();
        application.processEvents();
    }
    const QString resetResponsiveMode = window.property(
        finepaper::workbench::workbenchWidthModeProperty).toString();
    const bool resetSideVisibilityMatchesMode =
        resetResponsiveMode == QStringLiteral("wide")
            ? packageDock && packageDock->isVisible()
                && inspectorDock && inspectorDock->isVisible()
            : resetResponsiveMode == QStringLiteral("compact")
                ? packageDock && packageDock->isVisible()
                    && inspectorDock && !inspectorDock->isVisible()
                : packageDock && !packageDock->isVisible()
                    && inspectorDock && !inspectorDock->isVisible();
    check(resetWorkbenchLayoutAction && resetSideVisibilityMatchesMode
              && packagePanelAction && packagePanelAction->isChecked()
              && inspectorPanelAction && inspectorPanelAction->isChecked()
              && resultsDock && !resultsDock->isVisible()
              && window.dockWidgetArea(packageDock) == Qt::LeftDockWidgetArea
              && window.dockWidgetArea(inspectorDock) == Qt::RightDockWidgetArea
              && window.dockWidgetArea(resultsDock) == Qt::BottomDockWidgetArea
              && centerViews && centerViews->currentIndex() == 0,
          QStringLiteral(
              "Reset Workbench Layout restores panel intent, applies %1 responsiveness, "
              "selects the editor, and hides Results")
              .arg(resetResponsiveMode));

    auto* initialCanvasView = window.findChild<QGraphicsView*>(
        QStringLiteral("finepaper.canvasView"));
    if (initialCanvasView && canvasFocusAction) {
        initialCanvasView->setFocus(Qt::ShortcutFocusReason);
        QTest::keyClick(
            initialCanvasView,
            Qt::Key_F,
            Qt::ControlModifier | Qt::ShiftModifier);
        application.processEvents();
    }
    check(canvasFocusAction && !canvasFocusAction->isChecked()
              && centerViews && centerViews->currentIndex() == 0,
          QStringLiteral(
              "the hidden no-design Canvas Focus shortcut cannot displace the start task"));

    const auto verifyCanvasFocusRoundTrip = [&] {
        if (!initialCanvasView || !canvasFocusAction || !centerViews) {
            check(false, QStringLiteral(
                "Canvas Focus regression prerequisites are available"));
            return;
        }
        centerViews->setCurrentIndex(0);
        if (packageDock) {
            packageDock->hide();
        }
        if (inspectorDock) {
            inspectorDock->show();
            inspectorDock->raise();
        }
        if (domainDock) {
            domainDock->show();
        }
        if (resultsDock) {
            resultsDock->show();
        }
        application.processEvents();
        const QList<bool> customPanelVisibility = {
            packageDock && packageDock->isVisible(),
            inspectorDock && inspectorDock->isVisible(),
            domainDock && domainDock->isVisible(),
            resultsDock && resultsDock->isVisible()};
        const QList<Qt::DockWidgetArea> customPanelAreas = {
            packageDock ? window.dockWidgetArea(packageDock)
                        : Qt::NoDockWidgetArea,
            inspectorDock ? window.dockWidgetArea(inspectorDock)
                          : Qt::NoDockWidgetArea,
            domainDock ? window.dockWidgetArea(domainDock)
                       : Qt::NoDockWidgetArea,
            resultsDock ? window.dockWidgetArea(resultsDock)
                        : Qt::NoDockWidgetArea};
        const QList<bool> customPanelFloating = {
            packageDock && packageDock->isFloating(),
            inspectorDock && inspectorDock->isFloating(),
            domainDock && domainDock->isFloating(),
            resultsDock && resultsDock->isFloating()};
        const bool customInspectorDomainTabified = inspectorDock && domainDock
            && window.tabifiedDockWidgets(inspectorDock).contains(domainDock);
        const int canvasWidthBeforeFocus = initialCanvasView->viewport()->width();
        const int centerWidthBeforeFocus = centerViews->width();
        const QString modeBeforeFocus = window.property(
            finepaper::workbench::workbenchWidthModeProperty).toString();

        initialCanvasView->setFocus(Qt::ShortcutFocusReason);
        canvasFocusAction->trigger();
        application.processEvents();
        const int focusedCanvasWidth = initialCanvasView->viewport()->width();
        check(canvasFocusAction->isChecked()
                  && canvasFocusAction->text()
                      == QStringLiteral("Exit Canvas Focus")
                  && canvasControlsButton
                  && canvasControlsButton->text()
                      == QStringLiteral("Canvas Focus: Pan")
                  && packageDock && !packageDock->isVisible()
                  && inspectorDock && !inspectorDock->isVisible()
                  && domainDock && !domainDock->isVisible()
                  && resultsDock && !resultsDock->isVisible()
                  && centerViews->currentIndex() == 0
                  && focusIsWithin(initialCanvasView)
                  && focusedCanvasWidth > canvasWidthBeforeFocus
                  && focusedCanvasWidth >= qRound(window.width() * 0.75),
              QStringLiteral(
                  "Canvas Focus hides secondary panels, focuses the Editor "
                  "canvas, and expands its viewport (%1 to %2 at window width "
                  "%3; checked=%4; docks=%5/%6/%7/%8; focus=%9; mode=%10)")
                  .arg(canvasWidthBeforeFocus)
                  .arg(focusedCanvasWidth)
                  .arg(window.width())
                  .arg(canvasFocusAction->isChecked())
                  .arg(packageDock && packageDock->isVisible())
                  .arg(inspectorDock && inspectorDock->isVisible())
                  .arg(domainDock && domainDock->isVisible())
                  .arg(resultsDock && resultsDock->isVisible())
                  .arg(focusIsWithin(initialCanvasView))
                  .arg(window.property(
                      finepaper::workbench::workbenchWidthModeProperty)
                           .toString()));
        captureSmokeScreenshot(
            window, QStringLiteral("canvas-focus"), requestedTheme);

        canvasFocusAction->trigger();
        application.processEvents();
        const QList<bool> restoredPanelVisibility = {
            packageDock && packageDock->isVisible(),
            inspectorDock && inspectorDock->isVisible(),
            domainDock && domainDock->isVisible(),
            resultsDock && resultsDock->isVisible()};
        const QList<Qt::DockWidgetArea> restoredPanelAreas = {
            packageDock ? window.dockWidgetArea(packageDock)
                        : Qt::NoDockWidgetArea,
            inspectorDock ? window.dockWidgetArea(inspectorDock)
                          : Qt::NoDockWidgetArea,
            domainDock ? window.dockWidgetArea(domainDock)
                       : Qt::NoDockWidgetArea,
            resultsDock ? window.dockWidgetArea(resultsDock)
                        : Qt::NoDockWidgetArea};
        const QList<bool> restoredPanelFloating = {
            packageDock && packageDock->isFloating(),
            inspectorDock && inspectorDock->isFloating(),
            domainDock && domainDock->isFloating(),
            resultsDock && resultsDock->isFloating()};
        const bool restoredInspectorDomainTabified = inspectorDock && domainDock
            && window.tabifiedDockWidgets(inspectorDock).contains(domainDock);
        check(!canvasFocusAction->isChecked()
                  && canvasFocusAction->text() == QStringLiteral("Focus Canvas")
                  && restoredPanelVisibility == customPanelVisibility
                  && restoredPanelAreas == customPanelAreas
                  && restoredPanelFloating == customPanelFloating
                  && restoredInspectorDomainTabified
                      == customInspectorDomainTabified
                  && centerViews->currentIndex() == 0
                  && (modeBeforeFocus == QStringLiteral("wide")
                          ? qAbs(centerViews->width()
                                 - centerWidthBeforeFocus) <= 1
                          : centerViews->width()
                                >= centerWidthBeforeFocus),
              QStringLiteral(
                  "leaving Canvas Focus restores customized panel intent and "
                  "responsive sizing"));

        canvasFocusAction->trigger();
        application.processEvents();
        centerViews->setCurrentIndex(1);
        application.processEvents();
        application.processEvents();
        const QList<bool> tabSwitchPanelVisibility = {
            packageDock && packageDock->isVisible(),
            inspectorDock && inspectorDock->isVisible(),
            domainDock && domainDock->isVisible(),
            resultsDock && resultsDock->isVisible()};
        check(!canvasFocusAction->isChecked()
                  && canvasFocusAction->text() == QStringLiteral("Focus Canvas")
                  && centerViews->currentIndex() == 1
                  && tabSwitchPanelVisibility == customPanelVisibility
                  && focusIsWithin(centerViews)
                  && !focusIsWithin(initialCanvasView),
              QStringLiteral(
                  "switching tabs directly exits Canvas Focus, preserves the "
                  "requested view, restores panels, and moves focus out of the hidden canvas "
                  "(checked=%1, text=%2, index=%3, panels=%4/%5/%6/%7, "
                  "expected=%8/%9/%10/%11, centerFocus=%12, canvasFocus=%13)")
                  .arg(canvasFocusAction->isChecked())
                  .arg(canvasFocusAction->text())
                  .arg(centerViews->currentIndex())
                  .arg(tabSwitchPanelVisibility.value(0))
                  .arg(tabSwitchPanelVisibility.value(1))
                  .arg(tabSwitchPanelVisibility.value(2))
                  .arg(tabSwitchPanelVisibility.value(3))
                  .arg(customPanelVisibility.value(0))
                  .arg(customPanelVisibility.value(1))
                  .arg(customPanelVisibility.value(2))
                  .arg(customPanelVisibility.value(3))
                  .arg(focusIsWithin(centerViews))
                  .arg(focusIsWithin(initialCanvasView)));

        centerViews->setCurrentIndex(0);
        application.processEvents();
        canvasFocusAction->trigger();
        application.processEvents();
        if (packageNavigation) {
            packageNavigation->trigger();
            application.processEvents();
        }
        auto* endpointLibraryFilter = window.findChild<QLineEdit*>(
            QStringLiteral("finepaper.endpointPaletteFilter"));
        QWidget* expectedLibraryFocus = endpointLibraryFilter
                && endpointLibraryFilter->isEnabled()
                && endpointLibraryFilter->isVisibleTo(packageDock)
            ? static_cast<QWidget*>(endpointLibraryFilter)
            : static_cast<QWidget*>(creationPackageSelector);
        check(!canvasFocusAction->isChecked()
                  && packageDock && packageDock->isVisible()
                  && centerViews->currentIndex() == 0
                  && focusIsWithin(expectedLibraryFocus),
              QStringLiteral(
                  "opening a panel from Canvas Focus restores the workbench "
                  "before focusing its current Library task"));
        if (resetWorkbenchLayoutAction) {
            resetWorkbenchLayoutAction->trigger();
            application.processEvents();
        }
        restoreRequestedClientSize();
    };

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
    auto* creationPackageDetails = window.findChild<QLabel*>(
        QStringLiteral("finepaper.creationPackageDetails"));
    auto* packageLibrarySection = window.findChild<QGroupBox*>(
        QStringLiteral("finepaper.packageLibrarySection"));
    auto* packageLibraryScroll = window.findChild<QScrollArea*>(
        QStringLiteral("finepaper.packageLibraryScroll"));
    auto* installPackageButton = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.installPackage"));
    auto* reloadPackagesButton = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.reloadPackages"));
    auto* createButton = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.createDesign"));
    auto* endpointPalette = window.findChild<QListWidget*>(QStringLiteral("finepaper.endpointPalette"));
    auto* currentDesignSection = window.findChild<QGroupBox*>(
        QStringLiteral("finepaper.currentDesignSection"));
    auto* endpointLibrarySection = window.findChild<QGroupBox*>(
        QStringLiteral("finepaper.endpointLibrarySection"));
    auto* initialDomainSelector = window.findChild<QComboBox*>(
        finepaper::workbench::domainLayerSelectorName);
    check(availablePackages
              && availablePackages->text().startsWith(QStringLiteral("1 available")),
          QStringLiteral("runtime NoC IP availability is summarized in the workbench"));
    check(creationPackageSelector
              && creationPackageSelector->count() == 1
              && creationPackageSelector->currentData().toString()
                  == QStringLiteral("finepaper.noc@1.0.0")
              && creationPackageSelector->currentText()
                  == QStringLiteral("Finepaper Mesh NoC")
              && !creationPackageSelector->accessibleName().isEmpty(),
          QStringLiteral(
              "the Package for a new design is visible before opening the dialog"));
    check(packageDock && packageDock->windowTitle()
              == QStringLiteral("NoC Library")
              && creationPackageDetails
              && creationPackageDetails->textFormat() == Qt::PlainText
              && creationPackageDetails->text().contains(
                  QStringLiteral("finepaper.noc@1.0.0"))
              && creationPackageDetails->text().contains(
                  QStringLiteral("Mesh")),
          QStringLiteral(
              "the compact Package selector keeps the exact version and capability range visible in text"));
    const bool packageDetailsReachable = packageLibraryScroll
        && creationPackageDetails
        && (widgetIsFullyVisibleInScrollViewport(
                packageLibraryScroll, creationPackageDetails)
            || packageLibraryScroll->verticalScrollBar()->maximum() > 0);
    const bool packageActionsFit = packageLibrarySection
        && packageLibraryScroll && creationPackageSelector
        && packageDetailsReachable && createButton
        && installPackageButton && reloadPackagesButton
        && widgetIsFullyVisibleInScrollViewport(
            packageLibraryScroll, creationPackageSelector)
        && widgetIsFullyVisibleWithin(packageDock, createButton)
        && widgetIsFullyVisibleWithin(
            packageDock, installPackageButton)
        && widgetIsFullyVisibleWithin(
            packageDock, reloadPackagesButton)
        && installPackageButton->width()
            >= installPackageButton->sizeHint().width()
        && reloadPackagesButton->width()
            >= reloadPackagesButton->sizeHint().width();
    check(packageActionsFit,
          QStringLiteral(
              "Package maintenance actions remain fully visible inside the NoC Library"));
    if (fontScale >= 1.5) {
        const bool actionsStacked = installPackageButton
            && reloadPackagesButton
            && installPackageButton->geometry().bottom()
                < reloadPackagesButton->geometry().top();
        const bool actionsFitSideBySide = installPackageButton
            && reloadPackagesButton
            && installPackageButton->geometry().right()
                < reloadPackagesButton->geometry().left()
            && installPackageButton->width()
                >= installPackageButton->sizeHint().width()
            && reloadPackagesButton->width()
                >= reloadPackagesButton->sizeHint().width();
        check(actionsStacked || actionsFitSideBySide,
              QStringLiteral(
                  "large system fonts keep Package actions complete, stacking "
                  "only when the labels no longer fit side by side"));
    }
    check(activePackage
              && activePackage->text().contains(QStringLiteral("No design is open")),
          QStringLiteral("the workbench does not imply an active IP before design creation"));
    check(endpointPalette && endpointPalette->count() == 0,
          QStringLiteral("Endpoint types are shown only for the active design Package"));
    check(initialDomainSelector && initialDomainSelector->count() == 1
              && initialDomainSelector->itemData(0).toString().isEmpty()
              && !initialDomainSelector->isEnabled()
              && !initialDomainSelector->isVisible(),
          QStringLiteral("a Package without Domain schema exposes only a disabled None layer"));
    QAction* initialSaveAction = actionWithText(window, QStringLiteral("Save"));
    QAction* saveAsAction = actionWithText(window, QStringLiteral("Save As…"));
    QAction* initialValidateAction = actionWithText(
        window, QStringLiteral("Validate / DRC"));
    QAction* initialGenerateAction = actionWithText(window, QStringLiteral("Generate RTL"));
    QAction* initialSelectCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::selectCanvasActionName);
    QAction* initialPanCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::panCanvasActionName);
    auto* applyParameters = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.applyParameters"));
    auto* resizeMeshButton = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.resizeMesh"));
    auto* meshTopologyGroup = window.findChild<QWidget*>(
        QStringLiteral("finepaper.meshTopologyGroup"));
    auto* parameterGroup = window.findChild<QWidget*>(
        QStringLiteral("finepaper.parameterGroup"));
    auto* initialSelectionGroup = window.findChild<QWidget*>(
        finepaper::workbench::selectionInspectorName);
    auto* inspectorDesignSettings = window.findChild<QWidget*>(
        QStringLiteral("finepaper.inspectorDesignSettings"));
    auto* inspectorDesignSettingsContent = window.findChild<QWidget*>(
        QStringLiteral("finepaper.inspectorDesignSettingsContent"));
    auto* inspectorSelectionDetail = window.findChild<QLabel*>(
        QStringLiteral("finepaper.inspectorSelectionDetail"));
    auto* inspectorSelectionMetadata = window.findChild<QLabel*>(
        QStringLiteral("finepaper.inspectorSelectionMetadata"));
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
    check(initialSelectCanvasAction && !initialSelectCanvasAction->isEnabled()
              && !initialSelectCanvasAction->isChecked()
              && initialPanCanvasAction && !initialPanCanvasAction->isEnabled()
              && initialPanCanvasAction->isChecked(),
          QStringLiteral(
              "the default Pan mode is retained but disabled while no design exists"));
    check(applyParameters && !applyParameters->isEnabled(),
          QStringLiteral("parameter application is disabled without a design"));
    check(resizeMeshButton && !resizeMeshButton->isEnabled()
              && resizeMeshAction && !resizeMeshAction->isEnabled(),
          QStringLiteral("Mesh resize entry points are disabled without a design"));
    check(meshTopologyGroup && !meshTopologyGroup->isVisible()
              && parameterGroup && !parameterGroup->isVisible()
              && initialSelectionGroup
              && !initialSelectionGroup->isVisible()
              && inspectorDesignSettings
              && !inspectorDesignSettings->isVisible(),
          QStringLiteral(
              "the no-design Inspector hides topology, selection, and parameter editors"));

    check(createButton && createButton->isEnabled()
              && createButton->isVisible()
              && widgetIsFullyVisibleWithin(packageDock, createButton)
              && currentDesignSection && !currentDesignSection->isVisible()
              && endpointLibrarySection && !endpointLibrarySection->isVisible(),
          QStringLiteral(
              "the no-design library keeps a fully visible pinned creation entry without exposing Endpoint controls"));
    auto* canvasEmptyState = window.findChild<QScrollArea*>(
        QStringLiteral("finepaper.canvasEmptyState"));
    auto* emptyStateCreate = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.emptyStateCreate"));
    auto* emptyStateOpen = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.emptyStateOpen"));
    auto* emptyStateInstall = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.emptyStateInstall"));
    bool emptyStateActionsReachable = canvasEmptyState
        && emptyStateCreate && emptyStateOpen
        && widgetIsFullyVisibleInScrollViewport(
            canvasEmptyState, emptyStateCreate)
        && widgetIsFullyVisibleInScrollViewport(
            canvasEmptyState, emptyStateOpen);
    if (!emptyStateActionsReachable && canvasEmptyState
        && emptyStateOpen) {
        canvasEmptyState->ensureWidgetVisible(
            emptyStateOpen,
            finepaper::ui::UiMetrics::spacing12,
            finepaper::ui::UiMetrics::spacing12);
        application.processEvents();
        emptyStateActionsReachable =
            widgetIsFullyVisibleInScrollViewport(
                canvasEmptyState, emptyStateCreate)
            && widgetIsFullyVisibleInScrollViewport(
                canvasEmptyState, emptyStateOpen);
        canvasEmptyState->verticalScrollBar()->setValue(0);
        application.processEvents();
    }
    check(canvasEmptyState && canvasEmptyState->isVisible()
              && emptyStateCreate && emptyStateCreate->isVisible()
              && emptyStateCreate->isEnabled()
              && emptyStateOpen && emptyStateOpen->isVisible()
              && emptyStateOpen->isEnabled()
              && emptyStateActionsReachable
              && emptyStateInstall && !emptyStateInstall->isVisible(),
          QStringLiteral(
              "the no-design canvas keeps Create and Open reachable by scrolling while the Library guarantees a visible creation fallback"));

    finepaper::FinepaperMainWindow cleanupWindow(locations);
    cleanupWindow.resize(window.size());
    cleanupWindow.show();
    application.processEvents();
    const QVector<finepaper::Diagnostic> retainedFileDiagnostics{
        finepaper::Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("operation.cleanup_failed"),
            QStringLiteral("could not remove validation runtime directory"),
            QStringLiteral("/tmp/finepaper-runtime/validation-retained"),
            QStringLiteral("execution")}};
    const QStringList retainedFilePaths{
        QStringLiteral("/tmp/finepaper-runtime/validation-retained")};
    finepaper::FinepaperMainWindowSmokeAccess::
        presentRetainedFileCancellation(
            cleanupWindow,
            retainedFileDiagnostics,
            retainedFilePaths);
    application.processEvents();
    auto* cleanupBanner = cleanupWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.cleanupRecoveryBanner"));
    auto* cleanupActivity = cleanupWindow.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.activityLog"));
    check(!finepaper::FinepaperMainWindowSmokeAccess::
                  processCleanupIsUnresolved(cleanupWindow)
              && cleanupBanner && cleanupBanner->isHidden()
              && cleanupActivity
              && cleanupActivity->toPlainText().contains(
                  QStringLiteral("Package processes are verified stopped"))
              && cleanupActivity->toPlainText().contains(
                  retainedFilePaths.constFirst()),
          QStringLiteral(
              "retained runtime files remain recoverable without entering Package-process quarantine"));

    const QVector<finepaper::Diagnostic> cleanupDiagnostics{
        finepaper::Diagnostic{
            QStringLiteral("error"),
            QStringLiteral("operation.cleanup_failed"),
            QStringLiteral(
                "could not verify that Package process group 4242 stopped"),
            QStringLiteral("/tmp/finepaper-runtime/generate"),
            QStringLiteral("generator")}};
    const QStringList retainedCleanupPaths{
        QStringLiteral("/tmp/finepaper-runtime/run-4242"),
        QStringLiteral("/tmp/finepaper-runtime/output-4242")};
    finepaper::FinepaperMainWindowSmokeAccess::presentCleanupFailure(
        cleanupWindow,
        QStringLiteral("RTL generation"),
        cleanupDiagnostics,
        retainedCleanupPaths);
    application.processEvents();
    auto* cleanupStatus = cleanupWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.cleanupRecoveryStatus"));
    auto* reviewCleanup = cleanupWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.reviewCleanupDetails"));
    cleanupWindow.statusBar()->showMessage(
        QStringLiteral("A later transient workspace message"), 5000);
    application.processEvents();
    const QString cleanupActivityText = cleanupActivity
        ? cleanupActivity->toPlainText() : QString();
    check(cleanupBanner && cleanupBanner->isVisibleTo(&cleanupWindow)
              && cleanupStatus
              && cleanupStatus->text()
                  == QStringLiteral("Cleanup unresolved")
              && reviewCleanup && reviewCleanup->isVisibleTo(&cleanupWindow)
              && reviewCleanup->isEnabled()
              && reviewCleanup->property("finepaperRole").toString()
                     == QStringLiteral("primary")
              && widgetIsFullyVisibleWithin(
                  cleanupWindow.statusBar(), cleanupBanner)
              && cleanupActivityText.contains(
                  QStringLiteral("process group 4242"))
              && cleanupActivityText.contains(
                  QStringLiteral("/tmp/finepaper-runtime/generate"))
              && cleanupActivityText.contains(retainedCleanupPaths.at(0))
              && cleanupActivityText.contains(retainedCleanupPaths.at(1)),
          QStringLiteral(
              "cleanup-unresolved exposes a persistent text recovery area and writes complete process and retained-path details to Activity Log"));
    captureSmokeScreenshot(
        cleanupWindow, QStringLiteral("cleanup-unresolved"), requestedTheme);

    bool cleanupDialogReviewed = false;
    bool cleanupDialogSafeDefault = false;
    bool cleanupDetailsCopied = false;
    bool cleanupCopyFeedbackVisible = false;
    bool cleanupDialogActionsFit = false;
    bool cleanupReviewKeyboardFocus = false;
    if (reviewCleanup) {
        activateWindowForKeyboard(cleanupWindow);
        reviewCleanup->setFocus(Qt::TabFocusReason);
        application.processEvents();
        cleanupReviewKeyboardFocus = focusIsWithin(reviewCleanup);
    }
    QTimer::singleShot(0, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (!dialog || !dialog->isVisible()
                || dialog->objectName()
                    != QStringLiteral("finepaper.cleanupDetailsDialog")) {
                continue;
            }
            auto* details = dialog->findChild<QPlainTextEdit*>(
                QStringLiteral("finepaper.cleanupDetailsText"));
            auto* copy = dialog->findChild<QPushButton*>(
                QStringLiteral("finepaper.copyCleanupDetails"));
            auto* keepOpen = dialog->findChild<QPushButton*>(
                QStringLiteral("finepaper.keepOpenFromCleanupDetails"));
            auto* copyFeedback = dialog->findChild<QLabel*>(
                QStringLiteral("finepaper.cleanupCopyFeedback"));
            cleanupDialogSafeDefault = keepOpen && keepOpen->isDefault();
            cleanupDialogActionsFit = copy && keepOpen
                && widgetIsFullyVisibleWithin(dialog, copy)
                && widgetIsFullyVisibleWithin(dialog, keepOpen)
                && !copy->geometry().intersects(keepOpen->geometry());
            cleanupDialogReviewed = details
                && details->isVisibleTo(dialog)
                && details->toPlainText().contains(
                    QStringLiteral("process group 4242"))
                && details->toPlainText().contains(
                    retainedCleanupPaths.at(0));
            if (copy) {
                copy->click();
                cleanupDetailsCopied = QApplication::clipboard()
                    && QApplication::clipboard()->text().contains(
                        QStringLiteral("process group 4242"));
                cleanupCopyFeedbackVisible = copyFeedback
                    && !copyFeedback->isHidden()
                    && copyFeedback->text()
                        == QStringLiteral("Copied to clipboard.")
                    && copy->text() == QStringLiteral("Copy Again")
                    && copy->accessibleDescription().contains(
                        QStringLiteral("copied"), Qt::CaseInsensitive);
            }
            if (keepOpen) {
                keepOpen->click();
            } else {
                dialog->reject();
            }
            return;
        }
    });
    if (reviewCleanup) {
        reviewCleanup->click();
    }
    application.processEvents();
    check(cleanupReviewKeyboardFocus
              && cleanupDialogReviewed && cleanupDialogSafeDefault
              && cleanupDialogActionsFit
              && cleanupDetailsCopied
              && cleanupCopyFeedbackVisible
              && cleanupBanner
              && cleanupBanner->isVisibleTo(&cleanupWindow),
          QStringLiteral(
              "Review Details keeps Finepaper open by default and offers keyboard-focusable complete cleanup text with Copy Cleanup Details"));

    bool closeDialogSafeDefault = false;
    bool closeDialogGuidanceUsesReview = false;
    QTimer::singleShot(0, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (!messageBox || !messageBox->isVisible()
                || messageBox->objectName()
                    != QStringLiteral(
                        "finepaper.cleanupUnresolvedCloseConfirmation")) {
                continue;
            }
            auto* keepOpen = messageBox->findChild<QPushButton*>(
                QStringLiteral("finepaper.keepOpenWithUnresolvedCleanup"));
            closeDialogSafeDefault = keepOpen && keepOpen->isDefault()
                && messageBox->escapeButton() == keepOpen;
            closeDialogGuidanceUsesReview = messageBox->informativeText().contains(
                QStringLiteral("Review Details"));
            if (keepOpen) {
                keepOpen->click();
            }
            return;
        }
    });
    cleanupWindow.close();
    application.processEvents();
    check(closeDialogSafeDefault && closeDialogGuidanceUsesReview
              && cleanupWindow.isVisible()
              && cleanupBanner
              && cleanupBanner->isVisibleTo(&cleanupWindow),
          QStringLiteral(
              "Keep Finepaper Open remains the default and Escape-safe choice while cleanup is unresolved"));
    if (fontScale >= 1.5) {
        check(cleanupBanner && reviewCleanup
                  && widgetIsFullyVisibleWithin(
                      cleanupWindow.statusBar(), cleanupBanner)
                  && widgetIsFullyVisibleWithin(cleanupBanner, reviewCleanup),
              QStringLiteral(
                  "large text keeps the cleanup recovery area and Review Details action inside the status region without overlap"));
    }
    cleanupWindow.hide();
    application.processEvents();

    restoreRequestedClientSize();
    captureSmokeScreenshot(window, QStringLiteral("no-design"), requestedTheme);
    if (requestedScope == QStringLiteral("workbench")) {
        if (compactPresentation && workspaceSelector && centerViews) {
            workspaceSelector->setFocus(Qt::ShortcutFocusReason);
            QTest::keyClick(workspaceSelector, Qt::Key_Down);
            application.processEvents();
            check(centerViews->currentIndex() == 1
                      && workspaceSelector->currentText()
                          == QStringLiteral("Domain Configuration")
                      && canvasFocusAction
                      && !canvasFocusAction->isChecked(),
                  QStringLiteral(
                      "the Compact Workspace selector switches views by keyboard using complete text"));
        }
        if (centerViews) {
            centerViews->setCurrentIndex(0);
        }
        createDesignThroughDialog(
            window,
            QStringLiteral("finepaper.noc@1.0.0"),
            QStringLiteral("responsive_shell"));
        restoreRequestedClientSize();
        application.processEvents();
        application.processEvents();
        const QString designResponsiveMode = window.property(
            finepaper::workbench::workbenchWidthModeProperty).toString();
        const bool designPanelsMatchPriority =
            designResponsiveMode == QStringLiteral("wide")
                ? packageDock && packageDock->isVisible()
                    && inspectorDock && inspectorDock->isVisible()
                : designResponsiveMode == QStringLiteral("compact")
                    ? packageDock && !packageDock->isVisible()
                        && inspectorDock && inspectorDock->isVisible()
                    : packageDock && !packageDock->isVisible()
                        && inspectorDock && !inspectorDock->isVisible();
        const bool designCompactPresentation =
            designResponsiveMode != QStringLiteral("wide");
        const bool designCommandBarReady = runControls
            && runControls->isVisible()
            && canvasControlsButton
            && canvasControlsButton->isVisible()
                == designCompactPresentation
            && wideCanvasControls
            && wideCanvasControls->isVisible()
                == !designCompactPresentation
            && canvasModeControl
            && canvasModeControl->isVisible()
                == !designCompactPresentation
            && canvasFocusAction && canvasFocusAction->isVisible()
            && canvasFocusAction->isEnabled()
            && fitCanvasAction && fitCanvasAction->isVisible()
            && fitCanvasAction->isEnabled()
            && regularizeCanvasAction
            && regularizeCanvasAction->isVisible()
            && regularizeCanvasAction->isEnabled()
            && initialSelectCanvasAction
            && initialSelectCanvasAction->isVisible()
            && initialSelectCanvasAction->isEnabled()
            && initialPanCanvasAction
            && initialPanCanvasAction->isVisible()
            && initialPanCanvasAction->isEnabled();
        check(designPanelsMatchPriority
                  && packagePanelAction && packagePanelAction->isChecked()
                  && inspectorPanelAction && inspectorPanelAction->isChecked()
                  && designCommandBarReady,
              QStringLiteral(
                  "after design creation, %1 responsiveness restores the "
                  "Editor command bar and prioritizes Inspector without "
                  "erasing Package intent")
                  .arg(designResponsiveMode));

        QWidget* navigationFocus = designCompactPresentation
            ? static_cast<QWidget*>(workspaceSelector)
            : static_cast<QWidget*>(centerViews ? centerViews->tabBar()
                                                : nullptr);
        if (navigationFocus) {
            navigationFocus->setFocus(Qt::TabFocusReason);
        }
        const bool panWasChecked = initialPanCanvasAction
            && initialPanCanvasAction->isChecked();
        if (centerViews) {
            centerViews->setCurrentIndex(1);
            application.processEvents();
            QTest::keyClick(centerViews->currentWidget(), Qt::Key_V);
            application.processEvents();
        }
        const bool canvasWidgetsHidden = canvasControlsButton
            && !canvasControlsButton->isVisible()
            && wideCanvasControls && !wideCanvasControls->isVisible();
        const bool canvasActionsHidden = canvasFocusAction
            && !canvasFocusAction->isEnabled()
            && !canvasFocusAction->isVisible()
            && fitCanvasAction && !fitCanvasAction->isEnabled()
            && !fitCanvasAction->isVisible()
            && regularizeCanvasAction
            && !regularizeCanvasAction->isEnabled()
            && !regularizeCanvasAction->isVisible()
            && initialSelectCanvasAction
            && !initialSelectCanvasAction->isEnabled()
            && !initialSelectCanvasAction->isVisible();
        const bool canvasModeRetained = initialPanCanvasAction
            && initialPanCanvasAction->isChecked() == panWasChecked;
        const bool commandBarFocusSafe = !focusIsWithin(wideCanvasControls)
            && !focusIsWithin(canvasControlsButton);
        check(runControls && runControls->isVisible()
                  && canvasWidgetsHidden && canvasActionsHidden
                  && canvasModeRetained && commandBarFocusSafe,
              QStringLiteral(
                  "non-Editor Workspaces retain Run while suppressing Canvas "
                  "commands, shortcuts, and focus changes (run=%1, "
                  "widgetsHidden=%2, actionsHidden=%3, modeRetained=%4, "
                  "focusSafe=%5)")
                  .arg(runControls && runControls->isVisible())
                  .arg(canvasWidgetsHidden)
                  .arg(canvasActionsHidden)
                  .arg(canvasModeRetained)
                  .arg(commandBarFocusSafe));
        if (centerViews) {
            centerViews->setCurrentIndex(0);
            application.processEvents();
        }
        check(canvasFocusAction && canvasFocusAction->isEnabled()
                  && (designCompactPresentation
                          ? canvasControlsButton
                                && canvasControlsButton->isVisible()
                          : wideCanvasControls
                                && wideCanvasControls->isVisible()),
              QStringLiteral(
                  "returning to Editor restores the responsive Canvas commands"));
        verifyCanvasFocusRoundTrip();
        closeDiscarding(window);
        application.processEvents();
        QTextStream(stdout)
            << (failures == 0 ? "finepaper-gui-smoke passed"
                              : "finepaper-gui-smoke failed")
            << Qt::endl;
        return failures == 0 ? 0 : 1;
    }
    const QString provenanceDesignName =
        QStringLiteral("NoC %2-%3-%4-%5");
    const quint64 packageProbeBeforeCreate =
        finepaper::FinepaperMainWindowSmokeAccess::
            runtimePackageProbeGeneration(window);
    createDesignThroughDialog(
        window,
        QStringLiteral("finepaper.noc@1.0.0"),
        provenanceDesignName);
    const quint64 packageProbeAfterCreate =
        finepaper::FinepaperMainWindowSmokeAccess::
            runtimePackageProbeGeneration(window);
    finepaper::FinepaperMainWindowSmokeAccess::
        refreshDesignProjection(window);
    const quint64 packageProbeAfterRoutineRefresh =
        finepaper::FinepaperMainWindowSmokeAccess::
            runtimePackageProbeGeneration(window);
    check(packageProbeBeforeCreate > 0
              && packageProbeAfterCreate
                     == packageProbeBeforeCreate + 1
              && packageProbeAfterRoutineRefresh
                     == packageProbeAfterCreate,
          QStringLiteral(
              "Package availability is probed at the Create boundary but not during routine design projection"));
    restoreRequestedClientSize();
    check(window.isWindowModified(),
          QStringLiteral("creating a design marks the workbench dirty"));
    check(canvasEmptyState && !canvasEmptyState->isVisible(),
          QStringLiteral("the no-design empty state leaves the canvas after creation"));
    check(activePackage
              && activePackage->text().contains(QStringLiteral("finepaper.noc@1.0.0"))
              && endpointPalette && endpointPalette->count() == 2
              && endpointPalette->isEnabled()
              && currentDesignSection && currentDesignSection->isVisible()
              && endpointLibrarySection && endpointLibrarySection->isVisible()
              && createButton && createButton->isVisible()
              && initialSaveAction && initialSaveAction->isEnabled()
              && saveAsAction && saveAsAction->isEnabled()
              && initialValidateAction && initialValidateAction->isEnabled()
              && initialGenerateAction && initialGenerateAction->isEnabled(),
          QStringLiteral("active NoC IP, design actions and Endpoint Palette stay aligned"));
    verifyCanvasFocusRoundTrip();
    check(endpointLibrarySection && packageLibrarySection
              && packageLibrarySection->y() < endpointLibrarySection->y(),
          QStringLiteral(
              "the Library keeps new-design Package selection before the active Endpoint catalog"));
    check(applyParameters && !applyParameters->isEnabled(),
          QStringLiteral("Package defaults are editable but do not enable a no-op Apply"));
    check(meshTopologyGroup && meshTopologyGroup->isVisible()
              && parameterGroup && parameterGroup->isVisible()
              && initialSelectionGroup
              && initialSelectionGroup->isVisible()
              && inspectorDesignSettings
              && inspectorDesignSettings->isVisible()
              && inspectorDesignSettingsContent
              && inspectorDesignSettingsContent->isVisible(),
          QStringLiteral(
              "design settings are expanded when a design has no active selection"));
    check(resizeMeshButton && resizeMeshButton->isEnabled()
              && resizeMeshAction && resizeMeshAction->isEnabled(),
          QStringLiteral("Mesh resize entry points follow active Package metadata"));
    auto* designOverview = window.findChild<QLabel*>(
        QStringLiteral("finepaper.designOverview"));
    auto* designMetadata = window.findChild<QLabel*>(
        QStringLiteral("finepaper.designMetadata"));
    auto* designAvailability = window.findChild<QLabel*>(
        QStringLiteral("finepaper.designAvailability"));
    check(designOverview
              && designOverview->text() == provenanceDesignName
              && !designOverview->text().contains(QStringLiteral("%6"))
              && !designOverview->text().contains(QStringLiteral("%7"))
              && designMetadata
              && designMetadata->text().contains(
                  QStringLiteral("finepaper.noc@1.0.0"))
              && designMetadata->text().contains(QStringLiteral("Mesh"))
              && designAvailability && !designAvailability->isVisible(),
          QStringLiteral(
              "Inspector uses a compact plain-text design summary without reinterpreting placeholder text"));
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
    check(graphicsView
              && graphicsView->objectName()
                     == QStringLiteral("finepaper.canvasView")
              && !graphicsView->accessibleName().trimmed().isEmpty(),
          QStringLiteral(
              "the topology canvas exposes a stable automation id and accessible name"));
    if (requestedWindowSize == QSize(1280, 720)) {
        check(graphicsView && graphicsView->viewport()
                  && graphicsView->viewport()->width() >= 320
                  && graphicsView->viewport()->height() >= 360,
              QStringLiteral(
                  "at 1280x720 the central canvas retains a practical editing area (%1x%2)")
                  .arg(graphicsView && graphicsView->viewport()
                           ? graphicsView->viewport()->width() : 0)
                  .arg(graphicsView && graphicsView->viewport()
                           ? graphicsView->viewport()->height() : 0));
        check(inspectorScroll
                  && inspectorScroll->horizontalScrollBar()->maximum() == 0,
              QStringLiteral(
                  "at 1280x720 the Inspector does not require horizontal scrolling"));
    }
    QAction* selectCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::selectCanvasActionName);
    QAction* panCanvasAction = window.findChild<QAction*>(
        finepaper::workbench::panCanvasActionName);
    QAction* reduceMotionAction = window.findChild<QAction*>(
        finepaper::workbench::reducedMotionActionName);
    check(selectCanvasAction && panCanvasAction && panCanvasAction->isChecked()
              && nodeEditor
              && nodeEditor->canvasInteractionMode()
                  == finepaper::NocCanvasInteractionMode::Pan
              && graphicsView
              && graphicsView->dragMode() == QGraphicsView::ScrollHandDrag,
          QStringLiteral("canvas opens in the directly draggable Pan mode"));
    QWidget* canvasKeyTarget = nullptr;
    bool shortcutWindowActive = false;
    if (graphicsView) {
        shortcutWindowActive = activateWindowForKeyboard(window);
        graphicsView->setFocus(Qt::ShortcutFocusReason);
        (void)waitUntil(
            [graphicsView] { return focusIsWithin(graphicsView); },
            std::chrono::seconds(1));
        canvasKeyTarget = QApplication::focusWidget();
        if (!canvasKeyTarget || canvasKeyTarget->window() != &window) {
            canvasKeyTarget = window.focusWidget();
        }
        if (canvasKeyTarget) {
            QTest::keyClick(canvasKeyTarget, Qt::Key_V);
        }
        application.processEvents();
    }
    const bool selectShortcutApplied = selectCanvasAction
        && selectCanvasAction->isChecked()
        && nodeEditor
        && nodeEditor->canvasInteractionMode()
            == finepaper::NocCanvasInteractionMode::Select;
    if (canvasKeyTarget) {
        QTest::keyClick(canvasKeyTarget, Qt::Key_H);
        application.processEvents();
    }
    check(shortcutWindowActive
              && canvasKeyTarget && focusIsWithin(graphicsView)
              && selectShortcutApplied
              && panCanvasAction && panCanvasAction->isChecked()
              && nodeEditor
              && nodeEditor->canvasInteractionMode()
                  == finepaper::NocCanvasInteractionMode::Pan,
          QStringLiteral(
              "V and H switch the visible Canvas mode through real keyboard input "
              "(target=%1, canvasFocus=%2, selectApplied=%3, panChecked=%4, "
              "finalMode=%5, activeWindow=%6)")
              .arg(canvasKeyTarget != nullptr)
              .arg(focusIsWithin(graphicsView))
              .arg(selectShortcutApplied)
              .arg(panCanvasAction && panCanvasAction->isChecked())
              .arg(nodeEditor
                       ? static_cast<int>(
                             nodeEditor->canvasInteractionMode())
                       : -1)
              .arg(shortcutWindowActive && window.isActiveWindow()));
    if (reduceMotionAction) {
        const bool originalReducedMotion = reduceMotionAction->isChecked();
        reduceMotionAction->setChecked(!originalReducedMotion);
        application.processEvents();
        check(nodeEditor
                  && nodeEditor->reducedMotion() == !originalReducedMotion
                  && animatedView
                  && animatedView->reducedMotion() == !originalReducedMotion
                  && window.dockOptions().testFlag(
                         QMainWindow::AnimatedDocks)
                         == originalReducedMotion,
              QStringLiteral(
                  "Reduce Motion propagates to canvas feedback and workbench docks"));
        reduceMotionAction->setChecked(originalReducedMotion);
        application.processEvents();
    } else {
        check(false,
              QStringLiteral("View menu exposes the Reduce Motion preference"));
    }
    if (graphicsScene) {
        finepaper::AnimatedGraphicsView overlayRegressionView(graphicsScene);
        overlayRegressionView.resize(96, 96);
        overlayRegressionView.show();
        overlayRegressionView.beginEndpointDrag(
            QPoint(4, 4),
            QString(4096, QLatin1Char('W')),
            finepaper::EndpointDragTarget::Canvas);
        application.processEvents();
        auto* dragPulse = overlayRegressionView.findChild<QVariantAnimation*>(
            QStringLiteral("finepaper.endpointDragPulse"));
        const bool pulseInitiallyRunning = dragPulse
            && dragPulse->state() == QAbstractAnimation::Running;
        overlayRegressionView.setReducedMotion(true);
        const bool pulseStopped = dragPulse
            && dragPulse->state() == QAbstractAnimation::Stopped;
        overlayRegressionView.setReducedMotion(false);
        const bool pulseResumed = dragPulse
            && dragPulse->state() == QAbstractAnimation::Running;
        overlayRegressionView.resize(48, 48);
        application.processEvents();
        const QPixmap tinyOverlay = overlayRegressionView.grab();
        check(pulseInitiallyRunning && pulseStopped && pulseResumed
                  && dragPulse && dragPulse->duration() <= 400
                  && !tinyOverlay.isNull(),
              QStringLiteral(
                  "drag feedback is brief, resumes after Reduce Motion, and safely degrades in a transiently narrow canvas"));
        overlayRegressionView.endEndpointDrag();
        overlayRegressionView.close();
    }
    if (graphicsView && graphicsView->viewport()) {
        const QPoint panStart = blankViewportPosition(graphicsView);
        const QPoint panEnd(
            (std::min)(panStart.x() + 120,
                       graphicsView->viewport()->width() - 8),
            (std::min)(panStart.y() + 80,
                       graphicsView->viewport()->height() - 8));
        const QPoint viewportCenter = graphicsView->viewport()->rect().center();
        const QPointF sceneCenterBeforePan =
            graphicsView->mapToScene(viewportCenter);
        const int selectionCountBeforePan =
            graphicsScene ? graphicsScene->selectedItems().size() : -1;
        dragCanvasSelection(graphicsView, panStart, panEnd);
        application.processEvents();
        const QPointF sceneCenterAfterPan =
            graphicsView->mapToScene(viewportCenter);
        const QPointF panDelta =
            sceneCenterAfterPan - sceneCenterBeforePan;
        check(nodeEditor
                  && nodeEditor->canvasInteractionMode()
                      == finepaper::NocCanvasInteractionMode::Pan
                  && graphicsView->dragMode()
                      == QGraphicsView::ScrollHandDrag
                  && QPointF::dotProduct(panDelta, panDelta) > 1.0
                  && graphicsScene
                  && graphicsScene->selectedItems().size()
                         == selectionCountBeforePan,
              QStringLiteral(
                  "default Pan mode really moves the viewport without changing selection"));
        graphicsView->setFocus(Qt::OtherFocusReason);
        QKeyEvent shiftPress(
            QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        QApplication::sendEvent(graphicsView, &shiftPress);
        check(graphicsView->dragMode() == QGraphicsView::RubberBandDrag,
              QStringLiteral(
                  "holding Shift temporarily exposes box selection from Pan mode"));
        QKeyEvent shiftRelease(
            QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        QApplication::sendEvent(graphicsView, &shiftRelease);
        check(graphicsView->dragMode() == QGraphicsView::ScrollHandDrag,
              QStringLiteral(
                  "releasing Shift restores the persistent Pan interaction"));
    }
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
    if (animatedView && graphicsScene && router00) {
        graphicsScene->clearSelection();
        if (auto* router = graphicsScene->nodeGraphicsObject(*router00)) {
            router->setSelected(true);
            graphicsScene->nodeSelected(*router00);
        }
        application.processEvents();

        const std::unordered_set<QtNodes::NodeId> nodesBeforeBlockedCommands =
            graphicsScene->graphModel().allNodeIds();
        const std::unordered_set<QtNodes::ConnectionId>
            connectionsBeforeBlockedCommands = sceneConnectionIds(graphicsScene);
        const bool modifiedBeforeBlockedCommands = window.isWindowModified();
        bool blockedActionsInstalled = true;
        bool blockedActionsReported = true;
        QSet<QString> blockedActionNames;
        const bool canvasWindowActive = activateWindowForKeyboard(window);
        animatedView->setFocus(Qt::ShortcutFocusReason);
        application.processEvents();
        for (const finepaper::NocCanvasCommand command
             : finepaper::unavailableCanvasCommands) {
            const finepaper::UnavailableCanvasCommandPresentation presentation =
                finepaper::unavailableCanvasCommandPresentation(command);
            blockedActionNames.insert(presentation.actionObjectName);
            QAction* action = animatedView->findChild<QAction*>(
                presentation.actionObjectName, Qt::FindDirectChildrenOnly);
            blockedActionsInstalled = blockedActionsInstalled
                && action && animatedView->actions().contains(action)
                && action->shortcutContext() == Qt::WidgetShortcut
                && !action->shortcuts().isEmpty();
            if (action && !action->shortcuts().isEmpty()) {
                animatedView->setFocus(Qt::ShortcutFocusReason);
                window.statusBar()->clearMessage();
                if (canvasWindowActive) {
                    QTest::keySequence(
                        animatedView, action->shortcuts().constFirst());
                } else {
                    // Wayland may reject synthetic activation without a user
                    // token. The offscreen matrix exercises the actual key
                    // sequence; triggering here still verifies the native
                    // action-to-policy/status path.
                    action->trigger();
                }
                application.processEvents();
                const bool commandReported =
                    window.statusBar()->currentMessage()
                    == presentation.statusMessage;
                blockedActionsReported = blockedActionsReported
                    && commandReported;
                check(commandReported,
                      QStringLiteral(
                          "%1 shortcut reports the semantic canvas policy")
                          .arg(presentation.actionText));
            }
        }
        check(blockedActionsInstalled,
              QStringLiteral(
                  "canvas-local Copy, Paste, Duplicate, Undo, and Redo shortcuts are semantic no-op actions"));
        check(blockedActionsReported,
              QStringLiteral(
                  "unsupported canvas commands explain their semantic alternative in text"));

        bool activeCanvasShortcutsAreSemantic = true;
        for (QAction* action : animatedView->actions()) {
            if (!action || action == animatedView->clearSelectionAction()
                || action == animatedView->deleteSelectionAction()
                || action->shortcuts().isEmpty()) {
                continue;
            }
            activeCanvasShortcutsAreSemantic =
                activeCanvasShortcutsAreSemantic
                && blockedActionNames.contains(action->objectName())
                && action->shortcutContext() == Qt::WidgetShortcut;
        }
        check(activeCanvasShortcutsAreSemantic,
              QStringLiteral(
                  "every active canvas editing shortcut is an explicit semantic action"));

        animatedView->onCopySelectedObjects();
        animatedView->onPasteObjects();
        animatedView->onDuplicateSelectedObjects();
        application.processEvents();
        check(graphicsScene->graphModel().allNodeIds()
                      == nodesBeforeBlockedCommands
                  && sceneConnectionIds(graphicsScene)
                      == connectionsBeforeBlockedCommands
                  && window.isWindowModified()
                      == modifiedBeforeBlockedCommands
                  && !graphicsScene->undoStack().canUndo()
                  && !graphicsScene->undoStack().canRedo(),
              QStringLiteral(
                  "generic canvas command entry points cannot mutate design, projection, dirty state, or transient history"));

        const QtNodes::NodeId rejectedGenericNode =
            graphicsScene->graphModel().addNode(
                QStringLiteral("FinepaperNoCNode"));
        check(rejectedGenericNode == QtNodes::InvalidNodeId
                  && graphicsScene->graphModel().allNodeIds()
                      == nodesBeforeBlockedCommands,
              QStringLiteral(
                  "the graph model rejects generic node creation outside a semantic projection rebuild"));
        graphicsScene->clearSelection();
        application.processEvents();
    }
    if (graphicsView && graphicsScene) {
        graphicsScene->clearSelection();
        graphicsView->setFocus(Qt::TabFocusReason);
        QKeyEvent tabPress(
            QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(graphicsView, &tabPress);
        application.processEvents();
        check(graphicsScene->selectedItems().size() == 1,
              QStringLiteral(
                  "Tab keyboard navigation selects one semantic canvas item (%1 selected)")
                  .arg(graphicsScene->selectedItems().size()));
        graphicsScene->clearSelection();
        application.processEvents();
    }
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
        if (inspectorDock) {
            inspectorDock->raise();
        }
        const int windowHeightBeforeRouterSelection = window.height();
        auto* selectedRouter = graphicsScene->nodeGraphicsObject(*router00);
        auto* eastRouterObject = graphicsScene->nodeGraphicsObject(*router10);
        auto* southRouterObject = graphicsScene->nodeGraphicsObject(*router01);
        auto* diagonalRouterObject = graphicsScene->nodeGraphicsObject(*router11);
        check(selectedRouter && eastRouterObject && southRouterObject
                  && diagonalRouterObject,
              QStringLiteral(
                  "all Mesh Router graphics objects exist before selection tests"));
        const auto routerStackingIsStable = [=] {
            return selectedRouter && eastRouterObject && southRouterObject
                && diagonalRouterObject
                && selectedRouter->zValue() > 0.0
                && eastRouterObject->zValue() == selectedRouter->zValue()
                && southRouterObject->zValue() == selectedRouter->zValue()
                && diagonalRouterObject->zValue() == selectedRouter->zValue();
        };
        check(waitUntil(
                  routerStackingIsStable,
                  std::chrono::milliseconds(500)),
              QStringLiteral(
                  "every projected Router receives its semantic base stacking level"));
        if (selectedRouter) {
            selectedRouter->setSelected(true);
            graphicsScene->nodeSelected(*router00);
            application.processEvents();
        }
        check(endpointConfigurationGroup && elementConfigurationGroup
                  && !endpointConfigurationGroup->isVisible()
                  && !elementConfigurationGroup->isVisible()
                  && inspectorSelectionMetadata
                  && inspectorSelectionMetadata->text().contains(
                      QStringLiteral("Column 0 · Row 0"))
                  && !inspectorSelectionMetadata->text().contains(
                      QLatin1Char('%'))
                  && inspectorSelectionDetail
                  && inspectorSelectionDetail->text().contains(
                      QStringLiteral("formatVersion 3")),
              QStringLiteral(
                  "Router selection summarizes unavailable properties without an empty editor"));
        check(inspectorDesignSettingsContent
                  && !inspectorDesignSettingsContent->isVisible(),
              QStringLiteral(
                  "selecting a Router collapses design-wide settings"));
        check(window.height() == windowHeightBeforeRouterSelection
                  && window.minimumSizeHint().height() <= window.height(),
              QStringLiteral(
                  "revealing Router properties does not grow the top-level window (height %1, minimum hint %2)")
                  .arg(window.height())
                  .arg(window.minimumSizeHint().height()));
        QString routerInspectorCollision;
        check(inspectorScroll
                  && visibleSiblingLabelsDoNotOverlap(
                      inspectorScroll->widget(), &routerInspectorCollision),
              QStringLiteral(
                  "Router Inspector labels occupy distinct layout rows%1")
                  .arg(routerInspectorCollision.isEmpty()
                           ? QString()
                           : QStringLiteral(": ") + routerInspectorCollision));
        restoreRequestedClientSize();
        captureSmokeScreenshot(
            window, QStringLiteral("router-selected"), requestedTheme);
        check(eastRouterObject && southRouterObject
                  && eastRouterObject
                         ->data(finepaper::relatedHighlightDataRole).toBool()
                  && southRouterObject
                         ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("selecting a Router highlights directly connected Routers"));
        check(diagonalRouterObject
                  && !diagonalRouterObject
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
        auto* eastConnectionObject =
            graphicsScene->connectionGraphicsObject(eastLink);
        auto* southConnectionObject =
            graphicsScene->connectionGraphicsObject(southLink);
        check(eastConnectionObject && southConnectionObject
                  && eastConnectionObject
                         ->data(finepaper::relatedHighlightDataRole).toBool()
                  && southConnectionObject
                         ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("selecting a Router highlights all directly connected lines"));
        graphicsScene->clearSelection();
        application.processEvents();
        check(eastRouterObject
                  && !eastRouterObject
                          ->data(finepaper::relatedHighlightDataRole).toBool(),
              QStringLiteral("clearing selection clears connected-element highlights"));
        if (eastConnectionObject) {
            eastConnectionObject->setSelected(true);
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

    bool routerSpaceShortcutWorks = false;
    if (nodeEditor && graphicsView) {
        nodeEditor->selectElements(
            {{finepaper::ElementKind::Router, QStringLiteral("r-0-0")}});
        const bool collapsedBeforeSpace =
            nodeEditor->routerCollapsed(QStringLiteral("r-0-0"));
        QKeyEvent spacePress(
            QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
            QStringLiteral(" "));
        QApplication::sendEvent(graphicsView, &spacePress);
        application.processEvents();
        routerSpaceShortcutWorks = spacePress.isAccepted()
            && nodeEditor->routerCollapsed(QStringLiteral("r-0-0"))
                != collapsedBeforeSpace;
        QKeyEvent restoreSpacePress(
            QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
            QStringLiteral(" "));
        QApplication::sendEvent(graphicsView, &restoreSpacePress);
        application.processEvents();
        routerSpaceShortcutWorks = routerSpaceShortcutWorks
            && nodeEditor->routerCollapsed(QStringLiteral("r-0-0"))
                == collapsedBeforeSpace;
    }
    check(routerSpaceShortcutWorks,
          QStringLiteral(
              "Space toggles and restores the keyboard-selected Router"));

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
    const auto expandedSpacingRouter = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    if (graphicsScene && expandedSpacingRouter) {
        qreal minimumEndpointPortSpacing =
            (std::numeric_limits<qreal>::max)();
        std::optional<QPointF> previousEndpointPort = std::nullopt;
        for (unsigned int index = 0; index < 4U; ++index) {
            const QPointF port = graphicsScene->nodeGeometry().portPosition(
                *expandedSpacingRouter,
                QtNodes::PortType::In,
                finepaper::portIndex(finepaper::RouterInputPort::Endpoint)
                    + index);
            if (previousEndpointPort) {
                minimumEndpointPortSpacing = (std::min)(
                    minimumEndpointPortSpacing,
                    QLineF(*previousEndpointPort, port).length());
            }
            previousEndpointPort = port;
        }
        check(minimumEndpointPortSpacing >= 27.5,
              QStringLiteral(
                  "expanded Router Endpoint ports keep non-overlapping labels and hit targets"));
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
    check(graphicsScene && !graphicsScene->undoStack().canUndo()
              && !graphicsScene->undoStack().canRedo(),
          QStringLiteral(
              "a Router projection rebuild discards QtNodes history containing transient NodeIds"));
    auto* shortcutTextInput = window.findChild<QLineEdit*>(
        QStringLiteral("finepaper.outputRoot"));
    QString shortcutTextBeforeTyping;
    bool shortcutTextAcceptedInput = false;
    bool textUndoStayedLocal = false;
    if (shortcutTextInput) {
        shortcutTextInput->setFocus();
        shortcutTextBeforeTyping = shortcutTextInput->text();
        shortcutTextInput->setCursorPosition(shortcutTextInput->text().size());
        QKeyEvent pressR(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
        QKeyEvent releaseR(QEvent::KeyRelease, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
        QApplication::sendEvent(shortcutTextInput, &pressR);
        QApplication::sendEvent(shortcutTextInput, &releaseR);
        application.processEvents();
        shortcutTextAcceptedInput =
            shortcutTextInput->text() == shortcutTextBeforeTyping + QLatin1Char('r');
        QKeyEvent undoPress(
            QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        QKeyEvent undoRelease(
            QEvent::KeyRelease, Qt::Key_Z, Qt::ControlModifier);
        QApplication::sendEvent(shortcutTextInput, &undoPress);
        QApplication::sendEvent(shortcutTextInput, &undoRelease);
        application.processEvents();
        textUndoStayedLocal =
            shortcutTextInput->text() == shortcutTextBeforeTyping;
    }
    const std::optional<QPointF> routerAfterTextInput = nodeEditor
        ? nodeEditor->routerVisualPosition(QStringLiteral("r-0-0")) : std::nullopt;
    check(shortcutTextInput && shortcutTextAcceptedInput
              && routerAfterTextInput
              && QLineF(*routerAfterTextInput, movedRouterPosition).length() < 1.0,
          QStringLiteral("single-key canvas shortcuts do not fire while editing text"));
    check(textUndoStayedLocal,
          QStringLiteral(
              "Ctrl+Z remains available to a focused text field instead of reaching the canvas"));
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
    auto* selectionInspector = window.findChild<QWidget*>(
        finepaper::workbench::selectionInspectorName);
    auto* inspectorContextActions = window.findChild<QWidget*>(
        QStringLiteral("finepaper.inspectorContextActions"));
    const QList<QPushButton*> selectionCardButtons = selectionInspector
        ? selectionInspector->findChildren<QPushButton*>()
        : QList<QPushButton*>{};
    const QList<QPushButton*> contextButtons = inspectorContextActions
        ? inspectorContextActions->findChildren<QPushButton*>()
        : QList<QPushButton*>{};
    QSet<QString> contextButtonNames;
    for (const QPushButton* button : contextButtons) {
        if (button) {
            contextButtonNames.insert(button->objectName());
        }
    }
    const QSet<QString> expectedContextButtonNames = {
        QStringLiteral("finepaper.inspectorEditDomainAssignments"),
        QStringLiteral("finepaper.inspectorReviewDiagnostics"),
    };
    check(selectionInspector && selectionCardButtons.isEmpty()
              && inspectorContextActions
              && contextButtons.size() == expectedContextButtonNames.size()
              && contextButtonNames == expectedContextButtonNames,
          QStringLiteral(
              "selection Inspector exposes task navigation without duplicating connect or delete controls"));
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
    auto* newlyCreatedEndpoint = attachedEndpoint && graphicsScene
        ? graphicsScene->nodeGraphicsObject(*attachedEndpoint) : nullptr;
    auto* currentRouterObject = currentRouter00 && graphicsScene
        ? graphicsScene->nodeGraphicsObject(*currentRouter00) : nullptr;
    const QRect newlyCreatedEndpointViewport = newlyCreatedEndpoint && graphicsView
        ? graphicsView->mapFromScene(
              newlyCreatedEndpoint->sceneBoundingRect()).boundingRect()
        : QRect{};
    check(newlyCreatedEndpoint && newlyCreatedEndpoint->isSelected()
              && graphicsView && graphicsView->viewport()->rect().intersects(
                     newlyCreatedEndpointViewport),
          QStringLiteral(
              "a directly created Endpoint becomes selected and visible on the canvas"));
    check(newlyCreatedEndpoint && currentRouterObject
              && newlyCreatedEndpoint->zValue()
                  > currentRouterObject->zValue(),
          QStringLiteral(
              "every projected Endpoint remains above Routers before hit testing"));
    if (attachedEndpoint && graphicsScene) {
        const int windowHeightBeforeEndpointSelection = window.height();
        graphicsScene->clearSelection();
        application.processEvents();
        if (inspectorScroll) {
            inspectorScroll->verticalScrollBar()->setValue(
                inspectorScroll->verticalScrollBar()->maximum());
        }
        auto* endpointObject =
            graphicsScene->nodeGraphicsObject(*attachedEndpoint);
        check(endpointObject,
              QStringLiteral(
                  "the attached Endpoint has a graphics object before Inspector selection"));
        if (endpointObject) {
            endpointObject->setSelected(true);
            graphicsScene->nodeSelected(*attachedEndpoint);
            application.processEvents();
        }
        check(endpointConfigurationGroup && elementConfigurationGroup
                  && endpointConfigurationGroup->isVisible()
                  && !elementConfigurationGroup->isVisible(),
              QStringLiteral(
                  "Endpoint selection progressively discloses only Endpoint-owned properties"));
        check(inspectorDesignSettingsContent
                  && !inspectorDesignSettingsContent->isVisible(),
              QStringLiteral(
                  "Endpoint properties take priority while design settings stay collapsed"));
        auto* endpointTypeEditor = window.findChild<QComboBox*>(
            QStringLiteral("finepaper.endpointConfiguration.type"));
        auto* firstEndpointParameter = endpointConfigurationGroup
            ? endpointConfigurationGroup->findChild<QComboBox*>(
                  QStringLiteral("finepaper.schemaValue.protocol.scalar.choice"))
            : nullptr;
        check(inspectorScroll && endpointTypeEditor
                  && inspectorScroll->verticalScrollBar()->value() == 0
                  && widgetIntersectsScrollViewport(
                      inspectorScroll, endpointTypeEditor),
              QStringLiteral(
              "a new Endpoint selection returns the Inspector to its visible primary editor"));
        check(inspectorScroll && firstEndpointParameter
                  && widgetIsFullyVisibleInScrollViewport(
                      inspectorScroll, firstEndpointParameter),
              QStringLiteral(
                  "the first Endpoint parameter is fully visible at the top of the selected Inspector task"));
        if (inspectorNavigation && inspectorDock) {
            window.activateWindow();
            application.processEvents();
            inspectorDock->hide();
            inspectorNavigation->trigger();
            application.processEvents();
            application.processEvents();
        }
        check(inspectorNavigation && inspectorDock
                  && inspectorDock->isVisible()
                  && focusIsWithin(endpointTypeEditor),
              QStringLiteral(
                  "Inspector navigation focuses the active Endpoint editor "
                  "(actual %1/%2)")
                  .arg(QApplication::focusWidget()
                           ? QApplication::focusWidget()->objectName()
                           : QStringLiteral("none"),
                       QApplication::focusWidget()
                           ? QString::fromUtf8(
                                 QApplication::focusWidget()
                                     ->metaObject()->className())
                           : QStringLiteral("none")));
        const bool inspectorContentFits = inspectorScroll
            && inspectorScroll->widget()
            && inspectorScroll->widget()->height()
                <= inspectorScroll->viewport()->height();
        const bool inspectorContentScrolls = inspectorScroll
            && inspectorScroll->verticalScrollBar()->maximum() > 0;
        check(window.height() == windowHeightBeforeEndpointSelection
                  && window.minimumSizeHint().height() <= window.height()
                  && (inspectorContentFits || inspectorContentScrolls),
              QStringLiteral(
                  "Endpoint properties stay reachable by Inspector scrolling without growing the window (height %1, minimum hint %2)")
                  .arg(window.height())
                  .arg(window.minimumSizeHint().height()));
        QString endpointInspectorCollision;
        check(inspectorScroll
                  && visibleSiblingLabelsDoNotOverlap(
                      inspectorScroll->widget(), &endpointInspectorCollision),
              QStringLiteral(
                  "Endpoint Inspector labels occupy distinct layout rows%1")
                  .arg(endpointInspectorCollision.isEmpty()
                           ? QString()
                           : QStringLiteral(": ")
                               + endpointInspectorCollision));
        check(!requestedWindowSize.isValid()
                  || requestedWindowSize != QSize(1280, 720)
                  || (inspectorScroll
                      && inspectorScroll->horizontalScrollBar()->maximum() == 0),
              QStringLiteral(
                  "at 1280x720 Endpoint properties remain within the Inspector width"));
        restoreRequestedClientSize();
        captureSmokeScreenshot(
            window, QStringLiteral("endpoint-selected"), requestedTheme);
    }
    const auto firstAttachmentPort = attachedEndpoint
        ? attachmentPortForEndpoint(graphicsScene, *attachedEndpoint) : std::nullopt;

    const auto routerForSecondDrop = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    auto* routerForSecondDropGraphics = graphicsScene && routerForSecondDrop
        ? graphicsScene->nodeGraphicsObject(*routerForSecondDrop) : nullptr;
    check(routerForSecondDropGraphics,
          QStringLiteral("target Router is projected before the second Endpoint drop"));
    if (graphicsView && routerForSecondDropGraphics && endpointMime) {
        const QPoint dropPosition = graphicsView->mapFromScene(
            routerForSecondDropGraphics->sceneBoundingRect().center());
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
    auto* secondAttachedGraphics = nodeGraphicsWithCaptionPrefix(
        graphicsScene, QStringLiteral("master_1"));
    const std::optional<QtNodes::NodeId> secondAttachedEndpoint =
        secondAttachedGraphics
        ? std::optional<QtNodes::NodeId>(secondAttachedGraphics->nodeId())
        : std::nullopt;
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
    check(secondAttachedGraphics,
          QStringLiteral("second attached Endpoint has a projected graphics object"));
    if (graphicsView && secondAttachedGraphics) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            secondAttachedGraphics->sceneBoundingRect().center());
        sendContextMenu(graphicsView, endpointPosition);
        application.processEvents();
    }
    auto* secondEndpointMenu = nodeEditor ? nodeEditor->findChild<QMenu*>(
        finepaper::workbench::endpointContextMenuName) : nullptr;
    QAction* deleteSecondEndpoint = secondEndpointMenu ? secondEndpointMenu->findChild<QAction*>(
        finepaper::workbench::deleteEndpointActionName) : nullptr;
    if (deleteSecondEndpoint) {
        QPointer<QMenu> menuGuard(secondEndpointMenu);
        chooseMessageBoxButton(QMessageBox::Yes);
        deleteSecondEndpoint->trigger();
        if (menuGuard) {
            menuGuard->close();
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
    auto* currentRouter00AfterSecondGraphics =
        graphicsScene && currentRouter00AfterSecond
        ? graphicsScene->nodeGraphicsObject(*currentRouter00AfterSecond)
        : nullptr;
    check(currentRouter00AfterSecondGraphics,
          QStringLiteral("Router remains projected after deleting an Endpoint"));
    if (graphicsView && currentRouter00AfterSecondGraphics) {
        const QPoint routerPosition = graphicsView->mapFromScene(
            currentRouter00AfterSecondGraphics->sceneBoundingRect().center());
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
    auto* attachedEndpointForMenuGraphics =
        graphicsScene && attachedEndpointForMenu
        ? graphicsScene->nodeGraphicsObject(*attachedEndpointForMenu)
        : nullptr;
    check(attachedEndpointForMenuGraphics,
          QStringLiteral("remaining Endpoint stays projected for its context menu"));
    if (graphicsView && attachedEndpointForMenuGraphics) {
        const QPoint endpointPosition = graphicsView->mapFromScene(
            attachedEndpointForMenuGraphics->sceneBoundingRect().center());
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
        QPointer<QMenu> menuGuard(endpointMenu);
        chooseMessageBoxButton(QMessageBox::Yes);
        deleteEndpoint->trigger();
        if (menuGuard) {
            menuGuard->close();
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
        graphicsScene, QStringLiteral("New Endpoint draft\nMaster endpoint"));
    const finepaper::EndpointCanvasDraftState pendingDraftState = nodeEditor
        ? nodeEditor->endpointCanvasDraftState()
        : finepaper::EndpointCanvasDraftState{};
    auto* endpointDraftNotice = nodeEditor
        ? nodeEditor->findChild<QLabel*>(
              QStringLiteral("finepaper.endpointCanvasDraftNotice"))
        : nullptr;
    auto* endpointDraftInspectorTitle = window.findChild<QLabel*>(
        QStringLiteral("finepaper.inspectorSelectionTitle"));
    check(pendingEndpoint
              && graphicsScene->graphModel().allNodeIds().size()
                     == nodesBeforePending + 1
              && pendingDraftState.pendingNewCount() == 1
              && pendingDraftState.detachedCount() == 0
              && endpointDraftNotice && endpointDraftNotice->isVisible()
              && endpointDraftNotice->text().contains(
                  QStringLiteral("not in the design"))
              && endpointDraftInspectorTitle
              && endpointDraftInspectorTitle->text()
                     == QStringLiteral("Endpoint draft")
              && inspectorSelectionMetadata
              && inspectorSelectionMetadata->text().contains(
                  QStringLiteral("New · Not in design")),
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
                  QStringLiteral("New Endpoint draft\nMaster endpoint")),
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
        static_cast<void>(nodeEditor->regularizeLayout());
        application.processEvents();
        application.processEvents();
    }
    const auto pendingAfterProjectionRebuild = nodeIdWithCaptionPrefix(
        graphicsScene, QStringLiteral("New Endpoint draft\nMaster endpoint"));
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
               graphicsScene, QStringLiteral("New Endpoint draft\nMaster endpoint")),
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
    check(graphicsScene && !graphicsScene->undoStack().canUndo()
              && !graphicsScene->undoStack().canRedo(),
          QStringLiteral(
              "persisting a freely moved Endpoint discards projection-only move history"));
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
          QStringLiteral("Design menu exposes a topology regularization action"));
    if (regularizeAction) {
        chooseMessageBoxButton(QMessageBox::Yes);
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

    // The interaction sequence above deliberately moves and stacks several
    // nodes. Re-establish visible blank canvas before testing the blank-area
    // context menu, especially at the compact 1280x720 matrix size.
    if (nodeEditor) {
        nodeEditor->zoomToFit();
        application.processEvents();
    }
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
        graphicsScene, QStringLiteral("New Endpoint draft\nMaster endpoint"));
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
        graphicsScene, QStringLiteral("New Endpoint draft\nMaster endpoint"));
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
    std::optional<QPoint> attachmentLinePoint = std::nullopt;
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
        graphicsScene, QStringLiteral("Disconnected Endpoint\n"));
    const finepaper::EndpointCanvasDraftState detachedDraftState = nodeEditor
        ? nodeEditor->endpointCanvasDraftState()
        : finepaper::EndpointCanvasDraftState{};
    check(!endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-1"))
              && detachedBodyEndpoint
              && nodeEditor
              && detachedDraftState.pendingNewCount() == 0
              && detachedDraftState.detachedCount() == 1
              && nodeEditor->endpointCanvasDraftState().detachedEndpointIds()
                     == QStringList{exposedEndpointId}
              && endpointDraftNotice && endpointDraftNotice->isVisible()
              && endpointDraftNotice->text().contains(exposedEndpointId)
              && endpointDraftInspectorTitle
              && endpointDraftInspectorTitle->text()
                     == QStringLiteral("Endpoint ") + exposedEndpointId
              && inspectorSelectionMetadata
              && inspectorSelectionMetadata->text().contains(
                  QStringLiteral("Disconnected from")),
          QStringLiteral("line-menu Disconnect removes the attachment and keeps the Endpoint draft"));
    check(saveAsAction && !saveAsAction->isEnabled()
              && initialValidateAction && !initialValidateAction->isEnabled(),
          QStringLiteral(
              "detached Endpoint drafts visibly pause Save As and Validate before invocation"));
    bool sawEndpointCanvasDraftBlocker = false;
    {
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(
                QApplication::activeModalWidget());
            auto* messageBox = qobject_cast<QMessageBox*>(dialog);
            sawEndpointCanvasDraftBlocker = messageBox
                && messageBox->objectName()
                    == QStringLiteral(
                        "finepaper.endpointCanvasDraftBlocker");
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
        const bool saveAsAllowed =
            finepaper::FinepaperMainWindowSmokeAccess::
                endpointCanvasDraftOperationAllowed(
                    window, QStringLiteral("Save As"));
        check(!saveAsAllowed,
              QStringLiteral(
                  "the Save As defensive boundary rejects detached Endpoint drafts"));
        application.processEvents();
    }
    check(sawEndpointCanvasDraftBlocker
              && window.isWindowModified()
              && nodeEditor
              && nodeEditor->endpointCanvasDraftState().detachedEndpointIds()
                     == QStringList{exposedEndpointId},
          QStringLiteral("saving is blocked while a durable Endpoint survives only as a detached draft"));
    bool sawDetachedValidationBlocker = false;
    {
        QTimer::singleShot(0, [&] {
            auto* messageBox = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            sawDetachedValidationBlocker = messageBox
                && messageBox->objectName()
                    == QStringLiteral("finepaper.endpointCanvasDraftBlocker")
                && messageBox->text().startsWith(
                    QStringLiteral("Validate"));
            if (messageBox) {
                messageBox->accept();
            }
        });
        const bool validationAllowed =
            finepaper::FinepaperMainWindowSmokeAccess::
                endpointCanvasDraftOperationAllowed(
                    window, QStringLiteral("Validate"));
        check(!validationAllowed,
              QStringLiteral(
                  "the Validate defensive boundary rejects detached Endpoint drafts"));
        application.processEvents();
    }
    check(sawDetachedValidationBlocker && !window.operationBusy()
              && nodeEditor
              && nodeEditor->endpointCanvasDraftState().detachedCount() == 1,
          QStringLiteral(
              "validation cannot silently omit a disconnected durable Endpoint"));

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
    check(nodeEditor
              && nodeEditor->endpointCanvasDraftState().detachedEndpointIds()
                     .isEmpty(),
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
        graphicsScene, QStringLiteral("Disconnected Endpoint\n"));
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
        QPointer<QMenu> menuGuard(detachedEndpointMenu);
        chooseMessageBoxButton(QMessageBox::Yes);
        deleteDetachedEndpoint->trigger();
        if (menuGuard) {
            menuGuard->close();
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
               graphicsScene, QStringLiteral("Disconnected Endpoint\n"))
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
        endpointPalette->itemActivated(endpointPalette->item(0));
        application.processEvents();
    }
    check(nodeIdWithCaptionPrefix(graphicsScene, QStringLiteral("master")).has_value(),
          QStringLiteral(
              "selected-Router keyboard/item activation remains available as an attach shortcut"));
    check(endpointAttachedToRouter(graphicsScene, QStringLiteral("r-0-0")).has_value(),
          QStringLiteral("selected-Router shortcut attaches specifically to that Router"));

    const auto expandedRouter00 = nodeIdWithCaption(
        graphicsScene, QStringLiteral("r-0-0"));
    const QSize expandedRouterSize = graphicsScene && expandedRouter00
        ? graphicsScene->nodeGeometry().size(*expandedRouter00)
        : QSize{};
    check(nodeEditor && nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), true),
          QStringLiteral("Router can be collapsed from the workspace"));
    const auto collapsedRouter00 = nodeIdWithCaption(graphicsScene, QStringLiteral("r-0-0"));
    check(nodeEditor && nodeEditor->routerCollapsed(QStringLiteral("r-0-0")),
          QStringLiteral("Router collapsed state is tracked independently of NocDesign"));
    if (graphicsScene && collapsedRouter00) {
        const QSize collapsedSize = graphicsScene->nodeGeometry().size(*collapsedRouter00);
        check(collapsedSize.width() == collapsedSize.height()
                  && expandedRouterSize.isValid()
                  && collapsedSize.width() < expandedRouterSize.width(),
              QStringLiteral(
                  "collapsed Router remains square and becomes compact "
                  "(collapsed %1 × %2, expanded %3 × %4)")
                  .arg(collapsedSize.width())
                  .arg(collapsedSize.height())
                  .arg(expandedRouterSize.width())
                  .arg(expandedRouterSize.height()));
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
    auto* diagnosticsStatus = window.findChild<QLabel*>(
        QStringLiteral("finepaper.diagnosticsStatus"));
    auto* diagnosticsResultsPage = window.findChild<QWidget*>(
        QStringLiteral("finepaper.results.diagnosticsPage"));
    auto* problemReportStatus = window.findChild<QLabel*>(
        QStringLiteral("finepaper.problemReportStatus"));
    check(diagnosticsStatus
              && diagnosticsStatus->text().contains(
                  QStringLiteral("Current result"))
              && diagnosticsStatus->text().contains(
                  QString::fromUtf8("“") + provenanceDesignName
                  + QString::fromUtf8("”"))
              && diagnosticsStatus->textFormat() == Qt::PlainText
              && problemReportStatus
              && problemReportStatus->text().contains(
                  QStringLiteral("Current result"))
              && problemReportStatus->textFormat() == Qt::PlainText,
          QStringLiteral(
              "validation publishes literal plain-text provenance for arbitrary design names"));

    auto* reviewDiagnostics = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.inspectorReviewDiagnostics"));
    window.activateWindow();
    application.processEvents();
    if (inspectorNavigation) {
        inspectorNavigation->trigger();
        application.processEvents();
        application.processEvents();
    }
    const bool reviewDiagnosticsWasVisible = reviewDiagnostics
        && reviewDiagnostics->isVisibleTo(&window)
        && widgetIntersectsScrollViewport(
            inspectorScroll, reviewDiagnostics);
    captureSmokeScreenshot(
        window, QStringLiteral("diagnostics-task-route"), requestedTheme);
    if (reviewDiagnostics) {
        reviewDiagnostics->click();
        application.processEvents();
        application.processEvents();
    }
    check(reviewDiagnosticsWasVisible && resultsDock
              && resultsDock->isVisible() && resultTabs
              && resultTabs->currentWidget() == diagnosticsResultsPage
              && (focusIsWithin(diagnosticsStatus)
                  || focusIsWithin(initialDrcTable)),
          QStringLiteral(
              "Inspector exposes a visible diagnostics route and focuses the current DRC result "
              "(visible %1; dock %2; tab %3; focus %4)")
              .arg(reviewDiagnosticsWasVisible)
              .arg(resultsDock && resultsDock->isVisible())
              .arg(resultTabs ? resultTabs->currentIndex() : -1)
              .arg(QApplication::focusWidget()
                       ? QApplication::focusWidget()->objectName()
                       : QStringLiteral("none")));

    auto* outputPath = window.findChild<QLineEdit*>(QStringLiteral("finepaper.outputRoot"));
    auto* browseOutput = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.browseOutputRoot"));
    auto* generateOutput = window.findChild<QPushButton*>(
        QStringLiteral("finepaper.generateButton"));
    auto* generationControls = window.findChild<QWidget*>(
        QStringLiteral("finepaper.generationControls"));
    QAction* generateAction = actionWithText(window, QStringLiteral("Generate RTL"));
    check(generateAction != nullptr, QStringLiteral("shared RTL generation action is available"));
    if (outputPath) {
        outputPath->clear();
        application.processEvents();
        check(generateAction && !generateAction->isEnabled()
                  && generateOutput && !generateOutput->isEnabled()
                  && generateOutput->toolTip().contains(
                      QStringLiteral("Choose an output root")),
              QStringLiteral(
                  "blank output roots disable generation with inline recovery guidance"));
        outputPath->setText(outputRoot.path());
    }
    application.processEvents();
    application.processEvents();
    const QSize windowSizeBeforeResultsResize = window.size();
    const int automaticResultsHeight = resultsDock
        ? resultsDock->height() : 0;
    const int userRequestedResultsHeight = automaticResultsHeight
        + 3 * window.fontMetrics().lineSpacing();
    if (resultsDock) {
        window.resizeDocks(
            {resultsDock}, {userRequestedResultsHeight}, Qt::Vertical);
        application.processEvents();
    }
    const int userResultsHeight = resultsDock
        ? resultsDock->height() : 0;
    if (resultsDock) {
        resultsDock->hide();
    }
    if (generateAction) {
        generateAction->trigger();
        check(waitUntil([&window] { return !window.operationBusy(); }),
              QStringLiteral("GUI generation finishes asynchronously without blocking the event loop"));
    }
    application.processEvents();
    application.processEvents();
    const int restoredResultsDelta = resultsDock
        ? resultsDock->height() - userResultsHeight : -1;
    check(resultsDock && resultsDock->isVisible()
              && userResultsHeight
                  >= automaticResultsHeight
                      + window.fontMetrics().lineSpacing()
              && restoredResultsDelta >= -2
              && restoredResultsDelta <= 2
              && window.size() == windowSizeBeforeResultsResize,
          QStringLiteral(
              "automatic Results reopening preserves a useful user height "
              "without resizing the window (before %1, user %2, restored %3)")
              .arg(automaticResultsHeight)
              .arg(userResultsHeight)
              .arg(resultsDock ? resultsDock->height() : -1));
    if (resultsDock) {
        window.resizeDocks(
            {resultsDock}, {automaticResultsHeight}, Qt::Vertical);
        application.processEvents();
    }

    auto* generationStatus = window.findChild<QLabel*>(
        QStringLiteral("finepaper.generationStatus"));
    check(generationStatus
              && generationStatus->text().contains(
                  QStringLiteral("Current artifacts"))
              && generationStatus->text().contains(
                  QString::fromUtf8("“") + provenanceDesignName
                  + QString::fromUtf8("”"))
              && generationStatus->textFormat() == Qt::PlainText,
          QStringLiteral(
              "generation publishes literal plain-text provenance for arbitrary design names"));

    auto* artifacts = window.findChild<QTableWidget*>(QStringLiteral("finepaper.artifactTable"));
    check(artifacts && artifacts->rowCount() > 0,
          QStringLiteral("GUI generation flow reports real RTL artifacts"));
    if (artifacts && artifacts->rowCount() > 0) {
        const QString artifactPath = artifacts->item(0, 2)->text();
        check(!artifactPath.isEmpty(), QStringLiteral("reported GUI artifact has a path"));
    }
    if (resultsNavigation && resultsDock) {
        window.activateWindow();
        application.processEvents();
        resultsDock->hide();
        resultsNavigation->trigger();
        application.processEvents();
        application.processEvents();
    }
    check(resultsNavigation && resultsDock && artifacts
              && resultsDock->isVisible()
              && artifacts->rowCount() > 0
              && focusIsWithin(artifacts),
          QStringLiteral(
              "Results navigation focuses generated Artifacts on the active tab "
              "(actual %1/%2)")
              .arg(QApplication::focusWidget()
                       ? QApplication::focusWidget()->objectName()
                       : QStringLiteral("none"),
                   QApplication::focusWidget()
                       ? QString::fromUtf8(
                             QApplication::focusWidget()
                                 ->metaObject()->className())
                       : QStringLiteral("none")));

    const bool movedAfterResults = nodeEditor && nodeEditor->endpointMoveRequested
        ? nodeEditor->endpointMoveRequested(
              QStringLiteral("master_0"),
              finepaper::NocAttachmentTarget{
                  finepaper::RouterPosition{1, 0}, std::nullopt})
        : false;
    application.processEvents();
    auto* resultTabsAfterMutation = window.findChild<QTabWidget*>(
        QStringLiteral("finepaper.resultTabs"));
    check(movedAfterResults && diagnosticsStatus && generationStatus
              && diagnosticsStatus->text().contains(
                  QStringLiteral("Out of date"))
              && generationStatus->text().contains(
                  QStringLiteral("Out of date"))
              && diagnosticsStatus->text().contains(
                  QString::fromUtf8("“") + provenanceDesignName
                  + QString::fromUtf8("”"))
              && generationStatus->text().contains(
                  QString::fromUtf8("“") + provenanceDesignName
                  + QString::fromUtf8("”"))
              && generationStatus->accessibleDescription().contains(
                  QStringLiteral("these artifacts were generated for"))
              && generationStatus->accessibleDescription().contains(
                  QStringLiteral("before delivery"))
              && resultTabsAfterMutation
              && resultTabsAfterMutation->tabText(0).contains(
                  QStringLiteral("out of date"))
              && resultTabsAfterMutation->tabText(2).contains(
                  QStringLiteral("out of date")),
          QStringLiteral(
              "a durable design mutation keeps prior results visible but labels them out of date"));
    restoreRequestedClientSize();
    application.processEvents();
    QWidget* generationPage = resultTabsAfterMutation
        ? resultTabsAfterMutation->currentWidget() : nullptr;
    const int requiredArtifactViewportHeight = artifacts
        ? artifacts->verticalHeader()->defaultSectionSize() : 0;
    check(generationPage && generationStatus && generationControls
              && outputPath && browseOutput && generateOutput
              && widgetIsFullyVisibleWithin(
                  generationPage, generationStatus)
              && generationStatus->height()
                  >= generationStatus->heightForWidth(
                      generationStatus->width())
              && widgetIsFullyVisibleWithin(
                  generationPage, generationControls)
              && widgetIsFullyVisibleWithin(
                  generationPage, outputPath)
              && widgetIsFullyVisibleWithin(
                  generationPage, browseOutput)
              && widgetIsFullyVisibleWithin(
                  generationPage, generateOutput)
              && artifacts
              && widgetIsFullyVisibleWithin(generationPage, artifacts)
              && artifacts->viewport()->height()
                  >= requiredArtifactViewportHeight,
          QStringLiteral(
              "Generation status, text actions, and at least one Artifact row "
              "remain visible (page %1; status %2/%3; controls %4; table %5; "
              "header %6; viewport %7; row %8; dock %9; center %10)")
              .arg(generationPage ? generationPage->height() : -1)
              .arg(generationStatus ? generationStatus->height() : -1)
              .arg(generationStatus
                       ? generationStatus->heightForWidth(
                             generationStatus->width()) : -1)
              .arg(generationControls ? generationControls->height() : -1)
              .arg(artifacts ? artifacts->height() : -1)
              .arg(artifacts
                       ? artifacts->horizontalHeader()->height() : -1)
              .arg(artifacts ? artifacts->viewport()->height() : -1)
              .arg(requiredArtifactViewportHeight)
              .arg(resultsDock ? resultsDock->height() : -1)
              .arg(centerViews ? centerViews->height() : -1));
    check(graphicsView
              && graphicsView->viewport()->height()
                  >= finepaper::workbench::minimumCanvasTextLines
                      * window.fontMetrics().lineSpacing(),
          QStringLiteral(
              "the readable Results panel keeps a useful canvas viewport "
              "(viewport %1; required %2; center %3; dock %4)")
              .arg(graphicsView
                       ? graphicsView->viewport()->height() : -1)
              .arg(finepaper::workbench::minimumCanvasTextLines
                   * window.fontMetrics().lineSpacing())
              .arg(centerViews ? centerViews->height() : -1)
              .arg(resultsDock ? resultsDock->height() : -1));
    check(!requestedWindowSize.isValid()
              || window.size() == requestedWindowSize,
          QStringLiteral(
              "opening Results does not grow the requested client size (%1x%2)")
              .arg(window.width())
              .arg(window.height()));
    captureSmokeScreenshot(
        window, QStringLiteral("results-out-of-date"), requestedTheme);

    const QString blockedGenerationRoot = outputRoot.filePath(
        QStringLiteral("blocked-generation-root"));
    QFile blockedGenerationFile(blockedGenerationRoot);
    const bool blockedGenerationFixtureReady =
        blockedGenerationFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && blockedGenerationFile.write("not a directory") > 0;
    blockedGenerationFile.close();
    if (outputPath) {
        outputPath->setText(blockedGenerationRoot);
    }
    if (generateAction) {
        generateAction->trigger();
        check(waitUntil([&window] { return !window.operationBusy(); }),
              QStringLiteral(
                  "a generation failure returns control to the workbench"));
    }
    check(blockedGenerationFixtureReady && generationStatus
              && generationStatus->text().contains(
                  QStringLiteral("Current generation attempt"))
              && generationStatus->text().contains(
                  QStringLiteral("failed"), Qt::CaseInsensitive)
              && generationStatus->property("finepaperRole").toString()
                  == QStringLiteral("error"),
          QStringLiteral(
              "a failed generation attempt is published as an error, not as artifacts"));

    QAction* resultReloadPackagesAction = actionWithText(
        window, QStringLiteral("Reload Packages"));
    if (resultReloadPackagesAction) {
        resultReloadPackagesAction->trigger();
        application.processEvents();
    }
    check(resultReloadPackagesAction && generationStatus
              && generationStatus->text().contains(
                  QStringLiteral("Out of date"))
              && generationStatus->text().contains(
                  QStringLiteral("failed generation attempt"))
              && !generationStatus->text().contains(
                  QStringLiteral("artifacts were generated"))
              && generationStatus->property("finepaperRole").toString()
                  == QStringLiteral("error"),
          QStringLiteral(
              "Package reload safely preserves a failed attempt's error provenance without inventing artifacts"));
    if (outputPath) {
        outputPath->setText(outputRoot.path());
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

    std::optional<QtNodes::ConnectionId> semanticRouterLink = std::nullopt;
    std::optional<QtNodes::ConnectionId> semanticAttachment = std::nullopt;
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
    auto* semanticInspectorGroup = window.findChild<QWidget*>(
        finepaper::workbench::selectionInspectorName);
    auto* semanticInspector = semanticInspectorGroup
        ? semanticInspectorGroup->findChild<QLabel*>(
              QStringLiteral("finepaper.inspectorSelectionMetadata"))
        : nullptr;
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
    if (centerViews) {
        centerViews->setCurrentIndex(1);
        application.processEvents();
    }
    if (canvasFocusAction) {
        canvasFocusAction->trigger();
        application.processEvents();
    }
    const bool focusBlockedOutsideEditor = canvasFocusAction
        && !canvasFocusAction->isChecked();
    if (centerViews) {
        centerViews->setCurrentIndex(0);
        application.processEvents();
    }
    if (canvasFocusAction) {
        canvasFocusAction->trigger();
        application.processEvents();
    }
    check(focusBlockedOutsideEditor
              && canvasFocusAction && canvasFocusAction->isChecked(),
          QStringLiteral(
              "Canvas Focus enters only from Editor and can remain active while "
              "the workbench session closes"));
    if (nodeEditor) {
        nodeEditor->setRouterCollapsed(QStringLiteral("r-0-0"), true);
    }
    auto* activityResultPage = window.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.activityLog"));
    if (resultTabs && activityResultPage) {
        resultTabs->setCurrentWidget(activityResultPage);
    }
    closeDiscarding(window);

    check(QSettings().value(finepaper::workbench::resultTabSetting).toString()
              == QStringLiteral("finepaper.activityLog"),
          QStringLiteral(
              "the active Results page persists by stable semantic id instead of tab index"));

    finepaper::FinepaperMainWindow restoredWindow(locations);
    restoredWindow.show();
    application.processEvents();
    check(!maximizeWindow || restoredWindow.isMaximized(),
          QStringLiteral(
              "maximized state survives saveGeometry/restoreGeometry across workbench sessions"));
    auto* restoredPackageDock = restoredWindow.findChild<QDockWidget*>(
        finepaper::workbench::packageDockName);
    auto* restoredInspectorDock = restoredWindow.findChild<QDockWidget*>(
        finepaper::workbench::inspectorDockName);
    auto* restoredDomainDock = restoredWindow.findChild<QDockWidget*>(
        finepaper::workbench::domainManagerDockName);
    auto* restoredResultsDock = restoredWindow.findChild<QDockWidget*>(
        finepaper::workbench::resultsDockName);
    auto* restoredCenterViews = qobject_cast<QTabWidget*>(
        restoredWindow.centralWidget());
    auto* restoredResultTabs = restoredWindow.findChild<QTabWidget*>(
        QStringLiteral("finepaper.resultTabs"));
    auto* restoredActivityResultPage = restoredWindow.findChild<QPlainTextEdit*>(
        QStringLiteral("finepaper.activityLog"));
    check(restoredPackageDock && !restoredPackageDock->isVisible()
              && ((restoredInspectorDock && restoredInspectorDock->isVisible())
                  || (restoredDomainDock && restoredDomainDock->isVisible())
                  || (restoredResultsDock && restoredResultsDock->isVisible()))
              && restoredCenterViews
              && restoredCenterViews->currentIndex() == 0,
          QStringLiteral(
              "closing during Canvas Focus persists the underlying Editor and "
              "panel state, not the transient focus state"));
    check(restoredResultTabs && restoredActivityResultPage
              && restoredResultTabs->currentWidget()
                  == restoredActivityResultPage,
          QStringLiteral(
              "the semantic Results page id restores the same task across workbench sessions"));
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
        chooseMessageBoxButton(QMessageBox::Yes);
        repairWorkspaceAction->trigger();
        application.processEvents();
    }
    const finepaper::TopologyWorkspaceLoadResult repairedWorkspace =
        topologyWorkspaceWriter.load(topologyWorkspaceIdentity);
    const QString repairStatus = positionWindow.statusBar()->currentMessage();
    check(repairWorkspaceAction && repairedWorkspace.ok()
              && repairedWorkspace.state
              && repairStatus.contains(
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
    if (settingsBlocked && repairWorkspaceAction) {
        chooseMessageBoxButton(QMessageBox::Yes);
        repairWorkspaceAction->trigger();
        application.processEvents();
    }
    check(settingsBlocked
              && positionWindow.statusBar()->currentMessage().contains(
                  QStringLiteral("could not be saved"), Qt::CaseInsensitive),
          QStringLiteral(
              "Regularize Layout preserves the persistent save failure instead of reporting success"));
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
    int sessionDraftStateChanges = 0;
    sessionEditor.endpointCanvasDraftStateChanged = [&] {
        ++sessionDraftStateChanges;
    };
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
    static_cast<void>(sessionEditor.regularizeLayout());
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
              && sessionEditor.endpointCanvasDraftState().detachedEndpointIds()
              == QStringList{QStringLiteral("session_endpoint")}
              && sessionEditor.endpointCanvasDraftState().detachedCount() == 1
              && sessionDraftStateChanges == 1,
          QStringLiteral(
              "a canceled old deletion does not block a new disconnect, which "
              "creates a transient Endpoint draft"));
    const auto detachedNodeBeforeTypeRefresh = nodeIdWithCaptionPrefix(
        sessionScene,
        QStringLiteral("Disconnected Endpoint\nsession_endpoint"));
    sessionEditor.setEndpointTypes({{
        QStringLiteral("master"), QStringLiteral("Renamed master endpoint")}});
    application.processEvents();
    const auto detachedNodeAfterTypeRefresh = nodeIdWithCaptionPrefix(
        sessionScene,
        QStringLiteral(
            "Disconnected Endpoint\nsession_endpoint · Renamed master endpoint"));
    check(detachedNodeBeforeTypeRefresh && detachedNodeAfterTypeRefresh
              && detachedNodeBeforeTypeRefresh == detachedNodeAfterTypeRefresh
              && sessionDraftStateChanges == 2,
          QStringLiteral(
              "Endpoint type label refresh updates a detached draft in place without rebuilding the graph"));
    sessionEditor.setDesign(&sessionDesign);
    check(sessionEditor.endpointCanvasDraftState().detachedEndpointIds()
              == QStringList{QStringLiteral("session_endpoint")}
              && sessionDraftStateChanges == 2,
          QStringLiteral("same-session projection refresh retains the detached Endpoint draft"));
    sessionEditor.beginDocumentSession(QStringLiteral("document-session-b"));
    sessionEditor.setDesign(&sessionDesign);
    check(sessionEditor.endpointCanvasDraftState().detachedEndpointIds()
                  .isEmpty()
              && sessionDraftStateChanges == 3
              && !nodeIdWithCaptionPrefix(
                  sessionScene, QStringLiteral("Disconnected Endpoint\n")),
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
    capacityProjectionEditor.setEndpointTypes({
        {QStringLiteral("master"), QStringLiteral("Master endpoint")}});
    QMimeData capacityDraftMime;
    capacityDraftMime.setData(
        finepaper::workbench::endpointTypeMime,
        QByteArrayLiteral("master"));
    const QPoint capacityBlankPosition = blankViewportPosition(
        capacityProjectionView);
    if (capacityProjectionView) {
        QDragEnterEvent dragEnter(
            capacityBlankPosition,
            Qt::CopyAction,
            &capacityDraftMime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            capacityProjectionView->viewport(), &dragEnter);
        QDropEvent drop(
            QPointF(capacityBlankPosition),
            Qt::CopyAction,
            &capacityDraftMime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(
            capacityProjectionView->viewport(), &drop);
        application.processEvents();
    }
    auto* capacityConnect =
        capacityProjectionEditor.findChild<QToolButton*>(
            QStringLiteral("finepaper.endpointDraftConnect"));
    QMenu* capacityRouterMenu = capacityConnect
        ? capacityConnect->menu() : nullptr;
    if (capacityRouterMenu) {
        QMetaObject::invokeMethod(
            capacityRouterMenu, "aboutToShow", Qt::DirectConnection);
    }
    const bool capacityReasonVisible = capacityRouterMenu
        && std::any_of(
            capacityRouterMenu->actions().cbegin(),
            capacityRouterMenu->actions().cend(),
            [](const QAction* action) {
                return action
                    && (action->text().contains(
                            QStringLiteral("capacity"),
                            Qt::CaseInsensitive)
                        || action->toolTip().contains(
                            QStringLiteral("capacity"),
                            Qt::CaseInsensitive));
            });
    check(capacityConnect && capacityConnect->isEnabled()
              && capacityReasonVisible,
          QStringLiteral(
              "Connect remains reviewable and exposes the exact rejection when every Router is full"));
    capacityProjectionEditor.beginDocumentSession(
        QStringLiteral("capacity-projection-cleanup-session"));
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

    finepaper::NocDesign largePortDesign;
    largePortDesign.id = QStringLiteral("large-port-projection");
    largePortDesign.topology = {QStringLiteral("mesh"), 1, 2};
    finepaper::AttachmentDefinition largePortAttachment;
    largePortAttachment.maxPerRouter =
        finepaper::kMaximumEndpointAttachmentsPerRouter;
    largePortAttachment.slotMode = finepaper::AttachmentSlotMode::Automatic;
    const QFont normalApplicationFont = QApplication::font();
    QFont largeApplicationFont = normalApplicationFont;
    const qreal normalPointSize = normalApplicationFont.pointSizeF() > 0.0
        ? normalApplicationFont.pointSizeF() : 12.0;
    largeApplicationFont.setPointSizeF((std::max)(
        18.0, normalPointSize + 6.0));
    finepaper::NocNodeEditor largePortEditor;
    largePortEditor.beginDocumentSession(
        QStringLiteral("large-port-projection-session"));
    largePortEditor.setDesign(
        &largePortDesign,
        finepaper::attachment::policyFromPackage(largePortAttachment));
    largePortEditor.show();
    application.processEvents();
    auto* largePortView = largePortEditor.findChild<QGraphicsView*>();
    auto* largePortScene = largePortView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(largePortView->scene())
        : nullptr;
    const auto largePortRouter = nodeIdWithCaption(
        largePortScene, QStringLiteral("r-0-0"));
    const auto largePortNeighbor = nodeIdWithCaption(
        largePortScene, QStringLiteral("r-1-0"));
    const QSize normalCollapsedRouterSize =
        largePortScene && largePortRouter
        ? largePortScene->nodeGeometry().size(*largePortRouter) : QSize{};
    const QtNodes::ConnectionId largePortRouterLink =
        largePortRouter && largePortNeighbor
        ? QtNodes::ConnectionId{
              *largePortRouter,
              finepaper::portIndex(finepaper::RouterOutputPort::East),
              *largePortNeighbor,
              finepaper::portIndex(finepaper::RouterInputPort::West)}
        : QtNodes::ConnectionId{};
    auto* largePortLinkGraphics = largePortScene
        ? largePortScene->connectionGraphicsObject(largePortRouterLink)
        : nullptr;
    const QPointF normalFontLinkOutput = largePortLinkGraphics
        ? largePortLinkGraphics->out() : QPointF{};
    QApplication::setFont(largeApplicationFont);
    application.processEvents();
    const QSize largeCollapsedRouterSize = largePortScene && largePortRouter
        ? largePortScene->nodeGeometry().size(*largePortRouter) : QSize{};
    const QFont largeCaptionFont = finepaper::nocEditorFont(
        finepaper::NocEditorFontRole::Caption, QApplication::font());
    const int collapsedLabelMinimumWidth = QFontMetrics(largeCaptionFont)
        .horizontalAdvance(
            QStringLiteral("%1 endpoint ports")
                .arg(finepaper::kMaximumEndpointAttachmentsPerRouter))
        + 28;
    check(largePortRouter
              && largeCollapsedRouterSize.width()
                  >= collapsedLabelMinimumWidth
              && largeCollapsedRouterSize.width()
                  > normalCollapsedRouterSize.width(),
          QStringLiteral(
              "a live collapsed Router grows to retain its textual Endpoint-port summary after a font change"));
    const QPointF largeFontLinkOutput = largePortLinkGraphics
        ? largePortLinkGraphics->out() : QPointF{};
    check(largePortLinkGraphics
              && qAbs(largeFontLinkOutput.x() - normalFontLinkOutput.x())
                  > 0.1,
          QStringLiteral(
              "runtime font changes refresh existing Router-Link endpoints without rebuilding the graph"));
    QApplication::setFont(normalApplicationFont);
    application.processEvents();
    largePortEditor.setRouterCollapsed(QStringLiteral("r-0-0"), false);
    application.processEvents();
    const auto expandedLargePortRouter = nodeIdWithCaption(
        largePortScene, QStringLiteral("r-0-0"));
    const QSize largeRouterSize = largePortScene && expandedLargePortRouter
        ? largePortScene->nodeGeometry().size(*expandedLargePortRouter)
        : QSize{};
    const unsigned int logicalInputPortCount =
        largePortScene && expandedLargePortRouter
        ? largePortScene->graphModel().nodeData<unsigned int>(
              *expandedLargePortRouter, QtNodes::NodeRole::InPortCount)
        : 0U;
    const QPointF firstOverflowPosition =
        largePortScene && expandedLargePortRouter
        ? largePortScene->nodeGeometry().portPosition(
              *expandedLargePortRouter,
              QtNodes::PortType::In,
              finepaper::portIndex(finepaper::RouterInputPort::Endpoint) + 7U)
        : QPointF{};
    const QPointF lastLogicalPortPosition =
        largePortScene && expandedLargePortRouter
        ? largePortScene->nodeGeometry().portPosition(
              *expandedLargePortRouter,
              QtNodes::PortType::In,
              finepaper::portIndex(finepaper::RouterInputPort::Endpoint)
                  + static_cast<unsigned int>(
                      finepaper::kMaximumEndpointAttachmentsPerRouter - 1))
        : QPointF{};
    const finepaper::attachment::SlotResolution largePortAvailability =
        largePortEditor.endpointAttachmentAvailability(
            finepaper::RouterPosition{0, 0});
    const QPixmap largePortRendering = largePortEditor.grab();
    check(expandedLargePortRouter
              && logicalInputPortCount
                  == static_cast<unsigned int>(
                         finepaper::kMaximumEndpointAttachmentsPerRouter)
                      + finepaper::portIndex(
                          finepaper::RouterInputPort::Endpoint),
          QStringLiteral(
              "large legal attachment policies retain all logical Router ports"));
    check(largeRouterSize.width() <= 400
              && largeRouterSize.height() <= 400,
          QStringLiteral(
              "large legal attachment policies keep expanded Router geometry bounded"));
    check(qAbs(firstOverflowPosition.x() - lastLogicalPortPosition.x()) < 0.1
              && qAbs(firstOverflowPosition.y()
                      - lastLogicalPortPosition.y()) < 0.1,
          QStringLiteral(
              "large legal attachment policies aggregate overflow ports at one target"));
    check(largePortAvailability.kind
              != finepaper::attachment::SlotResolutionKind::Rejected,
          QStringLiteral(
              "large legal attachment policies retain an available logical slot"));
    check(!largePortRendering.isNull(),
          QStringLiteral(
              "large legal attachment policies remain renderable"));
    largePortEditor.close();

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
                  QStringLiteral("1 available")),
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
        explicitPalette->itemActivated(explicitPalette->item(0));
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
        explicitScene, QStringLiteral("New Endpoint draft\nDevice endpoint"));
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
                  explicitScene, QStringLiteral("Disconnected Endpoint\n")),
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
                  explicitScene, QStringLiteral("Disconnected Endpoint\n")),
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
    auto* multiPackageSelector = multiPackageWindow.findChild<QComboBox*>(
        QStringLiteral("finepaper.packageSelector"));
    check(multiAvailablePackages
              && multiAvailablePackages->text().startsWith(
                  QStringLiteral("2 available"))
              && multiPackageSelector && multiPackageSelector->count() == 2
              && chooseComboData(
                  multiPackageSelector,
                  QStringLiteral("test.number-parameter@1.0.0")),
          QStringLiteral(
              "multiple runnable Packages are selectable with visible text before creation"));
    createDesignThroughDialog(
        multiPackageWindow,
        {},
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
    QAction* numberValidate = actionWithText(
        numberWindow, QStringLiteral("Validate / DRC"));
    if (numberValidate) {
        chooseMessageBoxButton(QMessageBox::Cancel);
        numberValidate->trigger();
        application.processEvents();
    }
    check(numberValidate && !numberWindow.operationBusy()
              && numberEditor && numberEditor->value()
              && qAbs(numberEditor->value()->toDouble() - 2.75) < 0.000001
              && numberApply && numberApply->isEnabled(),
          QStringLiteral(
              "cancelling validation preserves an unapplied NoC parameter draft"));

    if (numberValidate) {
        chooseMessageBoxButton(QMessageBox::Discard);
        numberValidate->trigger();
        check(waitUntil([&numberWindow] {
                  return !numberWindow.operationBusy();
              }),
              QStringLiteral(
                  "authorized validation starts after discarding the NoC parameter draft"));
    }
    check(numberEditor && numberEditor->value()
              && qAbs(numberEditor->value()->toDouble() - 1.25) < 0.000001
              && numberApply && !numberApply->isEnabled(),
          QStringLiteral(
              "starting validation discards the authorized draft and restores the durable value"));

    if (numberEditor) {
        numberEditor->setValue(QJsonValue(2.75));
        if (numberEditor->valueChanged) {
            numberEditor->valueChanged();
        }
    }
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
    auto* retainedPackageAvailability = numberWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackageAvailability"));
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
              && retainedPackageAvailability
              && !retainedPackageAvailability->isHidden()
              && retainedPackageAvailability->text().contains(
                  QStringLiteral("Reload or reinstall"))
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
              && retainedPackageAvailability
              && retainedPackageAvailability->isHidden()
              && retainedAvailablePackages
              && retainedAvailablePackages->text().startsWith(
                  QStringLiteral("1 available"))
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
    if (requestedWindowSize.isValid()) {
        domainWindow.resize(requestedWindowSize);
    }
    domainWindow.show();
    application.processEvents();
    if (requestedWindowSize.isValid()
        && domainWindow.size() != requestedWindowSize) {
        domainWindow.resize(requestedWindowSize);
        application.processEvents();
    }
    check(domainWindow.openDesignFile(domainDesignPath),
          QStringLiteral("the Package-driven Domain design opens in the workbench"));
    application.processEvents();

    auto* domainSelector = domainWindow.findChild<QComboBox*>(
        finepaper::workbench::domainLayerSelectorName);
    auto* compactDomainMenu = domainWindow.findChild<QMenu*>(
        finepaper::workbench::compactDomainLayerMenuName);
    const bool domainCompact = domainWindow.property(
        finepaper::workbench::workbenchWidthModeProperty).toString()
        != QStringLiteral("wide");
    if (domainCompact && compactDomainMenu) {
        QMetaObject::invokeMethod(
            compactDomainMenu, "aboutToShow", Qt::DirectConnection);
    }
    const auto compactDomainContains = [compactDomainMenu](
        const QString& typeId) {
        if (!compactDomainMenu) {
            return false;
        }
        const QList<QAction*> actions = compactDomainMenu->actions();
        return std::any_of(
            actions.cbegin(), actions.cend(),
            [&typeId](const QAction* action) {
                return action && action->data().toString() == typeId;
            });
    };
    const bool domainLayerControlReachable = domainCompact
        ? compactDomainMenu
            && compactDomainMenu->actions().size() == 3
            && compactDomainContains(QStringLiteral("security-zone"))
            && compactDomainContains(QStringLiteral("fabric-tier"))
        : domainSelector && domainSelector->isVisible();
    check(domainSelector && domainLayerControlReachable
              && domainSelector->isEnabled()
              && domainSelector->count() == 3
              && domainSelector->itemText(0) == QStringLiteral("None")
              && domainSelector->itemData(0).toString().isEmpty()
              && domainSelector->findData(QStringLiteral("security-zone")) > 0
              && domainSelector->itemText(domainSelector->findData(
                     QStringLiteral("security-zone")))
                  == QStringLiteral("Security zones (security-zone)")
              && domainSelector->findData(QStringLiteral("fabric-tier")) > 0,
          QStringLiteral("Color by lists Package labels backed by arbitrary stable Domain type ids"));

    auto* domainManagerDock = domainWindow.findChild<QDockWidget*>(
        finepaper::workbench::domainManagerDockName);
    QAction* domainManagerToggle = domainWindow.findChild<QAction*>(
        finepaper::workbench::domainManagerToggleActionName);
    QAction* domainManagerNavigation = domainWindow.findChild<QAction*>(
        finepaper::workbench::domainNavigationActionName);
    if (domainCompact && domainManagerNavigation) {
        domainManagerNavigation->trigger();
        application.processEvents();
    } else if (domainManagerToggle && !domainManagerToggle->isChecked()) {
        domainManagerToggle->trigger();
        application.processEvents();
    }
    check(domainManagerDock && domainManagerToggle && domainManagerNavigation
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
    auto* domainManagerTypeControls = domainWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.domainManager.typeControls"));
    auto* domainManagerInstances = domainWindow.findChild<QTableWidget*>(
        QStringLiteral("finepaper.domainManager.instanceView"));
    auto* domainManagerTabs = domainWindow.findChild<QTabWidget*>(
        QStringLiteral("finepaper.domainManager.tabs"));
    auto* domainAssignmentPage = domainWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.domainManager.assignmentPage"));
    auto* domainAssignmentState = domainWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.domainManager.assignmentState"));
    auto* domainMultipleAssignment = domainWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.domainManager.assignmentEditor.multiple"));
    auto* domainApplyAssignment = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.applyAssignment"));
    auto* domainDiscardAssignment = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.domainManager.discardAssignment"));
    auto* domainAssignmentTaskBar = domainWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.domainAssignmentTaskBar"));
    auto* domainManagerScroll = domainWindow.findChild<QScrollArea*>(
        QStringLiteral("finepaper.domainManagerScroll"));
    auto* domainResultsDock = domainWindow.findChild<QDockWidget*>(
        finepaper::workbench::resultsDockName);
    auto* domainInspectorDock = domainWindow.findChild<QDockWidget*>(
        finepaper::workbench::inspectorDockName);
    QAction* domainCanvasFocusAction = domainWindow.findChild<QAction*>(
        finepaper::workbench::canvasFocusActionName);
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
    check(domainView
              && domainView->accessibleDescription().contains(
                  QStringLiteral("Trusted (zone-a)"))
              && domainView->accessibleDescription().contains(
                  QStringLiteral(
                      "Dashed connections indicate Domain crossings"))
              && domainView->accessibleDescription().contains(
                  QStringLiteral(
                      "A diamond marker indicates an edge override")),
          QStringLiteral(
              "the canvas explains stable Domain identities and non-text connection markers in accessible text"));
    auto* domainSelectionInspectorGroup = domainWindow.findChild<QWidget*>(
        finepaper::workbench::selectionInspectorName);
    auto* domainSelectionInspector = domainSelectionInspectorGroup
        ? domainSelectionInspectorGroup->findChild<QLabel*>(
              QStringLiteral("finepaper.inspectorSelectionDetail"))
        : nullptr;
    check(domainSelectionInspector
              && domainSelectionInspector->text().contains(
                  QStringLiteral("Active Domain layer"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("Security zones (security-zone)"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral(
                      "Router assignments: minimum 0, maximum unbounded."))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("Trusted (zone-a)")),
          QStringLiteral(
              "Router Inspector states the active Domain id, per-kind bounds, and full assignment in text"));
    auto* editDomainAssignments = domainWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.inspectorEditDomainAssignments"));
    auto* domainInspectorScroll = domainWindow.findChild<QScrollArea*>(
        QStringLiteral("finepaper.inspectorScroll"));
    QAction* domainInspectorNavigation = domainWindow.findChild<QAction*>(
        finepaper::workbench::inspectorNavigationActionName);
    if (domainResultsDock) {
        domainResultsDock->show();
        application.processEvents();
    }
    if (domainInspectorNavigation) {
        domainInspectorNavigation->trigger();
        application.processEvents();
        application.processEvents();
    }
    const bool editDomainAssignmentsWasVisible = editDomainAssignments
        && editDomainAssignments->isVisibleTo(&domainWindow)
        && widgetIntersectsScrollViewport(
            domainInspectorScroll, editDomainAssignments);
    captureSmokeScreenshot(
        domainWindow, QStringLiteral("domain-task-route"), requestedTheme);
    if (editDomainAssignments) {
        editDomainAssignments->click();
        application.processEvents();
        application.processEvents();
    }
    const bool compactDomainTaskLayout = !domainCompact
        || (domainResultsDock && !domainResultsDock->isVisible()
            && domainInspectorDock && domainInspectorDock->visibleRegion().isEmpty());
    const bool domainTaskEndingVisible = domainAssignmentTaskBar
        && domainAssignmentTaskBar->isVisibleTo(&domainWindow)
        && domainApplyAssignment && domainDiscardAssignment
        && widgetIsFullyVisibleWithin(
            domainAssignmentTaskBar, domainApplyAssignment)
        && widgetIsFullyVisibleWithin(
            domainAssignmentTaskBar, domainDiscardAssignment)
        && domainDiscardAssignment->isEnabled()
        && domainDiscardAssignment->text() == QStringLiteral("Done");
    captureSmokeScreenshot(
        domainWindow, QStringLiteral("domain-assignment-task"), requestedTheme);
    check(editDomainAssignmentsWasVisible
              && domainManagerDock && domainManagerDock->isVisible()
              && domainManagerTabs
              && domainManagerTabs->currentWidget() == domainAssignmentPage
              && domainManagerScroll && compactDomainTaskLayout
              && domainTaskEndingVisible
              && widgetIntersectsScrollViewport(
                  domainManagerScroll, domainMultipleAssignment)
              && (focusIsWithin(domainMultipleAssignment)
                  || focusIsWithin(domainManagerType)),
          QStringLiteral(
              "Router Inspector enters a readable Domain task with fixed endings and compact panel focus"));
    if (domainDiscardAssignment) {
        domainDiscardAssignment->click();
        application.processEvents();
        application.processEvents();
    }
    const bool domainTaskLayoutRestored = !domainCompact
        || (domainResultsDock && domainResultsDock->isVisible()
            && domainInspectorDock
            && !domainInspectorDock->visibleRegion().isEmpty());
    check(domainTaskLayoutRestored,
          QStringLiteral(
              "Done restores the Inspector and Results layout captured before the Domain task"));
    if (editDomainAssignments) {
        editDomainAssignments->click();
        application.processEvents();
        application.processEvents();
    }
    if (domainCanvasFocusAction) {
        domainCanvasFocusAction->trigger();
        application.processEvents();
        application.processEvents();
    }
    const bool canvasFocusEndedDomainTask = domainCanvasFocusAction
        && domainCanvasFocusAction->isChecked()
        && domainManagerDock && !domainManagerDock->isVisible();
    if (domainCanvasFocusAction && domainCanvasFocusAction->isChecked()) {
        domainCanvasFocusAction->trigger();
        application.processEvents();
        application.processEvents();
    }
    if (domainManagerNavigation) {
        domainManagerNavigation->trigger();
        application.processEvents();
    }
    const bool canvasFocusClearedTaskPresentation = domainManagerDock
        && domainManagerDock->isVisible()
        && domainManagerTypeControls
        && domainManagerTypeControls->isVisibleTo(&domainWindow)
        && domainManagerTabs && domainManagerTabs->tabBar()->isVisible();
    check(canvasFocusEndedDomainTask
              && canvasFocusClearedTaskPresentation,
          QStringLiteral(
              "Canvas Focus ends Domain assignment before taking layout ownership"));
    if (domainInspectorNavigation) {
        domainInspectorNavigation->trigger();
        application.processEvents();
    }
    if (editDomainAssignments) {
        editDomainAssignments->click();
        application.processEvents();
        application.processEvents();
    }
    if (domainManagerDock) {
        domainManagerDock->close();
        application.processEvents();
        application.processEvents();
    }
    const bool domainCloseRestoredLayout = !domainCompact
        || (domainResultsDock && domainResultsDock->isVisible()
            && domainInspectorDock
            && !domainInspectorDock->visibleRegion().isEmpty());
    if (domainManagerNavigation) {
        domainManagerNavigation->trigger();
        application.processEvents();
    }
    const bool domainCloseClearedTaskPresentation = domainManagerDock
        && domainManagerDock->isVisible()
        && domainManagerTypeControls
        && domainManagerTypeControls->isVisibleTo(&domainWindow)
        && domainManagerTabs && domainManagerTabs->tabBar()->isVisible();
    check(domainCloseRestoredLayout && domainCloseClearedTaskPresentation,
          QStringLiteral(
              "closing a focused Domain Dock restores the prior layout and clears its stripped task presentation"));
    if (domainInspectorNavigation) {
        domainInspectorNavigation->trigger();
        application.processEvents();
    }
    if (editDomainAssignments) {
        editDomainAssignments->click();
        application.processEvents();
        application.processEvents();
    }
    if (domainScene && domainLinkGraphics && domainRouterGraphics) {
        domainScene->clearSelection();
        domainLinkGraphics->setSelected(true);
        application.processEvents();
        check(editDomainAssignments && editDomainAssignments->isHidden(),
              QStringLiteral(
                  "Router Link selection hides the inapplicable Domain-assignment task route"));
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        application.processEvents();
        domainLayerSelectionCallbacks = 0;
    }

    QListWidgetItem* draftProbeAssignment = nullptr;
    if (domainScene && domainRouterGraphics && domainRouter1Graphics
        && domainMultipleAssignment) {
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        domainRouter1Graphics->setSelected(true);
        application.processEvents();
        for (int row = 0; row < domainMultipleAssignment->count(); ++row) {
            QListWidgetItem* item = domainMultipleAssignment->item(row);
            if (item && item->data(finepaper::domainManagerDomainIdRole)
                            .toString() == QStringLiteral("zone-a")) {
                draftProbeAssignment = item;
                break;
            }
        }
    }
    if (draftProbeAssignment) {
        draftProbeAssignment->setCheckState(
            draftProbeAssignment->checkState() == Qt::Checked
                ? Qt::Unchecked : Qt::Checked);
        application.processEvents();
    }
    const bool assignmentDraftWasStaged = draftProbeAssignment
        && domainDiscardAssignment && domainDiscardAssignment->isEnabled();
    if (domainScene && domainLinkGraphics) {
        domainScene->clearSelection();
        domainLinkGraphics->setSelected(true);
        application.processEvents();
    }
    const bool pendingDraftRouteRemainsAvailable = editDomainAssignments
        && !editDomainAssignments->isHidden()
        && domainDiscardAssignment && domainDiscardAssignment->isEnabled();
    if (domainDiscardAssignment) {
        domainDiscardAssignment->click();
        application.processEvents();
    }
    check(assignmentDraftWasStaged && pendingDraftRouteRemainsAvailable
              && editDomainAssignments && editDomainAssignments->isHidden(),
          QStringLiteral(
              "discarding an assignment draft refreshes the Inspector route against the latest ineligible selection"));
    if (domainScene && domainRouterGraphics) {
        domainScene->clearSelection();
        domainRouterGraphics->setSelected(true);
        application.processEvents();
        domainLayerSelectionCallbacks = 0;
    }
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
              && domainRouterGraphics->data(
                     finepaper::domainPatternBrushesDataRole).toList().size()
                  == 1
              && domainLinkGraphics
              && domainLinkGraphics->data(
                     finepaper::domainCrossingDataRole).toBool()
              && domainLinkGraphics->data(
                     finepaper::domainCrossingPaintColorDataRole)
                     .value<QColor>().isValid(),
          QStringLiteral("nodes and derived Mesh crossings receive generic Domain presentation roles"));

    if (domainScene && domainLinkGraphics) {
        domainScene->clearSelection();
        domainLinkGraphics->setSelected(true);
        application.processEvents();
    }
    check(domainSelectionInspector
              && domainSelectionInspector->text().contains(
                  QStringLiteral("Color-by Domain crossing"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("From set: Trusted (zone-a)"))
              && domainSelectionInspector->text().contains(
                  QStringLiteral("To set: Restricted (zone-b)"))
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
        }, std::chrono::seconds(4));
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
              && domainWindow.isWindowModified(),
          QStringLiteral(
              "a persistent complete-configuration draft remains separate from durable quick-Manager data and marks the workbench pending"));
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
              && domainWindow.isWindowModified(),
          QStringLiteral("editing Mixed choices stages an explicit atomic assignment and marks the workbench pending without mutating durable data"));

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
        domainEndpointPalette->itemActivated(
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
              && domainWindow.isWindowModified(),
          QStringLiteral(
              "authorizing discard before Endpoint creation keeps both pending Domain drafts when the later Endpoint dialog is cancelled"));
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
              && domainWindow.isWindowModified(),
          QStringLiteral(
              "Save, Validate and Generate remain available but cannot silently ignore quick or complete Domain drafts"));

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

    zoneAAssignment = domainAssignmentItem(QStringLiteral("zone-a"));
    zoneBAssignment = domainAssignmentItem(QStringLiteral("zone-b"));
    if (zoneAAssignment && zoneBAssignment) {
        zoneAAssignment->setCheckState(Qt::Unchecked);
        zoneBAssignment->setCheckState(Qt::Checked);
        application.processEvents();
    }
    QAction* domainReloadAction = actionWithText(
        domainWindow, QStringLiteral("Reload Packages"));
    check(domainReloadAction && domainApplyAssignment
              && domainApplyAssignment->isEnabled()
              && domainWindow.isWindowModified(),
          QStringLiteral(
              "a quick Domain assignment draft can be staged before Package reload"));
    if (domainReloadAction) {
        chooseMessageBoxButton(QMessageBox::Discard);
        domainReloadAction->trigger();
        application.processEvents();
        application.processEvents();
    }
    check(domainReloadAction && domainApplyAssignment
              && !domainApplyAssignment->isEnabled()
              && !domainWindow.isWindowModified(),
          QStringLiteral(
              "Package reload discards a Domain assignment while the borrowed old catalog is still alive"));

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

    const QString v3PackageRoot = QDir(projectRoot).filePath(
        QStringLiteral("packages/finepaper-noc-v3"));
    finepaper::RuntimeLocations v3Locations = {
        QStringList{v3PackageRoot}, outputRoot.path()};
    finepaper::FinepaperMainWindow elementReloadWindow(v3Locations);
    elementReloadWindow.show();
    application.processEvents();
    createDesignThroughDialog(
        elementReloadWindow,
        QStringLiteral("finepaper.noc@3.1.0"),
        QStringLiteral("element_reload_regression"));
    auto* elementReloadEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        elementReloadWindow.findChild<QWidget*>(
            QStringLiteral("finepaper.nodeEditor")));
    auto* elementReloadPanel =
        dynamic_cast<finepaper::ElementConfigurationPanel*>(
            elementReloadWindow.findChild<QWidget*>(
                QStringLiteral("finepaper.elementConfiguration")));
    const QString elementReloadDesignIdentity =
        QStringLiteral("design-session-1");
    auto* elementReloadView = elementReloadEditor
        ? elementReloadEditor->findChild<QGraphicsView*>() : nullptr;
    auto* elementReloadScene = elementReloadView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(
              elementReloadView->scene())
        : nullptr;
    const auto elementReloadRouter = nodeIdWithCaption(
        elementReloadScene, QStringLiteral("r-0-0"));
    if (elementReloadScene && elementReloadRouter) {
        elementReloadScene->clearSelection();
        elementReloadScene->nodeGraphicsObject(*elementReloadRouter)
            ->setSelected(true);
        elementReloadScene->nodeSelected(*elementReloadRouter);
        application.processEvents();
    }
    auto* virtualChannels = elementReloadWindow.findChild<QLineEdit*>(
        QStringLiteral(
            "finepaper.schemaValue.virtualChannels.scalar.text"));
    auto* elementApply = elementReloadWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    if (virtualChannels) {
        virtualChannels->setText(QStringLiteral("4"));
        QMetaObject::invokeMethod(
            virtualChannels,
            "textEdited",
            Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("4")));
        application.processEvents();
    }
    QAction* elementReloadAction = actionWithText(
        elementReloadWindow, QStringLiteral("Reload Packages"));
    check(elementReloadAction && elementApply && elementApply->isEnabled()
              && elementReloadPanel
              && elementReloadPanel->hasUnappliedDrafts(
                  elementReloadDesignIdentity),
          QStringLiteral(
              "an Element Configuration draft can be staged before Package reload"));
    if (elementReloadAction) {
        chooseMessageBoxButton(QMessageBox::Discard);
        elementReloadAction->trigger();
        application.processEvents();
        application.processEvents();
    }
    elementReloadView = elementReloadEditor
        ? elementReloadEditor->findChild<QGraphicsView*>() : nullptr;
    elementReloadScene = elementReloadView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(
              elementReloadView->scene())
        : nullptr;
    const auto reloadedElementRouter = nodeIdWithCaption(
        elementReloadScene, QStringLiteral("r-0-0"));
    if (elementReloadScene && reloadedElementRouter) {
        elementReloadScene->clearSelection();
        elementReloadScene->nodeGraphicsObject(*reloadedElementRouter)
            ->setSelected(true);
        elementReloadScene->nodeSelected(*reloadedElementRouter);
        application.processEvents();
    }
    virtualChannels = elementReloadWindow.findChild<QLineEdit*>(
        QStringLiteral(
            "finepaper.schemaValue.virtualChannels.scalar.text"));
    elementApply = elementReloadWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.elementConfiguration.apply"));
    check(elementReloadAction && virtualChannels
              && virtualChannels->text() == QStringLiteral("2")
              && elementApply && !elementApply->isEnabled()
              && elementReloadPanel
              && !elementReloadPanel->hasUnappliedDrafts(
                  elementReloadDesignIdentity),
          QStringLiteral(
              "Package reload discards an Element draft before replacing its borrowed catalog"));
    closeDiscarding(elementReloadWindow);

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
    auto* zeroPackageInstall = zeroPackageWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.installPackage"));
    QAction* zeroPackageNavigation = zeroPackageWindow.findChild<QAction*>(
        finepaper::workbench::packageNavigationActionName);
    QAction* missingNew = actionWithText(
        zeroPackageWindow, QStringLiteral("New NoC Design…"));
    check(missingCreate && !missingCreate->isEnabled()
              && missingNew && !missingNew->isEnabled(),
          QStringLiteral("zero-Package startup disables every New Design entry point consistently"));
    if (zeroPackageNavigation) {
        zeroPackageNavigation->trigger();
        application.processEvents();
    }
    check(zeroPackageNavigation && zeroPackageInstall
              && focusIsWithin(zeroPackageInstall),
          QStringLiteral(
              "Package navigation focuses Install when no runnable Package can be selected "
              "(mode=%1, dockVisible=%2, installEnabled=%3, installVisible=%4, "
              "focus=%5, localFocus=%6)")
              .arg(zeroPackageWindow.property(
                       finepaper::workbench::workbenchWidthModeProperty)
                       .toString())
              .arg(zeroPackageInstall
                       && zeroPackageInstall->isVisibleTo(&zeroPackageWindow))
              .arg(zeroPackageInstall && zeroPackageInstall->isEnabled())
              .arg(zeroPackageInstall && zeroPackageInstall->isVisible())
              .arg(QApplication::focusWidget()
                       ? QApplication::focusWidget()->objectName()
                       : QStringLiteral("none"))
              .arg(zeroPackageWindow.focusWidget()
                       ? zeroPackageWindow.focusWidget()->objectName()
                       : QStringLiteral("none")));
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
    auto* missingPackageAvailability = missingWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.activePackageAvailability"));
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
              && missingPackageAvailability
              && !missingPackageAvailability->isHidden()
              && missingPackageAvailability->text().contains(
                  QStringLiteral("Install this exact Package ID and version"))
              && missingAvailablePackages
              && missingAvailablePackages->text().startsWith(
                  QStringLiteral("1 available")),
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

    // Keep quick-add's deliberate create/delete projection rebuilds isolated
    // from the long-lived canvas interaction fixture above. QtNodes NodeIds are
    // transient across those rebuilds by design.
    const QString quickAddDesignPath = outputRoot.filePath(
        QStringLiteral("quick-add-regression.fpnoc"));
    check(writeEndpointDraftLifecycleDesign(quickAddDesignPath),
          QStringLiteral("quick-add regression design is writable"));
    finepaper::FinepaperMainWindow quickAddWindow(locations);
    quickAddWindow.resize(798, 720);
    quickAddWindow.show();
    application.processEvents();
    check(quickAddWindow.openDesignFile(quickAddDesignPath),
          QStringLiteral("quick-add regression design opens cleanly"));
    auto* quickPalette = quickAddWindow.findChild<QListWidget*>(
        QStringLiteral("finepaper.endpointPalette"));
    auto* quickFilter = quickAddWindow.findChild<QLineEdit*>(
        QStringLiteral("finepaper.endpointPaletteFilter"));
    auto* quickAddButton = quickAddWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.addEndpointToRouter"));
    auto* quickEditor = dynamic_cast<finepaper::NocNodeEditor*>(
        quickAddWindow.findChild<QWidget*>(QStringLiteral("finepaper.nodeEditor")));
    auto* quickView = quickEditor
        ? quickEditor->findChild<QGraphicsView*>() : nullptr;
    auto* quickAnimatedView = dynamic_cast<finepaper::AnimatedGraphicsView*>(
        quickView);
    auto* quickScene = quickView
        ? dynamic_cast<QtNodes::BasicGraphicsScene*>(quickView->scene()) : nullptr;
    if (quickEditor) {
        quickEditor->setRouterCollapsed(QStringLiteral("r-0-0"), false);
    }
    QAction* quickSave = actionWithText(
        quickAddWindow, QStringLiteral("Save"));
    QAction* quickSaveAs = actionWithText(
        quickAddWindow, QStringLiteral("Save As…"));
    QAction* quickInstall = actionWithText(
        quickAddWindow, QStringLiteral("Install Package Directory…"));
    QAction* quickReload = actionWithText(
        quickAddWindow, QStringLiteral("Reload Packages"));
    QAction* quickValidate = actionWithText(
        quickAddWindow, QStringLiteral("Validate / DRC"));
    QAction* quickGenerate = actionWithText(
        quickAddWindow, QStringLiteral("Generate RTL"));
    QAction* quickResize = quickAddWindow.findChild<QAction*>(
        QStringLiteral("finepaper.resizeMeshAction"));
    auto* quickResultsGenerate = quickAddWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.generateButton"));
    auto* quickLibraryInstall = quickAddWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.installPackage"));
    auto* quickLibraryReload = quickAddWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.reloadPackages"));
    auto* quickResultsDock = quickAddWindow.findChild<QDockWidget*>(
        finepaper::workbench::resultsDockName);
    auto* quickInspectorDock = quickAddWindow.findChild<QDockWidget*>(
        finepaper::workbench::inspectorDockName);
    auto* quickCanvasFocus = quickAddWindow.findChild<QAction*>(
        finepaper::workbench::canvasFocusActionName);
    if (quickResultsDock) {
        quickResultsDock->show();
        application.processEvents();
    }
    const bool quickResultsWereVisible = quickResultsDock
        && quickResultsDock->isVisible();
    const bool quickInspectorWasVisible = quickInspectorDock
        && quickInspectorDock->isVisible();
    QMimeData quickPendingMime;
    quickPendingMime.setData(
        finepaper::workbench::endpointTypeMime,
        QByteArrayLiteral("master"));
    const QPoint quickBlankPosition = blankViewportPosition(quickView);
    if (quickView) {
        QDragEnterEvent dragEnter(
            quickBlankPosition,
            Qt::CopyAction,
            &quickPendingMime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(quickView->viewport(), &dragEnter);
        QDropEvent drop(
            QPointF(quickBlankPosition),
            Qt::CopyAction,
            &quickPendingMime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(quickView->viewport(), &drop);
        application.processEvents();
    }
    const bool quickResultsAutoHidden = waitUntil([quickResultsDock] {
        return !quickResultsDock || !quickResultsDock->isVisible();
    });
    const bool quickInspectorAutoHidden = waitUntil([quickInspectorDock] {
        return !quickInspectorDock || !quickInspectorDock->isVisible();
    });
    const auto quickPendingEndpoint = nodeIdWithCaptionPrefix(
        quickScene, QStringLiteral("New Endpoint draft\nMaster endpoint"));
    auto* quickDraftNotice = quickEditor
        ? quickEditor->findChild<QLabel*>(
              QStringLiteral("finepaper.endpointCanvasDraftNotice"))
        : nullptr;
    auto* quickDraftTaskBar = quickEditor
        ? quickEditor->findChild<QWidget*>(
              QStringLiteral("finepaper.endpointDraftTaskBar"))
        : nullptr;
    auto* quickDraftReview = quickEditor
        ? quickEditor->findChild<QPushButton*>(
              QStringLiteral("finepaper.endpointDraftReview"))
        : nullptr;
    auto* quickDraftConnect = quickEditor
        ? quickEditor->findChild<QToolButton*>(
              QStringLiteral("finepaper.endpointDraftConnect"))
        : nullptr;
    auto* quickDraftDiscard = quickEditor
        ? quickEditor->findChild<QPushButton*>(
              QStringLiteral("finepaper.endpointDraftDiscard"))
        : nullptr;
    check(quickPendingEndpoint && quickEditor
              && quickEditor->endpointCanvasDraftState().pendingNewCount() == 1
              && quickAddWindow.isWindowModified(),
          QStringLiteral(
              "an unattached new Endpoint draft alone marks the clean document modified and discoverable"));
    check((!quickResultsWereVisible || quickResultsAutoHidden)
              && (!quickInspectorWasVisible || quickInspectorAutoHidden)
              && quickCanvasFocus && quickCanvasFocus->isChecked(),
          QStringLiteral(
              "compact Endpoint draft work enters reversible Canvas Focus and releases secondary-panel space"));
    check(quickDraftTaskBar && quickDraftTaskBar->isVisible()
              && quickDraftNotice && quickDraftNotice->isVisible()
              && widgetIsFullyVisibleWithin(quickEditor, quickDraftTaskBar)
              && quickDraftReview && quickDraftReview->isVisible()
              && !quickDraftReview->size().isEmpty()
              && widgetIsFullyVisibleWithin(
                  quickDraftTaskBar, quickDraftReview)
              && quickDraftConnect && quickDraftConnect->isVisible()
              && !quickDraftConnect->size().isEmpty()
              && widgetIsFullyVisibleWithin(
                  quickDraftTaskBar, quickDraftConnect)
              && quickDraftDiscard && quickDraftDiscard->isVisible()
              && !quickDraftDiscard->size().isEmpty()
              && widgetIsFullyVisibleWithin(
                  quickDraftTaskBar, quickDraftDiscard),
          QStringLiteral(
              "the Endpoint draft task bar keeps Review, Connect, and Discard visible on the canvas"));
    check(quickDraftReview && quickDraftReview->isEnabled()
              && quickDraftReview->focusPolicy() == Qt::StrongFocus
              && !quickDraftReview->accessibleName().trimmed().isEmpty()
              && quickDraftConnect && quickDraftConnect->isEnabled()
              && quickDraftConnect->focusPolicy() == Qt::StrongFocus
              && !quickDraftConnect->accessibleName().trimmed().isEmpty()
              && quickDraftDiscard && quickDraftDiscard->isEnabled()
              && quickDraftDiscard->focusPolicy() == Qt::StrongFocus
              && !quickDraftDiscard->accessibleName().trimmed().isEmpty(),
          QStringLiteral(
              "Endpoint draft task actions expose enabled keyboard and accessibility routes"));
    check(quickSave && !quickSave->isEnabled()
              && quickSaveAs && !quickSaveAs->isEnabled()
              && quickInstall && !quickInstall->isEnabled()
              && quickReload && !quickReload->isEnabled()
              && quickLibraryInstall && !quickLibraryInstall->isEnabled()
              && quickLibraryReload && !quickLibraryReload->isEnabled()
              && quickValidate && !quickValidate->isEnabled()
              && quickGenerate && !quickGenerate->isEnabled()
              && quickResize && !quickResize->isEnabled()
              && quickResultsGenerate && !quickResultsGenerate->isEnabled(),
          QStringLiteral(
              "unresolved Endpoint drafts consistently pause persistence, Package maintenance, validation, generation, and topology resize routes"));
    const auto hasUnavailableDraftHint = [](const auto* control) {
        return control
            && (control->toolTip().contains(
                    QStringLiteral("unavailable"), Qt::CaseInsensitive)
                || control->statusTip().contains(
                    QStringLiteral("unavailable"), Qt::CaseInsensitive));
    };
    check(hasUnavailableDraftHint(quickSave)
              && hasUnavailableDraftHint(quickSaveAs)
              && hasUnavailableDraftHint(quickInstall)
              && hasUnavailableDraftHint(quickReload)
              && hasUnavailableDraftHint(quickLibraryInstall)
              && hasUnavailableDraftHint(quickLibraryReload)
              && hasUnavailableDraftHint(quickValidate)
              && hasUnavailableDraftHint(quickGenerate)
              && hasUnavailableDraftHint(quickResize)
              && hasUnavailableDraftHint(quickResultsGenerate),
          QStringLiteral(
              "paused persistent actions explain that Endpoint drafts make them unavailable"));

    if (quickDraftReview) {
        quickDraftReview->setFocus(Qt::TabFocusReason);
        QTest::keyClick(quickDraftReview, Qt::Key_Space);
        application.processEvents();
    }
    auto* quickPendingGraphics = quickPendingEndpoint && quickScene
        ? quickScene->nodeGraphicsObject(*quickPendingEndpoint)
        : nullptr;
    check(quickView && focusIsWithin(quickView)
              && quickPendingGraphics
              && quickPendingGraphics->isSelected(),
          QStringLiteral(
              "keyboard Review selects and focuses the unresolved Endpoint draft"));

    QMenu* quickDraftRouterMenu = quickDraftConnect
        ? quickDraftConnect->menu() : nullptr;
    bool quickConnectMenuOpened = false;
    bool quickConnectMenuHasTarget = false;
    if (quickDraftConnect && quickDraftRouterMenu) {
        QTimer::singleShot(0, [&] {
            quickConnectMenuOpened = quickDraftRouterMenu->isVisible();
            for (QAction* action : quickDraftRouterMenu->actions()) {
                quickConnectMenuHasTarget = quickConnectMenuHasTarget
                    || (action->isEnabled()
                        && action->text().startsWith(
                            QStringLiteral("Router ")));
            }
            quickDraftRouterMenu->close();
        });
        quickDraftConnect->setFocus(Qt::TabFocusReason);
        QTest::keyClick(quickDraftConnect, Qt::Key_Space);
        application.processEvents();
    }
    check(quickConnectMenuOpened && quickConnectMenuHasTarget,
          QStringLiteral(
              "keyboard Connect opens a menu with an available Router target"));
    captureSmokeScreenshot(
        quickAddWindow, QStringLiteral("endpoint-draft"), requestedTheme);
    chooseMessageBoxButton(QMessageBox::Cancel);
    quickAddWindow.close();
    application.processEvents();
    check(quickAddWindow.isVisible() && quickEditor
              && quickEditor->endpointCanvasDraftState().pendingNewCount() == 1,
          QStringLiteral(
              "cancelling Close preserves an unresolved Endpoint canvas draft"));
    const auto defensiveBoundaryWasBlocked = [&](const QString& operation) {
        chooseMessageBoxButton(QMessageBox::Ok);
        const bool operationAllowed =
            finepaper::FinepaperMainWindowSmokeAccess::
                endpointCanvasDraftOperationAllowed(
                    quickAddWindow, operation);
        application.processEvents();
        return !operationAllowed;
    };
    const bool quickSaveBlocked = defensiveBoundaryWasBlocked(
        QStringLiteral("Save"));
    const bool quickSaveAsBlocked = defensiveBoundaryWasBlocked(
        QStringLiteral("Save As"));
    check(!quickAddWindow.operationBusy(),
          QStringLiteral("Save draft preflight leaves the workbench idle"));
    const bool quickValidateBlocked = defensiveBoundaryWasBlocked(
        QStringLiteral("Validate"));
    const bool quickGenerateBlocked = defensiveBoundaryWasBlocked(
        QStringLiteral("Generate"));
    const bool quickResizeBlocked = defensiveBoundaryWasBlocked(
        QStringLiteral("Resize"));
    check(quickSaveBlocked,
          QStringLiteral("Save uses the Endpoint canvas draft preflight"));
    check(quickSaveAsBlocked,
          QStringLiteral("Save As uses the Endpoint canvas draft preflight"));
    check(quickValidateBlocked,
          QStringLiteral("Validate uses the Endpoint canvas draft preflight"));
    check(quickGenerateBlocked,
          QStringLiteral("Generate uses the Endpoint canvas draft preflight"));
    check(quickResizeBlocked,
          QStringLiteral("Resize uses the Endpoint canvas draft preflight"));
    check(!quickAddWindow.operationBusy(),
          QStringLiteral(
              "Endpoint draft defensive preflights leave the workbench idle"));
    check(quickEditor
              && quickEditor->endpointCanvasDraftState().pendingNewCount() == 1,
          QStringLiteral(
              "blocked persistent operations preserve the Endpoint canvas draft"));
    if (quickDraftDiscard) {
        chooseMessageBoxButton(QMessageBox::Yes);
        quickDraftDiscard->click();
        application.processEvents();
    }
    check(quickEditor && quickEditor->endpointCanvasDraftState().empty()
              && !quickAddWindow.isWindowModified()
              && quickSave && !quickSave->isEnabled()
              && quickSaveAs && quickSaveAs->isEnabled()
              && quickInstall && quickInstall->isEnabled()
              && quickReload && quickReload->isEnabled()
              && quickLibraryInstall && quickLibraryInstall->isEnabled()
              && quickLibraryReload && quickLibraryReload->isEnabled()
              && quickValidate && quickValidate->isEnabled()
              && quickGenerate && quickGenerate->isEnabled()
              && quickResize && quickResize->isEnabled()
              && quickResultsGenerate && quickResultsGenerate->isEnabled()
              && quickDraftTaskBar && !quickDraftTaskBar->isVisible()
              && quickDraftNotice && !quickDraftNotice->isVisible()
              && (!quickResultsWereVisible
                  || (quickResultsDock && quickResultsDock->isVisible()))
              && (!quickInspectorWasVisible
                  || (quickInspectorDock && quickInspectorDock->isVisible()))
              && quickCanvasFocus && !quickCanvasFocus->isChecked(),
          QStringLiteral(
              "task-bar Discard restores the clean document, persistent actions, and prior panel layout"));

    if (quickEditor) {
        quickEditor->beginDocumentSession(
            QStringLiteral(
                "smoke-endpoint-draft-discard-source-session"));
    }
    if (quickView) {
        QDragEnterEvent dragEnter(
            quickBlankPosition,
            Qt::CopyAction,
            &quickPendingMime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(quickView->viewport(), &dragEnter);
        QDropEvent drop(
            QPointF(quickBlankPosition),
            Qt::CopyAction,
            &quickPendingMime,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(quickView->viewport(), &drop);
        application.processEvents();
    }
    const bool secondDraftEnteredCanvasFocus = waitUntil([
        quickEditor, quickCanvasFocus] {
        return quickEditor
            && quickEditor->endpointCanvasDraftState().pendingNewCount() == 1
            && quickCanvasFocus && quickCanvasFocus->isChecked();
    });
    if (quickCanvasFocus && quickCanvasFocus->isChecked()) {
        quickCanvasFocus->trigger();
        application.processEvents();
        quickCanvasFocus->trigger();
        application.processEvents();
    }
    const bool userReenteredCanvasFocus = quickCanvasFocus
        && quickCanvasFocus->isChecked();
    const QString staleDiscardDraftId = quickEditor
            && !quickEditor->endpointCanvasDraftState().empty()
        ? quickEditor->endpointCanvasDraftState().items().front().id.value
        : QString();
    bool discardConfirmationReplacedSession = false;
    bool replacementReusedDraftIdentity = false;
    if (quickEditor && quickView
        && quickDraftDiscard && quickDraftDiscard->isVisible()) {
        QTimer::singleShot(0, [&] {
            auto* confirmation = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            if (!confirmation
                || confirmation->objectName()
                    != QStringLiteral(
                        "finepaper.discardEndpointDraftsConfirmation")) {
                chooseMessageBoxButton(QMessageBox::Yes);
                return;
            }

            quickEditor->beginDocumentSession(
                QStringLiteral(
                    "smoke-reentrant-endpoint-draft-session"));
            QDragEnterEvent dragEnter(
                quickBlankPosition,
                Qt::CopyAction,
                &quickPendingMime,
                Qt::LeftButton,
                Qt::NoModifier);
            QApplication::sendEvent(quickView->viewport(), &dragEnter);
            QDropEvent drop(
                QPointF(quickBlankPosition),
                Qt::CopyAction,
                &quickPendingMime,
                Qt::LeftButton,
                Qt::NoModifier);
            QApplication::sendEvent(quickView->viewport(), &drop);

            const finepaper::EndpointCanvasDraftState replacementState =
                quickEditor->endpointCanvasDraftState();
            discardConfirmationReplacedSession =
                replacementState.pendingNewCount() == 1;
            replacementReusedDraftIdentity =
                replacementState.items().size() == 1
                && replacementState.items().front().id.value
                    == staleDiscardDraftId;
            if (QAbstractButton* confirm =
                    confirmation->button(QMessageBox::Yes)) {
                confirm->click();
            }
        });
        quickDraftDiscard->click();
        application.processEvents();
    }
    check(discardConfirmationReplacedSession
              && replacementReusedDraftIdentity
              && quickEditor
              && quickEditor->endpointCanvasDraftState().pendingNewCount() == 1
              && nodeGraphicsWithCaptionPrefix(
                  quickScene,
                  QStringLiteral("New Endpoint draft\nMaster endpoint")),
          QStringLiteral(
              "an old Discard confirmation cannot remove an indistinguishable Endpoint draft created by a replacement document session"));
    if (quickEditor) {
        quickEditor->setEditingEnabled(false);
        application.processEvents();
    }
    check(quickDraftReview && quickDraftReview->isEnabled()
              && quickDraftDiscard && quickDraftDiscard->isEnabled()
              && quickDraftConnect && !quickDraftConnect->isEnabled()
              && quickDraftConnect->toolTip().contains(
                  QStringLiteral("read-only"),
                  Qt::CaseInsensitive),
          QStringLiteral(
              "a read-only design still lets users review and discard local new Endpoint drafts while explaining why Connect is unavailable"));
    if (quickDraftDiscard && quickDraftDiscard->isVisible()) {
        chooseMessageBoxButton(QMessageBox::Yes);
        quickDraftDiscard->click();
        application.processEvents();
    }
    if (quickEditor) {
        quickEditor->setEditingEnabled(true);
    }
    check(secondDraftEnteredCanvasFocus && userReenteredCanvasFocus
              && quickEditor
              && quickEditor->endpointCanvasDraftState().empty()
              && quickCanvasFocus && quickCanvasFocus->isChecked(),
          QStringLiteral(
              "resolving a draft does not exit a newer user-owned Canvas Focus session"));
    if (quickCanvasFocus && quickCanvasFocus->isChecked()) {
        quickCanvasFocus->trigger();
        application.processEvents();
    }
    check(quickCanvasFocus && !quickCanvasFocus->isChecked()
              && (!quickResultsWereVisible
                  || (quickResultsDock && quickResultsDock->isVisible()))
              && (!quickInspectorWasVisible
                  || (quickInspectorDock && quickInspectorDock->isVisible())),
          QStringLiteral(
              "exiting the newer user-owned Canvas Focus restores its own panel snapshot"));
    const auto quickRouter = nodeIdWithCaption(
        quickScene, QStringLiteral("r-0-0"));
    if (quickScene && quickRouter) {
        quickScene->clearSelection();
        quickScene->nodeGraphicsObject(*quickRouter)->setSelected(true);
        quickScene->nodeSelected(*quickRouter);
        application.processEvents();
    }
    if (quickFilter) {
        quickFilter->setText(QStringLiteral("slave"));
        application.processEvents();
    }
    check(quickFilter && quickPalette && quickPalette->count() == 2
              && quickPalette->item(0)->isHidden()
              && !quickPalette->item(1)->isHidden(),
          QStringLiteral(
              "Endpoint filtering matches runtime labels without rebuilding the Palette"));
    if (quickFilter) {
        quickFilter->clear();
    }
    if (quickPalette && quickPalette->count() > 0) {
        quickPalette->setCurrentRow(0);
    }
    application.processEvents();
    const std::size_t nodesBeforeQuickAdd = quickScene
        ? quickScene->graphModel().allNodeIds().size() : 0;
    check(quickAddButton && quickAddButton->isEnabled(),
          QStringLiteral(
              "selecting a Router and Endpoint type enables keyboard-friendly quick-add"));
    bool quickBusyObserved = false;
    if (quickValidate && quickAddButton && quickAddButton->isEnabled()) {
        quickValidate->trigger();
        quickBusyObserved = quickAddWindow.operationBusy();
        waitUntil([&quickAddWindow] {
            return !quickAddWindow.operationBusy();
        });
        application.processEvents();
    }
    check(quickValidate && quickBusyObserved
              && quickAddButton && quickAddButton->isEnabled(),
          QStringLiteral(
              "quick-add returns to its enabled state after an asynchronous operation"));
    if (quickAddButton) {
        quickAddButton->click();
        application.processEvents();
    }
    waitUntil([quickScene] {
        auto* graphics = nodeGraphicsWithCaptionPrefix(
            quickScene, QStringLiteral("master_0"));
        return graphics && graphics->isSelected()
            && endpointAttachedToRouter(
                   quickScene, QStringLiteral("r-0-0"))
                   == std::optional<QtNodes::NodeId>(graphics->nodeId());
    }, std::chrono::seconds(2));
    auto* quickAddedGraphics = nodeGraphicsWithCaptionPrefix(
        quickScene, QStringLiteral("master_0"));
    const std::optional<QtNodes::NodeId> quickAddedEndpoint =
        quickAddedGraphics
        ? std::optional<QtNodes::NodeId>(quickAddedGraphics->nodeId())
        : std::nullopt;
    check(quickAddedEndpoint && quickScene
              && quickScene->graphModel().allNodeIds().size()
                     == nodesBeforeQuickAdd + 1
              && endpointAttachedToRouter(
                     quickScene, QStringLiteral("r-0-0"))
                     == quickAddedEndpoint
              && quickAddedGraphics && quickAddedGraphics->isSelected(),
          QStringLiteral(
              "Add to selected Router creates, attaches, and selects the Palette choice"));
    QAction* quickDeleteSelection = quickAnimatedView
        ? quickAnimatedView->deleteSelectionAction() : nullptr;
    const auto quickAddedConnection = quickAddedEndpoint
        ? attachmentConnectionForEndpoint(quickScene, *quickAddedEndpoint)
        : std::nullopt;
    if (quickScene && quickAddedConnection && quickDeleteSelection) {
        quickScene->clearSelection();
        if (auto* connection = quickScene->connectionGraphicsObject(
                *quickAddedConnection)) {
            connection->setSelected(true);
            quickDeleteSelection->trigger();
            application.processEvents();
            application.processEvents();
        }
    }
    check(quickEditor
              && quickEditor->endpointCanvasDraftState().detachedEndpointIds()
                     == QStringList{QStringLiteral("master_0")}
              && nodeGraphicsWithCaptionPrefix(
                     quickScene,
                     QStringLiteral("Disconnected Endpoint\nmaster_0")),
          QStringLiteral(
              "disconnecting a quick-added Endpoint reserves its durable ID in the canvas draft"));
    const auto quickRouterForReservedId = nodeIdWithCaption(
        quickScene, QStringLiteral("r-0-0"));
    if (quickScene && quickRouterForReservedId) {
        quickScene->clearSelection();
        quickScene->nodeGraphicsObject(*quickRouterForReservedId)
            ->setSelected(true);
        quickScene->nodeSelected(*quickRouterForReservedId);
        application.processEvents();
    }
    if (quickPalette && quickPalette->count() > 0) {
        quickPalette->setCurrentRow(0);
    }
    if (quickAddButton && quickAddButton->isEnabled()) {
        quickAddButton->click();
        application.processEvents();
    }
    waitUntil([quickScene] {
        auto* graphics = nodeGraphicsWithCaptionPrefix(
            quickScene, QStringLiteral("master_1"));
        return graphics && endpointAttachedToRouter(
                   quickScene, QStringLiteral("r-0-0"))
                   == std::optional<QtNodes::NodeId>(graphics->nodeId());
    }, std::chrono::seconds(2));
    auto* quickReplacementGraphics = nodeGraphicsWithCaptionPrefix(
        quickScene, QStringLiteral("master_1"));
    check(quickReplacementGraphics && quickEditor
              && quickEditor->endpointCanvasDraftState().detachedEndpointIds()
                     == QStringList{QStringLiteral("master_0")}
              && !nodeGraphicsWithCaptionPrefix(
                     quickScene, QStringLiteral("master_0\n")),
          QStringLiteral(
              "new Endpoint allocation skips IDs reserved by disconnected canvas Endpoints"));
    if (quickScene && quickReplacementGraphics && quickDeleteSelection) {
        quickScene->clearSelection();
        quickReplacementGraphics->setSelected(true);
        quickScene->nodeSelected(quickReplacementGraphics->nodeId());
        chooseMessageBoxButton(QMessageBox::Yes);
        quickDeleteSelection->trigger();
        application.processEvents();
    }
    auto* quickDetachedGraphics = nodeGraphicsWithCaptionPrefix(
        quickScene, QStringLiteral("Disconnected Endpoint\nmaster_0"));
    if (quickScene && quickDetachedGraphics && quickDeleteSelection) {
        quickScene->clearSelection();
        quickDetachedGraphics->setSelected(true);
        quickScene->nodeSelected(quickDetachedGraphics->nodeId());
        chooseMessageBoxButton(QMessageBox::Yes);
        quickDeleteSelection->trigger();
        application.processEvents();
    }
    const std::size_t nodesAfterQuickDelete = quickScene
        ? quickScene->graphModel().allNodeIds().size() : 0;
    check(quickAnimatedView && quickScene && quickEditor
              && nodesAfterQuickDelete == nodesBeforeQuickAdd
              && !nodeGraphicsWithCaptionPrefix(
                  quickScene, QStringLiteral("master_0"))
              && quickEditor->endpointCanvasDraftState().detachedEndpointIds()
                     .isEmpty(),
          QStringLiteral(
              "the canvas Delete shortcut permanently removes the selected Endpoint instead of detaching it"));
    closeDiscarding(quickAddWindow);

    QTemporaryDir slowOperationRoot(
        QStringLiteral("/tmp/finepaper-slow-operation-XXXXXX"));
    const QString slowPackageRoot = QDir(slowOperationRoot.path()).filePath(
        QStringLiteral("slow-package"));
    const QString slowDesignPath = QDir(slowOperationRoot.path()).filePath(
        QStringLiteral("slow-operation.fpnoc"));
    check(slowOperationRoot.isValid()
              && createSlowOperationPackage(
                  QDir(projectRoot).filePath(
                      QStringLiteral("packages/finepaper-noc")),
                  slowPackageRoot)
              && writeSlowOperationDesign(slowDesignPath),
          QStringLiteral(
              "a deterministic slow Package fixture is available for operation lifecycle testing"));
    finepaper::RuntimeLocations slowLocations = {
        QStringList{slowPackageRoot},
        QDir(slowOperationRoot.path()).filePath(QStringLiteral("output"))};
    finepaper::FinepaperMainWindow slowWindow(slowLocations);
    slowWindow.show();
    application.processEvents();
    check(slowWindow.openDesignFile(slowDesignPath),
          QStringLiteral("the slow operation design opens as an editable clean document"));
    auto* slowValidate = actionWithText(
        slowWindow, QStringLiteral("Validate / DRC"));
    auto* slowReduceMotion = slowWindow.findChild<QAction*>(
        finepaper::workbench::reducedMotionActionName);
    auto* operationStrip = slowWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.operationTaskStrip"));
    auto* operationState = slowWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.operationTaskState"));
    auto* operationName = slowWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.operationTaskName"));
    auto* cancelOperation = slowWindow.findChild<QPushButton*>(
        QStringLiteral("finepaper.cancelOperation"));
    auto* slowCanvasView = slowWindow.findChild<QWidget*>(
        QStringLiteral("finepaper.canvasView"));
    auto* slowDiagnosticsStatus = slowWindow.findChild<QLabel*>(
        QStringLiteral("finepaper.diagnosticsStatus"));
    if (slowReduceMotion) {
        slowReduceMotion->setChecked(false);
    }
    if (slowValidate) {
        slowValidate->trigger();
    }
    const bool operationFeedbackAppeared = waitUntil(
        [operationStrip, operationState, &slowWindow] {
            return slowWindow.operationBusy()
                && operationStrip
                && operationStrip->isVisibleTo(&slowWindow)
                && operationState
                && operationState->text() == QStringLiteral("Running");
        },
        std::chrono::seconds(3));
    QTest::qWait(400);
    check(slowValidate && slowWindow.operationBusy()
              && operationStrip && operationStrip->isVisibleTo(&slowWindow)
              && operationState
              && operationState->text() == QStringLiteral("Running")
              && operationName
              && operationName->accessibleName().contains(
                  QStringLiteral("Validating"))
              && cancelOperation && cancelOperation->isEnabled()
              && operationFeedbackAppeared,
          QStringLiteral(
              "a slow validation exposes operation text, delayed progress, and an enabled Cancel action"));
    captureSmokeScreenshot(
        slowWindow, QStringLiteral("operation-running"), requestedTheme);
    activateWindowForKeyboard(slowWindow);
    if (cancelOperation) {
        cancelOperation->setFocus(Qt::TabFocusReason);
        QTest::keyClick(cancelOperation, Qt::Key_Space);
    }
    application.processEvents();
    check(slowWindow.operationBusy()
              && operationState
              && operationState->text()
                     == QStringLiteral("Cancel requested")
              && cancelOperation && !cancelOperation->isEnabled()
              && cancelOperation->text() == QString::fromUtf8("Cancelling…")
              && slowCanvasView && focusIsWithin(slowCanvasView),
          QStringLiteral(
              "keyboard Cancel becomes a visible idempotent request and returns focus to the canvas"));
    check(waitUntil(
              [&slowWindow] { return !slowWindow.operationBusy(); },
              std::chrono::seconds(5)),
          QStringLiteral(
              "cancelling validation waits for the Package process group and returns the workbench to idle"));
    check(operationStrip && !operationStrip->isVisibleTo(&slowWindow)
              && slowDiagnosticsStatus
              && slowDiagnosticsStatus->text().startsWith(
                  QStringLiteral("Cancelled")),
          QStringLiteral(
              "a cancelled validation publishes no replacement result and leaves explicit recovery text"));

    if (slowValidate) {
        slowValidate->trigger();
    }
    check(waitUntil(
              [&slowWindow, operationState] {
                  return slowWindow.operationBusy()
                      && operationState
                      && operationState->text() == QStringLiteral("Running");
              },
              std::chrono::seconds(3)),
          QStringLiteral("a second slow validation starts for Close-flow testing"));
    chooseVisibleButtonByObjectName(
        QStringLiteral("finepaper.keepOperationRunning"));
    slowWindow.close();
    application.processEvents();
    check(slowWindow.isVisible() && slowWindow.operationBusy()
              && operationState
              && operationState->text() == QStringLiteral("Running")
              && cancelOperation && cancelOperation->isEnabled(),
          QStringLiteral(
              "Keep Running is the safe Close choice and leaves the operation unchanged"));

    chooseVisibleButtonByObjectName(
        QStringLiteral("finepaper.cancelOperationAndClose"));
    slowWindow.close();
    application.processEvents();
    const bool closeCancellationVisible =
        !slowWindow.operationBusy()
        || (operationState
            && operationState->text()
                   == QStringLiteral("Cancel requested"));
    check(closeCancellationVisible,
          QStringLiteral(
              "Cancel Operation and Close first presents cancellation instead of destroying a live worker"));
    check(waitUntil(
              [&slowWindow] {
                  return !slowWindow.operationBusy()
                      && !slowWindow.isVisible();
              },
              std::chrono::seconds(5)),
          QStringLiteral(
              "Cancel Operation and Close closes only after the Package process has stopped"));

    check(makeSlowOperationPackageCompleteNaturally(slowPackageRoot),
          QStringLiteral(
              "the operation fixture can complete inside the Close confirmation event loop"));
    finepaper::FinepaperMainWindow closeRaceWindow(slowLocations);
    closeRaceWindow.show();
    application.processEvents();
    check(closeRaceWindow.openDesignFile(slowDesignPath),
          QStringLiteral("the Close completion-race design opens cleanly"));
    QAction* closeRaceValidate = actionWithText(
        closeRaceWindow, QStringLiteral("Validate / DRC"));
    if (closeRaceValidate) {
        closeRaceValidate->trigger();
    }
    check(closeRaceWindow.operationBusy(),
          QStringLiteral(
              "the Close completion-race starts while validation is busy"));
    QTimer::singleShot(400, [] {
        chooseVisibleButtonByObjectName(
            QStringLiteral("finepaper.cancelOperationAndClose"));
    });
    closeRaceWindow.close();
    application.processEvents();
    check(!closeRaceWindow.operationBusy()
              && !closeRaceWindow.isVisible(),
          QStringLiteral(
              "Cancel Operation and Close still closes when the worker finishes inside the confirmation dialog"));

    QTextStream(stdout) << (failures == 0 ? "finepaper-gui-smoke passed"
                                          : "finepaper-gui-smoke failed")
                        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
