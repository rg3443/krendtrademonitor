#ifndef QTINTERFACE_TREEWIDGET_H
#define QTINTERFACE_TREEWIDGET_H

#include "QtInterface/components/baseitemviewwidget.h"

class QTreeView;

namespace QtInterface {

    //! \brief Дерево: QTreeView + сортировка + фильтр (с родителями найденного) + Delegate.
    class TreeWidget : public BaseItemViewWidget {
    Q_OBJECT
    public:
        explicit TreeWidget(QWidget* parent = nullptr);

        QTreeView*              GetTree() const;

        //! \brief Подогнать ширину колонок под содержимое.
        void                    AlignColumns();

    protected:
        QAbstractItemView*      CreateView() override;
        bool                    InitForChild() override;
    };
}

#endif // QTINTERFACE_TREEWIDGET_H
