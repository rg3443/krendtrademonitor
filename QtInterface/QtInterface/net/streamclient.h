#ifndef QTINTERFACE_STREAMCLIENT_H
#define QTINTERFACE_STREAMCLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QUrl>

class QTimer;
class QWebSocket;

namespace QtInterface {

    /*!
     *  \brief Поток событий сервера по WebSocket: {"type": ..., "ts": ..., "data": {...}}.
     *
     *  Сам переподключается после обрыва (через 1, 2, 4… до 30 с) и считает связь
     *  потерянной, если 60 с не было ни одного сообщения (сервер шлёт ping раз в 20 с).
     *  Каждое сообщение раздаётся сигналом Message(type, data, ts) — подписчик сам
     *  выбирает нужные типы.
     *  \code
     *  StreamClient stream;
     *  stream.SetUrl(QUrl("ws://127.0.0.1:8900/api/stream"));
     *  stream.SetToken(token);
     *  connect(&stream, &StreamClient::Message, this, [](const QString& type, const QJsonObject& data, double ts){ ... });
     *  stream.Start();
     *  \endcode
     */
    class StreamClient : public QObject {
    Q_OBJECT
    public:
        explicit StreamClient(QObject* parent = nullptr);

        void                    SetUrl(const QUrl& url);
        QUrl                    GetUrl() const;
        //! \brief Токен для заголовка Authorization: Bearer <token>.
        void                    SetToken(const QString& token);

        //! \brief Подключиться и держать связь.
        void                    Start();
        //! \brief Отключиться и больше не переподключаться.
        void                    Stop();

        bool                    IsConnected() const;

        //! \brief Отправить серверу JSON-объект. \return false, если не подключены.
        bool                    Send(const QJsonObject& message);

    signals:
        //! \brief Сообщение сервера (кроме ping).
        void Message(const QString& type, const QJsonObject& data, double ts);
        //! \brief Связь появилась/пропала.
        void ConnectedChanged(bool connected);

    private:
        void                    Open();
        void                    OnTextMessage(const QString& text);
        void                    OnDisconnected();
        void                    ScheduleReconnect();
        void                    SetConnected(bool connected);

        QWebSocket*     socket;
        QTimer*         reconnectTimer;
        QTimer*         watchdog;
        QUrl            url;
        QString         token;
        int             retryDelayMs;
        bool            isRunning;
        bool            isConnected;
    };
}

#endif // QTINTERFACE_STREAMCLIENT_H
