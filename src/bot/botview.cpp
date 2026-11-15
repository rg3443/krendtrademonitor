#include "botview.h"

#include "botlink.h"
#include "market/candle.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/style/theme.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>

#include <algorithm>

using namespace QtInterface;

namespace Trade {

namespace {
    const int kMaxItems = 400;

    QString Time(double ts)
    {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ts * 1000));
        return dt.date() == QDate::currentDate() ? dt.toString("HH:mm:ss") : dt.toString("dd.MM HH:mm");
    }

    QString Esc(const QString& s) { return s.toHtmlEscaped().replace("\n", "<br>"); }

    QString Num(const QJsonValue& v)
    {
        if(v.isDouble()) {
            const double d = v.toDouble();
            return d == static_cast<qint64>(d) ? QString::number(static_cast<qint64>(d)) : QString::number(d, 'g', 6);
        }
        if(v.isBool())   return v.toBool() ? "да" : "нет";
        if(v.isNull() || v.isUndefined()) return "—";
        if(v.isString()) return v.toString();
        const QJsonDocument doc = v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
}

BotView::BotView(QWidget* parent, ApiClient* api_, BotLink* bot_)
    : BaseWidget(parent)
    , api(api_)
    , bot(bot_)
    , isReplanInFlight(false)
    , isWaitingForPlan(false)
    , status(nullptr)
    , showPlans(nullptr)
    , showEvents(nullptr)
    , showNotify(nullptr)
    , feed(nullptr)
    , comment(nullptr)
    , replanButton(nullptr)
    , replanStatus(nullptr)
{
}

bool BotView::InitForChild()
{
    if(!api || !bot) return LogErr("InitForChild: нет ApiClient или BotLink");

    status = new QLabel(this);
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    showPlans  = new QCheckBox("планы", this);
    showEvents = new QCheckBox("события", this);
    showNotify = new QCheckBox("сообщения бота", this);
    for(QCheckBox* box : { showPlans, showEvents, showNotify }) {
        box->setChecked(true);
        connect(box, &QCheckBox::toggled, this, &BotView::Render);
    }

    feed = new QTextBrowser(this);
    feed->setOpenLinks(false);
    feed->setReadOnly(true);

    comment = new QLineEdit(this);
    comment->setPlaceholderText("сообщение для Claude к вызову (необязательно): например «пролив к 62к, посмотри лонг»");
    comment->setMaxLength(2000);
    connect(comment, &QLineEdit::returnPressed, this, &BotView::Replan);

    replanButton = new QPushButton("Вызвать к размышлению", this);
    replanButton->setToolTip("POST /api/replan: Claude заново оценит рынок и выдаст новый план.\n"
                             "Текст из строки слева уйдёт ему как комментарий оператора.");
    connect(replanButton, &QPushButton::clicked, this, &BotView::Replan);

    replanStatus = new QLabel(this);

    connect(bot, &BotLink::PlanReceived, this, [this](const QJsonObject& plan) {
        AddPlan(plan);
        if(isWaitingForPlan) {
            isWaitingForPlan = false;
            replanStatus->setText(QString("<span style='color:%1'>новый план #%2</span>")
                                      .arg(cGood).arg(plan.value("id").toInt()));
        }
        Render();
    });
    connect(bot, &BotLink::EventReceived, this, [this](const QJsonObject& e) { AddEvent(e); Render(); });
    connect(bot, &BotLink::NotifyReceived, this, [this](const QString& t, double ts) { AddNotify(t, ts); Render(); });
    connect(bot, &BotLink::StateChanged, this, &BotView::UpdateStatus);
    connect(bot, &BotLink::PlanningChanged, this, [this](bool active, const QJsonObject& info) {
        const QString kind = info.value("kind").toString() == "reversal" ? "разворот" : "план";
        if(active) {
            const QString why = info.value("reason").toString();
            replanStatus->setText(QString("<span style='color:%1'>Claude думает (%2)%3…</span>")
                                      .arg(cAccent, kind, why.isEmpty() ? QString() : ": " + why.toHtmlEscaped()));
        } else if(!info.value("ok").toBool(true)) {
            isWaitingForPlan = false;
            replanStatus->setText(QString("<span style='color:%1'>Claude не дал %2: %3</span>")
                                      .arg(cBad, kind, info.value("error").toString().toHtmlEscaped()));
        }
    });
    connect(bot, &BotLink::StreamReconnected, this, &BotView::RequestHistory);

    RequestHistory();
    return true;
}

bool BotView::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->addWidget(status, 0, 0);

    auto* filters = new QHBoxLayout();
    filters->addWidget(new QLabel("Показывать:", this));
    filters->addWidget(showPlans);
    filters->addWidget(showEvents);
    filters->addWidget(showNotify);
    filters->addStretch(1);
    mainLayout->addLayout(filters, 1, 0);

    mainLayout->addWidget(feed, 2, 0);
    mainLayout->setRowStretch(2, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addWidget(comment, 1);
    bottom->addWidget(replanButton);
    mainLayout->addLayout(bottom, 3, 0);
    mainLayout->addWidget(replanStatus, 4, 0);
    UpdateStatus();
    return true;
}

bool BotView::ApplyStyleForChild(const Theme& theme)
{
    const ThemeColors& c = theme.GetColors();
    cText = c.text.name(); cMuted = c.mutedText.name(); cGood = c.good.name(); cBad = c.bad.name();
    cWarning = c.warning.name(); cAccent = c.accent.name(); cBorder = c.border.name(); cAltBase = c.alternateBase.name();
    replanButton->setStyleSheet(QString("QPushButton { font-weight: bold; padding: 4px 14px; }"));
    UpdateStatus();
    Render();
    return true;
}

bool BotView::UpdateForChild()
{
    RequestHistory();
    return true;
}

void BotView::RequestHistory()
{
    api->Get("/api/plans?limit=30", [this](const ApiReply& reply) {
        if(!reply.ok) { LogErr("планы не получены: " + reply.error); return; }
        for(const QJsonValue& v : reply.json.array()) AddPlan(v.toObject());
        Render();
    }, this);
    api->Get("/api/events?limit=150", [this](const ApiReply& reply) {
        if(!reply.ok) { LogErr("события не получены: " + reply.error); return; }
        for(const QJsonValue& v : reply.json.array()) AddEvent(v.toObject());
        Render();
    }, this);
}

void BotView::AddPlan(const QJsonObject& plan)
{
    AddItem("plan:" + QString::number(plan.value("id").toVariant().toLongLong()),
            { FeedItem::Kind::Plan, plan.value("ts").toDouble(), plan });
}

void BotView::AddEvent(const QJsonObject& event)
{
    AddItem("event:" + QString::number(event.value("id").toVariant().toLongLong()),
            { FeedItem::Kind::Event, event.value("ts").toDouble(), event });
}

void BotView::AddNotify(const QString& text, double ts)
{
    AddItem(QString("notify:%1:%2").arg(ts, 0, 'f', 3).arg(qHash(text)),
            { FeedItem::Kind::Notify, ts, QJsonObject{ { "text", text }, { "ts", ts } } });
}

void BotView::AddItem(const QString& key, const FeedItem& item)
{
    lItems.insert(key, item);
    if(lItems.size() <= kMaxItems) return;
    // выкинуть самые старые
    auto oldest = lItems.begin();
    for(auto it = lItems.begin(); it != lItems.end(); ++it)
        if(it->ts < oldest->ts) oldest = it;
    lItems.erase(oldest);
}

void BotView::Render()
{
    if(!feed) return;
    QVector<const FeedItem*> rows;
    for(const FeedItem& it : lItems) {
        if(it.kind == FeedItem::Kind::Plan   && !showPlans->isChecked())  continue;
        if(it.kind == FeedItem::Kind::Event  && !showEvents->isChecked()) continue;
        if(it.kind == FeedItem::Kind::Notify && !showNotify->isChecked()) continue;
        rows.append(&it);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const FeedItem* a, const FeedItem* b) { return a->ts < b->ts; });

    QString html;
    for(const FeedItem* it : rows) {
        switch(it->kind) {
            case FeedItem::Kind::Plan:   html += PlanHtml(it->data);   break;
            case FeedItem::Kind::Event:  html += EventHtml(it->data);  break;
            case FeedItem::Kind::Notify: html += NotifyHtml(it->data); break;
        }
    }
    if(rows.isEmpty())
        html = QString("<p style='color:%1'>пока пусто — бот ещё ничего не решал</p>").arg(cMuted);

    QScrollBar* bar = feed->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;
    const int keep = bar->value();
    feed->setHtml(html);
    bar->setValue(atBottom ? bar->maximum() : keep);
}

QString BotView::PlanHtml(const QJsonObject& p) const
{
    const QJsonObject plan = p.value("plan").toObject();
    QString html = QString("<table width='100%' cellspacing='0' cellpadding='6' style='margin-top:6px; background:%1;"
                           " border-left: 3px solid %2'><tr><td>").arg(cAltBase, cAccent);
    html += QString("<b style='color:%1'>План #%2</b> <span style='color:%3'>%4 · %5</span>")
                .arg(cAccent).arg(p.value("id").toInt()).arg(cMuted, Time(p.value("ts").toDouble()),
                     Esc(p.value("reason").toString("—")));
    const QString op = p.value("operator_comment").toString();
    if(!op.isEmpty())
        html += QString("<br><span style='color:%1'>оператор: «%2»</span>").arg(cWarning, Esc(op));

    QString summary = p.value("summary").toString();
    if(summary.isEmpty()) summary = plan.value("summary").toString();
    if(!summary.isEmpty()) html += "<p style='margin:4px 0'>" + Esc(summary) + "</p>";

    // полное рассуждение, если сервер его отдаёт (поле reasoning)
    QString reasoning = p.value("reasoning").toString();
    if(reasoning.isEmpty()) reasoning = plan.value("analysis").toString();
    if(!reasoning.isEmpty())
        html += QString("<p style='margin:4px 0; color:%1'>%2</p>").arg(cMuted, Esc(reasoning));

    const bool enabled = plan.value("trading_enabled").toBool();
    QStringList params;
    params << QString("<span style='white-space:nowrap'>режим <b>%1</b></span>").arg(Esc(plan.value("regime").toString("—")));
    params << QString("<span style='color:%1'>%2</span>").arg(enabled ? cGood : cBad, enabled ? "торговля вкл" : "торговля выкл");
    QStringList sides;
    if(plan.value("allow_long").toBool())  sides << "лонг";
    if(plan.value("allow_short").toBool()) sides << "шорт";
    params << "стороны: " + (sides.isEmpty() ? QString("нет") : sides.join("/"));
    params << "плечо " + Num(plan.value("leverage")) << "позиция $" + Num(plan.value("position_usd"))
           << "SL " + Num(plan.value("stop_loss_pct")) + "%" << "TP " + Num(plan.value("take_profit_pct")) + "%"
           << "держать ≤" + Num(plan.value("max_hold_minutes")) + "м" << "план на " + Num(plan.value("valid_for_minutes")) + "м";
    html += "<div>" + params.join(" · ") + "</div>";

    // остальные параметры — мелко
    static const QStringList shown = { "regime", "trading_enabled", "allow_long", "allow_short", "leverage",
                                       "position_usd", "stop_loss_pct", "take_profit_pct", "max_hold_minutes",
                                       "valid_for_minutes", "summary", "reasoning", "analysis" };
    QStringList rest;
    for(auto it = plan.begin(); it != plan.end(); ++it)
        if(!shown.contains(it.key())) rest << it.key() + "=" + Num(it.value());
    const QJsonObject usage = p.value("usage").toObject();
    if(!usage.isEmpty())
        rest << QString("токены %1→%2").arg(Num(usage.value("input_tokens")), Num(usage.value("output_tokens")));
    if(!p.value("model").toString().isEmpty()) rest << p.value("model").toString();
    if(!rest.isEmpty())
        html += QString("<br><span style='color:%1; font-size:small'>%2</span>").arg(cMuted, Esc(rest.join(" · ")));

    html += "</td></tr></table>";
    return html;
}

QString BotView::EventHtml(const QJsonObject& e) const
{
    const QString kind = e.value("kind").toString();
    QString color = cText;
    if(kind.contains("error") || kind.contains("failed") || kind == "panic" || kind == "order_rejected") color = cBad;
    else if(kind == "risk_reject" || kind.startsWith("reversal")) color = cWarning;
    else if(kind == "operator_action") color = cAccent;

    QString html = QString("<div style='margin-top:3px'><span style='color:%1'>%2</span> "
                           "<b style='color:%3'>%4</b> %5")
                       .arg(cMuted, Time(e.value("ts").toDouble()), color, Esc(kind), Esc(e.value("message").toString()));
    const QJsonObject data = e.value("data").toObject();
    const QJsonArray reasons = data.value("reasons").toArray();
    if(!reasons.isEmpty()) {
        QStringList r;
        for(const QJsonValue& v : reasons) r << Num(v);
        html += QString("<br><span style='color:%1'>&nbsp;&nbsp;причины: %2</span>").arg(cMuted, Esc(r.join("; ")));
    }
    return html + "</div>";
}

QString BotView::NotifyHtml(const QJsonObject& n) const
{
    return QString("<div style='margin-top:3px'><span style='color:%1'>%2</span> <b>бот:</b> %3</div>")
        .arg(cMuted, Time(n.value("ts").toDouble()), Esc(n.value("text").toString()));
}

void BotView::UpdateStatus()
{
    if(!status) return;
    const QJsonObject s = bot->GetState();
    if(s.isEmpty()) {
        status->setText(QString("<span style='color:%1'>состояние бота ещё не получено</span>").arg(cMuted));
    } else {
        const QString mode = s.value("mode").toString();
        QStringList parts;
        parts << QString("режим <b style='color:%1'>%2</b>").arg(mode == "off" ? cBad : cGood, Esc(mode));
        if(!s.value("api_ok").toBool(true))
            parts << QString("<b style='color:%1'>торговый API не отвечает</b>").arg(cBad);
        const QJsonValue botError = s.value("bot_error");
        if(!botError.isNull() && !botError.isUndefined()) {
            const QString text = botError.isString() ? botError.toString() : Num(botError);
            if(!text.isEmpty())
                parts << QString("<b style='color:%1'>ошибка бота: %2</b>").arg(cBad, Esc(text));
        }

        const QJsonObject plan = s.value("plan").toObject();
        if(!plan.isEmpty())
            parts << QString("план #%1 (%2, %3 мин назад)").arg(plan.value("id").toInt())
                         .arg(Esc(plan.value("regime").toString())).arg(plan.value("age_min").toDouble(), 0, 'f', 0);
        else
            parts << QString("<span style='color:%1'>плана нет</span>").arg(cWarning);

        const QJsonObject pos = s.value("position").toObject();
        if(!pos.isEmpty()) {
            const double pnl = pos.value("unrealized_pnl").toDouble();
            parts << QString("позиция %1 %2 @ %3, PnL <b style='color:%4'>%5 $</b>")
                         .arg(pos.value("side").toString() == "long" ? "лонг" : "шорт", Num(pos.value("size")),
                              FormatPrice(pos.value("entry_price").toDouble()), pnl >= 0 ? cGood : cBad)
                         .arg(pnl, 0, 'f', 2);
        } else {
            parts << "без позиции";
        }

        const QJsonObject budget = s.value("budget").toObject();
        if(!budget.isEmpty()) {
            const double eq = budget.value("equity").toDouble();
            const double b = budget.value("budget_usd").toDouble();
            parts << QString("бюджет <b style='color:%1'>%2 $</b> из %3 $").arg(eq >= b ? cGood : cBad)
                         .arg(eq, 0, 'f', 2).arg(b, 0, 'f', 0);
        }
        parts << QString("сделок сегодня: %1").arg(s.value("trades_today").toInt());
        const QString botAcc = s.value("bot_account").toString();
        const QString opAcc = s.value("operator_account").toString();
        if(!botAcc.isEmpty())
            parts << QString("<span style='color:%1'>аккаунты: бот %2, оператор %3</span>")
                         .arg(cMuted, Esc(botAcc), Esc(opAcc.isEmpty() ? QString("—") : opAcc));
        status->setText(parts.join(" · "));
    }

    const bool planning = bot->IsPlanning();
    replanButton->setEnabled(!planning && !isReplanInFlight);
    replanButton->setText(planning ? "Claude думает…" : "Вызвать к размышлению");
}

void BotView::Replan()
{
    if(isReplanInFlight || bot->IsPlanning()) return;
    isReplanInFlight = true;
    replanButton->setEnabled(false);
    const QString text = comment->text().trimmed();
    QJsonObject body;
    if(!text.isEmpty()) body.insert("comment", text);
    replanStatus->setText(QString("<span style='color:%1'>отправляю…</span>").arg(cMuted));

    api->Post("/api/replan", QJsonDocument(body), [this, text](const ApiReply& reply) {
        isReplanInFlight = false;
        if(!reply.ok) {
            replanStatus->setText(QString("<span style='color:%1'>не вызвать: %2</span>").arg(cBad, reply.error.toHtmlEscaped()));
        } else if(reply.json.object().value("accepted").toBool()) {
            isWaitingForPlan = true;
            comment->clear();
            LogMsg("бот вызван к размышлению" + (text.isEmpty() ? QString() : ": " + text));
            replanStatus->setText(QString("<span style='color:%1'>принято — Claude думает, план придёт через 10–30 с</span>").arg(cAccent));
        } else {
            replanStatus->setText(QString("<span style='color:%1'>не принято: %2</span>")
                                      .arg(cWarning, reply.json.object().value("reason").toString().toHtmlEscaped()));
        }
        UpdateStatus();
    }, this);
}

} // Trade
