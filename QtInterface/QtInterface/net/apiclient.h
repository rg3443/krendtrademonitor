#ifndef QTINTERFACE_APICLIENT_H
#define QTINTERFACE_APICLIENT_H

#include <QJsonDocument>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace QtInterface {

    //! \brief Результат запроса к серверу.
    struct ApiReply {
        bool            ok = false;         //!< \brief HTTP 2xx и тело — JSON (или пустое).
        int             httpStatus = 0;     //!< \brief Код HTTP; 0 — до сервера не достучались.
        QJsonDocument   json;               //!< \brief Тело ответа (если это JSON).
        QByteArray      body;               //!< \brief Тело ответа как есть (для файлов: txt и т. п.).
        QString         contentType;        //!< \brief Content-Type ответа.
        QString         error;              //!< \brief Понятный текст ошибки, если !ok.
    };

    /*!
     *  \brief Клиент HTTP API сервера: Bearer-токен, JSON, асинхронные запросы.
     *
     *  Колбэк вызывается в потоке GUI. Если передан context и он уже удалён —
     *  колбэк не вызывается (виджет закрыли, пока шёл запрос).
     *  Ошибки сервера вида {"error": "..."} попадают в ApiReply::error.
 *  Ответ в конверте {"ok": true, "data": X} разворачивается: в ApiReply::json — сразу X.
     *  Ответ не в JSON (Content-Type не json, например text/plain файла) не ошибка:
     *  его тело — в ApiReply::body.
     */
    class ApiClient : public QObject {
    Q_OBJECT
    public:
        using Callback = std::function<void(const ApiReply& reply)>;

        explicit ApiClient(QObject* parent = nullptr);

        //! \brief Адрес сервера, например http://127.0.0.1:8080
        void                    SetBaseUrl(const QUrl& url);
        QUrl                    GetBaseUrl() const;
        //! \brief Токен для заголовка Authorization: Bearer <token>.
        void                    SetToken(const QString& token);
        //! \brief Таймаут запроса, мс.
        void                    SetTimeout(int ms);

        //! \brief Есть ли связь с сервером (по последнему ответу).
        bool                    IsOnline() const;

        void                    Get(const QString& path, Callback callback, QObject* context = nullptr);
        void                    Post(const QString& path, const QJsonDocument& body, Callback callback, QObject* context = nullptr);
        void                    Put(const QString& path, const QJsonDocument& body, Callback callback, QObject* context = nullptr);
        void                    Delete(const QString& path, Callback callback, QObject* context = nullptr);

    signals:
        //! \brief Связь с сервером появилась/пропала.
        void OnlineChanged(bool online);
        //! \brief Любой запрос не удался (для строки состояния и журнала).
        void RequestFailed(const QString& path, const QString& error);

    private:
        void                    Send(const QByteArray& verb, const QString& path, const QByteArray& body,
                                     Callback callback, QObject* context);
        ApiReply                ParseReply(QNetworkReply* reply, bool timedOut) const;
        void                    SetOnline(bool online);

        QNetworkAccessManager*  nam;
        QUrl                    baseUrl;
        QString                 token;
        int                     timeoutMs;
        bool                    online;
    };
}

#endif // QTINTERFACE_APICLIENT_H
