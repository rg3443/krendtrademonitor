#include "orderdialog.h"

#include "market/candle.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/style/theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>

using namespace QtInterface;

namespace Trade {

namespace {
    //! \brief Текст поля как число для сервера: "0,01" → "0.01"; пустая строка — поле не заполнено.
    QString NumText(const QLineEdit* edit) { return edit->text().trimmed().replace(',', '.').remove(' '); }

    //! \brief Положительное число из поля; 0 — пусто, -1 — не число.
    double NumValue(const QLineEdit* edit)
    {
        const QString t = NumText(edit);
        if(t.isEmpty()) return 0;
        bool ok = false;
        const double v = t.toDouble(&ok);
        return ok && v > 0 ? v : -1;
    }
}

OrderDialog::OrderDialog(QWidget* parent, ApiClient* api_, const QString& symbol_, double marketPrice_)
    : BaseWidget(parent)
    , api(api_)
    , symbol(symbol_)
    , marketPrice(marketPrice_)
    , isInFlight(false)
    , header(nullptr)
    , buyButton(nullptr)
    , sellButton(nullptr)
    , typeBox(nullptr)
    , qty(nullptr)
    , price(nullptr)
    , tifBox(nullptr)
    , leverage(nullptr)
    , stopLoss(nullptr)
    , takeProfit(nullptr)
    , reduceOnly(nullptr)
    , estimate(nullptr)
    , status(nullptr)
    , submitButton(nullptr)
    , cancelButton(nullptr)
{
}

bool OrderDialog::InitForChild()
{
    if(!api) return LogErr("InitForChild: нет ApiClient");
    if(symbol.isEmpty()) return LogErr("InitForChild: не задана монета");
    setWindowFlags(Qt::Dialog);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Новый ордер — " + symbol);

    header = new QLabel(this);

    buyButton = new QPushButton("Buy / лонг", this);
    sellButton = new QPushButton("Sell / шорт", this);
    auto* sides = new QButtonGroup(this);
    for(QPushButton* b : { buyButton, sellButton }) {
        b->setCheckable(true);
        b->setAutoDefault(false);
        sides->addButton(b);
    }
    connect(sides, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this, [this]() { UpdateEstimate(); });

    typeBox = new QComboBox(this);
    typeBox->addItems({ "Market", "Limit" });
    connect(typeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { UpdateFields(); });

    qty = new QLineEdit(this);
    qty->setPlaceholderText("в базовой монете (для 1000PEPEUSDT — в «1000PEPE»)");
    price = new QLineEdit(this);
    tifBox = new QComboBox(this);
    tifBox->addItems({ "GTC", "IOC", "FOK", "PostOnly" });

    leverage = new QSpinBox(this);
    leverage->setRange(0, 125);
    leverage->setSpecialValueText("не менять");
    leverage->setPrefix("x");
    leverage->setToolTip("Если задано — перед ордером сервер поставит это плечо по монете");

    stopLoss = new QLineEdit(this);
    stopLoss->setPlaceholderText("цена, необязательно");
    takeProfit = new QLineEdit(this);
    takeProfit->setPlaceholderText("цена, необязательно");

    reduceOnly = new QCheckBox("только уменьшать позицию (reduce-only)", this);

    estimate = new QLabel(this);
    status = new QLabel(this);
    status->setWordWrap(true);

    submitButton = new QPushButton("Отправить ордер", this);
    submitButton->setAutoDefault(false);
    connect(submitButton, &QPushButton::clicked, this, &OrderDialog::Submit);
    cancelButton = new QPushButton("Закрыть", this);
    cancelButton->setAutoDefault(false);
    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);

    for(QLineEdit* e : { qty, price })
        connect(e, &QLineEdit::textChanged, this, [this]() { UpdateEstimate(); });
    connect(leverage, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { UpdateEstimate(); });
    return true;
}

bool OrderDialog::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(header, 0, 0);

    auto* sides = new QHBoxLayout();
    sides->addWidget(buyButton, 1);
    sides->addWidget(sellButton, 1);
    mainLayout->addLayout(sides, 1, 0);

    auto* form = new QFormLayout();
    form->addRow("Тип", typeBox);
    form->addRow("Количество", qty);
    form->addRow("Цена", price);
    form->addRow("Time in force", tifBox);
    form->addRow("Плечо", leverage);
    form->addRow("Стоп-лосс", stopLoss);
    form->addRow("Тейк-профит", takeProfit);
    form->addRow("", reduceOnly);
    mainLayout->addLayout(form, 2, 0);

    mainLayout->addWidget(estimate, 3, 0);
    mainLayout->addWidget(status, 4, 0);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(submitButton);
    buttons->addWidget(cancelButton);
    mainLayout->addLayout(buttons, 5, 0);

    setMinimumWidth(420);
    UpdateFields();
    qty->setFocus();
    return true;
}

bool OrderDialog::ApplyStyleForChild(const Theme& theme)
{
    const ThemeColors& c = theme.GetColors();
    cGood = c.good.name(); cBad = c.bad.name(); cMuted = c.mutedText.name();
    const QString side = "QPushButton { font-weight: bold; padding: 8px; }"
                         " QPushButton:checked { background: %1; color: white; border: 1px solid %1; }";
    buyButton->setStyleSheet(side.arg(cGood));
    sellButton->setStyleSheet(side.arg(cBad));
    submitButton->setStyleSheet("QPushButton { font-weight: bold; padding: 4px 14px; }");
    SetMarketPrice(marketPrice);
    return true;
}

bool OrderDialog::IsLimit() const { return typeBox->currentText() == "Limit"; }

void OrderDialog::SetMarketPrice(double price_)
{
    marketPrice = price_;
    if(!header) return;
    header->setText(QString("<span style='font-size:16px'><b>%1</b> · %2</span> <span style='color:%3'>%4 · аккаунт оператора</span>")
                        .arg(SymbolDisplayName(symbol), marketPrice > 0 ? FormatPrice(marketPrice) : QString("—"),
                             cMuted, symbol));
    UpdateEstimate();
}

void OrderDialog::UpdateFields()
{
    const bool limit = IsLimit();
    price->setEnabled(limit);
    tifBox->setEnabled(limit);
    price->setPlaceholderText(limit ? "цена лимитного ордера" : "по рынку");
    if(limit && price->text().isEmpty() && marketPrice > 0) price->setText(QString::number(marketPrice, 'g', 10));
    UpdateEstimate();
}

void OrderDialog::UpdateEstimate()
{
    if(!estimate) return;
    const double q = NumValue(qty);
    const double p = IsLimit() ? NumValue(price) : marketPrice;
    if(q <= 0 || p <= 0) {
        estimate->setText(QString("<span style='color:%1'>объём: —</span>").arg(cMuted));
        return;
    }
    QString text = QString("объём ≈ <b>%1 $</b>").arg(q * p, 0, 'f', 2);
    if(leverage->value() > 0)
        text += QString(" · маржа ≈ <b>%1 $</b> при x%2").arg(q * p / leverage->value(), 0, 'f', 2).arg(leverage->value());
    estimate->setText(text);
}

void OrderDialog::SetStatus(const QString& text, const QString& color)
{
    status->setText(QString("<span style='color:%1'>%2</span>").arg(color, text.toHtmlEscaped()));
}

void OrderDialog::Submit()
{
    if(isInFlight) return;
    if(!buyButton->isChecked() && !sellButton->isChecked()) { SetStatus("выбери сторону: Buy или Sell", cBad); return; }
    if(NumValue(qty) <= 0) { SetStatus("количество — число больше нуля", cBad); qty->setFocus(); return; }
    const bool limit = IsLimit();
    if(limit && NumValue(price) <= 0) { SetStatus("для Limit нужна цена — число больше нуля", cBad); price->setFocus(); return; }
    if(NumValue(stopLoss) < 0) { SetStatus("стоп-лосс — цена или пусто", cBad); stopLoss->setFocus(); return; }
    if(NumValue(takeProfit) < 0) { SetStatus("тейк-профит — цена или пусто", cBad); takeProfit->setFocus(); return; }

    const QString side = buyButton->isChecked() ? "Buy" : "Sell";
    QJsonObject body{
        { "symbol", symbol }, { "side", side }, { "order_type", typeBox->currentText() },
        { "qty", NumText(qty) }, { "reduce_only", reduceOnly->isChecked() },
    };
    QStringList lines;
    lines << QString("%1 %2 %3 %4").arg(side, typeBox->currentText(), NumText(qty), symbol);
    if(limit) {
        body.insert("price", NumText(price));
        body.insert("time_in_force", tifBox->currentText());
        lines << QString("цена %1, %2").arg(NumText(price), tifBox->currentText());
    } else {
        lines << QString("по рынку (сейчас ≈ %1)").arg(marketPrice > 0 ? FormatPrice(marketPrice) : QString("—"));
    }
    if(leverage->value() > 0) { body.insert("leverage", leverage->value()); lines << QString("плечо x%1").arg(leverage->value()); }
    if(NumValue(stopLoss) > 0) { body.insert("stop_loss", NumText(stopLoss)); lines << "стоп-лосс " + NumText(stopLoss); }
    else if(!reduceOnly->isChecked()) lines << "БЕЗ стоп-лосса";
    if(NumValue(takeProfit) > 0) { body.insert("take_profit", NumText(takeProfit)); lines << "тейк-профит " + NumText(takeProfit); }
    if(reduceOnly->isChecked()) lines << "reduce-only";

    if(QMessageBox::question(this, "Новый ордер", "Отправить ордер на биржу (аккаунт оператора)?\n\n" + lines.join("\n"),
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    isInFlight = true;
    submitButton->setEnabled(false);
    SetStatus("отправляю…", cMuted);
    const QString what = lines.join(", ");
    api->Post("/api/order", QJsonDocument(body), [this, what](const ApiReply& reply) {
        isInFlight = false;
        submitButton->setEnabled(true);
        if(!reply.ok) {
            SetStatus("ордер не принят: " + reply.error, cBad);
            return;
        }
        const QJsonObject result = reply.json.object();
        LogMsg("ордер отправлен: " + what + " → " + result.value("order_link_id").toString());
        emit Placed(result);
        QStringList warnings;
        for(const QJsonValue& v : result.value("warnings").toArray()) warnings << v.toString();
        if(!warnings.isEmpty())
            QMessageBox::warning(this, "Новый ордер", "Ордер принят, но сервер предупреждает:\n\n• " + warnings.join("\n• "));
        close();
    }, this);
}

void OrderDialog::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Escape) { close(); return; }
    BaseWidget::keyPressEvent(event);
}

} // Trade
