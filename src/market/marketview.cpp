#include "marketview.h"

#include "botlink.h"
#include "market/candle.h"
#include "market/candlechart.h"
#include "market/symbolpicker.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/style/theme.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>

using namespace QtInterface;

namespace Trade {

namespace {
    struct RangeDef { const char* key; const char* title; };
    const RangeDef kRanges[] = {
        { "15m", "15 мин" }, { "30m", "30 мин" }, { "1h", "1 час" }, { "4h", "4 ч" },
        { "12h", "12 ч" }, { "24h", "24 ч" }, { "7d", "7 дн" },
    };
}

MarketView::MarketView(QWidget* parent, ApiClient* api_, Poller* poller_, BotLink* bot_,
                       const QStringList& favorites, const QStringList& fallbackSymbols, int candlesIntervalMs_)
    : BaseWidget(parent)
    , api(api_)
    , poller(poller_)
    , bot(bot_)
    , lFavorites(favorites)
    , lFallback(fallbackSymbols)
    , candlesIntervalMs(candlesIntervalMs_)
    , generation(0)
    , isCandlesInFlight(false)
    , isTickerInFlight(false)
    , price(0)
    , symbolLabel(nullptr)
    , priceLabel(nullptr)
    , changeLabel(nullptr)
    , infoLabel(nullptr)
    , rangeButtons(nullptr)
    , pickerButton(nullptr)
    , chart(nullptr)
    , candlesTimer(nullptr)
{
}

bool MarketView::InitForChild()
{
    if(!api || !poller || !bot) return LogErr("InitForChild: нет ApiClient, Poller или BotLink");

    QSettings settings;
    symbol = settings.value("market/symbol", lFavorites.value(0, "BTCUSDT")).toString();
    range  = settings.value("market/range", "1h").toString();

    symbolLabel = new QLabel(this);
    priceLabel  = new QLabel(this);
    changeLabel = new QLabel(this);
    infoLabel   = new QLabel(this);
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    rangeButtons = new QButtonGroup(this);
    rangeButtons->setExclusive(true);
    for(const RangeDef& r : kRanges) {
        auto* b = new QPushButton(r.title, this);
        b->setCheckable(true);
        b->setProperty("range", QString(r.key));
        b->setChecked(range == r.key);
        rangeButtons->addButton(b);
    }
    connect(rangeButtons, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this,
            [this](QAbstractButton* b) { SetRange(b->property("range").toString()); });

    pickerButton = new QPushButton("Токены…", this);
    pickerButton->setToolTip("Выбрать монету для графика");
    connect(pickerButton, &QPushButton::clicked, this, &MarketView::OpenPicker);

    chart = Create<CandleChart>(this);

    candlesTimer = new QTimer(this);
    candlesTimer->setInterval(candlesIntervalMs);
    connect(candlesTimer, &QTimer::timeout, this, &MarketView::RequestCandles);
    candlesTimer->start();

    // цена: тики потока (монета бота — всем, остальные — по подписке); без потока — опрос тикера
    connect(poller, &Poller::Poll, this, [this]() {
        if(!bot->IsStreamConnected()) RequestTicker();
    });
    connect(bot, &BotLink::TickError, this, [this](const QString& s, const QString& error) {
        if(s == symbol) infoLabel->setText(QString("<span style='color:%1'>цена %2 не получена: %3</span>")
                                               .arg(Theme::Instance().GetColors().warning.name(), s, error.toHtmlEscaped()));
    });
    connect(bot, &BotLink::Tick, this, [this](const QString& s, double p, double ts) {
        if(s == symbol) OnPrice(p, ts);
    });
    connect(bot, &BotLink::StateChanged, this, &MarketView::UpdateBotOverlay);
    connect(bot, &BotLink::TradeReceived, this, [this]() { if(IsBotSymbol()) RequestTrades(); });

    return true;
}

bool MarketView::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout();
    top->setSpacing(10);
    top->addWidget(symbolLabel);
    top->addWidget(priceLabel);
    top->addWidget(changeLabel);
    top->addStretch(1);
    for(QAbstractButton* b : rangeButtons->buttons()) top->addWidget(b);
    top->addSpacing(12);
    top->addWidget(pickerButton);
    mainLayout->addLayout(top, 0, 0);
    mainLayout->addWidget(chart, 1, 0);
    mainLayout->addWidget(infoLabel, 2, 0);
    mainLayout->setRowStretch(1, 1);

    SetSymbol(symbol);      // первая загрузка
    return true;
}

bool MarketView::ApplyStyleForChild(const Theme& theme)
{
    Q_UNUSED(theme);
    symbolLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    priceLabel->setStyleSheet("font-size: 18px;");
    UpdateHeader();
    UpdateBotOverlay();
    return true;
}

bool MarketView::UpdateForChild()
{
    RequestCandles();
    return true;
}

QString MarketView::GetSymbol() const { return symbol; }
bool MarketView::IsBotSymbol() const  { return symbol == bot->GetBotSymbol(); }

void MarketView::SetSymbol(const QString& symbol_)
{
    symbol = symbol_.trimmed().toUpper();
    if(symbol.isEmpty()) symbol = "BTCUSDT";
    QSettings().setValue("market/symbol", symbol);
    ++generation;
    isCandlesInFlight = isTickerInFlight = false;
    price = 0;
    lBotTrades = QJsonArray();
    chart->Clear();
    chart->SetMessage("загрузка " + symbol + "…");
    bot->SubscribeTicks(symbol);
    UpdateHeader();
    RequestCandles();
    RequestTicker();
    if(IsBotSymbol()) RequestTrades();
    UpdateBotOverlay();
}

void MarketView::SetRange(const QString& range_)
{
    range = range_;
    QSettings().setValue("market/range", range);
    for(QAbstractButton* b : rangeButtons->buttons())
        b->setChecked(b->property("range").toString() == range);
    ++generation;
    isCandlesInFlight = false;
    chart->SetMessage("загрузка…");
    RequestCandles();
}

void MarketView::RequestCandles()
{
    if(isCandlesInFlight) return;
    isCandlesInFlight = true;
    const int gen = generation;
    const QString path = QString("/api/candles?symbol=%1&range=%2")
                             .arg(QString(QUrl::toPercentEncoding(symbol)), range);
    api->Get(path, [this, gen](const ApiReply& reply) {
        if(gen != generation) return;        // монету или окно уже сменили
        isCandlesInFlight = false;
        if(!reply.ok) {
            if(chart->GetCandles().isEmpty()) chart->SetMessage("свечи не получены: " + reply.error);
            infoLabel->setText(QString("<span style='color:%1'>свечи: %2</span>")
                                   .arg(Theme::Instance().GetColors().bad.name(), reply.error.toHtmlEscaped()));
            return;
        }
        const QJsonObject o = reply.json.object();
        const QVector<Candle> candles = Candle::ListFromJson(o.value("candles").toArray());
        const QString interval = o.value("interval").toString();
        chart->SetMessage(candles.isEmpty() ? QString("нет свечей по ") + symbol : QString());
        chart->SetCandles(candles, IntervalSeconds(interval));
        if(price <= 0 && !candles.isEmpty()) price = candles.last().close;
        infoLabel->setText(QString("<span style='color:%1'>свеча %2 · источник %3 · обновлено %4</span>")
                               .arg(Theme::Instance().GetColors().mutedText.name(), interval,
                                    o.value("source").toString("?"),
                                    QDateTime::currentDateTime().toString("HH:mm:ss")));
        UpdateHeader();
    }, this);
}

void MarketView::RequestTicker()
{
    if(isTickerInFlight) return;
    isTickerInFlight = true;
    const int gen = generation;
    api->Get("/api/ticker/" + QString(QUrl::toPercentEncoding(symbol)), [this, gen](const ApiReply& reply) {
        if(gen != generation) return;
        isTickerInFlight = false;
        if(!reply.ok) return;           // ошибка уже в журнале ApiClient; график живёт на свечах
        const double p = reply.json.object().value("price").toDouble();
        if(p > 0) OnPrice(p, QDateTime::currentMSecsSinceEpoch() / 1000.0);
    }, this);
}

void MarketView::RequestTrades()
{
    const int gen = generation;
    api->Get("/api/trades?limit=100", [this, gen](const ApiReply& reply) {
        if(gen != generation || !reply.ok) return;
        lBotTrades = reply.json.array();
        UpdateBotOverlay();
    }, this);
}

void MarketView::OnPrice(double price_, double ts)
{
    price = price_;
    chart->SetLivePrice(price, ts);
    UpdateHeader();
}

void MarketView::UpdateHeader()
{
    if(!symbolLabel) return;
    const ThemeColors& c = Theme::Instance().GetColors();
    symbolLabel->setText(SymbolDisplayName(symbol));
    symbolLabel->setToolTip(symbol + (IsBotSymbol() ? "\nмонета бота: только просмотр, торгует бот" : QString()));
    priceLabel->setText(price > 0 ? FormatPrice(price) : QString("—"));

    const QVector<Candle>& candles = chart->GetCandles();
    if(price > 0 && !candles.isEmpty() && candles.first().open > 0) {
        const double change = (price - candles.first().open) / candles.first().open * 100;
        changeLabel->setText(QString("<span style='color:%1'>%2%3%</span> <span style='color:%4'>за окно</span>")
                                 .arg((change >= 0 ? c.good : c.bad).name(), change >= 0 ? "+" : "")
                                 .arg(change, 0, 'f', 2).arg(c.mutedText.name()));
    } else {
        changeLabel->clear();
    }
}

void MarketView::UpdateBotOverlay()
{
    if(!chart) return;
    if(!IsBotSymbol()) {
        chart->SetLevels({});
        chart->SetMarkers({});
        return;
    }
    const ThemeColors& c = Theme::Instance().GetColors();

    QVector<CandleChart::Level> levels;
    const QJsonObject pos = bot->GetState().value("position").toObject();
    if(!pos.isEmpty()) {
        const bool isLong = pos.value("side").toString() == "long";
        const QString who = pos.value("owner").toString() == "manual" ? " (ручн.)" : QString();
        levels.append({ pos.value("entry_price").toDouble(), (isLong ? "лонг" : "шорт") + who, c.accent, false });
        levels.append({ pos.value("stop_loss").toDouble(), "SL", c.bad, true });
        levels.append({ pos.value("take_profit").toDouble(), "TP", c.good, true });
        levels.append({ pos.value("liquidation_price").toDouble(), "ликв.", c.warning, true });
    }
    chart->SetLevels(levels);

    QVector<CandleChart::Marker> markers;
    for(const QJsonValue& v : lBotTrades) {
        const QJsonObject t = v.toObject();
        const bool isLong = t.value("side").toString() == "long";
        const double pnl = t.value("realized_pnl").toDouble();
        const QString tip = QString("%1 %2, плечо %3\nPnL %4 $ · %5")
                                .arg(isLong ? "лонг" : "шорт").arg(t.value("size").toDouble())
                                .arg(t.value("leverage").toInt()).arg(pnl, 0, 'f', 2)
                                .arg(t.value("close_reason").toString());
        if(t.value("ts_open").toDouble() > 0)
            markers.append({ t.value("ts_open").toDouble(), t.value("entry_price").toDouble(), true, isLong, tip });
        if(t.value("ts_close").toDouble() > 0)
            markers.append({ t.value("ts_close").toDouble(), t.value("exit_price").toDouble(), false, isLong, tip });
    }
    chart->SetMarkers(markers);
}

void MarketView::OpenPicker()
{
    if(picker) { picker->raise(); picker->activateWindow(); return; }
    picker = Create<SymbolPicker>(this, api, lFavorites, lFallback, symbol);
    connect(picker, &SymbolPicker::Chosen, this, &MarketView::SetSymbol);
    picker->show();
}

} // Trade
