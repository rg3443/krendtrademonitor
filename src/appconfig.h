#ifndef KT_APPCONFIG_H
#define KT_APPCONFIG_H

#include <QString>
#include <QStringList>
#include <QUrl>

namespace Trade {

    /*!
     *  \brief Настройки монитора из trademonitor.json рядом с exe.
     *  \code
     *  {
     *    "server_url": "http://127.0.0.1:8900",      // API бота через SSH-туннель:
     *                                                 // ssh -N -L 8900:127.0.0.1:8900 krendel-bot
     *    "token": "MONITOR_TOKEN из /opt/krendeltrader/.env",
     *    "poll_interval_ms": 3000,                    // лог сервера, цена не-BTC монет
     *    "candles_interval_ms": 5000,                 // свечи (кеш сервера — 5 с)
     *    "timeout_ms": 10000,
     *    "favorite_symbols": ["BTCUSDT", "ETHUSDT", "1000PEPEUSDT"],   // верх списка токенов
     *    "symbols": ["SOLUSDT", "XRPUSDT", ...]       // запасной список, если нет /api/symbols
     *  }
     *  \endcode
     */
    struct AppConfig {
        QUrl        serverUrl         = QUrl("http://127.0.0.1:8900");
        QString     token;
        int         pollIntervalMs    = 3000;
        int         candlesIntervalMs = 5000;
        int         timeoutMs         = 10000;
        QStringList favoriteSymbols   = { "BTCUSDT", "ETHUSDT", "1000PEPEUSDT" };
        QStringList symbols           = { "SOLUSDT", "XRPUSDT", "DOGEUSDT", "BNBUSDT", "ADAUSDT",
                                          "TONUSDT", "AVAXUSDT", "LINKUSDT", "SUIUSDT", "LTCUSDT",
                                          "DOTUSDT", "TRXUSDT", "NEARUSDT", "APTUSDT", "ARBUSDT",
                                          "OPUSDT", "WIFUSDT", "1000BONKUSDT", "SHIB1000USDT", "ENAUSDT" };

        //! \brief ws://…/api/stream по адресу сервера.
        QUrl            GetStreamUrl() const;

        //! \brief Путь к trademonitor.json (рядом с exe).
        static QString  GetPath();

        /*!
         *  \brief Прочитать настройки. Если файла нет — создать шаблон и вернуть значения по умолчанию.
         *  \return false, если файл есть, но не читается или не JSON (подробности в журнале)
         */
        bool            Load();
    };
}

#endif // KT_APPCONFIG_H
