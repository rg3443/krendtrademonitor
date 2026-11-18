#ifndef KT_ORDERDIALOG_H
#define KT_ORDERDIALOG_H

#include "QtInterface/core/basewidget.h"

#include <QJsonObject>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace QtInterface {
    class ApiClient;
}

namespace Trade {

    /*!
     *  \brief Окно «Новый ордер»: ручной ордер оператора по одной монете (POST /api/order).
     *
     *  Сторона (Buy/Sell), тип (Market/Limit), количество в базовой монете, цена и
     *  time-in-force для Limit, плечо, SL, TP, reduce-only. Перед отправкой переспрашивает.
     *  После принятого ордера шлёт Placed() и закрывается; предупреждения сервера
     *  («нет стоп-лосса» и т. п.) показывает отдельным окном.
     *  \code
     *  auto* dialog = QtInterface::Create<OrderDialog>(this, api, "ETHUSDT", 3412.5);
     *  connect(dialog, &OrderDialog::Placed, ordersView, &OrdersView::OnUpdate);
     *  dialog->show();
     *  \endcode
     */
    class OrderDialog : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        OrderDialog(QWidget* parent, QtInterface::ApiClient* api, const QString& symbol, double marketPrice);

    public slots:
        //! \brief Свежая рыночная цена монеты (для шапки и оценки объёма).
        void                    SetMarketPrice(double price);

    signals:
        //! \brief Ордер принят сервером: {"order_id", "order_link_id", "account", "warnings"}.
        void Placed(const QJsonObject& result);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;
        void                    keyPressEvent(QKeyEvent* event) override;

    private:
        bool                    IsLimit() const;
        void                    UpdateFields();
        void                    UpdateEstimate();
        void                    SetStatus(const QString& text, const QString& color);
        void                    Submit();

        QtInterface::ApiClient* api;
        QString                 symbol;
        double                  marketPrice;
        bool                    isInFlight;

        QLabel*                 header;
        QPushButton*            buyButton;
        QPushButton*            sellButton;
        QComboBox*              typeBox;
        QLineEdit*              qty;
        QLineEdit*              price;
        QComboBox*              tifBox;
        QSpinBox*               leverage;
        QLineEdit*              stopLoss;
        QLineEdit*              takeProfit;
        QCheckBox*              reduceOnly;
        QLabel*                 estimate;
        QLabel*                 status;
        QPushButton*            submitButton;
        QPushButton*            cancelButton;
        QString                 cGood, cBad, cMuted;
    };
}

#endif // KT_ORDERDIALOG_H
