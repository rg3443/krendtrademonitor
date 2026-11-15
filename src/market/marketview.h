#ifndef KT_MARKETVIEW_H
#define KT_MARKETVIEW_H

#include "QtInterface/core/basewidget.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>

class QButtonGroup;
class QLabel;
class QPushButton;
class QTimer;

namespace QtInterface {
    class ApiClient;
    class Poller;
}

namespace Trade {

    class BotLink;
    class CandleChart;
    class SymbolPicker;

    /*!
     *  \brief Обзорщик токена: график котировок выбранной монеты.
     *
     *  - сверху: монета, текущая цена, изменение за окно, кнопки окна (15м … 24ч), «Токены»;
     *  - свечи — GET /api/candles раз в candles_interval_ms (кеш сервера 5 с);
     *  - цена — тики потока для монеты бота, GET /api/ticker/{symbol} для остальных;
     *  - для монеты бота на графике — вход, SL, TP и ликвидация его позиции и отметки сделок.
     *  Последние монета и окно запоминаются между запусками.
     */
    class MarketView : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        MarketView(QWidget* parent, QtInterface::ApiClient* api, QtInterface::Poller* poller, BotLink* bot,
                   const QStringList& favorites, const QStringList& fallbackSymbols, int candlesIntervalMs);

        QString                 GetSymbol() const;

    public slots:
        void                    SetSymbol(const QString& symbol);
        //! \brief Окно графика: "15m" | "30m" | "1h" | "4h" | "12h" | "24h" | "7d".
        void                    SetRange(const QString& range);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;
        bool                    UpdateForChild() override;

    private:
        bool                    IsBotSymbol() const;
        void                    RequestCandles();
        void                    RequestTicker();
        void                    RequestTrades();
        void                    OnPrice(double price, double ts);
        void                    UpdateHeader();
        void                    UpdateBotOverlay();
        void                    OpenPicker();

        QtInterface::ApiClient* api;
        QtInterface::Poller*    poller;
        BotLink*                bot;
        QStringList             lFavorites;
        QStringList             lFallback;
        int                     candlesIntervalMs;

        QString                 symbol;
        QString                 range;
        int                     generation;         //!< \brief растёт при смене монеты/окна: старые ответы выбрасываются
        bool                    isCandlesInFlight;
        bool                    isTickerInFlight;
        double                  price;
        QJsonArray              lBotTrades;

        QLabel*                 symbolLabel;
        QLabel*                 priceLabel;
        QLabel*                 changeLabel;
        QLabel*                 infoLabel;
        QButtonGroup*           rangeButtons;
        QPushButton*            pickerButton;
        CandleChart*            chart;
        QTimer*                 candlesTimer;
        QPointer<SymbolPicker>  picker;
    };
}

#endif // KT_MARKETVIEW_H
