#ifndef QTINTERFACE_TABLEWIDGET_H
#define QTINTERFACE_TABLEWIDGET_H

#include "QtInterface/components/baseitemviewwidget.h"

class QTableView;

namespace QtInterface {

    //! \brief Таблица: QTableView + сортировка по заголовку + фильтр + Delegate.
    class TableWidget : public BaseItemViewWidget {
    Q_OBJECT
    public:
        explicit TableWidget(QWidget* parent = nullptr);

        QTableView*             GetTable() const;

        //! \brief Подогнать ширину колонок под содержимое (последняя растягивается).
        void                    AlignColumns();

    protected:
        QAbstractItemView*      CreateView() override;
        bool                    InitForChild() override;
    };
}

#endif // QTINTERFACE_TABLEWIDGET_H
