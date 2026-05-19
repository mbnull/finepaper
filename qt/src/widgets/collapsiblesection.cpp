// CollapsibleSection implementation.
#include "widgets/collapsiblesection.h"

#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent) {
    m_toggleButton = new QToolButton(this);
    m_toggleButton->setText(title);
    m_toggleButton->setCheckable(true);
    m_toggleButton->setChecked(true);
    m_toggleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggleButton->setAutoRaise(false);
    m_toggleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_toggleButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  border: 1px solid #c7c7c7;"
        "  background: #e9e9e9;"
        "  padding: 3px 4px;"
        "  text-align: left;"
        "}"
        "QToolButton:checked {"
        "  background: #eeeeee;"
        "}"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addWidget(m_toggleButton);

    connect(m_toggleButton, &QToolButton::toggled, this, [this](bool expanded) {
        if (m_contentWidget) {
            m_contentWidget->setVisible(expanded);
        }
        updateTogglePresentation();
    });
    updateTogglePresentation();
}

void CollapsibleSection::setContentWidget(QWidget* content) {
    if (m_contentWidget == content) {
        return;
    }

    if (m_contentWidget) {
        m_layout->removeWidget(m_contentWidget);
        m_contentWidget->deleteLater();
    }

    m_contentWidget = content;
    if (!m_contentWidget) {
        return;
    }

    m_contentWidget->setParent(this);
    m_contentWidget->setVisible(isExpanded());
    m_layout->addWidget(m_contentWidget);
}

QWidget* CollapsibleSection::contentWidget() {
    return m_contentWidget;
}

const QWidget* CollapsibleSection::contentWidget() const {
    return m_contentWidget;
}

QToolButton* CollapsibleSection::toggleButton() {
    return m_toggleButton;
}

const QToolButton* CollapsibleSection::toggleButton() const {
    return m_toggleButton;
}

bool CollapsibleSection::isExpanded() const {
    return m_toggleButton && m_toggleButton->isChecked();
}

void CollapsibleSection::setExpanded(bool expanded) {
    if (!m_toggleButton) {
        return;
    }
    m_toggleButton->setChecked(expanded);
    if (m_contentWidget) {
        m_contentWidget->setVisible(expanded);
    }
    updateTogglePresentation();
}

void CollapsibleSection::updateTogglePresentation() {
    if (!m_toggleButton) {
        return;
    }
    m_toggleButton->setArrowType(isExpanded() ? Qt::DownArrow : Qt::RightArrow);
}
