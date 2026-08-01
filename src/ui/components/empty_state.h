#pragma once

#include <QFrame>
#include <QString>

class QHBoxLayout;
class QEvent;
class QLabel;
class QPushButton;

namespace finepaper::ui {

class EmptyState final : public QFrame {
public:
    explicit EmptyState(QWidget* parent = nullptr);

    void setEyebrow(const QString& text);
    void setTitle(const QString& text);
    void setDescription(const QString& text);
    QPushButton* addActionButton(const QString& text,
                                 const QString& role = QString());

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyRoleFonts();

    QLabel* m_eyebrow = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_description = nullptr;
    QHBoxLayout* m_actions = nullptr;
};

} // namespace finepaper::ui
