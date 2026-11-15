#include "poller.h"

#include <QTimer>

namespace QtInterface {

Poller::Poller(QObject* parent)
    : QObject(parent)
    , timer(new QTimer(this))
{
    timer->setInterval(3000);
    connect(timer, &QTimer::timeout, this, &Poller::Poll);
}

void Poller::SetInterval(int ms) { timer->setInterval(qMax(250, ms)); }
int  Poller::GetInterval() const { return timer->interval(); }

void Poller::Start()
{
    timer->start();
    emit Poll();        // первый опрос сразу, не через интервал
}

void Poller::Stop()          { timer->stop(); }
bool Poller::IsActive() const { return timer->isActive(); }

void Poller::PollNow()
{
    if(timer->isActive()) timer->start();   // перезапуск отсчёта
    emit Poll();
}

} // QtInterface
