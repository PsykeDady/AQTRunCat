#pragma once

#include <QLayout>
#include <QList>

// v2F1.5: items left to right, wrapping to the next line when the width is
// used up (Qt has no such layout). Used for the children of a Custom
// Metrics group in the Dashboard.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int horizontalSpacing = 12, int verticalSpacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect &rect) override;

private:
    int arrange(const QRect &rect, bool apply) const;

    QList<QLayoutItem *> m_items;
    int m_horizontal;
    int m_vertical;
};
