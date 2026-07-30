#pragma once

#include <QString>

namespace finepaper::workbench {

inline const QString endpointTypeMime = QStringLiteral("application/x-finepaper-noc-endpoint-type");

inline const QString packageRootsSetting = QStringLiteral("runtime/installedPackageRoots");
inline const QString geometrySetting = QStringLiteral("workbench/geometry");
inline const QString windowStateSetting = QStringLiteral("workbench/windowState");
inline const QString centerViewSetting = QStringLiteral("workbench/centerView");
inline const QString resultTabSetting = QStringLiteral("workbench/resultTab");
inline const QString routerLayoutsSetting = QStringLiteral("workbench/routerLayouts");
inline const QString endpointLayoutsSetting = QStringLiteral("workbench/endpointLayouts");
inline const QString collapsedRoutersSetting = QStringLiteral("workbench/collapsedRouters");
inline const QString domainLayerSelectionsSetting =
    QStringLiteral("workbench/domainLayerSelections");

inline const QString packageDockName = QStringLiteral("finepaper.packageDock");
inline const QString inspectorDockName = QStringLiteral("finepaper.inspectorDock");
inline const QString domainManagerDockName =
    QStringLiteral("finepaper.domainManagerDock");
inline const QString resultsDockName = QStringLiteral("finepaper.resultsDock");
inline const QString activityBarName = QStringLiteral("finepaper.activityBar");
inline const QString selectionInspectorName = QStringLiteral("finepaper.selectionInspector");
inline const QString endpointContextMenuName = QStringLiteral("finepaper.endpointContextMenu");
inline const QString deleteEndpointActionName = QStringLiteral("finepaper.deleteEndpointAction");
inline const QString detachEndpointActionName = QStringLiteral("finepaper.detachEndpointAction");
inline const QString connectionContextMenuName = QStringLiteral("finepaper.connectionContextMenu");
inline const QString disconnectConnectionActionName =
    QStringLiteral("finepaper.disconnectConnectionAction");
inline const QString routerContextMenuName = QStringLiteral("finepaper.routerContextMenu");
inline const QString canvasContextMenuName = QStringLiteral("finepaper.canvasContextMenu");
inline const QString createEndpointMenuName = QStringLiteral("finepaper.createEndpointMenu");
inline const QString connectRouterMenuName = QStringLiteral("finepaper.connectRouterMenu");

inline const QString packageToggleActionName = QStringLiteral("finepaper.togglePackagePanel");
inline const QString inspectorToggleActionName = QStringLiteral("finepaper.toggleInspectorPanel");
inline const QString domainManagerToggleActionName =
    QStringLiteral("finepaper.toggleDomainManagerPanel");
inline const QString resultsToggleActionName = QStringLiteral("finepaper.toggleResultsPanel");
inline const QString regularizeActionName = QStringLiteral("finepaper.regularizeLayout");
inline const QString selectCanvasActionName =
    QStringLiteral("finepaper.selectCanvasMode");
inline const QString panCanvasActionName =
    QStringLiteral("finepaper.panCanvasMode");
inline const QString domainLayerSelectorName =
    QStringLiteral("finepaper.domainLayerSelector");

inline const QString editorViewId = QStringLiteral("noc-editor");
inline const QString domainConfigurationViewId =
    QStringLiteral("domain-configuration");
inline const QString performanceViewId = QStringLiteral("performance-analysis");
inline const QString problemReportViewId = QStringLiteral("problem-report");

inline const QString editorViewTitle = QStringLiteral("NoC Editor");
inline const QString domainConfigurationViewTitle =
    QStringLiteral("Domain Configuration");
inline const QString performanceViewTitle = QStringLiteral("Performance Analysis");
inline const QString problemReportViewTitle = QStringLiteral("Problem Report");

inline const QString drcTabTitle = QStringLiteral("DRC Problems");
inline const QString activityTabTitle = QStringLiteral("Activity Log");
inline const QString generationTabTitle = QStringLiteral("Generation Outputs");

inline QString designWorkspaceKey(const QString& packageId,
                                  const QString& packageVersion,
                                  const QString& designId) {
    const auto segment = [](const QString& value) {
        return QString::number(value.size()) + QLatin1Char(':') + value;
    };
    return segment(packageId) + segment(packageVersion) + segment(designId);
}

} // namespace finepaper::workbench
