#include "marketview.h"

#include "botlink.h"
#include "market/candle.h"
#include "market/candlechart.h"
#include "market/symbolpicker.h"
#include "orders/orderdialog.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/style/theme.h"

#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>

using namespace QtInterface;

namespace Trade {

namespace {
    struct IntervalDef { const char* key; const char* title; };
    const IntervalDef kIntervals[] = {
        { "1m", "1 мин" }, { "5m", "5 мин" }, { "15m", "15 мин" }, { "30m", "30 мин" }, { "1h", "1 час" },
        { "2h", "2 часа" }, { "12h", "12 часов" }, { "1d", "24 часа" }, { "1w", "неделя" },
    };
    const char* const kDefaultInterval = "15m";

    //! \brief Окно графика (параметр range в /api/candles) — значения сервера, по возрастанию.
    struct RangeDef { const char* key; qint64 seconds; const char* title; };
    const RangeDef kRanges[] = {
        { "15m", 900, "15 мин" },  { "30m", 1800, "30 мин" },  { "1h", 3600, "1 час" },    { "2h", 7200, "2 часа" },
        { "4h", 14400, "4 часа" }, { "12h", 43200, "12 часов" }, { "24h", 86400, "24 часа" },
        { "7d", 604800, "7 дней" }, { "30d", 2592000, "30 дней" }, { "90d", 7776000, "90 дней" },
        { "1y", 31536000, "1 год" }, { "2y", 63072000, "2 года" }, { "3y", 94608000, "3 года" },
        { "5y", 157680000, "5 лет" },
    };
    const int  kRangeCount    = int(sizeof(kRanges) / sizeof(kRanges[0]));
    const char* const kDefaultRange = "24h";
    const int  kMinCandles    = 2;      //!< \brief меньше — «+» не даёт (одна свеча — не график)
    const int  kMaxCandles    = 2100;   //!< \brief больше сервер не отдаёт (ошибка 400)

    int FindRange(const QString& key)
    {
        for(int i = 0; i < kRangeCount; ++i) if(key == kRanges[i].key) return i;
        return -1;
    }

    //! \brief Свечей в окне: range ÷ interval, как считает сервер (целое, вниз).
    int CandleCount(int rangeIndex, int intervalSec)
    {
        return intervalSec > 0 ? int(kRanges[rangeIndex].seconds / intervalSec) : 0;
    }

    bool IsRangeValid(int rangeIndex, int intervalSec)
    {
        const int n = CandleCount(rangeIndex, intervalSec);
        return n >= kMinCandles && n <= kMaxCandles;
    }

    /*!
     *  \brief Ближайшее окно для этой свечи: то же, если годится, иначе — где свечей
     *  ближе всего к wantCount (по отношению, а не разнице: 50 ближе к 100, чем 200).
     */
    int FitRange(int rangeIndex, int intervalSec, int wantCount)
    {
        if(rangeIndex >= 0 && IsRangeValid(rangeIndex, intervalSec)) return rangeIndex;
        int best = -1;
        double bestDist = 0;
        for(int i = 0; i < kRangeCount; ++i) {
            if(!IsRangeValid(i, intervalSec)) continue;
            const double dist = std::fabs(std::log(double(CandleCount(i, intervalSec)) / std::max(1, wantCount)));
            if(best < 0 || dist < bestDist) { best = i; bestDist = dist; }
        }
        return best;
    }

    //! \brief «1 свеча» / «2 свечи» / «5 свечей».
    QString CandlesText(int n)
    {
        const int m10 = n % 10, m100 = n % 100;
        const char* word = (m10 == 1 && m100 != 11) ? "свеча"
                         : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? "свечи" : "свечей";
        return QString("%1 %2").arg(n).arg(word);
    }
    const char* const kDefaultVectorSet = "основной";
    const double kVectorWidth = 2;      //!< \brief толщина нового вектора по умолчанию, px
    const int kTailCandles = 2;             //!< \brief сколько последних свечей брать между полными загрузками
    const qint64 kFullReloadMs = 10 * 60 * 1000;    //!< \brief полная перезагрузка свечей — на всякий случай
    const int kLiqRefreshMs = 60000;    //!< \brief карта ликвидаций: кеш сервера 2 мин, опрос раз в минуту
    const int kLiqRetryMs   = 3000;     //!< \brief повтор после ошибки: первый расчёт до ~10 с, дальше — кеш
    const int kLiqMaxRetries = 3;
    const int kOrderButtonSize = 36;
    const int kOrderButtonGap  = 10;    //!< \brief отступ кнопки от края области свечей
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
    , vectorStore(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("vectors"))
    , liqTimer(nullptr)
    , isLiqInFlight(false)
    , liqRetries(0)
    , generation(0)
    , isCandlesInFlight(false)
    , needFullLoad(true)
    , lastFullLoadMs(0)
    , isTickerInFlight(false)
    , price(0)
    , symbolLabel(nullptr)
    , priceLabel(nullptr)
    , changeLabel(nullptr)
    , infoLabel(nullptr)
    , intervalBox(nullptr)
    , pickerButton(nullptr)
    , chart(nullptr)
    , candlesTimer(nullptr)
    , orderButton(nullptr)
{
}

bool MarketView::InitForChild()
{
    if(!api || !poller || !bot) return LogErr("InitForChild: нет ApiClient, Poller или BotLink");

    QSettings settings;
    symbol = settings.value("market/symbol", lFavorites.value(0, "BTCUSDT")).toString();
    interval = settings.value("market/interval", kDefaultInterval).toString();

    symbolLabel = new QLabel(this);
    priceLabel  = new QLabel(this);
    changeLabel = new QLabel(this);
    infoLabel   = new QLabel(this);
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    intervalBox = new QComboBox(this);
    intervalBox->setToolTip("Размер свечи");
    for(const IntervalDef& d : kIntervals) intervalBox->addItem(d.title, QString(d.key));
    if(intervalBox->findData(interval) < 0) interval = kDefaultInterval;
    intervalBox->setCurrentIndex(intervalBox->findData(interval));
    range = settings.value("market/range", kDefaultRange).toString();
    const int fitted = FitRange(FindRange(range), IntervalSeconds(interval), 100);
    range = kRanges[fitted >= 0 ? fitted : FindRange(kDefaultRange)].key;
    connect(intervalBox, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { SetInterval(intervalBox->itemData(index).toString()); });

    pickerButton = new QPushButton("Токены…", this);
    pickerButton->setToolTip("Выбрать монету для графика");
    connect(pickerButton, &QPushButton::clicked, this, &MarketView::OpenPicker);

    chart = Create<CandleChart>(this);
    connect(chart, &CandleChart::ZoomRequested, this, &MarketView::Zoom);
    UpdateZoomState();

    // векторы теханализа: набор по умолчанию создаётся при первом запуске
    connect(chart, &CandleChart::VectorDrawn, this, &MarketView::SaveVector);
    connect(chart, &CandleChart::VectorSetSelected, this, &MarketView::SelectVectorSet);
    connect(chart, &CandleChart::NewVectorSetRequested, this, &MarketView::CreateVectorSet);
    connect(chart, &CandleChart::VectorChanged, this, &MarketView::UpdateVector);
    connect(chart, &CandleChart::VectorDeleteRequested, this, &MarketView::DeleteVector);
    chart->SetNewVectorStyle(QColor(settings.value("market/vector_color",
                                                   Theme::Instance().GetColors().accent.name()).toString()),
                             settings.value("market/vector_width", kVectorWidth).toDouble());
    connect(chart, &CandleChart::NewVectorStyleChanged, this, [](const QColor& color, double width) {
        QSettings settings;
        settings.setValue("market/vector_color", color.name());
        settings.setValue("market/vector_width", width);
    });
    vectorSet = settings.value("market/vector_set", kDefaultVectorSet).toString();
    if(!vectorStore.HasSet(vectorSet)) {
        QString error;
        if(!vectorStore.HasSet(kDefaultVectorSet) && !vectorStore.CreateSet(kDefaultVectorSet, &error)) LogErr(error);
        vectorSet = kDefaultVectorSet;
    }
    LoadVectors();

    // карта ликвидаций: «череп» — выключатель; состояние помнится между запусками
    liqTimer = new QTimer(this);
    liqTimer->setInterval(kLiqRefreshMs);
    connect(liqTimer, &QTimer::timeout, this, &MarketView::RequestLiquidations);
    connect(chart, &CandleChart::LiquidationsToggled, this, &MarketView::SetLiquidationsActive);
    chart->SetLiquidationsActive(settings.value("market/liquidations", false).toBool());
    if(chart->IsLiquidationsActive()) liqTimer->start();    // первый запрос — из SetSymbol

    orderButton = new QPushButton("+", chart);
    orderButton->setFixedSize(kOrderButtonSize, kOrderButtonSize);
    orderButton->setCursor(Qt::PointingHandCursor);
    orderButton->setToolTip("Новый ордер по этой монете");
    connect(orderButton, &QPushButton::clicked, this, &MarketView::OpenOrderDialog);
    chart->installEventFilter(this);
    PlaceOrderButton();

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
    top->addWidget(intervalBox);
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
    const ThemeColors& c = theme.GetColors();
    orderButton->setStyleSheet(QString("QPushButton { border: none; border-radius: %1px; background: %2; color: %3;"
                                       " font-size: 22px; font-weight: bold; padding: 0 0 3px 0; }"
                                       " QPushButton:hover { background: %4; }")
                                   .arg(kOrderButtonSize / 2).arg(c.accent.name(), c.selectionText.name(),
                                                                  c.accent.darker(115).name()));
    symbolLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    priceLabel->setStyleSheet("font-size: 18px;");
    UpdateHeader();
    UpdateBotOverlay();
    return true;
}

bool MarketView::UpdateForChild()
{
    needFullLoad = true;        // F5 — перечитать свечи целиком
    RequestCandles();
    return true;
}

QString MarketView::GetSymbol() const { return symbol; }
bool MarketView::IsBotSymbol() const  { return symbol == bot->GetBotSymbol(); }

void MarketView::SetSymbol(const QString& symbol_)
{
    symbol = symbol_.trimmed().toUpper();
    if(symbol.isEmpty()) symbol = "BTCUSDT";
    if(orderDialog) orderDialog->close();       // окно ордера привязано к прежней монете
    QSettings().setValue("market/symbol", symbol);
    ++generation;
    isCandlesInFlight = isTickerInFlight = false;
    price = 0;
    lBotTrades = QJsonArray();
    chart->Clear();
    ShowVectors();
    chart->SetMessage("загрузка " + symbol + "…");
    bot->SubscribeTicks(symbol);
    UpdateHeader();
    needFullLoad = true;
    RequestCandles();
    RequestTicker();
    if(IsBotSymbol()) RequestTrades();
    UpdateBotOverlay();
    isLiqInFlight = false;          // ответ по прежней монете выбросится (сверка символа)
    liqRetries = 0;
    if(chart->IsLiquidationsActive()) RequestLiquidations();
}

void MarketView::SetLiquidationsActive(bool active)
{
    QSettings().setValue("market/liquidations", active);
    liqRetries = 0;
    if(active) {
        liqTimer->start();
        RequestLiquidations();
    } else {
        liqTimer->stop();
        chart->SetLiquidationsInfo(QString());
    }
}

void MarketView::RequestLiquidations()
{
    if(!chart->IsLiquidationsActive() || isLiqInFlight) return;
    isLiqInFlight = true;
    const QString requested = symbol;
    if(chart->GetCandles().isEmpty() || liqRetries > 0)
        chart->SetLiquidationsInfo("загрузка… (первый расчёт на сервере — до ~10 с)");
    // levels — все уровни ±10% для черточек; стакан не нужен
    api->Get("/api/liqmap?symbol=" + QString(QUrl::toPercentEncoding(requested)) + "&top=10&book=false",
             [this, requested](const ApiReply& reply) {
        if(requested != symbol) return;     // монету уже сменили
        isLiqInFlight = false;
        if(!chart->IsLiquidationsActive()) return;
        if(!reply.ok) {
            chart->SetLiquidationsInfo("не получена: " + reply.error);
            // первый расчёт бывает дольше таймаута клиента; сервер досчитает и закеширует — повторить
            if(liqRetries < kLiqMaxRetries) {
                ++liqRetries;
                QTimer::singleShot(kLiqRetryMs, this, &MarketView::RequestLiquidations);
            }
            return;
        }
        liqRetries = 0;
        const QJsonObject o = reply.json.object();
        QVector<CandleChart::LiqLevel> levels;
        for(const QJsonValue& item : o.value("levels").toArray()) {
            const QJsonObject l = item.toObject();
            CandleChart::LiqLevel level;
            level.price    = l.value("price").toDouble();
            level.longUsd  = l.value("long_usd").toDouble();
            level.shortUsd = l.value("short_usd").toDouble();
            if(level.price > 0 && level.longUsd + level.shortUsd > 0) levels.append(level);
        }
        chart->SetLiquidations(levels, o.value("step").toDouble());
        const double ts = o.value("ts").toDouble();
        chart->SetLiquidationsInfo(QString("%1 уровней · %2 · расчёт %3")
                                       .arg(levels.size())
                                       .arg(o.value("source").toString("?"),
                                            ts > 0 ? QDateTime::fromMSecsSinceEpoch(qint64(ts * 1000)).toString("HH:mm:ss")
                                                   : QString("?")));
    }, this);
}

void MarketView::SetInterval(const QString& interval_)
{
    const int index = intervalBox->findData(interval_);
    if(index < 0) return;
    // окно: прежнее, если с новой свечой в нём от 2 до 2100 свечей; иначе — где их примерно столько же, сколько было
    const int rangeIndex = FindRange(range);
    const int wasCount = rangeIndex >= 0 ? CandleCount(rangeIndex, IntervalSeconds(interval)) : 100;
    const int fitted = FitRange(rangeIndex, IntervalSeconds(interval_), wasCount);
    if(fitted < 0) return;      // для этой свечи нет ни одного окна (не бывает при нынешних списках)
    interval = interval_;
    intervalBox->setCurrentIndex(index);
    SetRange(kRanges[fitted].key);
}

void MarketView::SetRange(const QString& range_)
{
    if(FindRange(range_) < 0) return;
    range = range_;
    QSettings settings;
    settings.setValue("market/interval", interval);
    settings.setValue("market/range", range);
    UpdateZoomState();
    ++generation;
    isCandlesInFlight = false;
    needFullLoad = true;
    chart->SetMessage("загрузка…");
    RequestCandles();
}

void MarketView::Zoom(bool zoomIn)
{
    // «+» — ближайшее окно меньше (меньше свечей), «−» — больше; окна, где свечей не 2…2100, пропускаем
    const int intervalSec = IntervalSeconds(interval);
    const int step = zoomIn ? -1 : 1;
    for(int i = FindRange(range) + step; i >= 0 && i < kRangeCount; i += step) {
        if(IsRangeValid(i, intervalSec)) { SetRange(kRanges[i].key); return; }
    }
}

void MarketView::UpdateZoomState()
{
    const int current = FindRange(range);
    const int intervalSec = IntervalSeconds(interval);
    bool canIn = false, canOut = false;
    for(int i = 0; i < kRangeCount; ++i) {
        if(!IsRangeValid(i, intervalSec)) continue;
        if(i < current) canIn = true;
        if(i > current) canOut = true;
    }
    const QString info = current >= 0
        ? QString("Окно %1: %2").arg(kRanges[current].title, CandlesText(CandleCount(current, intervalSec)))
        : QString();
    chart->SetZoomState(canIn, canOut, info);
}

void MarketView::LoadVectors()
{
    QString error;
    lSetVectors = vectorStore.Load(vectorSet, &error);
    if(!error.isEmpty()) {
        LogErr(error);
        infoLabel->setText(QString("<span style='color:%1'>векторы: %2</span>")
                               .arg(Theme::Instance().GetColors().bad.name(), error.toHtmlEscaped()));
    }
    chart->SetVectorSets(vectorStore.ListSets(), vectorSet);
    ShowVectors();
}

void MarketView::ShowVectors()
{
    QVector<ChartVector> shown;
    for(const ChartVector& v : lSetVectors)
        if(v.symbol == symbol) shown.append(v);
    chart->SetVectors(shown);
}

void MarketView::SelectVectorSet(const QString& name)
{
    if(!vectorStore.HasSet(name)) return;
    vectorSet = name;
    QSettings().setValue("market/vector_set", vectorSet);
    LoadVectors();
}

void MarketView::CreateVectorSet()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Новый набор векторов", "Имя набора:", QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if(!ok || name.isEmpty()) return;
    QString why;
    if(!VectorStore::IsValidSetName(name, &why)) {
        QMessageBox::warning(this, "Новый набор векторов", "Имя «" + name + "» не годится: " + why);
        return;
    }
    if(vectorStore.HasSet(name)) {
        QMessageBox::information(this, "Новый набор векторов", "Набор «" + name + "» уже есть — выбираю его.");
    } else {
        QString error;
        if(!vectorStore.CreateSet(name, &error)) {
            QMessageBox::warning(this, "Новый набор векторов", error);
            return;
        }
        LogMsg("создан набор векторов: " + name);
    }
    SelectVectorSet(name);
}

void MarketView::SaveVector(double t1, double p1, double t2, double p2)
{
    ChartVector v;
    v.symbol = symbol;
    v.t1 = t1; v.p1 = p1;
    v.t2 = t2; v.p2 = p2;
    v.color = chart->GetNewVectorColor();
    v.width = chart->GetNewVectorWidth();
    QString error;
    if(!vectorStore.Append(vectorSet, v, &error)) {
        LogErr(error);
        QMessageBox::warning(this, "Вектор не сохранён", error);
        return;
    }
    LoadVectors();      // перечитать с диска: на экране — ровно то, что сохранилось
}

void MarketView::UpdateVector(const ChartVector& vector)
{
    QString error;
    if(!vectorStore.Update(vectorSet, vector, &error)) {
        LogErr(error);
        QMessageBox::warning(this, "Вектор не изменён", error);
    }
    LoadVectors();
}

void MarketView::DeleteVector(const QString& id)
{
    QString error;
    if(!vectorStore.Remove(vectorSet, id, &error)) {
        LogErr(error);
        QMessageBox::warning(this, "Вектор не удалён", error);
    }
    LoadVectors();
}

void MarketView::RequestCandles()
{
    if(isCandlesInFlight) return;
    isCandlesInFlight = true;
    const int gen = generation;
    // Полная загрузка — при смене монеты/окна/свечи, F5, раз в kFullReloadMs и после дыры в ряду;
    // в остальное время — только последние свечи: сервер дёргает OKX одним маленьким запросом.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool full = needFullLoad || chart->GetCandles().isEmpty() || now - lastFullLoadMs > kFullReloadMs;
    const QString sym = QString(QUrl::toPercentEncoding(symbol));
    const QString path = full ? QString("/api/candles?symbol=%1&range=%2&interval=%3").arg(sym, range, interval)
                              : QString("/api/candles?symbol=%1&interval=%2&limit=%3").arg(sym, interval).arg(kTailCandles);
    api->Get(path, [this, gen, full, now](const ApiReply& reply) {
        if(gen != generation) return;        // монету, окно или размер свечи уже сменили
        isCandlesInFlight = false;
        const ThemeColors& c = Theme::Instance().GetColors();
        if(!reply.ok) {
            if(chart->GetCandles().isEmpty()) chart->SetMessage("свечи не получены: " + reply.error);
            infoLabel->setText(QString("<span style='color:%1'>свечи: %2</span>")
                                   .arg(c.bad.name(), reply.error.toHtmlEscaped()));
            return;
        }
        const QJsonObject o = reply.json.object();
        const QVector<Candle> candles = Candle::ListFromJson(o.value("candles").toArray());
        const QString got = o.value("interval").toString(interval);     // размер свечи, который отдал сервер
        const QString updated = QDateTime::currentDateTime().toString("HH:mm:ss");

        if(!full) {
            if(!chart->MergeCandles(candles, IntervalSeconds(got))) {
                needFullLoad = true;        // дыра (сон, сеть) или чужой размер свечи — перезагрузить всё
                RequestCandles();
                return;
            }
            infoLabel->setText(QString("<span style='color:%1'>%2 · обновлено %3</span>")
                                   .arg(c.mutedText.name(), candlesInfo, updated));
            UpdateHeader();
            return;
        }

        needFullLoad = false;
        lastFullLoadMs = now;
        chart->SetMessage(candles.isEmpty() ? QString("нет свечей по ") + symbol : QString());
        chart->SetCandles(candles, IntervalSeconds(got));
        if(price <= 0 && !candles.isEmpty()) price = candles.last().close;
        const int count = o.value("count").toInt(candles.size());
        candlesInfo = QString("окно %1 · свеча %2 · %3 · источник %4")
                          .arg(o.value("range").toString(range), got, CandlesText(count), o.value("source").toString("?"));
        infoLabel->setText(QString("<span style='color:%1'>%2 · обновлено %3</span>")
                               .arg(c.mutedText.name(), candlesInfo, updated));
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
    if(orderDialog) orderDialog->SetMarketPrice(price);
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
    const double open = candles.isEmpty() ? 0 : candles.first().open;
    if(price > 0 && open > 0) {
        const double change = (price - open) / open * 100;
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

void MarketView::OpenOrderDialog()
{
    if(orderDialog) { orderDialog->raise(); orderDialog->activateWindow(); return; }
    orderDialog = Create<OrderDialog>(this, api, symbol, price);
    connect(orderDialog, &OrderDialog::Placed, this, &MarketView::OrderPlaced);
    orderDialog->show();
}

void MarketView::PlaceOrderButton()
{
    const QRect plot = chart->GetPlotRect();
    orderButton->move(plot.right() - kOrderButtonGap - kOrderButtonSize + 1,
                      plot.bottom() - kOrderButtonGap - kOrderButtonSize + 1);
    orderButton->raise();
}

bool MarketView::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == chart && event->type() == QEvent::Resize) PlaceOrderButton();
    return BaseWidget::eventFilter(watched, event);
}

} // Trade
