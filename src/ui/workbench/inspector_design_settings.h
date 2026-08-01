#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QToolButton;
class QVBoxLayout;

namespace finepaper::ui {

// Text-first disclosure for design-wide Inspector controls.  The notice stays
// visible while the controls are collapsed so an unapplied parameter draft is
// never hidden from the user.
class InspectorDesignSettings final : public QWidget {
public:
    explicit InspectorDesignSettings(QWidget* parent = nullptr);

    void addSection(QWidget* section);
    [[nodiscard]] bool isExpanded() const;
    void setExpanded(bool expanded);
    void setDraftNotice(const QString& text,
                        const QString& semanticRole = QStringLiteral("warning"));

private:
    void updateToggleText();

    QToolButton* m_toggle = nullptr;
    QLabel* m_draftNotice = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
};

} // namespace finepaper::ui
