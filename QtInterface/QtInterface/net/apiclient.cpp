#include "apiclient.h"

#include "QtInterface/core/logger.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace QtInterface {

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , nam(new QNetworkAccessManager(this))
    , timeoutMs(10000)
    , online(false)
{
}

void ApiClient::SetBaseUrl(const QUrl& url) { baseUrl = url; }
QUrl ApiClient::GetBaseUrl() const          { return baseUrl; }
void ApiClient::SetToken(const QString& t)  { token = t; }
void ApiClient::SetTimeout(int ms)          { timeoutMs = qMax(1000, ms); }
bool ApiClient::IsOnline() const            { return online; }

void ApiClient::Get(const QString& path, Callback callback, QObject* context)
{
    Send("GET", path, QByteArray(), std::move(callback), context);
}

void ApiClient::Post(const QString& path, const QJsonDocument& body, Callback callback, QObject* context)
{
    Send("POST", path, body.toJson(QJsonDocument::Compact), std::move(callback), context);
}

void ApiClient::Put(const QString& path, const QJsonDocument& body, Callback callback, QObject* context)
{
    Send("PUT", path, body.toJson(QJsonDocument::Compact), std::move(callback), context);
}

void ApiClient::Delete(const QString& path, Callback callback, QObject* context)
{
    Send("DELETE", path, QByteArray(), std::move(callback), context);
}

void ApiClient::Send(const QByteArray& verb, const QString& path, const QByteArray& body,
                     Callback callback, QObject* context)
{
    QString basePath = baseUrl.path();
    while(basePath.endsWith('/')) basePath.chop(1);
    // path может содержать запрос: "/api/x?limit=10" — путь и запрос задаются отдельно,
    // иначе QUrl закодирует '?' в путь как %3F
    const int q = path.indexOf('?');
    QUrl url = baseUrl;
    url.setPath(basePath + (q < 0 ? path : path.left(q)), QUrl::TolerantMode);
    if(q >= 0) url.setQuery(path.mid(q + 1));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    if(!token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());

    QNetworkReply* reply = nam->sendCustomRequest(request, verb, body);

    // В Qt 5.12 нет QNetworkRequest::setTransferTimeout — таймаут своим таймером.
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("qi_timedOut", true);
        reply->abort();
    });
    timer->start(timeoutMs);

    const bool hasContext = context != nullptr;
    QPointer<QObject> guard(context);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        const ApiReply result = ParseReply(reply, reply->property("qi_timedOut").toBool());

        // httpStatus == 0 — сервер не ответил вовсе; любой HTTP-ответ значит, что связь есть.
        SetOnline(result.httpStatus != 0);
        if(!result.ok) emit RequestFailed(path, result.error);

        if(hasContext && !guard) return;      // получатель уже удалён
        if(callback) callback(result);
    });
}

ApiReply ApiClient::ParseReply(QNetworkReply* reply, bool timedOut) const
{
    ApiReply result;
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    // Пустой Content-Type считаем JSON — так отвечает API; text/plain и т. п. — файл, не разбираем.
    const bool isJson = result.contentType.isEmpty() || result.contentType.contains("json", Qt::CaseInsensitive);

    if(!result.body.isEmpty()) {
        QJsonParseError parseError;
        result.json = QJsonDocument::fromJson(result.body, &parseError);
        if(isJson && parseError.error != QJsonParseError::NoError && result.httpStatus >= 200 && result.httpStatus < 300) {
            result.error = "ответ сервера — не JSON: " + parseError.errorString();
            return result;
        }
    }

    // Конверт {"ok": true, "data": ...}: дальше отдаём сразу data.
    // Скаляр (число, строку) QJsonDocument в Qt 5 не держит — тогда {"value": ...}.
    QString envelopeError;
    if(result.json.isObject()) {
        const QJsonObject o = result.json.object();
        if(o.contains("ok") && (o.contains("data") || o.contains("error"))) {
            envelopeError = o.value("error").toString();
            // ошибка торгового API: {"error", "code": "risk" | "bybit" | "dry_run" ..., "ret_code": 110007}
            QStringList codes;
            if(!o.value("code").toString().isEmpty()) codes << o.value("code").toString();
            if(!o.value("ret_code").isNull() && !o.value("ret_code").isUndefined())
                codes << o.value("ret_code").toVariant().toString();
            if(!envelopeError.isEmpty() && !codes.isEmpty()) envelopeError += " [" + codes.join(' ') + "]";
            const QJsonValue data = o.value("data");
            if(data.isArray())       result.json = QJsonDocument(data.toArray());
            else if(data.isObject()) result.json = QJsonDocument(data.toObject());
            else                     result.json = QJsonDocument(QJsonObject{ { "value", data } });
            if(!o.value("ok").toBool(true) && envelopeError.isEmpty()) envelopeError = "сервер ответил ok=false";
        }
    }

    if(timedOut) {
        result.httpStatus = 0;
        result.error = QString("сервер не ответил за %1 с").arg(timeoutMs / 1000);
    } else if(result.httpStatus == 0) {
        result.error = "нет связи с сервером: " + reply->errorString();
    } else if(result.httpStatus < 200 || result.httpStatus >= 300) {
        QString serverError = envelopeError;
        if(serverError.isEmpty()) serverError = result.json.object().value("error").toString();
        if(serverError.isEmpty()) serverError = result.json.object().value("detail").toString();
        result.error = QString("HTTP %1: %2").arg(result.httpStatus)
                           .arg(serverError.isEmpty() ? reply->errorString() : serverError);
        if(result.httpStatus == 401) result.error += " (проверь token в trademonitor.json)";
        if(result.httpStatus == 429) result.error += " (слишком часто — биржа ограничила запросы)";
    } else if(!envelopeError.isEmpty()) {
        result.error = envelopeError;
    } else {
        result.ok = true;
    }
    return result;
}

void ApiClient::SetOnline(bool online_)
{
    if(online == online_) return;
    online = online_;
    if(online) LogInfo("ApiClient", "связь с сервером есть: " + baseUrl.toString());
    else       LogWarning("ApiClient", "связь с сервером потеряна: " + baseUrl.toString());
    emit OnlineChanged(online);
}

} // QtInterface
