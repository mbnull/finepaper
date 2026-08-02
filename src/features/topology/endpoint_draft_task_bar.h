#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>

#include <functional>

class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QWidget;

namespace finepaper {

struct EndpointDraftTaskBarState final {
    bool visible = false;
    QString title;
    QString guidance;
    QString reviewText;
    QString connectText;
    QString discardText;
    bool reviewEnabled = false;
    bool connectEnabled = false;
    bool discardEnabled = false;
    bool deletesDetachedEndpoints = false;
    QString connectUnavailableReason;
    QString discardUnavailableReason;

    bool operator==(const EndpointDraftTaskBarState&) const = default;
};

// Text-first task route for unresolved Endpoint canvas drafts. The topology
// workspace owns the draft lifecycle and connection menu; this widget only
// presents the current task and forwards explicit review/discard requests.
class EndpointDraftTaskBar final : public QFrame {
    Q_DISABLE_COPY_MOVE(EndpointDraftTaskBar)

public:
    explicit EndpointDraftTaskBar(QWidget* parent = nullptr);

    void setState(const EndpointDraftTaskBarState& state);
    void setConnectMenu(QMenu* menu);
    [[nodiscard]] QWidget* preferredFocusTarget();

    std::function<void()> reviewRequested;
    std::function<void()> discardRequested;

private:
    void updatePresentation();
    void updateConnectPresentation();

    EndpointDraftTaskBarState m_state;
    QPointer<QMenu> m_connectMenu;
    QLabel* m_title = nullptr;
    QLabel* m_guidance = nullptr;
    QLabel* m_connectStatus = nullptr;
    QPushButton* m_review = nullptr;
    QToolButton* m_connect = nullptr;
    QPushButton* m_discard = nullptr;
};

} // namespace finepaper
