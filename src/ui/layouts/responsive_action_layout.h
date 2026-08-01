#pragma once

#include <QLayout>
#include <QVector>

namespace finepaper::ui {

// Keeps a concise action row while labels fit and stacks the same controls
// before a narrow panel or large system font can clip them.
class ResponsiveActionLayout final : public QLayout {
public:
    explicit ResponsiveActionLayout(QWidget* parent = nullptr);
    ~ResponsiveActionLayout() override;

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSize() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;

private:
    [[nodiscard]] QSize horizontalSize() const;
    [[nodiscard]] bool shouldStack(int availableWidth) const;

    QVector<QLayoutItem*> m_items;
};

} // namespace finepaper::ui
