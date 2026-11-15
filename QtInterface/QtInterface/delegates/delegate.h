#ifndef QTINTERFACE_DELEGATE_H
#define QTINTERFACE_DELEGATE_H

#include "QtInterface/delegates/cellcomponent.h"

#include <QHash>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QVector>

class QAbstractItemView;

namespace QtInterface {

    /*!
     *  \brief Универсальный делегат: ставит вместо ячейки любой компонент (CellComponent).
     *
     *  Привязки (Bind*) проверяются от последней к первой — побеждает добавленная позже.
     *  Ячейка без привязки рисуется обычным образом.
     *
     *  Компоненты Mode::Persistent живут в ячейках постоянно: после Attach(view)
     *  делегат сам открывает их для новых строк, после сортировки и сброса модели,
     *  и обновляет при dataChanged. Удаляет их представление вместе со строками.
     *
     *  Индексы, которые получает делегат, — индексы модели представления (часто прокси).
     */
    class Delegate : public QStyledItemDelegate {
    Q_OBJECT
    public:
        //! \brief Условие привязки: подходит ли ячейка.
        using Match = std::function<bool(const QModelIndex& index)>;

        explicit Delegate(QObject* parent = nullptr);
        ~Delegate() override;

        /*!
         *  \brief Подключить делегат к представлению: ставит себя делегатом и следит
         *  за моделью представления. Вызывать снова после view->setModel().
         */
        void                    Attach(QAbstractItemView* view);

        //! \brief Привязать компонент к ячейкам, подходящим под условие. \return id привязки
        int                     Bind(Match match, CellComponent component);
        //! \brief Привязать компонент ко всем ячейкам колонки. \return id привязки
        int                     BindColumn(int column, CellComponent component);
        /*!
         *  \brief Привязать компонент к одной ячейке (row, column). \return id привязки
         *  Для статичных таблиц: при сортировке/фильтре строка с этим номером будет другой.
         */
        int                     BindCell(int row, int column, CellComponent component);
        //! \brief Снять привязку. \return false, если такой нет
        bool                    Unbind(int bindingId);
        //! \brief Снять все привязки.
        void                    UnbindAll();

        //! \brief Индекс ячейки, в которой стоит компонент; невалидный, если это не наш компонент.
        QModelIndex             IndexOf(const QWidget* component) const;

        //! \brief Пересоздать/открыть постоянные компоненты во всём представлении.
        void                    RefreshComponents();

        // --- QStyledItemDelegate ---
        QWidget*    createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
        void        setEditorData(QWidget* editor, const QModelIndex& index) const override;
        void        setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
        void        updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
        void        paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
        QSize       sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    private:
        struct Binding {
            int             id;
            Match           match;
            CellComponent   component;
        };
        struct ComponentInfo {
            int                     bindingId;
            QPersistentModelIndex   index;
        };

        const Binding*  FindBinding(const QModelIndex& index) const;
        const Binding*  FindBindingById(int id) const;
        //! \brief Открыть/закрыть постоянные компоненты в строках [first, last] под parent (рекурсивно для деревьев).
        void            SyncRows(const QModelIndex& parent, int first, int last);
        void            SyncIndex(const QModelIndex& index);
        void            OnDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);
        void            DetachModel();

        QVector<Binding>                            lBindings;
        int                                         nextBindingId;
        QPointer<QAbstractItemView>                 view;
        QVector<QMetaObject::Connection>            lModelConnections;
        mutable QHash<const QObject*, ComponentInfo> lComponents;   //!< \brief созданные компоненты (ключ — сам виджет)
        mutable QHash<int, QSize>                   lSizeCache;     //!< \brief sizeHint первого компонента по id привязки
    };
}

#endif // QTINTERFACE_DELEGATE_H
