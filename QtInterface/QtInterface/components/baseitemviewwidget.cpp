#include "baseitemviewwidget.h"

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QSortFilterProxyModel>

namespace QtInterface {

BaseItemViewWidget::BaseItemViewWidget(QWidget* parent)
    : BaseWidget(parent)
    , view(nullptr)
    , proxy(nullptr)
    , delegate(nullptr)
    , filterEdit(nullptr)
{
}

bool BaseItemViewWidget::InitForChild()
{
    view = CreateView();
    if(!view) return LogErr("InitForChild: CreateView вернул nullptr");

    proxy = new QSortFilterProxyModel(this);
    proxy->setSortRole(Qt::EditRole);
    proxy->setFilterRole(Qt::DisplayRole);
    proxy->setFilterKeyColumn(-1);                  // по всем колонкам
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setRecursiveFilteringEnabled(true);      // для деревьев: показывать родителей найденного
    proxy->setDynamicSortFilter(true);
    view->setModel(proxy);

    delegate = new Delegate(view);
    delegate->Attach(view);

    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(true);

    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Фильтр…");
    filterEdit->setClearButtonEnabled(true);
    connect(filterEdit, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);

    connect(view, &QAbstractItemView::activated, this, [this](const QModelIndex& i) {
        emit Activated(MapToSource(i));
    });
    currentConnection = connect(view->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        [this](const QModelIndex& current) { emit CurrentChanged(MapToSource(current.sibling(current.row(), 0))); });
    return true;
}

bool BaseItemViewWidget::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setSpacing(4);
    mainLayout->addWidget(filterEdit, 0, 0);
    mainLayout->addWidget(view, 1, 0);
    return true;
}

void BaseItemViewWidget::SetModel(QAbstractItemModel* model)
{
    proxy->setSourceModel(model);
    delegate->RefreshComponents();
}

QAbstractItemModel* BaseItemViewWidget::GetModel() const { return proxy->sourceModel(); }

void BaseItemViewWidget::SetFilterVisible(bool visible) { filterEdit->setVisible(visible); }
void BaseItemViewWidget::SetFilterText(const QString& text) { filterEdit->setText(text); }

QModelIndex BaseItemViewWidget::MapToSource(const QModelIndex& viewIndex) const
{
    return viewIndex.isValid() ? proxy->mapToSource(viewIndex) : QModelIndex();
}

QModelIndex BaseItemViewWidget::MapFromSource(const QModelIndex& sourceIndex) const
{
    return sourceIndex.isValid() ? proxy->mapFromSource(sourceIndex) : QModelIndex();
}

int BaseItemViewWidget::GetCurrentSourceRow() const
{
    const QModelIndex source = MapToSource(view->currentIndex());
    return source.isValid() ? source.row() : -1;
}

QAbstractItemView*     BaseItemViewWidget::GetView() const     { return view; }
QSortFilterProxyModel* BaseItemViewWidget::GetProxy() const    { return proxy; }
Delegate*              BaseItemViewWidget::GetDelegate() const { return delegate; }

} // QtInterface
