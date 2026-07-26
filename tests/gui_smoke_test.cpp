#include "gui/main_window.h"
#include "gui/noc_editor_style.h"
#include "gui/noc_node_editor.h"
#include "gui/workbench_config.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QGraphicsView>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolBar>

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

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

    const qsizetype itemsBeforeEndpoint = graphicsView && graphicsView->scene()
        ? graphicsView->scene()->items().size() : 0;
    if (nodeEditor && nodeEditor->endpointTypeDropped) {
        nodeEditor->endpointTypeDropped(QStringLiteral("master"), finepaper::RouterPosition{0, 0});
        application.processEvents();
    }
    const qsizetype itemsAfterEndpoint = graphicsView && graphicsView->scene()
        ? graphicsView->scene()->items().size() : 0;
    check(itemsAfterEndpoint > itemsBeforeEndpoint,
          QStringLiteral("Endpoint drop callback updates NocDesign and rebuilds the NodeEditor"));

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
    restoredWindow.close();

    QTextStream(stdout) << (failures == 0 ? "finepaper-gui-smoke passed"
                                          : "finepaper-gui-smoke failed")
                        << Qt::endl;
    return failures == 0 ? 0 : 1;
}
