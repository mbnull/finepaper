#include "ui/layouts/responsive_action_layout.h"

#include <QStyle>
#include <QWidget>

#include <algorithm>

namespace finepaper::ui {
namespace {

int effectiveSpacing(const QLayout* layout, Qt::Orientation orientation) {
    if (!layout) {
        return 0;
    }
    if (layout->spacing() >= 0) {
        return layout->spacing();
    }
    const QWidget* parent = layout->parentWidget();
    if (!parent) {
        return 0;
    }
    return parent->style()->pixelMetric(
        orientation == Qt::Horizontal
            ? QStyle::PM_LayoutHorizontalSpacing
            : QStyle::PM_LayoutVerticalSpacing,
        nullptr,
        parent);
}

int itemHeightForWidth(QLayoutItem* item, int width) {
    if (!item) {
        return 0;
    }
    return item->hasHeightForWidth()
        ? item->heightForWidth((std::max)(0, width))
        : (std::max)(item->minimumSize().height(),
                     item->sizeHint().height());
}

} // namespace

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
        const int itemHeight = stacked
            ? itemHeightForWidth(item, availableWidth)
            : (std::max)(item->minimumSize().height(),
                         item->sizeHint().height());
        result = stacked ? result + itemHeight
                         : (std::max)(result, itemHeight);
        ++activeItems;
    }
    if (stacked && activeItems > 1) {
        result += (activeItems - 1)
            * effectiveSpacing(this, Qt::Vertical);
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
        const int verticalSpacing = effectiveSpacing(
            this, Qt::Vertical);
        for (QLayoutItem* item : m_items) {
            if (!item || item->isEmpty()) {
                continue;
            }
            const int itemHeight = itemHeightForWidth(item, area.width());
            item->setGeometry(
                QRect(area.left(), y, area.width(), itemHeight));
            y += itemHeight + verticalSpacing;
        }
        return;
    }

    int totalWidth = 0;
    int activeItems = 0;
    int expandingItems = 0;
    for (QLayoutItem* item : m_items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        totalWidth += (std::max)(
            item->minimumSize().width(), item->sizeHint().width());
        if (item->expandingDirections().testFlag(Qt::Horizontal)) {
            ++expandingItems;
        }
        ++activeItems;
    }
    if (activeItems == 0) {
        return;
    }
    const int horizontalSpacing = effectiveSpacing(
        this, Qt::Horizontal);
    totalWidth += (activeItems - 1) * horizontalSpacing;

    const int extraWidth = expandingItems > 0
        ? (std::max)(0, area.width() - totalWidth)
        : 0;
    int remainingExtra = extraWidth;
    int remainingExpanding = expandingItems;
    int x = expandingItems > 0
        ? area.left() : area.right() - totalWidth + 1;
    for (QLayoutItem* item : m_items) {
        if (!item || item->isEmpty()) {
            continue;
        }
        const QSize itemSize = item->sizeHint().expandedTo(
            item->minimumSize());
        int itemWidth = itemSize.width();
        if (item->expandingDirections().testFlag(Qt::Horizontal)
            && remainingExpanding > 0) {
            const int share = remainingExtra / remainingExpanding;
            itemWidth += share;
            remainingExtra -= share;
            --remainingExpanding;
        }
        const int itemHeight = (std::min)(
            area.height(), itemHeightForWidth(item, itemWidth));
        const QRect logicalRect(
            x,
            area.top() + (area.height() - itemHeight) / 2,
            itemWidth,
            itemHeight);
        const QWidget* parent = parentWidget();
        item->setGeometry(parent
            ? QStyle::visualRect(
                  parent->layoutDirection(), area, logicalRect)
            : logicalRect);
        x += itemWidth + horizontalSpacing;
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
        result.rwidth() += (activeItems - 1)
            * effectiveSpacing(this, Qt::Horizontal);
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
