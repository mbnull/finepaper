#include "ui/components/segmented_action_control.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPointer>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>

namespace finepaper::ui {
namespace {

QString accessibleActionName(const QString& actionText) {
    QString name;
    name.reserve(actionText.size());
    for (qsizetype index = 0; index < actionText.size(); ++index) {
        if (actionText.at(index) != QLatin1Char('&')) {
            name.append(actionText.at(index));
            continue;
        }
        if (index + 1 < actionText.size()
            && actionText.at(index + 1) == QLatin1Char('&')) {
            name.append(QLatin1Char('&'));
            ++index;
        }
    }
    return name.trimmed();
}

void updateAccessibility(QToolButton* button, const QAction* action) {
    if (!button || !action) {
        return;
    }

    const QString name = accessibleActionName(action->text());
    button->setAccessibleName(name);

    QString description = action->statusTip().trimmed();
    if (description.isEmpty()) {
        description = action->toolTip().trimmed();
    }
    if (description == name) {
        description.clear();
    }
    button->setAccessibleDescription(description);
}

bool isAvailable(const QToolButton* button) {
    return button && button->isEnabled() && !button->isHidden();
}

} // namespace

SegmentedActionControl::SegmentedActionControl(QWidget* parent)
    : QWidget(parent) {
    setProperty("finepaperRole", QStringLiteral("segmentedControl"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    auto* controlLayout = new QHBoxLayout(this);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(0);
}

QToolButton* SegmentedActionControl::addAction(
    QAction* action,
    const QString& objectName) {
    if (!action) {
        return nullptr;
    }

    auto* button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setProperty("finepaperRole", QStringLiteral("segment"));
    if (!objectName.isEmpty()) {
        button->setObjectName(objectName);
    }
    updateAccessibility(button, action);
    button->installEventFilter(this);

    layout()->addWidget(button);
    m_buttons.append(button);

    connect(action, &QAction::changed, button, [button, action] {
        updateAccessibility(button, action);
    });
    connect(button, &QObject::destroyed, this,
            [this, button] { m_buttons.removeOne(button); });
    connect(action, &QObject::destroyed, this,
            [this, guardedButton = QPointer<QToolButton>(button)] {
                if (!guardedButton) {
                    return;
                }
                m_buttons.removeOne(guardedButton);
                layout()->removeWidget(guardedButton);
                guardedButton->hide();
                guardedButton->deleteLater();
            });
    connect(action, &QAction::triggered, this,
            [this, action] { ensureExclusiveSelection(action); });
    connect(action, &QAction::toggled, this,
            [this, guardedAction = QPointer<QAction>(action)](bool checked) {
                if (checked) {
                    return;
                }
                QTimer::singleShot(0, this, [this, guardedAction] {
                    if (guardedAction) {
                        ensureExclusiveSelection(guardedAction);
                    }
                });
            });

    ensureExclusiveSelection(action);
    return button;
}

QList<QToolButton*> SegmentedActionControl::buttons() const {
    return m_buttons;
}

bool SegmentedActionControl::eventFilter(QObject* watched, QEvent* event) {
    if (!event || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    const int currentIndex = buttonIndex(watched);
    if (currentIndex < 0) {
        return QWidget::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->modifiers().testAnyFlags(
            Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return QWidget::eventFilter(watched, event);
    }

    QToolButton* target = nullptr;
    switch (keyEvent->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        int step = keyEvent->key() == Qt::Key_Right ? 1 : -1;
        if (layoutDirection() == Qt::RightToLeft) {
            step = -step;
        }
        target = adjacentButton(currentIndex, step);
        break;
    }
    case Qt::Key_Home:
        target = edgeButton(false);
        break;
    case Qt::Key_End:
        target = edgeButton(true);
        break;
    default:
        return QWidget::eventFilter(watched, event);
    }

    if (!target) {
        return QWidget::eventFilter(watched, event);
    }
    keyEvent->accept();
    activateButton(target);
    return true;
}

int SegmentedActionControl::buttonIndex(const QObject* object) const {
    for (int index = 0; index < m_buttons.size(); ++index) {
        if (m_buttons.at(index) == object) {
            return index;
        }
    }
    return -1;
}

QToolButton* SegmentedActionControl::adjacentButton(
    int currentIndex,
    int step) const {
    if (m_buttons.size() < 2 || step == 0) {
        return nullptr;
    }

    int candidateIndex = currentIndex;
    for (int visited = 0; visited < m_buttons.size() - 1; ++visited) {
        candidateIndex = (candidateIndex + step + m_buttons.size())
            % m_buttons.size();
        QToolButton* candidate = m_buttons.at(candidateIndex);
        if (isAvailable(candidate)) {
            return candidate;
        }
    }
    return nullptr;
}

QToolButton* SegmentedActionControl::edgeButton(bool fromEnd) const {
    if (fromEnd) {
        for (auto iterator = m_buttons.crbegin();
             iterator != m_buttons.crend(); ++iterator) {
            if (isAvailable(*iterator)) {
                return *iterator;
            }
        }
        return nullptr;
    }

    for (QToolButton* button : m_buttons) {
        if (isAvailable(button)) {
            return button;
        }
    }
    return nullptr;
}

void SegmentedActionControl::activateButton(QToolButton* button) {
    if (!isAvailable(button)) {
        return;
    }
    button->setFocus(Qt::OtherFocusReason);
    button->click();
}

void SegmentedActionControl::ensureExclusiveSelection(
    QAction* preferredAction) {
    if (!preferredAction || !preferredAction->isCheckable()) {
        return;
    }
    QActionGroup* group = preferredAction->actionGroup();
    if (!group
        || group->exclusionPolicy()
            != QActionGroup::ExclusionPolicy::Exclusive
        || group->checkedAction()) {
        return;
    }
    preferredAction->setChecked(true);
}

} // namespace finepaper::ui
