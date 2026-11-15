#include "delegate.h"

#include "QtInterface/core/logger.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QPainter>

namespace QtInterface {

Delegate::Delegate(QObject* parent)
    : QStyledItemDelegate(parent)
    , nextBindingId(1)
{
}

Delegate::~Delegate()
{
    DetachModel();
}

// ---------- привязки ----------

void Delegate::Attach(QAbstractItemView* view_)
{
    DetachModel();
    view = view_;
    if(!view) return;

    view->setItemDelegate(this);
    QAbstractItemModel* model = view->model();
    if(!model) return;

    lModelConnections << connect(model, &QAbstractItemModel::rowsInserted, this,
                                 [this](const QModelIndex& parent, int first, int last) { SyncRows(parent, first, last); });
    lModelConnections << connect(model, &QAbstractItemModel::columnsInserted, this, [this]() { RefreshComponents(); });
    lModelConnections << connect(model, &QAbstractItemModel::modelReset,      this, [this]() { RefreshComponents(); });
    lModelConnections << connect(model, &QAbstractItemModel::layoutChanged,   this, [this]() { RefreshComponents(); });
    lModelConnections << connect(model, &QAbstractItemModel::dataChanged,     this, &Delegate::OnDataChanged);
    RefreshComponents();
}

void Delegate::DetachModel()
{
    for(const auto& c : lModelConnections) disconnect(c);
    lModelConnections.clear();
}

int Delegate::Bind(Match match, CellComponent component)
{
    if(!match || !component.create) {
        LogError("Delegate", "Bind: пустое условие или нет create у компонента");
        return 0;
    }
    const int id = nextBindingId++;
    lBindings.append(Binding{ id, std::move(match), std::move(component) });
    RefreshComponents();
    return id;
}

int Delegate::BindColumn(int column, CellComponent component)
{
    return Bind([column](const QModelIndex& i) { return i.column() == column; }, std::move(component));
}

int Delegate::BindCell(int row, int column, CellComponent component)
{
    return Bind([row, column](const QModelIndex& i) { return i.row() == row && i.column() == column; },
                std::move(component));
}

bool Delegate::Unbind(int bindingId)
{
    for(int i = 0; i < lBindings.size(); ++i) {
        if(lBindings[i].id == bindingId) {
            lBindings.remove(i);
            RefreshComponents();
            return true;
        }
    }
    return false;
}

void Delegate::UnbindAll()
{
    lBindings.clear();
    RefreshComponents();
}

QModelIndex Delegate::IndexOf(const QWidget* component) const
{
    auto it = lComponents.constFind(component);
    return it == lComponents.constEnd() ? QModelIndex() : QModelIndex(it->index);
}

const Delegate::Binding* Delegate::FindBinding(const QModelIndex& index) const
{
    if(!index.isValid()) return nullptr;
    for(int i = lBindings.size() - 1; i >= 0; --i)     // побеждает добавленная позже
        if(lBindings[i].match(index)) return &lBindings[i];
    return nullptr;
}

const Delegate::Binding* Delegate::FindBindingById(int id) const
{
    for(const Binding& b : lBindings)
        if(b.id == id) return &b;
    return nullptr;
}

// ---------- постоянные компоненты ----------

void Delegate::RefreshComponents()
{
    if(!view || !view->model()) return;
    const int rows = view->model()->rowCount();
    if(rows > 0) SyncRows(QModelIndex(), 0, rows - 1);
}

void Delegate::SyncRows(const QModelIndex& parent, int first, int last)
{
    if(!view || !view->model()) return;
    QAbstractItemModel* model = view->model();
    const int columns = model->columnCount(parent);
    for(int row = first; row <= last; ++row) {
        for(int column = 0; column < columns; ++column)
            SyncIndex(model->index(row, column, parent));
        // деревья: дочерние строки
        const QModelIndex rowIndex = model->index(row, 0, parent);
        const int children = model->rowCount(rowIndex);
        if(children > 0) SyncRows(rowIndex, 0, children - 1);
    }
}

void Delegate::SyncIndex(const QModelIndex& index)
{
    const Binding* binding = FindBinding(index);
    const bool wanted = binding && binding->component.mode == CellComponent::Mode::Persistent;
    const bool isOpen = view->isPersistentEditorOpen(index);

    if(isOpen) {
        QWidget* current = view->indexWidget(index);
        auto it = lComponents.constFind(current);
        const bool isOurs = it != lComponents.constEnd();
        if(wanted && isOurs && it->bindingId == binding->id) return;   // уже стоит нужный
        if(!isOurs && !wanted) return;                                 // чужой редактор — не трогаем
        view->closePersistentEditor(index);
    }
    if(wanted) view->openPersistentEditor(index);
}

void Delegate::OnDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    // Привязка может зависеть от данных (Bind с условием) — проверить, нужен ли компонент теперь.
    if(!view || !topLeft.isValid() || !bottomRight.isValid() || lBindings.isEmpty()) return;
    QAbstractItemModel* model = view->model();
    const QModelIndex parent = topLeft.parent();
    for(int row = topLeft.row(); row <= bottomRight.row(); ++row)
        for(int column = topLeft.column(); column <= bottomRight.column(); ++column)
            SyncIndex(model->index(row, column, parent));
}

// ---------- QStyledItemDelegate ----------

QWidget* Delegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const Binding* binding = FindBinding(index);
    if(!binding)
        return QStyledItemDelegate::createEditor(parent, option, index);

    QWidget* widget = binding->component.create(parent, QPersistentModelIndex(index));
    if(!widget) {
        LogError("Delegate", "createEditor: компонент вернул nullptr");
        return nullptr;
    }
    lComponents.insert(widget, ComponentInfo{ binding->id, QPersistentModelIndex(index) });
    if(!lSizeCache.contains(binding->id)) lSizeCache.insert(binding->id, widget->sizeHint());
    connect(widget, &QObject::destroyed, this, [this](QObject* obj) { lComponents.remove(obj); });
    return widget;
}

void Delegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto it = lComponents.constFind(editor);
    if(it == lComponents.constEnd()) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    // По привязке, с которой компонент создан: тип виджета у неё гарантированно верный.
    const Binding* binding = FindBindingById(it->bindingId);
    if(binding && binding->component.update)
        binding->component.update(editor, index);
}

void Delegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    auto it = lComponents.constFind(editor);
    if(it == lComponents.constEnd()) {
        QStyledItemDelegate::setModelData(editor, model, index);
        return;
    }
    const Binding* binding = FindBindingById(it->bindingId);
    if(binding && binding->component.commit)
        binding->component.commit(editor, model, index);
}

void Delegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if(lComponents.contains(editor)) editor->setGeometry(option.rect);
    else                             QStyledItemDelegate::updateEditorGeometry(editor, option, index);
}

void Delegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const Binding* binding = FindBinding(index);
    const bool covered = binding
                      && binding->component.mode == CellComponent::Mode::Persistent
                      && !binding->component.paintCellText;
    if(!covered) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    // Ячейку закрывает компонент: рисуем только фон/выделение, без текста.
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.icon = QIcon();
    const QWidget* widget = opt.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
}

QSize Delegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize base = QStyledItemDelegate::sizeHint(option, index);
    const Binding* binding = FindBinding(index);
    if(!binding) return base;
    if(binding->component.sizeHint) return binding->component.sizeHint(index);

    // размер первого созданного компонента этой привязки
    auto it = lSizeCache.constFind(binding->id);
    return it == lSizeCache.constEnd() ? base : base.expandedTo(*it);
}

} // QtInterface
