#include "streamclient.h"

#include "QtInterface/core/logger.h"

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTimer>
#include <QWebSocket>

namespace QtInterface {

namespace {
    const int kMinRetryMs = 1000;
    const int kMaxRetryMs = 30000;
    const int kSilenceMs  = 60000;     //!< \brief столько без сообщений — связь потеряна
}

StreamClient::StreamClient(QObject* parent)
    : QObject(parent)
    , socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , reconnectTimer(new QTimer(this))
    , watchdog(new QTimer(this))
    , retryDelayMs(kMinRetryMs)
    , isRunning(false)
    , isConnected(false)
{
    reconnectTimer->setSingleShot(true);
    connect(reconnectTimer, &QTimer::timeout, this, &StreamClient::Open);

    watchdog->setSingleShot(true);
    watchdog->setInterval(kSilenceMs);
    connect(watchdog, &QTimer::timeout, this, [this]() {
        LogWarning("StreamClient", "поток молчит больше минуты — переподключаюсь");
        socket->abort();
    });

    connect(socket, &QWebSocket::connected, this, [this]() {
        retryDelayMs = kMinRetryMs;
        watchdog->start();
        SetConnected(true);
    });
    connect(socket, &QWebSocket::disconnected, this, &StreamClient::OnDisconnected);
    connect(socket, &QWebSocket::textMessageReceived, this, &StreamClient::OnTextMessage);
    // Неудачное подключение в Qt 5 не всегда даёт disconnected — ловим переход в Unconnected.
    connect(socket, &QWebSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if(state == QAbstractSocket::UnconnectedState && !isConnected) ScheduleReconnect();
    });
}

void StreamClient::SetUrl(const QUrl& url_)     { url = url_; }
QUrl StreamClient::GetUrl() const               { return url; }
void StreamClient::SetToken(const QString& t)   { token = t; }
bool StreamClient::IsConnected() const          { return isConnected; }

void StreamClient::Start()
{
    if(isRunning) return;
    isRunning = true;
    Open();
}

void StreamClient::Stop()
{
    isRunning = false;
    reconnectTimer->stop();
    watchdog->stop();
    socket->close();
    SetConnected(false);
}

bool StreamClient::Send(const QJsonObject& message)
{
    if(!isConnected) return false;
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    return true;
}

void StreamClient::Open()
{
    if(!isRunning) return;
    QNetworkRequest request(url);
    if(!token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    socket->open(request);
}

void StreamClient::OnTextMessage(const QString& text)
{
    watchdog->start();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if(err.error != QJsonParseError::NoError || !doc.isObject()) {
        LogWarning("StreamClient", "сообщение потока — не JSON-объект: " + err.errorString());
        return;
    }
    const QJsonObject o = doc.object();
    const QString type = o.value("type").toString();
    if(type == "ping") return;
    emit Message(type, o.value("data").toObject(), o.value("ts").toDouble());
}

void StreamClient::OnDisconnected()
{
    watchdog->stop();
    if(socket->closeCode() == 4401)
        LogError("StreamClient", "сервер отклонил токен потока (4401) — проверь token");
    else if(isConnected)
        LogWarning("StreamClient", "поток событий оборвался: " + socket->errorString());
    SetConnected(false);
    ScheduleReconnect();
}

void StreamClient::ScheduleReconnect()
{
    if(!isRunning || reconnectTimer->isActive()) return;
    reconnectTimer->start(retryDelayMs);
    retryDelayMs = qMin(retryDelayMs * 2, kMaxRetryMs);
}

void StreamClient::SetConnected(bool connected)
{
    if(isConnected == connected) return;
    isConnected = connected;
    if(connected) LogInfo("StreamClient", "поток событий подключён: " + url.toString());
    emit ConnectedChanged(connected);
}

} // QtInterface
