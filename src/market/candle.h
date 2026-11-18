#ifndef KT_CANDLE_H
#define KT_CANDLE_H

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace Trade {

    //! \brief Свеча из /api/candles. Время — Unix-секунды начала свечи.
    struct Candle {
        double ts     = 0;
        double open   = 0;
        double high   = 0;
        double low    = 0;
        double close  = 0;
        double volume = 0;

        //! \brief Разобрать массив candles; кривые элементы пропускаются, порядок — по времени.
        static QVector<Candle>  ListFromJson(const QJsonArray& array);
    };

    //! \brief Интервал свечи "1m" / "5m" / "1h" / "1d" / "1w" → секунды (0, если не разобрать).
    int         IntervalSeconds(const QString& interval);

    //! \brief Цена с разумным числом знаков: 64396.7, 3412.55, 0.003816.
    QString     FormatPrice(double price);

    //! \brief Короткое имя монеты для кнопок: BTCUSDT → BTC, 1000PEPEUSDT → PEPE, SHIB1000USDT → SHIB.
    QString     SymbolDisplayName(const QString& symbol);
}

#endif // KT_CANDLE_H
