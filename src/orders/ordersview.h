#ifndef KT_ORDERSVIEW_H
#define KT_ORDERSVIEW_H

#include "QtInterface/core/basewidget.h"

#include <QElapsedTimer>

class QLabel;
class QPushButton;

namespace QtInterface {
    class ApiClient;
    class Poller;
    class TableWidget;
}

namespace Trade {

    class OrdersModel;

    /*!
     *  \brief Открытые ордера аккаунта оператора (GET /api/orders), включая SL/TP.
     *
     *  Список перечитывается по Poller, пока вкладка видна, но не чаще раза в 5 с
     *  (каждый запрос уходит в торговый API), а также по «Обновить», F5 и после
     *  создания ордера (OnUpdate()). Кнопка «Отменить» в строке снимает ордер
     *  (POST /api/order/cancel) после подтверждения.
     */
    class OrdersView : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        OrdersView(QWidget* parent, QtInterface::ApiClient* api, QtInterface::Poller* poller);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    UpdateForChild() override;

    private:
        void                    Request();
        //! \brief Отменить ордер по ключу строки (POST /api/order/cancel), с подтверждением.
        void                    Cancel(const QString& id);

        QtInterface::ApiClient*     api;
        QtInterface::Poller*        poller;
        OrdersModel*                model;
        QtInterface::TableWidget*   table;
        QLabel*                     summary;
        QPushButton*                refreshButton;
        bool                        isRequestInFlight;
        bool                        isFirstData;
        QElapsedTimer               sinceRequest;
    };
}

#endif // KT_ORDERSVIEW_H
