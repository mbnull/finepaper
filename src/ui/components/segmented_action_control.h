#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QEvent;
class QToolButton;

namespace finepaper::ui {

// Presents a small, mutually exclusive set of text actions as one control.
// The QAction objects remain the source of truth for text, state, shortcuts,
// and availability.
class SegmentedActionControl final : public QWidget {
    Q_DISABLE_COPY_MOVE(SegmentedActionControl)

public:
    explicit SegmentedActionControl(QWidget* parent = nullptr);

    QToolButton* addAction(QAction* action,
                           const QString& objectName = QString());
    [[nodiscard]] QList<QToolButton*> buttons() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] int buttonIndex(const QObject* object) const;
    [[nodiscard]] QToolButton* adjacentButton(int currentIndex,
                                               int step) const;
    [[nodiscard]] QToolButton* edgeButton(bool fromEnd) const;
    void activateButton(QToolButton* button);
    void ensureExclusiveSelection(QAction* preferredAction);

    QList<QToolButton*> m_buttons;
};

} // namespace finepaper::ui
