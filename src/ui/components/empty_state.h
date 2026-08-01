#pragma once

#include <QFrame>
#include <QString>

class QEvent;
class QLabel;
class QLayout;
class QPushButton;
class QResizeEvent;
class QShowEvent;

namespace finepaper::ui {

class EmptyState final : public QFrame {
public:
    explicit EmptyState(QWidget* parent = nullptr);

    void setEyebrow(const QString& text);
    void setTitle(const QString& text);
    void setDescription(const QString& text);
    QPushButton* addActionButton(const QString& text,
                                 const QString& role = QString());

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void applyRoleFonts();
    void requestReflow();

    QLabel* m_eyebrow = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_description = nullptr;
    QLayout* m_actions = nullptr;
    bool m_reflowPending = false;
};

} // namespace finepaper::ui
