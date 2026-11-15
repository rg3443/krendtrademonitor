#ifndef QTINTERFACE_CELLCOMPONENT_H
#define QTINTERFACE_CELLCOMPONENT_H

#include "QtInterface/core/basewidget.h"

#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QSize>
#include <QWidget>

#include <functional>
#include <type_traits>

class QAbstractItemModel;

namespace QtInterface {

    /*!
     *  \brief Описание компонента, который Delegate ставит вместо ячейки.
     *  Компонентом может быть абсолютно любой QWidget (в том числе BaseWidget-компонент
     *  проекта): делегат знает только, как его создать и как передать ему данные ячейки.
     *
     *  Обычно собирается шаблоном Component<W>(), а не руками.
     */
    struct CellComponent {
        //! \brief Когда компонент живёт в ячейке.
        enum class Mode {
            Persistent, //!< \brief Всегда (виджет создаётся для каждой подходящей ячейки и обновляется с данными).
            OnEdit      //!< \brief Только пока ячейку редактируют (как обычный редактор Qt).
        };

        Mode mode = Mode::Persistent;

        /*!
         *  \brief Создать виджет. index — индекс ячейки в модели представления (обычно прокси).
         *  Если компоненту нужен «свой» индекс позже (кнопка → какая строка), пусть сохранит
         *  QPersistentModelIndex: он переживает сортировку и вставку строк.
         */
        std::function<QWidget*(QWidget* parent, const QPersistentModelIndex& index)> create;

        //! \brief Данные ячейки → компонент. Вызывается сразу после создания и при каждом dataChanged.
        std::function<void(QWidget* widget, const QModelIndex& index)> update;

        //! \brief Компонент → модель (только для Mode::OnEdit и редактируемых моделей). Необязательно.
        std::function<void(QWidget* widget, QAbstractItemModel* model, const QModelIndex& index)> commit;

        //! \brief Желаемый размер ячейки. Необязательно: по умолчанию — sizeHint виджета-образца.
        std::function<QSize(const QModelIndex& index)> sizeHint;

        //! \brief Рисовать ли под компонентом обычный текст ячейки (для полупрозрачных компонентов).
        bool paintCellText = false;
    };

    /*!
     *  \brief Собрать CellComponent для виджета типа W.
     *  \param update  данные ячейки → виджет: void(W*, const QModelIndex&)
     *  \param setup   однократная настройка после создания: void(W*, const QPersistentModelIndex&)
     *  \param mode    Persistent (всегда в ячейке) или OnEdit (только при редактировании)
     *
     *  W создаётся как new W(parent); если W — BaseWidget, то через Create<W>(parent).
     *  \code
     *  delegate->BindColumn(3, Component<QProgressBar>(
     *      [](QProgressBar* bar, const QModelIndex& i){ bar->setValue(i.data(Qt::EditRole).toInt()); },
     *      [](QProgressBar* bar, const QPersistentModelIndex&){ bar->setRange(0, 30); }));
     *  \endcode
     */
    template<class W>
    CellComponent Component(std::function<void(W*, const QModelIndex&)> update,
                            std::function<void(W*, const QPersistentModelIndex&)> setup = {},
                            CellComponent::Mode mode = CellComponent::Mode::Persistent) {
        static_assert(std::is_base_of<QWidget, W>::value, "Component<W>: W должен быть QWidget");
        CellComponent c;
        c.mode = mode;
        c.create = [setup](QWidget* parent, const QPersistentModelIndex& index) -> QWidget* {
            W* w = nullptr;
            if constexpr (std::is_base_of<BaseWidget, W>::value) w = Create<W>(parent);
            else                                                 w = new W(parent);
            if(setup) setup(w, index);
            return w;
        };
        if(update) {
            c.update = [update](QWidget* w, const QModelIndex& index) { update(static_cast<W*>(w), index); };
        }
        return c;
    }
}

#endif // QTINTERFACE_CELLCOMPONENT_H
