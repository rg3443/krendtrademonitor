#include "treewidget.h"

#include <QHeaderView>
#include <QTreeView>

namespace QtInterface {

TreeWidget::TreeWidget(QWidget* parent)
    : BaseItemViewWidget(parent)
{
}

QAbstractItemView* TreeWidget::CreateView()
{
    return new QTreeView(this);
}

bool TreeWidget::InitForChild()
{
    if(!BaseItemViewWidget::InitForChild()) return false;
    QTreeView* tree = GetTree();
    tree->setSortingEnabled(true);
    tree->sortByColumn(-1, Qt::AscendingOrder);
    tree->setUniformRowHeights(false);
    tree->header()->setStretchLastSection(true);
    return true;
}

QTreeView* TreeWidget::GetTree() const { return static_cast<QTreeView*>(GetView()); }

void TreeWidget::AlignColumns()
{
    QTreeView* tree = GetTree();
    if(!tree->model()) return;
    for(int i = 0; i < tree->model()->columnCount(); ++i)
        tree->resizeColumnToContents(i);
}

} // QtInterface
