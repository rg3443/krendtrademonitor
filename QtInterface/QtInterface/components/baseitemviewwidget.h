#ifndef QTINTERFACE_BASEITEMVIEWWIDGET_H
#define QTINTERFACE_BASEITEMVIEWWIDGET_H

#include "QtInterface/core/basewidget.h"
#include "QtInterface/delegates/delegate.h"

class QAbstractItemModel;
class QAbstractItemView;
class QLineEdit;
class QSortFilterProxyModel;

namespace QtInterface {

    /*!
     *  \brief Общая часть таблицы и дерева: представление + прокси (сортировка/фильтр)
     *  + Delegate + необязательная строка фильтра над представлением.
     *
     *  Наследник создаёт само представление в CreateView() (QTableView, QTreeView…)
     *  и при желании донастраивает его, переопределив InitForChild()/SetupUIForChild()
     *  и вызвав версию BaseItemViewWidget.
     *
     *  Сортировка идёт по Qt::EditRole («сырые» значения), фильтр — по тексту всех колонок.
     */
    class BaseItemViewWidget : public BaseWidget {
    Q_OBJECT
    public:
        explicit BaseItemViewWidget(QWidget* parent = nullptr);

        //! \brief Показать модель (владение не забирает). nullptr — убрать модель.
        void                    SetModel(QAbstractItemModel* model);
        QAbstractItemModel*     GetModel() const;

        //! \brief Показать/скрыть строку фильтра.
        void                    SetFilterVisible(bool visible);
        //! \brief Задать текст фильтра программно.
        void                    SetFilterText(const QString& text);

        //! \brief Индекс представления (прокси) → индекс исходной модели.
        QModelIndex             MapToSource(const QModelIndex& viewIndex) const;
        //! \brief Индекс исходной модели → индекс представления (прокси).
        QModelIndex             MapFromSource(const QModelIndex& sourceIndex) const;

        //! \brief Строка исходной модели под курсором; -1, если нет.
        int                     GetCurrentSourceRow() const;

        QAbstractItemView*      GetView() const;
        QSortFilterProxyModel*  GetProxy() const;
        Delegate*               GetDelegate() const;

    signals:
        //! \brief Текущая строка сменилась (индекс исходной модели, колонка 0).
        void CurrentChanged(const QModelIndex& sourceIndex);
        //! \brief Двойной щелчок / Enter по ячейке (индекс исходной модели).
        void Activated(const QModelIndex& sourceIndex);

    protected:
        //! \brief Наследник создаёт представление (родитель — this).
        virtual QAbstractItemView*  CreateView() = 0;

        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;

    private:
        QAbstractItemView*      view;
        QSortFilterProxyModel*  proxy;
        Delegate*               delegate;
        QLineEdit*              filterEdit;
        QMetaObject::Connection currentConnection;
    };
}

#endif // QTINTERFACE_BASEITEMVIEWWIDGET_H
