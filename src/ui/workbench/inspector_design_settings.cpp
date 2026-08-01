#include "ui/workbench/inspector_design_settings.h"

#include "ui/theme/ui_tokens.h"

#include <QLabel>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace finepaper::ui {

InspectorDesignSettings::InspectorDesignSettings(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("finepaper.inspectorDesignSettings"));
    setMinimumWidth(0);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(UiMetrics::spacing8);

    m_toggle = new QToolButton(this);
    m_toggle->setObjectName(
        QStringLiteral("finepaper.inspectorDesignSettingsToggle"));
    m_toggle->setCheckable(true);
    m_toggle->setChecked(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_toggle->setArrowType(Qt::NoArrow);
    m_toggle->setProperty("finepaperRole", QStringLiteral("quiet"));
    m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_toggle);

    m_draftNotice = new QLabel(this);
    m_draftNotice->setObjectName(
        QStringLiteral("finepaper.parameterDraftStatus"));
    m_draftNotice->setTextFormat(Qt::PlainText);
    m_draftNotice->setWordWrap(true);
    m_draftNotice->setMinimumWidth(0);
    m_draftNotice->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_draftNotice->hide();
    root->addWidget(m_draftNotice);

    m_content = new QWidget(this);
    m_content->setObjectName(
        QStringLiteral("finepaper.inspectorDesignSettingsContent"));
    m_content->setMinimumWidth(0);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(UiMetrics::spacing12);
    root->addWidget(m_content);

    connect(m_toggle, &QToolButton::toggled, m_content, &QWidget::setVisible);
    connect(m_toggle, &QToolButton::toggled,
            this, [this] { updateToggleText(); });
    updateToggleText();
}

void InspectorDesignSettings::addSection(QWidget* section) {
    if (section) {
        m_contentLayout->addWidget(section);
    }
}

bool InspectorDesignSettings::isExpanded() const {
    return m_toggle && m_toggle->isChecked();
}

void InspectorDesignSettings::setExpanded(bool expanded) {
    if (!m_toggle || m_toggle->isChecked() == expanded) {
        return;
    }
    m_toggle->setChecked(expanded);
}

void InspectorDesignSettings::setDraftNotice(
    const QString& text,
    const QString& semanticRole) {
    if (!m_draftNotice) {
        return;
    }
    const QString normalizedText = text.trimmed();
    m_draftNotice->setText(normalizedText);
    m_draftNotice->setVisible(!normalizedText.isEmpty());
    if (m_draftNotice->property("finepaperRole").toString()
        != semanticRole) {
        m_draftNotice->setProperty("finepaperRole", semanticRole);
        m_draftNotice->style()->unpolish(m_draftNotice);
        m_draftNotice->style()->polish(m_draftNotice);
    }
    updateToggleText();
    updateGeometry();
}

void InspectorDesignSettings::updateToggleText() {
    if (!m_toggle) {
        return;
    }
    QString text;
    if (m_toggle->isChecked()) {
        text = QStringLiteral("Hide design settings");
    } else if (m_draftNotice && m_draftNotice->isVisible()) {
        text = QStringLiteral("Review parameter draft");
    } else {
        text = QStringLiteral("Show design settings");
    }
    m_toggle->setText(text);
    m_toggle->setAccessibleName(text);
}

} // namespace finepaper::ui
