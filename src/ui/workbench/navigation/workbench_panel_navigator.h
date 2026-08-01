#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

class QAction;
class QDockWidget;
class QMainWindow;
class QMenu;
class QWidget;

namespace finepaper::ui {

enum class WorkbenchPanelId {
    Package,
    Inspector,
    Domain,
    Results,
};

enum class WorkbenchPanelIntent {
    Resume,
    EditSelection,
    ReviewDiagnostics,
};

enum class PanelVisibilityMode {
    ResponsivePreference,
    NativeDock,
};

struct WorkbenchPanelRoute final {
    WorkbenchPanelId id = WorkbenchPanelId::Package;
    QDockWidget* dock = nullptr;
    PanelVisibilityMode visibilityMode =
        PanelVisibilityMode::ResponsivePreference;
    std::function<QWidget*(WorkbenchPanelIntent)> focusTarget;
};

// Owns the workbench's text-first panel commands and routes explicit
// activation to MainWindow. Layout policy remains exclusively owned by
// WorkbenchLayoutController; this class only presents commands and repairs
// focus after a Dock is hidden by responsive reflow.
class WorkbenchPanelNavigator final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WorkbenchPanelNavigator)

public:
    explicit WorkbenchPanelNavigator(
        QMainWindow& window,
        QList<WorkbenchPanelRoute> routes);

    [[nodiscard]] QAction* visibilityAction(WorkbenchPanelId id);
    [[nodiscard]] QAction* navigationAction(WorkbenchPanelId id);
    [[nodiscard]] QDockWidget* dock(WorkbenchPanelId id);
    void addVisibilityActions(QMenu& menu) const;
    void addNavigationActions(QMenu& menu) const;
    void activate(
        WorkbenchPanelId id,
        WorkbenchPanelIntent intent = WorkbenchPanelIntent::Resume);

signals:
    void panelActivationRequested(
        finepaper::ui::WorkbenchPanelId id,
        finepaper::ui::WorkbenchPanelIntent intent);
    void panelActivationFailed(
        finepaper::ui::WorkbenchPanelId id,
        finepaper::ui::WorkbenchPanelIntent intent);
    void workspaceFocusRequested();

private:
    struct RouteState final {
        QPointer<QDockWidget> dock;
        QPointer<QAction> visibilityAction;
        QPointer<QAction> navigationAction;
        std::function<QWidget*(WorkbenchPanelIntent)> focusTarget;
    };

    struct PendingActivation final {
        WorkbenchPanelId id = WorkbenchPanelId::Package;
        WorkbenchPanelIntent intent = WorkbenchPanelIntent::Resume;
        int remainingRetries = 0;
    };

    [[nodiscard]] RouteState* route(WorkbenchPanelId id);
    [[nodiscard]] bool focusIsUsable(QWidget* focus) const;
    [[nodiscard]] bool focusReachedTarget(QWidget* target) const;
    [[nodiscard]] bool panelIsExposed(const QDockWidget* dock) const;
    void requestFocusRepair(QDockWidget* dock);
    void scheduleFocusDispatch();
    void dispatchFocus();

    inline static constexpr std::size_t panelCount =
        static_cast<std::size_t>(WorkbenchPanelId::Results) + 1;

    QMainWindow& m_window;
    std::array<std::optional<RouteState>, panelCount> m_routes;
    QTimer m_focusTimer;
    std::optional<PendingActivation> m_pendingActivation = std::nullopt;
    QPointer<QWidget> m_lastNonNullFocus;
    bool m_focusRepairPending = false;
};

} // namespace finepaper::ui
