#include "tablewidget.h"

#include <QHeaderView>
#include <QTableView>

namespace QtInterface {

TableWidget::TableWidget(QWidget* parent)
    : BaseItemViewWidget(parent)
{
}

QAbstractItemView* TableWidget::CreateView()
{
    return new QTableView(this);
}

bool TableWidget::InitForChild()
{
    if(!BaseItemViewWidget::InitForChild()) return false;
    QTableView* table = GetTable();
    table->setSortingEnabled(true);
    table->sortByColumn(-1, Qt::AscendingOrder);    // до щелчка по заголовку — порядок модели
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(28);   // хватает для кнопок/прогресса в ячейках
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setHighlightSections(false);
    return true;
}

QTableView* TableWidget::GetTable() const { return static_cast<QTableView*>(GetView()); }

void TableWidget::AlignColumns()
{
    QTableView* table = GetTable();
    table->resizeColumnsToContents();
    // resizeColumnsToContents сбивает растяжку последней колонки — вернуть её
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setStretchLastSection(true);
}

} // QtInterface
