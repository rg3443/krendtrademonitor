#ifndef QTINTERFACE_BASETABLEMODEL_H
#define QTINTERFACE_BASETABLEMODEL_H

#include "QtInterface/models/tablecolumn.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QSet>
#include <QVector>

#include <functional>
#include <vector>

namespace QtInterface {

    /*!
     *  \brief Табличная модель над списком структур T с ключом Key.
     *
     *  Данные хранятся по значению, колонки описываются TableColumn<T>.
     *  SetRows() принимает свежий снимок (например, ответ сервера) и обновляет
     *  модель по ключу: изменившиеся строки — dataChanged, новые — в конец,
     *  пропавшие — удаляются. Поэтому выделение, прокрутка и компоненты делегата
     *  при опросе сервера не сбрасываются.
     *
     *  Key должен поддерживать qHash и == (QString, qint64, int…).
     *  Модель только для чтения: просмотр, без редактирования.
     *
     *  \code
     *  BaseTableModel<Bot, QString> model([](const Bot& b){ return b.login; });
     *  model.AddColumn({ "Логин", [](const Bot& b){ return b.login; } });
     *  model.SetRows(freshBots);
     *  \endcode
     */
    template<class T, class Key>
    class BaseTableModel : public QAbstractTableModel {
    public:
        using KeyFunc = std::function<Key(const T&)>;

        explicit BaseTableModel(KeyFunc keyOf_, QObject* parent = nullptr)
            : QAbstractTableModel(parent), keyOf(std::move(keyOf_)) {}

        //! \brief Добавить колонку. Колонки задаются до первых данных.
        void AddColumn(TableColumn<T> column) {
            const int at = lColumns.size();
            beginInsertColumns(QModelIndex(), at, at);
            lColumns.append(std::move(column));
            endInsertColumns();
        }

        //! \brief Колонка по номеру (для делегатов и представлений).
        const TableColumn<T>& GetColumn(int column) const { return lColumns.at(column); }

        //! \brief Номер колонки по заголовку; -1, если нет.
        int FindColumn(const QString& title) const {
            for(int i = 0; i < lColumns.size(); ++i)
                if(lColumns[i].title == title) return i;
            return -1;
        }

        /*!
         *  \brief Обновить модель свежим снимком данных (по ключу).
         *  Порядок существующих строк сохраняется, новые идут в конец.
         */
        void SetRows(std::vector<T> fresh) {
            // 1) ключи свежего снимка
            // (ключи считаем заранее: дальше элементы fresh перемещаются)
            std::vector<Key> freshKeys;
            freshKeys.reserve(fresh.size());
            QHash<Key, int> freshIndex;
            for(size_t i = 0; i < fresh.size(); ++i) {
                freshKeys.push_back(keyOf(fresh[i]));
                freshIndex.insert(freshKeys.back(), static_cast<int>(i));
            }

            // 2) удалить пропавшие (с конца, смежными диапазонами)
            for(int row = lRows.size() - 1; row >= 0; ) {
                if(freshIndex.contains(keyOf(lRows[row]))) { --row; continue; }
                int first = row;
                while(first > 0 && !freshIndex.contains(keyOf(lRows[first - 1]))) --first;
                beginRemoveRows(QModelIndex(), first, row);
                lRows.erase(lRows.begin() + first, lRows.begin() + row + 1);
                endRemoveRows();
                row = first - 1;
            }

            // 3) обновить существующие
            QSet<Key> existing;
            for(int row = 0; row < lRows.size(); ++row) {
                const Key key = keyOf(lRows[row]);
                existing.insert(key);
                T& fresher = fresh[static_cast<size_t>(freshIndex.value(key))];
                const int changedFrom = FirstChangedColumn(lRows[row], fresher);
                lRows[row] = std::move(fresher);
                if(changedFrom >= 0)
                    emit dataChanged(index(row, changedFrom), index(row, lColumns.size() - 1));
            }

            // 4) добавить новые
            QVector<T> added;
            for(size_t i = 0; i < fresh.size(); ++i) {
                const Key& key = freshKeys[i];
                if(existing.contains(key)) continue;    // уже есть (или дубликат ключа в снимке)
                existing.insert(key);
                added.append(std::move(fresh[i]));
            }
            if(!added.isEmpty()) {
                beginInsertRows(QModelIndex(), lRows.size(), lRows.size() + added.size() - 1);
                lRows += added;
                endInsertRows();
            }
        }

        //! \brief Удалить все строки.
        void Clear() {
            if(lRows.isEmpty()) return;
            beginResetModel();
            lRows.clear();
            endResetModel();
        }

        //! \brief Строка по номеру; nullptr, если номера нет.
        const T* GetRow(int row) const {
            return (row >= 0 && row < lRows.size()) ? &lRows[row] : nullptr;
        }

        //! \brief Строка по индексу модели (не прокси!).
        const T* GetRow(const QModelIndex& idx) const {
            return (idx.isValid() && idx.model() == this) ? GetRow(idx.row()) : nullptr;
        }

        //! \brief Номер строки по ключу; -1, если нет.
        int FindRow(const Key& key) const {
            for(int row = 0; row < lRows.size(); ++row)
                if(keyOf(lRows[row]) == key) return row;
            return -1;
        }

        const QVector<T>& GetRows() const { return lRows; }

        // --- QAbstractTableModel ---
        int rowCount(const QModelIndex& parent = QModelIndex()) const override {
            return parent.isValid() ? 0 : lRows.size();
        }
        int columnCount(const QModelIndex& parent = QModelIndex()) const override {
            return parent.isValid() ? 0 : lColumns.size();
        }
        QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override {
            if(!idx.isValid() || idx.row() >= lRows.size() || idx.column() >= lColumns.size())
                return QVariant();
            const T& row = lRows[idx.row()];
            const TableColumn<T>& col = lColumns[idx.column()];
            switch(role) {
                case Qt::DisplayRole:       return DisplayText(col, row);
                case Qt::EditRole:          return col.value ? col.value(row) : QVariant();
                case Qt::TextAlignmentRole: return static_cast<int>(col.align);
                case Qt::ToolTipRole: {
                    QVariant tip = col.role ? col.role(row, role) : QVariant();
                    return tip.isValid() ? tip : QVariant(DisplayText(col, row));
                }
                default:                    return col.role ? col.role(row, role) : QVariant();
            }
        }
        QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override {
            if(orientation != Qt::Horizontal || section < 0 || section >= lColumns.size())
                return QAbstractTableModel::headerData(section, orientation, role);
            if(role == Qt::DisplayRole) return lColumns[section].title;
            if(role == Qt::ToolTipRole && !lColumns[section].headerToolTip.isEmpty())
                return lColumns[section].headerToolTip;
            return QVariant();
        }
        Qt::ItemFlags flags(const QModelIndex& idx) const override {
            return idx.isValid() ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags;
        }

    private:
        static QString DisplayText(const TableColumn<T>& col, const T& row) {
            if(col.text)  return col.text(row);
            if(col.value) return col.value(row).toString();
            return QString();
        }
        //! \brief Первая колонка, где значение или текст изменились; -1 — строка та же.
        int FirstChangedColumn(const T& oldRow, const T& newRow) const {
            for(int c = 0; c < lColumns.size(); ++c) {
                const TableColumn<T>& col = lColumns[c];
                if(col.value && col.value(oldRow) != col.value(newRow)) return c;
                if(col.text  && col.text(oldRow)  != col.text(newRow))  return c;
            }
            return -1;
        }

        KeyFunc                 keyOf;
        QVector<TableColumn<T>> lColumns;
        QVector<T>              lRows;
    };
}

#endif // QTINTERFACE_BASETABLEMODEL_H
