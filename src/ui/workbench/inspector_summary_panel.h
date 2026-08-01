#pragma once

#include <QString>
#include <QWidget>

#include <optional>

class QEvent;
class QFrame;
class QLabel;

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

class InspectorSummaryPanel final : public QWidget {
public:
    explicit InspectorSummaryPanel(QWidget* parent = nullptr);

    void setDesignSummary(const InspectorDesignSummary& summary);
    void setSelectionSummary(const std::optional<InspectorSelectionSummary>& summary);

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyRoleFonts();

    QWidget* m_designContext = nullptr;
    QLabel* m_designTitle = nullptr;
    QLabel* m_designMetadata = nullptr;
    QLabel* m_designAvailability = nullptr;

    QFrame* m_selectionContext = nullptr;
    QLabel* m_selectionTitle = nullptr;
    QLabel* m_selectionMetadata = nullptr;
    QLabel* m_selectionDetail = nullptr;
};

} // namespace finepaper::ui
