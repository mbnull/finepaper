#include "ui/layouts/responsive_action_layout.h"

#include <algorithm>

namespace finepaper::ui {

ResponsiveActionLayout::ResponsiveActionLayout(QWidget* parent)
    : QLayout(parent) {}

ResponsiveActionLayout::~ResponsiveActionLayout() {
    for (QLayoutItem* item : m_items) {
        delete item;
    }
    m_items.clear();
}

void ResponsiveActionLayout::addItem(QLayoutItem* item) {
    m_items.push_back(item);
    invalidate();
}

int ResponsiveActionLayout::count() const {
    return m_items.size();
}

QLayoutItem* ResponsiveActionLayout::itemAt(int index) const {
    return index >= 0 && index < m_items.size()
               ? m_items.at(index)
               : nullptr;
}

QLayoutItem* ResponsiveActionLayout::takeAt(int index) {
    if (index < 0 || index >= m_items.size()) {
        return nullptr;
    }
    return m_items.takeAt(index);
}

QSize ResponsiveActionLayout::sizeHint() const {
    return horizontalSize();
}

QSize ResponsiveActionLayout::minimumSize() const {
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

bool ResponsiveActionLayout::hasHeightForWidth() const {
    return true;
}

int ResponsiveActionLayout::heightForWidth(int width) const {
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

void ResponsiveActionLayout::setGeometry(const QRect& rect) {
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
        item->setGeometry(QRect(
            x,
            area.top() + (area.height() - itemHeight) / 2,
            itemSize.width(),
            itemHeight));
        x += itemSize.width() + spacing();
    }
}

QSize ResponsiveActionLayout::horizontalSize() const {
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

bool ResponsiveActionLayout::shouldStack(int availableWidth) const {
    const QMargins margins = contentsMargins();
    return horizontalSize().width()
               - margins.left() - margins.right()
           > availableWidth;
}

} // namespace finepaper::ui
