#include "ui/components/empty_state.h"

#include "ui/theme/ui_tokens.h"

#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace finepaper::ui {
namespace {

constexpr int preferredCardWidth = 520;

// Keeps actions on one concise row while there is enough room and stacks them
// when their full labels would otherwise be compressed or clipped.
class ResponsiveActionLayout final : public QLayout {
public:
    explicit ResponsiveActionLayout(QWidget* parent = nullptr)
        : QLayout(parent) {}

    ~ResponsiveActionLayout() override {
        for (QLayoutItem* item : m_items) {
            delete item;
        }
        m_items.clear();
    }

    void addItem(QLayoutItem* item) override {
        m_items.push_back(item);
        invalidate();
    }

    [[nodiscard]] int count() const override {
        return m_items.size();
    }

    [[nodiscard]] QLayoutItem* itemAt(int index) const override {
        return index >= 0 && index < m_items.size()
                   ? m_items.at(index)
                   : nullptr;
    }

    QLayoutItem* takeAt(int index) override {
        if (index < 0 || index >= m_items.size()) {
            return nullptr;
        }
        return m_items.takeAt(index);
    }

    [[nodiscard]] QSize sizeHint() const override {
        return horizontalSize();
    }

    [[nodiscard]] QSize minimumSize() const override {
        QSize result;
        int activeItems = 0;
        for (QLayoutItem* item : m_items) {
            if (!item || item->isEmpty()) {
                continue;
            }
            result.setWidth((std::max)(
                result.width(), item->minimumSize().width()));
            result.setHeight((std::max)(
                result.height(), item->minimumSize().height()));
            ++activeItems;
        }
        if (activeItems > 0) {
            const QMargins margins = contentsMargins();
            result.rwidth() += margins.left() + margins.right();
            result.rheight() += margins.top() + margins.bottom();
        }
        return result;
    }

    [[nodiscard]] bool hasHeightForWidth() const override {
        return true;
    }

    [[nodiscard]] int heightForWidth(int width) const override {
        const QMargins margins = contentsMargins();
        const int availableWidth = (std::max)(
            0, width - margins.left() - margins.right());
        const bool stacked = shouldStack(availableWidth);
        int result = 0;
        int activeItems = 0;
        for (QLayoutItem* item : m_items) {
            if (!item || item->isEmpty()) {
                continue;
            }
            const int itemHeight = (std::max)(
                item->minimumSize().height(), item->sizeHint().height());
            result = stacked ? result + itemHeight
                             : (std::max)(result, itemHeight);
            ++activeItems;
        }
        if (stacked && activeItems > 1) {
            result += (activeItems - 1) * spacing();
        }
        return result + margins.top() + margins.bottom();
    }

    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        const QRect area = contentsRect();
        if (area.isEmpty()) {
            return;
        }

        if (shouldStack(area.width())) {
            int y = area.top();
            for (QLayoutItem* item : m_items) {
                if (!item || item->isEmpty()) {
                    continue;
                }
                const int itemHeight = (std::max)(
                    item->minimumSize().height(), item->sizeHint().height());
                item->setGeometry(
                    QRect(area.left(), y, area.width(), itemHeight));
                y += itemHeight + spacing();
            }
            return;
        }

        int totalWidth = 0;
        int activeItems = 0;
        for (QLayoutItem* item : m_items) {
            if (!item || item->isEmpty()) {
                continue;
            }
            totalWidth += (std::max)(
                item->minimumSize().width(), item->sizeHint().width());
            ++activeItems;
        }
        if (activeItems == 0) {
            return;
        }
        totalWidth += (activeItems - 1) * spacing();

        int x = area.right() - totalWidth + 1;
        for (QLayoutItem* item : m_items) {
            if (!item || item->isEmpty()) {
                continue;
            }
            const QSize itemSize = item->sizeHint().expandedTo(
                item->minimumSize());
            const int itemHeight = (std::min)(
                area.height(), itemSize.height());
            item->setGeometry(QRect(x,
                                    area.top()
                                        + (area.height() - itemHeight) / 2,
                                    itemSize.width(), itemHeight));
            x += itemSize.width() + spacing();
        }
    }

private:
    [[nodiscard]] QSize horizontalSize() const {
        QSize result;
        int activeItems = 0;
        for (QLayoutItem* item : m_items) {
            if (!item || item->isEmpty()) {
                continue;
            }
            const QSize itemSize = item->sizeHint().expandedTo(
                item->minimumSize());
            result.rwidth() += itemSize.width();
            result.setHeight((std::max)(
                result.height(), itemSize.height()));
            ++activeItems;
        }
        if (activeItems > 1) {
            result.rwidth() += (activeItems - 1) * spacing();
        }
        if (activeItems > 0) {
            const QMargins margins = contentsMargins();
            result.rwidth() += margins.left() + margins.right();
            result.rheight() += margins.top() + margins.bottom();
        }
        return result;
    }

    [[nodiscard]] bool shouldStack(int availableWidth) const {
        const QMargins margins = contentsMargins();
        return horizontalSize().width()
                   - margins.left() - margins.right()
               > availableWidth;
    }

    QVector<QLayoutItem*> m_items;
};

} // namespace

EmptyState::EmptyState(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("finepaper.canvasEmptyStateCard"));
    setProperty("finepaperRole", QStringLiteral("card"));
    setMaximumWidth(560);
    setAccessibleName(QStringLiteral("NoC editor start actions"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        UiMetrics::spacing32, UiMetrics::spacing24,
        UiMetrics::spacing32, UiMetrics::spacing24);
    layout->setSpacing(UiMetrics::spacing12);

    m_eyebrow = new QLabel(this);
    m_eyebrow->setProperty("finepaperRole", QStringLiteral("muted"));
    layout->addWidget(m_eyebrow);

    m_title = new QLabel(this);
    m_title->setProperty("finepaperRole", QStringLiteral("title"));
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_description = new QLabel(this);
    m_description->setProperty("finepaperRole", QStringLiteral("muted"));
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_description);

    m_actions = new ResponsiveActionLayout;
    m_actions->setContentsMargins(0, UiMetrics::spacing8, 0, 0);
    m_actions->setSpacing(UiMetrics::spacing8);
    layout->addLayout(m_actions);
    applyRoleFonts();
}

void EmptyState::setEyebrow(const QString& text) {
    m_eyebrow->setText(text);
    m_eyebrow->setVisible(!text.isEmpty());
    updateGeometry();
}

void EmptyState::setTitle(const QString& text) {
    m_title->setText(text);
    updateGeometry();
}

void EmptyState::setDescription(const QString& text) {
    m_description->setText(text);
    setAccessibleDescription(text);
    updateGeometry();
}

QPushButton* EmptyState::addActionButton(
    const QString& text,
    const QString& role) {
    auto* button = new QPushButton(text, this);
    if (!role.isEmpty()) {
        button->setProperty("finepaperRole", role);
    }
    m_actions->addWidget(button);
    updateGeometry();
    return button;
}

QSize EmptyState::sizeHint() const {
    const int availableMaximum = maximumWidth() < QWIDGETSIZE_MAX
                                     ? maximumWidth()
                                     : preferredCardWidth;
    const int width = (std::max)(
        minimumSizeHint().width(),
        (std::min)(preferredCardWidth, availableMaximum));
    return QSize(width, heightForWidth(width));
}

bool EmptyState::hasHeightForWidth() const {
    return true;
}

int EmptyState::heightForWidth(int width) const {
    if (!layout()) {
        return QFrame::sizeHint().height();
    }
    return layout()->totalHeightForWidth((std::max)(0, width));
}

void EmptyState::changeEvent(QEvent* event) {
    QFrame::changeEvent(event);
    if (event && event->type() == QEvent::FontChange) {
        applyRoleFonts();
        updateGeometry();
    }
}

void EmptyState::applyRoleFonts() {
    if (m_eyebrow) {
        m_eyebrow->setFont(fontForRole(UiFontRole::Label, font()));
    }
    if (m_title) {
        m_title->setFont(fontForRole(UiFontRole::Title, font()));
    }
    if (m_description) {
        m_description->setFont(fontForRole(UiFontRole::Body, font()));
    }
}

} // namespace finepaper::ui
