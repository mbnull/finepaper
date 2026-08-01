#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <optional>

class QAction;
class QDockWidget;
class QEvent;
class QMainWindow;
class QWidget;

namespace finepaper::ui {

enum class WorkbenchPanelRole {
    Package,
    Inspector,
    Domain,
};

enum class WorkbenchWidthMode {
    Wide,
    Compact,
    CanvasOnly,
};

struct WorkbenchWidthRequirements final {
    int availableWidth = 0;
    int allPanelsWidth = 0;
    int onePanelWidth = 0;
    int hysteresis = 0;
};

// Pure width policy used by the controller and its focused regression tests.
// Requirements are measured from the current font, Dock extents, and style.
[[nodiscard]] WorkbenchWidthMode classifyWorkbenchWidth(
    const WorkbenchWidthRequirements& requirements,
    WorkbenchWidthMode currentMode);

struct WorkbenchPanelBinding final {
    WorkbenchPanelRole role = WorkbenchPanelRole::Package;
    QDockWidget* dock = nullptr;
    QAction* preferenceAction = nullptr;
};

// Arbitrates persistent panel intent, automatic responsive suppression, and
// the explicit transient Canvas Focus mode. Physical Dock visibility is never
// used as a substitute for the user's preference.
class WorkbenchLayoutController final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WorkbenchLayoutController)

public:
    using PresentationCallback =
        std::function<void(WorkbenchWidthMode)>;

    WorkbenchLayoutController(
        QMainWindow* window,
        QWidget* centerWorkspace,
        QList<WorkbenchPanelBinding> sidePanels,
        QList<QDockWidget*> secondaryPanels,
        PresentationCallback presentationCallback = {},
        QWidget* compactNavigation = nullptr);

    void start();
    void reevaluateNow();

    [[nodiscard]] WorkbenchWidthMode widthMode() const;
    [[nodiscard]] bool panelAutoSuppressed(WorkbenchPanelRole role) const;
    [[nodiscard]] bool userPanelVisible(WorkbenchPanelRole role) const;
    void setCompactPreferredPanel(WorkbenchPanelRole role);
    void setUserPanelVisible(WorkbenchPanelRole role, bool visible);
    void revealPanel(WorkbenchPanelRole role);
    void captureUserPanelVisibilityFromLayout();
    void restoreUserPanelVisibility(const QVariantMap& settings);
    [[nodiscard]] QVariantMap persistentUserPanelVisibility() const;

    [[nodiscard]] bool canvasFocusActive() const;
    bool enterCanvasFocus();
    bool leaveCanvasFocus();
    [[nodiscard]] QByteArray persistentWindowState() const;
    void beginShutdown();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PanelState final {
        WorkbenchPanelBinding binding;
        bool userVisible = true;
        bool autoSuppressed = false;
        int readableExtent = 0;
        QString preferenceLabel;
        QString preferenceStatusTip;
    };

    [[nodiscard]] PanelState* panel(WorkbenchPanelRole role);
    [[nodiscard]] const PanelState* panel(WorkbenchPanelRole role) const;
    [[nodiscard]] WorkbenchWidthRequirements widthRequirements();
    [[nodiscard]] int readableWideCenterWidth() const;
    [[nodiscard]] int readableCompactCenterWidth() const;
    [[nodiscard]] int readableCenterWidthForMode() const;
    [[nodiscard]] int readablePanelExtent(PanelState& state) const;
    [[nodiscard]] bool isContextRole(WorkbenchPanelRole role) const;
    void scheduleReevaluation();
    void applyResponsivePanelVisibility();
    void applyPanelVisibility(PanelState& state, bool visible);
    void syncPreferenceAction(PanelState& state);
    void setCompactReveal(std::optional<WorkbenchPanelRole> role);

    QMainWindow* m_window = nullptr;
    QWidget* m_centerWorkspace = nullptr;
    QWidget* m_compactNavigation = nullptr;
    QList<PanelState> m_sidePanels;
    QList<QDockWidget*> m_secondaryPanels;
    PresentationCallback m_presentationCallback;
    QTimer m_reflowTimer;
    QByteArray m_canvasFocusRestoreState;
    std::optional<WorkbenchPanelRole> m_compactReveal = std::nullopt;
    WorkbenchPanelRole m_compactPreferredPanel =
        WorkbenchPanelRole::Inspector;
    bool m_compactRevealTransitionPending = false;
    WorkbenchWidthMode m_widthMode = WorkbenchWidthMode::Wide;
    bool m_presentationInitialized = false;
    bool m_started = false;
    bool m_applyingLayout = false;
    bool m_canvasFocusActive = false;
    bool m_shuttingDown = false;
};

} // namespace finepaper::ui
