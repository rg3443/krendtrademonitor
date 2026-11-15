#ifndef KT_BOTLINK_H
#define KT_BOTLINK_H

#include <QJsonObject>
#include <QObject>

namespace QtInterface {
    class ApiClient;
    class Poller;
    class StreamClient;
}

namespace Trade {

    /*!
     *  \brief Живое состояние бота: поток /api/stream, разобранный по типам сообщений.
     *
     *  Хранит последний /api/state (из hello/state) и раздаёт сигналы по типам потока.
     *  Пока поток не подключён, по Poller сам опрашивает GET /api/state, чтобы
     *  строка состояния и линии позиции на графике не замирали.
     */
    class BotLink : public QObject {
    Q_OBJECT
    public:
        BotLink(QtInterface::ApiClient* api, QtInterface::StreamClient* stream, QtInterface::Poller* poller,
                QObject* parent = nullptr);

        //! \brief Последнее состояние бота (как /api/state); пустое, пока не пришло.
        const QJsonObject&      GetState() const;
        //! \brief Символ бота (обычно BTCUSDT).
        QString                 GetBotSymbol() const;
        //! \brief Claude сейчас думает над планом.
        bool                    IsPlanning() const;
        bool                    IsStreamConnected() const;

        /*!
         *  \brief Получать тики монеты из потока (монеты бота тики идут всем и так).
         *  Одна подписка за раз: прошлая снимается. После переподключения восстанавливается сама.
         */
        void                    SubscribeTicks(const QString& symbol);

    signals:
        //! \brief Новое состояние бота (hello, state или опрос /api/state).
        void StateChanged(const QJsonObject& state);
        //! \brief Тик цены бота (~раз в 5 с).
        void Tick(const QString& symbol, double price, double ts);
        //! \brief Новый план Claude (как элемент /api/plans).
        void PlanReceived(const QJsonObject& plan);
        //! \brief Новое событие (как элемент /api/events).
        void EventReceived(const QJsonObject& event);
        //! \brief Сделка бота открыта/закрыта (как элемент /api/trades).
        void TradeReceived(const QJsonObject& trade);
        //! \brief Сообщение бота (то, что ушло бы в Telegram).
        void NotifyReceived(const QString& text, double ts);
        //! \brief Поток подключился — подписчикам пора перечитать историю (могли пропустить).
        void StreamReconnected();
        /*!
         *  \brief Claude начал/закончил думать (сообщение planning потока).
         *  info: {"active", "kind", "reason", "comment"} или {"active": false, "ok", "plan_id" | "error"}.
         */
        void PlanningChanged(bool active, const QJsonObject& info);
        //! \brief Торговый API не отдал цену подписанной монеты.
        void TickError(const QString& symbol, const QString& error);
        void StreamConnectedChanged(bool connected);

    private:
        void                    OnMessage(const QString& type, const QJsonObject& data, double ts);
        void                    SetState(const QJsonObject& state);
        void                    PollState();

        QtInterface::ApiClient*     api;
        QtInterface::StreamClient*  stream;
        QJsonObject                 state;
        QString                     tickSymbol;         //!< \brief на что подписаны (пусто — ни на что)
        bool                        isStateRequestInFlight;
    };
}

#endif // KT_BOTLINK_H
