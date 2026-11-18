#ifndef KT_MAINWINDOW_H
#define KT_MAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QSplitter;

namespace QtInterface {
    class ApiClient;
    class Poller;
    class TabWidget;
}

namespace Trade {

    struct AppConfig;
    class BotLink;
    class MarketView;
    class BotView;

    /*!
     *  \brief Главное окно.
     *  Слева — обзорщик токена (график), справа — связь с ботом (раздумья и вызов),
     *  снизу — вкладки «Лог сервера», «Ордера», «Рекомендации», «Журнал монитора».
     *  В строке состояния — связь с API, поток событий, режим и цена бота.
     *  F5 — опросить сервер сейчас. Раскладка окна запоминается.
     */
    class MainWindow : public QMainWindow {
    Q_OBJECT
    public:
        MainWindow(const AppConfig& config, QtInterface::ApiClient* api, QtInterface::Poller* poller, BotLink* bot,
                   QWidget* parent = nullptr);

    protected:
        void                    closeEvent(QCloseEvent* event) override;

    private:
        void                    UpdateConnection();
        void                    UpdateBotStatus();

        QtInterface::ApiClient* api;
        QtInterface::Poller*    poller;
        BotLink*                bot;
        QSplitter*              vertical;
        QSplitter*              horizontal;
        MarketView*             market;
        BotView*                botView;
        QtInterface::TabWidget* bottomTabs;
        QLabel*                 connectionLabel;
        QLabel*                 streamLabel;
        QLabel*                 botLabel;
    };
}

#endif // KT_MAINWINDOW_H
