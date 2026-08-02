#pragma once

#include <QFrame>
#include <QString>

#include <functional>

class QLabel;
class QPushButton;
class QWidget;

namespace finepaper {

// Presentation state for the focused Domain-assignment task. The task bar
// deliberately owns no assignment data: DomainManagerPanel remains the source
// of truth and supplies concise, selection-specific copy through this state.
struct DomainAssignmentTaskBarState final {
    bool taskActive = false;
    QString title;
    QString status;
    QString applyText;
    QString discardText;
    bool applyEnabled = false;
    bool discardEnabled = false;
    QString applyUnavailableReason;
    QString discardUnavailableReason;
    QString discardAccessibleDescription;

    bool operator==(const DomainAssignmentTaskBarState&) const = default;
};

// Text-first task route for applying or discarding a staged Domain assignment.
// Apply and Discard remain present in every state so the task has a stable
// ending; inactive or unavailable actions explain their state accessibly.
class DomainAssignmentTaskBar final : public QFrame {
    Q_DISABLE_COPY_MOVE(DomainAssignmentTaskBar)

public:
    explicit DomainAssignmentTaskBar(QWidget* parent = nullptr);

    void setState(const DomainAssignmentTaskBarState& state);
    [[nodiscard]] const DomainAssignmentTaskBarState& state() const {
        return m_state;
    }
    [[nodiscard]] QWidget* preferredFocusTarget();

    std::function<void()> applyRequested;
    std::function<void()> discardRequested;

private:
    void updatePresentation();

    DomainAssignmentTaskBarState m_state;
    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_discard = nullptr;
};

} // namespace finepaper
