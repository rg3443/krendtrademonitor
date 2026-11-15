#ifndef QTINTERFACE_TABWIDGET_H
#define QTINTERFACE_TABWIDGET_H

#include "QtInterface/core/basewidget.h"

class QTabWidget;

namespace QtInterface {

    //! \brief Вкладки. Вкладки — любые виджеты; BaseWidget-вкладки получают UpdateAll.
    class TabWidget : public BaseWidget {
    Q_OBJECT
    public:
        explicit TabWidget(QWidget* parent = nullptr);

        /*!
         *  \brief Добавить вкладку.
         *  \param index  позиция: -1 — в конец, 0 — первой
         *  \return номер вкладки или -1
         */
        int                     AddTab(QWidget* widget, const QString& tabName, int index = -1);
        //! \brief Убрать вкладку (виджет не удаляется). \return бывший номер или -1
        int                     RemoveTab(QWidget* widget);
        //! \brief Переименовать вкладку.
        void                    SetTabName(QWidget* widget, const QString& newName);
        //! \brief Убрать все вкладки (виджеты не удаляются).
        void                    Clear();

        QTabWidget*             GetObj() const;

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;

    private:
        QTabWidget* obj;
    };
}

#endif // QTINTERFACE_TABWIDGET_H
