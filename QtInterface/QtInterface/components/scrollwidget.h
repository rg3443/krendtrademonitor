#ifndef QTINTERFACE_SCROLLWIDGET_H
#define QTINTERFACE_SCROLLWIDGET_H

#include "QtInterface/core/basewidget.h"

class QBoxLayout;
class QScrollArea;

namespace QtInterface {

    //! \brief Прокручиваемый список произвольных виджетов (ScrollWidget/ListWidget из QtShell).
    class ScrollWidget : public BaseWidget {
    Q_OBJECT
    public:
        //! \brief Направление списка.
        enum class Direction {
            Vertical,
            Horizontal
        };

        explicit ScrollWidget(QWidget* parent = nullptr, Direction direction = Direction::Vertical);

        //! \brief Добавить виджет в конец (становится дочерним).
        bool                    Add(QWidget* item);
        //! \brief Убрать виджет из списка и удалить его.
        bool                    Remove(QWidget* item);
        //! \brief Убрать и удалить все виджеты.
        void                    Clear();
        //! \brief Виджеты списка по порядку.
        QList<QWidget*>         GetItems() const;

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;

    private:
        Direction               direction;
        QScrollArea*            scrollArea;
        QWidget*                itemContainer;
        QBoxLayout*             itemLayout;
        QList<QWidget*>         lItems;
    };
}

#endif // QTINTERFACE_SCROLLWIDGET_H
