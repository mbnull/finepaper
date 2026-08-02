#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QEvent;
class QFrame;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace finepaper::ui {

// Presentation-only data for the open design. Model-specific formatting stays
// with the workbench controller so this component can remain reusable.
struct InspectorDesignSummary final {
    QString title;
    QString metadata;
    QString availability;
};

// Presentation-only data for the active canvas selection. An absent summary
// hides the selection context without removing or rebuilding its widgets.
struct InspectorSelectionSummary final {
    QString title;
    QString metadata;
    QString detail;
};

struct InspectorContextActions final {
    bool editDomainAssignments = false;
    bool reviewDiagnostics = false;
    bool disconnectEndpointAttachment = false;
};

class InspectorSummaryPanel final : public QWidget {
public:
    explicit InspectorSummaryPanel(QWidget* parent = nullptr);

    void setDesignSummary(const InspectorDesignSummary& summary);
    void setSelectionSummary(const std::optional<InspectorSelectionSummary>& summary);
    // Selection tasks use a compact context header so the first editable
    // field remains in the Inspector viewport. Details stay available through
    // an explicit text disclosure.
    void setSelectionTaskFocused(bool focused);
    void setContextActions(const InspectorContextActions& actions);
    [[nodiscard]] QWidget* preferredFocusTarget();

    std::function<void()> editDomainAssignmentsRequested;
    std::function<void()> reviewDiagnosticsRequested;
    std::function<void()> disconnectEndpointAttachmentRequested;

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyRoleFonts();
    void updatePresentation();

    QWidget* m_designContext = nullptr;
    QLabel* m_designTitle = nullptr;
    QLabel* m_designMetadata = nullptr;
    QLabel* m_designAvailability = nullptr;

    QFrame* m_selectionContext = nullptr;
    QLabel* m_selectionTitle = nullptr;
    QLabel* m_selectionMetadata = nullptr;
    QLabel* m_selectionDetail = nullptr;
    QToolButton* m_selectionDetailToggle = nullptr;
    QVBoxLayout* m_selectionLayout = nullptr;
    QWidget* m_contextActions = nullptr;
    QPushButton* m_editDomainAssignments = nullptr;
    QPushButton* m_reviewDiagnostics = nullptr;
    QPushButton* m_disconnectEndpointAttachment = nullptr;
    bool m_hasDesignSummary = false;
    bool m_hasSelection = false;
    bool m_hasSelectionDetail = false;
    bool m_selectionTaskFocused = false;
    bool m_selectionDetailsExpanded = false;
    QString m_selectionIdentity;
};

} // namespace finepaper::ui
