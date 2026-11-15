// krendtrademonitor — десктоп-монитор торгового бота krendeltrader (Qt 5.12).
#include "appconfig.h"
#include "botlink.h"
#include "mainwindow.h"

#include "QtInterface/core/logger.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/net/streamclient.h"
#include "QtInterface/style/theme.h"

#include <QApplication>

using namespace QtInterface;

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);   // в Qt5 по умолчанию выключено
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setApplicationName("krendtrademonitor");
    QApplication::setOrganizationName("krendel");     // для QSettings (раскладка окна, последняя монета)

    Theme::Instance().ApplyToApp(app);

    Trade::AppConfig config;
    config.Load();

    ApiClient api;
    api.SetBaseUrl(config.serverUrl);
    api.SetToken(config.token);
    api.SetTimeout(config.timeoutMs);

    StreamClient stream;
    stream.SetUrl(config.GetStreamUrl());
    stream.SetToken(config.token);

    Poller poller;
    poller.SetInterval(config.pollIntervalMs);

    Trade::BotLink bot(&api, &stream, &poller);

    Trade::MainWindow window(config, &api, &poller, &bot);
    window.show();
    poller.Start();
    stream.Start();

    return app.exec();
}
