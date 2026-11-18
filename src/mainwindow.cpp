#include "mainwindow.h"

#include "appconfig.h"
#include "botlink.h"
#include "advice/adviceview.h"
#include "bot/botview.h"
#include "market/candle.h"
#include "market/marketview.h"
#include "orders/ordersview.h"
#include "serverlog/serverlogview.h"
#include "QtInterface/components/logwidget.h"
#include "QtInterface/components/tabwidget.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/style/theme.h"

#include <QAction>
#include <QCloseEvent>
#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>

using namespace QtInterface;

namespace Trade {

MainWindow::MainWindow(const AppConfig& config, ApiClient* api_, Poller* poller_, BotLink* bot_, QWidget* parent)
    : QMainWindow(parent)
    , api(api_)
    , poller(poller_)
    , bot(bot_)
    , vertical(nullptr)
    , horizontal(nullptr)
    , market(nullptr)
    , botView(nullptr)
    , bottomTabs(nullptr)
    , connectionLabel(nullptr)
    , streamLabel(nullptr)
    , botLabel(nullptr)
{
    setWindowTitle("krendtrademonitor — " + api->GetBaseUrl().toString());
    resize(1400, 900);

    market = Create<MarketView>(nullptr, api, poller, bot, config.favoriteSymbols, config.symbols, config.candlesIntervalMs);
    botView = Create<BotView>(nullptr, api, bot);

    bottomTabs = Create<TabWidget>(nullptr);
    bottomTabs->AddTab(Create<ServerLogView>(nullptr, api, poller), "Лог сервера");
    auto* orders = Create<OrdersView>(nullptr, api, poller);
    bottomTabs->AddTab(orders, "Ордера");
    connect(market, &MarketView::OrderPlaced, orders, &OrdersView::OnUpdate);
    bottomTabs->AddTab(Create<AdviceView>(nullptr, api), "Рекомендации");
    bottomTabs->AddTab(Create<LogWidget>(nullptr), "Журнал монитора");

    horizontal = new QSplitter(Qt::Horizontal);
    horizontal->addWidget(market);
    horizontal->addWidget(botView);
    horizontal->setStretchFactor(0, 3);
    horizontal->setStretchFactor(1, 2);

    vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(horizontal);
    vertical->addWidget(bottomTabs);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    setCentralWidget(vertical);

    botView->setMinimumWidth(380);
    horizontal->setSizes({ 880, 520 });
    vertical->setSizes({ 620, 260 });

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    vertical->restoreState(settings.value("window/vertical").toByteArray());
    horizontal->restoreState(settings.value("window/horizontal").toByteArray());

    connectionLabel = new QLabel(this);
    streamLabel = new QLabel(this);
    botLabel = new QLabel(this);
    statusBar()->addWidget(botLabel, 1);
    statusBar()->addPermanentWidget(streamLabel);
    statusBar()->addPermanentWidget(connectionLabel);
    connect(api, &ApiClient::OnlineChanged, this, &MainWindow::UpdateConnection);
    connect(bot, &BotLink::StreamConnectedChanged, this, &MainWindow::UpdateConnection);
    connect(bot, &BotLink::StateChanged, this, &MainWindow::UpdateBotStatus);
    connect(bot, &BotLink::Tick, this, [this](const QString& symbol) {
        if(symbol == bot->GetBotSymbol()) UpdateBotStatus();
    });
    UpdateConnection();
    UpdateBotStatus();

    auto* refresh = new QAction("Обновить", this);
    refresh->setShortcut(Qt::Key_F5);
    connect(refresh, &QAction::triggered, this, [this]() {
        poller->PollNow();
        market->OnUpdate();
        botView->OnUpdate();
    });
    addAction(refresh);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/vertical", vertical->saveState());
    settings.setValue("window/horizontal", horizontal->saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::UpdateConnection()
{
    const ThemeColors& c = Theme::Instance().GetColors();
    connectionLabel->setText(api->IsOnline()
        ? QString("<span style='color:%1'>● API на связи</span>").arg(c.good.name())
        : QString("<span style='color:%1'>● нет связи с API (туннель ssh -L 8900 поднят?)</span>").arg(c.bad.name()));
    streamLabel->setText(bot->IsStreamConnected()
        ? QString("<span style='color:%1'>● поток событий</span>").arg(c.good.name())
        : QString("<span style='color:%1'>● поток отключён</span>").arg(c.warning.name()));
}

void MainWindow::UpdateBotStatus()
{
    const ThemeColors& c = Theme::Instance().GetColors();
    const QJsonObject s = bot->GetState();
    if(s.isEmpty()) {
        botLabel->setText(QString("<span style='color:%1'>F5 — обновить сейчас</span>").arg(c.mutedText.name()));
        return;
    }
    const QString mode = s.value("mode").toString();
    QString text = QString("бот: <b style='color:%1'>%2</b> · %3 <b>%4</b>")
                       .arg((mode == "off" ? c.bad : c.good).name(), mode.toHtmlEscaped(),
                            SymbolDisplayName(bot->GetBotSymbol()), FormatPrice(s.value("price").toDouble()));
    if(bot->IsPlanning())
        text += QString(" · <span style='color:%1'>Claude думает…</span>").arg(c.accent.name());
    const QString botError = s.value("bot_error").toString();
    if(!botError.isEmpty())
        text += QString(" · <b style='color:%1'>ошибка бота</b>").arg(c.bad.name());
    const QJsonObject budget = s.value("budget").toObject();
    if(!budget.isEmpty())
        text += QString(" · equity бота %1 $").arg(budget.value("equity").toDouble(), 0, 'f', 2);
    botLabel->setText(text);
}

} // Trade
