#ifndef KT_BOTVIEW_H
#define KT_BOTVIEW_H

#include "QtInterface/core/basewidget.h"

#include <QJsonObject>
#include <QMap>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace QtInterface {
    class ApiClient;
}

namespace Trade {

    class BotLink;

    /*!
     *  \brief Связь с ботом: лента его раздумий и вызов к размышлению.
     *
     *  Лента — планы Claude (/api/plans: рассуждение и параметры), события
     *  (/api/events: отказы риска, ошибки ордеров, развороты, действия оператора)
     *  и сообщения бота (notify из потока). История подгружается при старте и после
     *  переподключения потока, новое приходит из /api/stream.
     *
     *  Внизу — строка комментария и кнопка «Вызвать к размышлению» (POST /api/replan):
     *  комментарий уходит Claude вместе с запросом плана.
     */
    class BotView : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        BotView(QWidget* parent, QtInterface::ApiClient* api, BotLink* bot);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;
        bool                    UpdateForChild() override;

    private:
        //! \brief Запись ленты. Ключ "plan:15" / "event:120" / "notify:<ts>" — от повторов.
        struct FeedItem {
            enum class Kind { Plan, Event, Notify };
            Kind        kind;
            double      ts;
            QJsonObject data;
        };

        void                    RequestHistory();
        void                    AddPlan(const QJsonObject& plan);
        void                    AddEvent(const QJsonObject& event);
        void                    AddNotify(const QString& text, double ts);
        void                    AddItem(const QString& key, const FeedItem& item);
        void                    Render();
        QString                 PlanHtml(const QJsonObject& p) const;
        QString                 EventHtml(const QJsonObject& e) const;
        QString                 NotifyHtml(const QJsonObject& n) const;
        void                    UpdateStatus();
        void                    Replan();

        QtInterface::ApiClient* api;
        BotLink*                bot;
        QMap<QString, FeedItem> lItems;
        bool                    isReplanInFlight;
        bool                    isWaitingForPlan;   //!< \brief вызвали — ждём новый план

        QLabel*                 status;
        QLabel*                 botBudget;
        QCheckBox*              showPlans;
        QCheckBox*              showEvents;
        QCheckBox*              showNotify;
        QTextBrowser*           feed;
        QLineEdit*              comment;
        QPushButton*            replanButton;
        QLabel*                 replanStatus;

        QString                 cText, cMuted, cGood, cBad, cWarning, cAccent, cBorder, cAltBase;
    };
}

#endif // KT_BOTVIEW_H
