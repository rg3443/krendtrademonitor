#include "botlink.h"

#include "QtInterface/core/logger.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/net/streamclient.h"

#include <QJsonArray>
#include <QJsonDocument>

using namespace QtInterface;

namespace Trade {

BotLink::BotLink(ApiClient* api_, StreamClient* stream_, Poller* poller, QObject* parent)
    : QObject(parent)
    , api(api_)
    , stream(stream_)
    , isStateRequestInFlight(false)
{
    connect(stream, &StreamClient::Message, this, &BotLink::OnMessage);
    connect(stream, &StreamClient::ConnectedChanged, this, [this](bool connected) {
        emit StreamConnectedChanged(connected);
        if(connected) {
            if(!tickSymbol.isEmpty())
                stream->Send(QJsonObject{ { "subscribe", tickSymbol } });
            emit StreamReconnected();
        }
    });
    connect(poller, &Poller::Poll, this, [this]() {
        if(!stream->IsConnected()) PollState();
    });
}

const QJsonObject& BotLink::GetState() const    { return state; }
bool BotLink::IsPlanning() const                { return state.value("planning").toBool(); }
bool BotLink::IsStreamConnected() const         { return stream->IsConnected(); }

QString BotLink::GetBotSymbol() const
{
    const QString s = state.value("symbol").toString();
    return s.isEmpty() ? QString("BTCUSDT") : s;
}

void BotLink::OnMessage(const QString& type, const QJsonObject& data, double ts)
{
    if(type == "hello" || type == "state") {
        SetState(data);
    } else if(type == "tick") {
        const double price = data.value("price").toDouble();
        const QString symbol = data.value("symbol").toString(GetBotSymbol());
        if(price > 0) {
            if(symbol == GetBotSymbol()) {      // тики подписанных монет цену бота не трогают
                state.insert("price", price);
                state.insert("last_tick_ts", ts);
            }
            emit Tick(symbol, price, ts);
        }
    } else if(type == "planning") {
        const bool active = data.value("active").toBool();
        state.insert("planning", active);
        emit PlanningChanged(active, data);
        emit StateChanged(state);
    } else if(type == "tick_error") {
        emit TickError(data.value("symbol").toString(), data.value("error").toString());
    } else if(type == "subscribed") {
        const QJsonArray rejected = data.value("rejected").toArray();
        if(!rejected.isEmpty())
            LogWarning("BotLink", "сервер не подписал на тики: "
                       + QString::fromUtf8(QJsonDocument(rejected).toJson(QJsonDocument::Compact)));
    } else if(type == "plan") {
        emit PlanReceived(data);
    } else if(type == "event") {
        emit EventReceived(data);
    } else if(type == "trade") {
        emit TradeReceived(data);
    } else if(type == "notify") {
        emit NotifyReceived(data.value("text").toString(), ts);
    } else if(type == "log") {
        // строки лога — забирает ServerLogView через /api/logs, тут не нужны
    } else {
        LogDebug("BotLink", "неизвестный тип сообщения потока: " + type);
    }
}

void BotLink::SubscribeTicks(const QString& symbol)
{
    const QString s = symbol == GetBotSymbol() ? QString() : symbol;
    if(s == tickSymbol) return;
    if(!tickSymbol.isEmpty()) stream->Send(QJsonObject{ { "unsubscribe", tickSymbol } });
    tickSymbol = s;
    if(!tickSymbol.isEmpty()) stream->Send(QJsonObject{ { "subscribe", tickSymbol } });
}

void BotLink::SetState(const QJsonObject& state_)
{
    state = state_;
    emit StateChanged(state);
}

void BotLink::PollState()
{
    if(isStateRequestInFlight) return;
    isStateRequestInFlight = true;
    api->Get("/api/state", [this](const ApiReply& reply) {
        isStateRequestInFlight = false;
        if(reply.ok && reply.json.isObject()) SetState(reply.json.object());
    }, this);
}

} // Trade
