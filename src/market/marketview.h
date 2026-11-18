#ifndef KT_MARKETVIEW_H
#define KT_MARKETVIEW_H

#include "market/vectorstore.h"
#include "QtInterface/core/basewidget.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>

class QComboBox;
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
    class OrderDialog;
    class SymbolPicker;

    /*!
     *  \brief Обзорщик токена: график котировок выбранной монеты.
     *
     *  - сверху: монета, текущая цена, изменение за окно, размер свечи (1 мин … неделя), «Токены»;
     *  - «−» / «+» слева сверху на графике — окно range (свечей = окно ÷ размер свечи);
     *  - линейка — нарисовать вектор теханализа двумя кликами; он сохраняется в выбранный набор
     *    (VectorStore, JSON в папке данных приложения), на графике — векторы набора по этой монете;
     *    цвет и толщина новых — в той же панели; правый клик по вектору — цвет, толщина, удалить;
     *  - «череп» — карта ликвидаций (GET /api/liqmap раз в минуту, пока включён; оценка сервера);
     *  - свечи — целиком (range + interval) при смене монеты/окна/свечи, по F5 и раз в 10 мин;
     *    между ними раз в candles_interval_ms — только 2 последние (interval + limit=2), склейка
     *    в CandleChart::MergeCandles; дыра в ряду — снова целиком;
     *  - цена — тики потока для монеты бота, GET /api/ticker/{symbol} для остальных;
     *  - для монеты бота на графике — вход, SL, TP и ликвидация его позиции и отметки сделок;
     *  - «+» в правом нижнем углу графика — окно «Новый ордер» по выбранной монете.
     *  Последние монета, размер свечи и окно запоминаются между запусками.
     */
    class MarketView : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        MarketView(QWidget* parent, QtInterface::ApiClient* api, QtInterface::Poller* poller, BotLink* bot,
                   const QStringList& favorites, const QStringList& fallbackSymbols, int candlesIntervalMs);

        QString                 GetSymbol() const;

    public slots:
        void                    SetSymbol(const QString& symbol);
        //! \brief Размер свечи: "1m" | "5m" | "15m" | "30m" | "1h" | "2h" | "12h" | "1d" | "1w".
        void                    SetInterval(const QString& interval);
        //! \brief Окно графика: "15m" … "24h" | "7d" | "30d" | "90d" | "1y" | "2y" | "3y" | "5y"; свечей = окно ÷ размер свечи.
        void                    SetRange(const QString& range);

    signals:
        //! \brief Оператор отправил ручной ордер из окна «Новый ордер» (ответ POST /api/order).
        void OrderPlaced(const QJsonObject& result);

    protected:
        bool                    eventFilter(QObject* watched, QEvent* event) override;
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
        //! \brief «+» — окно меньше (меньше свечей), «−» — больше; только окна, где свечей 2…2100.
        void                    Zoom(bool zoomIn);
        void                    UpdateZoomState();
        //! \brief Перечитать набор векторов с диска и показать векторы этой монеты.
        void                    LoadVectors();
        void                    ShowVectors();
        void                    SelectVectorSet(const QString& name);
        void                    CreateVectorSet();
        void                    SaveVector(double t1, double p1, double t2, double p2);
        void                    UpdateVector(const ChartVector& vector);
        void                    DeleteVector(const QString& id);
        //! \brief GET /api/liqmap для этой монеты (только пока включён «череп»).
        void                    RequestLiquidations();
        void                    SetLiquidationsActive(bool active);
        void                    UpdateBotOverlay();
        void                    OpenPicker();
        void                    OpenOrderDialog();
        void                    PlaceOrderButton();

        QtInterface::ApiClient* api;
        QtInterface::Poller*    poller;
        BotLink*                bot;
        QStringList             lFavorites;
        QStringList             lFallback;
        int                     candlesIntervalMs;

        QString                 symbol;
        QString                 interval;           //!< \brief размер свечи: параметр interval в /api/candles
        QString                 range;              //!< \brief окно: параметр range в /api/candles («−» / «+»)
        VectorStore             vectorStore;
        QString                 vectorSet;          //!< \brief выбранный набор векторов
        QVector<ChartVector>    lSetVectors;        //!< \brief все векторы набора (всех монет)
        QTimer*                 liqTimer;           //!< \brief обновление карты ликвидаций
        bool                    isLiqInFlight;
        int                     liqRetries;         //!< \brief повторы после ошибки (первый расчёт на сервере долгий)
        int                     generation;         //!< \brief растёт при смене монеты/окна/свечи: старые ответы выбрасываются
        bool                    isCandlesInFlight;
        bool                    needFullLoad;       //!< \brief следующий запрос свечей — целиком (сменился вид, дыра)
        qint64                  lastFullLoadMs;     //!< \brief когда свечи грузились целиком
        QString                 candlesInfo;        //!< \brief «окно · свеча · N свечей · источник» под графиком
        bool                    isTickerInFlight;
        double                  price;
        QJsonArray              lBotTrades;

        QLabel*                 symbolLabel;
        QLabel*                 priceLabel;
        QLabel*                 changeLabel;
        QLabel*                 infoLabel;
        QComboBox*              intervalBox;
        QPushButton*            pickerButton;
        CandleChart*            chart;
        QTimer*                 candlesTimer;
        QPointer<SymbolPicker>  picker;
        QPushButton*            orderButton;        //!< \brief «+» в правом нижнем углу графика
        QPointer<OrderDialog>   orderDialog;
    };
}

#endif // KT_MARKETVIEW_H
