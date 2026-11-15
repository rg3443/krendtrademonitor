#ifndef QTINTERFACE_POLLER_H
#define QTINTERFACE_POLLER_H

#include <QObject>

class QTimer;

namespace QtInterface {

    /*!
     *  \brief Таймер опроса сервера: раз в интервал шлёт Poll().
     *  Подписчики (представления данных) по Poll() запрашивают свежие данные.
     *  Подписчик сам следит, чтобы не слать новый запрос, пока не пришёл ответ на старый.
     */
    class Poller : public QObject {
    Q_OBJECT
    public:
        explicit Poller(QObject* parent = nullptr);

        void                    SetInterval(int ms);
        int                     GetInterval() const;

        void                    Start();
        void                    Stop();
        bool                    IsActive() const;

    public slots:
        //! \brief Опросить прямо сейчас (и начать отсчёт интервала заново).
        void                    PollNow();

    signals:
        //! \brief Пора запросить свежие данные.
        void Poll();

    private:
        QTimer* timer;
    };
}

#endif // QTINTERFACE_POLLER_H
