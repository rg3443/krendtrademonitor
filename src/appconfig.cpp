#include "appconfig.h"

#include "QtInterface/core/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace QtInterface;

namespace Trade {

namespace {
    QStringList ToStringList(const QJsonValue& v, const QStringList& fallback)
    {
        if(!v.isArray()) return fallback;
        QStringList out;
        for(const QJsonValue& item : v.toArray()) {
            const QString s = item.toString().trimmed().toUpper();
            if(!s.isEmpty() && !out.contains(s)) out << s;
        }
        return out;
    }
}

QUrl AppConfig::GetStreamUrl() const
{
    QUrl url = serverUrl;
    url.setScheme(serverUrl.scheme() == "https" ? "wss" : "ws");
    QString path = url.path();
    while(path.endsWith('/')) path.chop(1);
    url.setPath(path + "/api/stream");
    return url;
}

QString AppConfig::GetPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("trademonitor.json");
}

bool AppConfig::Load()
{
    const QString path = GetPath();
    QFile file(path);

    if(!file.exists()) {
        QJsonObject tpl {
            { "server_url",          serverUrl.toString() },
            { "token",               "" },
            { "poll_interval_ms",    pollIntervalMs },
            { "candles_interval_ms", candlesIntervalMs },
            { "timeout_ms",          timeoutMs },
            { "favorite_symbols",    QJsonArray::fromStringList(favoriteSymbols) },
        };
        if(file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(tpl).toJson(QJsonDocument::Indented));
            LogWarning("AppConfig", "нет " + path + " — создан шаблон, впиши token (MONITOR_TOKEN сервера)");
        } else {
            LogError("AppConfig", "не создать " + path + ": " + file.errorString());
        }
        return true;
    }

    if(!file.open(QIODevice::ReadOnly)) {
        LogError("AppConfig", "не открыть " + path + ": " + file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        LogError("AppConfig", path + " — не JSON-объект: " + parseError.errorString());
        return false;
    }

    const QJsonObject o = doc.object();
    serverUrl         = QUrl(o.value("server_url").toString(serverUrl.toString()));
    token             = o.value("token").toString();
    pollIntervalMs    = qMax(500,  o.value("poll_interval_ms").toInt(pollIntervalMs));
    candlesIntervalMs = qMax(1000, o.value("candles_interval_ms").toInt(candlesIntervalMs));
    timeoutMs         = o.value("timeout_ms").toInt(timeoutMs);
    favoriteSymbols   = ToStringList(o.value("favorite_symbols"), favoriteSymbols);
    symbols           = ToStringList(o.value("symbols"), symbols);

    if(token.isEmpty())
        LogWarning("AppConfig", "в " + path + " пустой token — сервер ответит 401");
    LogInfo("AppConfig", "сервер " + serverUrl.toString() + ", поток " + GetStreamUrl().toString());
    return true;
}

} // Trade
