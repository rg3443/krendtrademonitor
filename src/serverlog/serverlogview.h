#ifndef KT_SERVERLOGVIEW_H
#define KT_SERVERLOGVIEW_H

#include "QtInterface/core/basewidget.h"

#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace QtInterface {
    class ApiClient;
    class Poller;
}

namespace Trade {

    /*!
     *  \brief Лог сервера: хвост data/bot.log (GET /api/logs), дописывается по Poller.
     *
     *  Если сервер отдаёт курсор (`offset` в ответе), следующий запрос — `?after=<offset>`
     *  и приходят только новые строки. Если нет — берётся хвост и новые строки
     *  находятся по совпадению с последней уже показанной.
     *  Фильтр (grep) применяется на сервере; «Пауза» останавливает опрос.
     */
    class ServerLogView : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        ServerLogView(QWidget* parent, QtInterface::ApiClient* api, QtInterface::Poller* poller);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;
        bool                    UpdateForChild() override;

    private:
        void                    Request();
        void                    Reset();
        void                    Append(const QStringList& lines);
        QStringList             NewLinesByOverlap(const QStringList& lines) const;
        void                    UpdateStatus(const QString& error = QString());

        QtInterface::ApiClient* api;
        QtInterface::Poller*    poller;
        bool                    isRequestInFlight;
        int                     generation;
        qint64                  offset;             //!< \brief курсор сервера; -1 — сервер его не отдаёт
        QStringList             lTail;              //!< \brief последние показанные строки (для поиска нахлёста)
        int                     shownLines;

        QLineEdit*              grep;
        QCheckBox*              pause;
        QPushButton*            clearButton;
        QLabel*                 status;
        QPlainTextEdit*         text;
        QString                 cError, cWarning, cMuted;
    };
}

#endif // KT_SERVERLOGVIEW_H
