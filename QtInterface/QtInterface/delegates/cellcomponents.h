#ifndef QTINTERFACE_CELLCOMPONENTS_H
#define QTINTERFACE_CELLCOMPONENTS_H

#include "QtInterface/delegates/cellcomponent.h"

#include <QStringList>

namespace QtInterface {

    /*!
     *  \brief Готовые компоненты ячеек (наследники DelegateComboBox/Png/Button из QtShell).
     *  Всё, чего здесь нет, собирается через Component<W>() из любого виджета.
     */
    namespace Cells {

        //! \brief Колбэк действия в ячейке: индекс ячейки (переживает сортировку).
        using IndexAction = std::function<void(const QModelIndex& index)>;

        /*!
         *  \brief Кнопка в ячейке.
         *  \param text     текст кнопки; пустой — текст берётся из ячейки (DisplayRole)
         *  \param onClick  что сделать по нажатию
         */
        CellComponent Button(const QString& text, IndexAction onClick);

        /*!
         *  \brief Выпадающий список. Текущий пункт берётся из ячейки (DisplayRole).
         *  \param items     варианты
         *  \param onChosen  выбран другой пункт: (индекс ячейки, текст пункта). Модель он не меняет.
         */
        CellComponent ComboBox(const QStringList& items,
                               std::function<void(const QModelIndex&, const QString&)> onChosen = {});

        /*!
         *  \brief Картинка. Путь берётся из ячейки (EditRole); если задан fixedPath — всегда он.
         *  Относительный путь считается от папки программы.
         */
        CellComponent Image(const QString& fixedPath = QString());

        /*!
         *  \brief Полоса прогресса: значение из ячейки (EditRole), диапазон [minimum, maximum].
         *  \param format  формат текста QProgressBar ("%v из %m", "%p%"...)
         */
        CellComponent Progress(int minimum, int maximum, const QString& format = "%v");

        /*!
         *  \brief Цветная плашка с текстом ячейки (DisplayRole) — для статусов.
         *  \param colorOf  цвет плашки по ячейке
         */
        CellComponent Badge(std::function<QColor(const QModelIndex&)> colorOf);
    }
}

#endif // QTINTERFACE_CELLCOMPONENTS_H
