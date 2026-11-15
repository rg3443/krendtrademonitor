#ifndef QTINTERFACE_TABLECOLUMN_H
#define QTINTERFACE_TABLECOLUMN_H

#include <QString>
#include <QVariant>

#include <functional>

namespace QtInterface {

    /*!
     *  \brief Описание колонки типизированной модели BaseTableModel<T, Key>.
     *  Заменяет строковый DSL "|col-|col+" из QtShell: колонка знает,
     *  как достать значение из строки-структуры T.
     *
     *  Роли модели:
     *  - Qt::EditRole          — value(row): «сырое» значение (число, дата…), по нему сортировка
     *                            и его же получают компоненты делегата;
     *  - Qt::DisplayRole       — text(row), а если text не задан — value(row).toString();
     *  - Qt::TextAlignmentRole — align;
     *  - Qt::ToolTipRole       — role(row, ToolTipRole), а если пусто — текст ячейки;
     *  - любая другая роль     — role(row, роль) (цвет, шрифт, иконка…).
     */
    template<class T>
    struct TableColumn {
        QString                                     title;   //!< \brief Заголовок колонки.
        std::function<QVariant(const T&)>           value;   //!< \brief Сырое значение (обязательно).
        std::function<QString(const T&)>            text  = {}; //!< \brief Текст для показа (необязательно).
        std::function<QVariant(const T&, int role)> role  = {}; //!< \brief Прочие роли (необязательно).
        Qt::Alignment                               align = Qt::AlignLeft | Qt::AlignVCenter;
        QString                                     headerToolTip = {}; //!< \brief Подсказка к заголовку.
    };
}

#endif // QTINTERFACE_TABLECOLUMN_H
