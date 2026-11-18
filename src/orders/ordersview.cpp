#include "ordersview.h"

#include "market/candle.h"
#include "QtInterface/components/tablewidget.h"
#include "QtInterface/delegates/cellcomponents.h"
#include "QtInterface/delegates/delegate.h"
#include "QtInterface/models/basetablemodel.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/style/theme.h"

#include <QColor>
#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

using namespace QtInterface;

namespace Trade {

namespace {
    const int kMinIntervalMs = 5000;    //!< \brief не чаще этого опрашиваем сами

    //! \brief Открытый ордер (элемент /api/orders).
    struct Order {
        QString id;             //!< \brief order_id, а если его нет — order_link_id
        QString symbol;
        QString side;           //!< \brief Buy | Sell
        QString type;           //!< \brief Market | Limit
        QString stopType;       //!< \brief StopLoss | TakeProfit | … (пусто — обычный ордер)
        QString status;
        QString owner;          //!< \brief bot | monitor | пусто
        QString linkId;
        double  qty = 0;
        double  price = 0;
        double  trigger = 0;
        double  stopLoss = 0;
        double  takeProfit = 0;
        double  created = 0;
        bool    isReduceOnly = false;

        static Order FromJson(const QJsonObject& o)
        {
            Order r;
            r.linkId = o.value("order_link_id").toString();
            r.id = o.value("order_id").toString();
            if(r.id.isEmpty()) r.id = r.linkId;
            r.symbol = o.value("symbol").toString();
            r.side = o.value("side").toString();
            r.type = o.value("order_type").toString();
            r.stopType = o.value("stop_order_type").toString();
            r.status = o.value("status").toString();
            r.owner = o.value("owner").toString();
            r.qty = o.value("qty").toDouble();
            r.price = o.value("price").toDouble();
            r.trigger = o.value("trigger_price").toDouble();
            r.stopLoss = o.value("stop_loss").toDouble();
            r.takeProfit = o.value("take_profit").toDouble();
            r.created = o.value("created").toDouble();
            r.isReduceOnly = o.value("reduce_only").toBool();
            return r;
        }
    };

    QString Price(double p) { return p > 0 ? FormatPrice(p) : QString("—"); }
}

//! \brief Таблица открытых ордеров.
class OrdersModel : public BaseTableModel<Order, QString> {
public:
    explicit OrdersModel(QObject* parent)
        : BaseTableModel<Order, QString>([](const Order& o) { return o.id; }, parent)
    {
        const Qt::Alignment right = Qt::AlignRight | Qt::AlignVCenter;
        AddColumn({ "Монета", [](const Order& o) { return o.symbol; },
                    [](const Order& o) { return SymbolDisplayName(o.symbol); },
                    [](const Order& o, int role) { return role == Qt::ToolTipRole ? QVariant(o.symbol) : QVariant(); } });
        AddColumn({ "Сторона", [](const Order& o) { return o.side; }, {},
                    [](const Order& o, int role) {
                        if(role != Qt::ForegroundRole) return QVariant();
                        const ThemeColors& c = Theme::Instance().GetColors();
                        return QVariant(o.side == "Buy" ? c.good : c.bad);
                    } });
        AddColumn({ "Тип", [](const Order& o) { return o.type; },
                    [](const Order& o) {
                        QString t = o.stopType.isEmpty() ? o.type : o.stopType + " (" + o.type + ")";
                        if(o.isReduceOnly) t += " · reduce";
                        return t;
                    } });
        AddColumn({ "Кол-во", [](const Order& o) { return o.qty; },
                    [](const Order& o) { return QString::number(o.qty, 'g', 10); }, {}, right });
        AddColumn({ "Цена", [](const Order& o) { return o.price; }, [](const Order& o) { return Price(o.price); }, {}, right });
        AddColumn({ "Триггер", [](const Order& o) { return o.trigger; }, [](const Order& o) { return Price(o.trigger); },
                    {}, right, "Цена срабатывания условного ордера (SL/TP)" });
        AddColumn({ "SL", [](const Order& o) { return o.stopLoss; }, [](const Order& o) { return Price(o.stopLoss); }, {}, right });
        AddColumn({ "TP", [](const Order& o) { return o.takeProfit; }, [](const Order& o) { return Price(o.takeProfit); }, {}, right });
        AddColumn({ "Статус", [](const Order& o) { return o.status; } });
        AddColumn({ "Создан", [](const Order& o) { return o.created; },
                    [](const Order& o) {
                        return o.created > 0 ? QDateTime::fromMSecsSinceEpoch(qint64(o.created * 1000)).toString("dd.MM HH:mm:ss")
                                             : QString("—");
                    } });
        AddColumn({ "Чей", [](const Order& o) { return o.owner; },
                    [](const Order& o) {
                        return o.owner == "bot" ? QString("бот") : o.owner == "monitor" ? QString("монитор") : QString("—");
                    },
                    [](const Order& o, int role) { return role == Qt::ToolTipRole ? QVariant(o.linkId) : QVariant(); } });
        AddColumn({ "", [](const Order& o) { return o.id; }, [](const Order&) { return QString("Отменить"); } });
    }

    enum { ColCancel = 11 };
};

OrdersView::OrdersView(QWidget* parent, ApiClient* api_, Poller* poller_)
    : BaseWidget(parent)
    , api(api_)
    , poller(poller_)
    , model(nullptr)
    , table(nullptr)
    , summary(nullptr)
    , refreshButton(nullptr)
    , isRequestInFlight(false)
    , isFirstData(true)
{
}

bool OrdersView::InitForChild()
{
    if(!api || !poller) return LogErr("InitForChild: нет ApiClient или Poller");

    model = new OrdersModel(this);
    table = Create<TableWidget>(this);
    table->SetModel(model);
    table->GetDelegate()->BindColumn(OrdersModel::ColCancel, Cells::Button("Отменить", [this](const QModelIndex& index) {
        Cancel(index.data(Qt::EditRole).toString());
    }));

    summary = new QLabel(this);
    summary->setWordWrap(true);

    refreshButton = new QPushButton("Обновить", this);
    connect(refreshButton, &QPushButton::clicked, this, &OrdersView::Request);

    connect(poller, &Poller::Poll, this, [this]() {
        if(isVisible() && (!sinceRequest.isValid() || sinceRequest.elapsed() >= kMinIntervalMs)) Request();
    });

    Request();
    return true;
}

bool OrdersView::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout();
    top->addWidget(summary, 1);
    top->addWidget(refreshButton);
    mainLayout->addLayout(top, 0, 0);
    mainLayout->addWidget(table, 1, 0);
    mainLayout->setRowStretch(1, 1);
    return true;
}

bool OrdersView::UpdateForChild()
{
    Request();
    return true;
}

void OrdersView::Request()
{
    if(isRequestInFlight) return;
    isRequestInFlight = true;
    sinceRequest.start();
    api->Get("/api/orders", [this](const ApiReply& reply) {
        isRequestInFlight = false;
        const ThemeColors& c = Theme::Instance().GetColors();
        if(!reply.ok) {
            summary->setText(QString("<span style='color:%1'>Ордера не получены: %2</span>")
                                 .arg(c.bad.name(), reply.error.toHtmlEscaped()));
            return;
        }
        if(!reply.json.isArray()) {
            summary->setText(QString("<span style='color:%1'>Ордера: сервер ответил не списком</span>").arg(c.bad.name()));
            LogErr("/api/orders: ответ не массив");
            return;
        }
        std::vector<Order> orders;
        for(const QJsonValue& v : reply.json.array()) {
            Order o = Order::FromJson(v.toObject());
            if(!o.id.isEmpty()) orders.push_back(std::move(o));
        }
        const int count = static_cast<int>(orders.size());
        model->SetRows(std::move(orders));
        if(isFirstData && model->rowCount() > 0) {
            table->AlignColumns();
            isFirstData = false;
        }
        summary->setText(QString("Открытых ордеров: <b>%1</b> <span style='color:%2'>— аккаунт оператора, включая SL/TP · "
                                 "обновлено %3</span>")
                             .arg(count).arg(c.mutedText.name(), QDateTime::currentDateTime().toString("HH:mm:ss")));
    }, this);
}

void OrdersView::Cancel(const QString& id)
{
    const Order* found = model->GetRow(model->FindRow(id));
    if(!found) return;
    const Order o = *found;         // строка может уйти из модели, пока открыт вопрос
    const ThemeColors& c = Theme::Instance().GetColors();

    const QString what = QString("%1 %2 %3 %4%5").arg(o.side, o.stopType.isEmpty() ? o.type : o.stopType)
                             .arg(o.qty, 0, 'g', 10).arg(o.symbol, o.price > 0 ? " по " + FormatPrice(o.price)
                                                                   : o.trigger > 0 ? " триггер " + FormatPrice(o.trigger) : QString());
    if(QMessageBox::question(this, "Отмена ордера", "Отменить ордер на бирже?\n\n" + what,
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    QJsonObject body{ { "symbol", o.symbol } };
    if(o.id == o.linkId) body.insert("order_link_id", o.linkId);    // сервер не дал order_id
    else body.insert("order_id", o.id);
    summary->setText(QString("<span style='color:%1'>отменяю ордер…</span>").arg(c.mutedText.name()));
    api->Post("/api/order/cancel", QJsonDocument(body), [this, what](const ApiReply& reply) {
        if(!reply.ok) {
            summary->setText(QString("<span style='color:%1'>Ордер не отменён: %2</span>")
                                 .arg(Theme::Instance().GetColors().bad.name(), reply.error.toHtmlEscaped()));
            return;
        }
        LogMsg("ордер отменён: " + what);
        Request();
    }, this);
}

} // Trade
