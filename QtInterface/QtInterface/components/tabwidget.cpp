#include "tabwidget.h"

#include <QTabWidget>

namespace QtInterface {

TabWidget::TabWidget(QWidget* parent)
    : BaseWidget(parent)
    , obj(nullptr)
{
}

bool TabWidget::InitForChild()
{
    obj = new QTabWidget(this);
    obj->setDocumentMode(false);
    return true;
}

bool TabWidget::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->addWidget(obj);
    return true;
}

int TabWidget::AddTab(QWidget* widget, const QString& tabName, int index)
{
    if(!widget) { LogErr("AddTab: nullptr"); return -1; }
    const int at = (index < 0) ? obj->addTab(widget, tabName) : obj->insertTab(index, widget, tabName);
    if(auto* base = qobject_cast<BaseWidget*>(widget))
        base->ConnectToUpdates(this);
    return at;
}

int TabWidget::RemoveTab(QWidget* widget)
{
    const int at = obj->indexOf(widget);
    if(at < 0) return -1;
    obj->removeTab(at);
    if(auto* base = qobject_cast<BaseWidget*>(widget))
        disconnect(this, &BaseWidget::UpdateAll, base, &BaseWidget::OnUpdate);
    return at;
}

void TabWidget::SetTabName(QWidget* widget, const QString& newName)
{
    const int at = obj->indexOf(widget);
    if(at >= 0) obj->setTabText(at, newName);
}

void TabWidget::Clear()
{
    while(obj->count() > 0) RemoveTab(obj->widget(0));
}

QTabWidget* TabWidget::GetObj() const { return obj; }

} // QtInterface
