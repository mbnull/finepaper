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
inline const QString collapsedRoutersSetting = QStringLiteral("workbench/collapsedRouters");

inline const QString packageDockName = QStringLiteral("finepaper.packageDock");
inline const QString inspectorDockName = QStringLiteral("finepaper.inspectorDock");
inline const QString resultsDockName = QStringLiteral("finepaper.resultsDock");
inline const QString activityBarName = QStringLiteral("finepaper.activityBar");

inline const QString packageToggleActionName = QStringLiteral("finepaper.togglePackagePanel");
inline const QString inspectorToggleActionName = QStringLiteral("finepaper.toggleInspectorPanel");
inline const QString resultsToggleActionName = QStringLiteral("finepaper.toggleResultsPanel");

inline const QString editorViewId = QStringLiteral("noc-editor");
inline const QString performanceViewId = QStringLiteral("performance-analysis");
inline const QString problemReportViewId = QStringLiteral("problem-report");

inline const QString editorViewTitle = QStringLiteral("NoC Editor");
inline const QString performanceViewTitle = QStringLiteral("Performance Analysis");
inline const QString problemReportViewTitle = QStringLiteral("Problem Report");

inline const QString drcTabTitle = QStringLiteral("DRC Problems");
inline const QString activityTabTitle = QStringLiteral("Activity Log");
inline const QString generationTabTitle = QStringLiteral("Generation Outputs");

} // namespace finepaper::workbench
