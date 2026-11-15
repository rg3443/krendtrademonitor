#include "cellcomponents.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>

namespace QtInterface {
namespace Cells {

CellComponent Button(const QString& text, IndexAction onClick)
{
    return Component<QPushButton>(
        [text](QPushButton* button, const QModelIndex& index) {
            button->setText(text.isEmpty() ? index.data(Qt::DisplayRole).toString() : text);
        },
        [onClick](QPushButton* button, const QPersistentModelIndex& index) {
            button->setCursor(Qt::PointingHandCursor);
            QObject::connect(button, &QPushButton::clicked, button, [onClick, index]() {
                if(onClick && index.isValid()) onClick(index);
            });
        });
}

CellComponent ComboBox(const QStringList& items, std::function<void(const QModelIndex&, const QString&)> onChosen)
{
    return Component<QComboBox>(
        [](QComboBox* box, const QModelIndex& index) {
            const QSignalBlocker block(box);   // смена данных из модели — не выбор пользователя
            box->setCurrentText(index.data(Qt::DisplayRole).toString());
        },
        [items, onChosen](QComboBox* box, const QPersistentModelIndex& index) {
            box->addItems(items);
            QObject::connect(box, QOverload<int>::of(&QComboBox::activated), box, [box, onChosen, index](int) {
                if(onChosen && index.isValid()) onChosen(index, box->currentText());
            });
        });
}

CellComponent Image(const QString& fixedPath)
{
    return Component<QLabel>(
        [fixedPath](QLabel* label, const QModelIndex& index) {
            QString path = fixedPath.isEmpty() ? index.data(Qt::EditRole).toString() : fixedPath;
            if(QDir::isRelativePath(path))
                path = QDir(QCoreApplication::applicationDirPath()).filePath(path);
            QPixmap pixmap(path);
            if(!pixmap.isNull() && label->height() > 0)
                pixmap = pixmap.scaledToHeight(label->height() - 2, Qt::SmoothTransformation);
            label->setPixmap(pixmap);
        },
        [](QLabel* label, const QPersistentModelIndex&) {
            label->setAlignment(Qt::AlignCenter);
        });
}

CellComponent Progress(int minimum, int maximum, const QString& format)
{
    return Component<QProgressBar>(
        [minimum, maximum](QProgressBar* bar, const QModelIndex& index) {
            bar->setValue(qBound(minimum, index.data(Qt::EditRole).toInt(), maximum));
        },
        [minimum, maximum, format](QProgressBar* bar, const QPersistentModelIndex&) {
            bar->setRange(minimum, maximum);
            bar->setFormat(format);
            bar->setTextVisible(true);
        });
}

CellComponent Badge(std::function<QColor(const QModelIndex&)> colorOf)
{
    return Component<QLabel>(
        [colorOf](QLabel* label, const QModelIndex& index) {
            const QColor color = colorOf ? colorOf(index) : QColor(Qt::gray);
            label->setText(index.data(Qt::DisplayRole).toString());
            label->setToolTip(index.data(Qt::ToolTipRole).toString());
            label->setStyleSheet(QString("QLabel { background: %1; color: white; border-radius: 3px;"
                                         " margin: 3px 6px; padding: 0 6px; font-weight: bold; }")
                                     .arg(color.name()));
        },
        [](QLabel* label, const QPersistentModelIndex&) {
            label->setAlignment(Qt::AlignCenter);
        });
}

} // Cells
} // QtInterface
