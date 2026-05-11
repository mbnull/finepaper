// CollapsibleSection provides a compact Eclipse-style expandable section.
#pragma once

#include <QString>
#include <QWidget>

class QToolButton;
class QVBoxLayout;

class CollapsibleSection : public QWidget {
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    void setContentWidget(QWidget* content);
    QWidget* contentWidget() const;
    QToolButton* toggleButton() const;

    bool isExpanded() const;
    void setExpanded(bool expanded);

private:
    void updateTogglePresentation();

    QToolButton* m_toggleButton = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_layout = nullptr;
};
