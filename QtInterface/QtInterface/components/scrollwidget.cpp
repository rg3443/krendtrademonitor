#include "scrollwidget.h"

#include <QBoxLayout>
#include <QScrollArea>

namespace QtInterface {

ScrollWidget::ScrollWidget(QWidget* parent, Direction direction_)
    : BaseWidget(parent)
    , direction(direction_)
    , scrollArea(nullptr)
    , itemContainer(nullptr)
    , itemLayout(nullptr)
{
}

bool ScrollWidget::InitForChild()
{
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    itemContainer = new QWidget(scrollArea);
    itemLayout = new QBoxLayout(direction == Direction::Vertical ? QBoxLayout::TopToBottom
                                                                  : QBoxLayout::LeftToRight, itemContainer);
    itemLayout->setContentsMargins(4, 4, 4, 4);
    itemLayout->addStretch(1);      // элементы прижаты к началу
    scrollArea->setWidget(itemContainer);
    return true;
}

bool ScrollWidget::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->addWidget(scrollArea);
    return true;
}

bool ScrollWidget::Add(QWidget* item)
{
    if(!item) return LogErr("Add: nullptr");
    itemLayout->insertWidget(itemLayout->count() - 1, item);   // перед растяжкой
    lItems.append(item);
    return true;
}

bool ScrollWidget::Remove(QWidget* item)
{
    if(!lItems.removeOne(item)) return false;
    itemLayout->removeWidget(item);
    item->deleteLater();
    return true;
}

void ScrollWidget::Clear()
{
    const QList<QWidget*> items = lItems;
    for(QWidget* item : items) Remove(item);
}

QList<QWidget*> ScrollWidget::GetItems() const { return lItems; }

} // QtInterface
