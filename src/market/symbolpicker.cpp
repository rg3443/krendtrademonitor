#include "symbolpicker.h"

#include "market/candle.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/style/theme.h"

#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

using namespace QtInterface;

namespace Trade {

namespace {
    const int kColumns   = 8;
    const int kCellSize  = 84;

    void ClearGrid(QGridLayout* grid)
    {
        while(QLayoutItem* item = grid->takeAt(0)) {
            if(QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
}

SymbolPicker::SymbolPicker(QWidget* parent, ApiClient* api_, const QStringList& favorites,
                           const QStringList& fallback, const QString& current_)
    : BaseWidget(parent)
    , api(api_)
    , lFavorites(favorites)
    , current(current_)
    , search(nullptr)
    , status(nullptr)
    , favoritesBox(nullptr)
    , othersBox(nullptr)
    , favoritesGrid(nullptr)
    , othersGrid(nullptr)
{
    for(const QString& s : fallback)
        if(!lFavorites.contains(s)) lOthers << s;
}

bool SymbolPicker::InitForChild()
{
    if(!api) return LogErr("InitForChild: нет ApiClient");
    setWindowFlags(Qt::Dialog);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Токены");

    search = new QLineEdit(this);
    search->setPlaceholderText("поиск: eth, sol, pepe…  (Enter — первая монета)");
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, &SymbolPicker::Rebuild);
    connect(search, &QLineEdit::returnPressed, this, [this]() {
        const QString f = search->text().trimmed().toUpper();
        for(const QString& s : lFavorites + lOthers)
            if(f.isEmpty() || s.contains(f) || SymbolDisplayName(s).contains(f)) { Choose(s); return; }
    });

    status = new QLabel(this);
    favoritesBox = new QWidget(this);
    favoritesGrid = new QGridLayout(favoritesBox);
    favoritesGrid->setContentsMargins(0, 0, 0, 0);
    favoritesGrid->setSpacing(6);
    othersBox = new QWidget(this);
    othersGrid = new QGridLayout(othersBox);
    othersGrid->setContentsMargins(0, 0, 0, 0);
    othersGrid->setSpacing(6);

    Request();
    return true;
}

bool SymbolPicker::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(search, 0, 0);
    mainLayout->addWidget(favoritesBox, 1, 0);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line, 2, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* holder = new QWidget(scroll);
    auto* holderLayout = new QVBoxLayout(holder);
    holderLayout->setContentsMargins(0, 0, 0, 0);
    holderLayout->addWidget(othersBox);
    holderLayout->addStretch(1);
    scroll->setWidget(holder);
    mainLayout->addWidget(scroll, 3, 0);
    mainLayout->setRowStretch(3, 1);
    mainLayout->addWidget(status, 4, 0);

    resize(kColumns * (kCellSize + 6) + 60, 560);
    Rebuild();
    search->setFocus();
    return true;
}

bool SymbolPicker::ApplyStyleForChild(const Theme& theme)
{
    const ThemeColors& c = theme.GetColors();
    const QString base = "QPushButton { min-width: %1px; max-width: %1px; min-height: %1px; max-height: %1px;"
                         " border: 1px solid %2; border-radius: 6px; background: %3; color: %4; font-weight: bold; }"
                         " QPushButton:hover { border: 2px solid %5; }";
    styleNormal   = base.arg(kCellSize).arg(c.border.name(), c.base.name(), c.text.name(), c.accent.name());
    styleFavorite = base.arg(kCellSize).arg(c.accent.name(), c.alternateBase.name(), c.text.name(), c.accent.name())
                    + " QPushButton { font-size: 15px; }";
    styleCurrent  = base.arg(kCellSize).arg(c.selection.name(), c.selection.name(), c.selectionText.name(), c.accent.name());
    Rebuild();
    return true;
}

void SymbolPicker::Request()
{
    status->setText("загружаю список монет…");
    api->Get("/api/symbols?limit=200", [this](const ApiReply& reply) {
        if(!reply.ok) {
            status->setText(QString("<span style='color:%1'>список с сервера не получен (%2) — показан запасной из trademonitor.json</span>")
                                .arg(Theme::Instance().GetColors().warning.name(), reply.error.toHtmlEscaped()));
            return;
        }
        // {"symbols": [{"symbol", "base", "turnover_24h", ...}], "ready": true} (или просто массив)
        const QJsonObject root = reply.json.object();
        const QJsonArray array = reply.json.isArray() ? reply.json.array() : root.value("symbols").toArray();
        if(reply.json.isObject() && !root.value("ready").toBool(true)) {
            status->setText(QString("<span style='color:%1'>сервер ещё собирает список монет (около минуты после старта бота) — "
                                    "показан запасной из trademonitor.json</span>").arg(Theme::Instance().GetColors().warning.name()));
            return;
        }
        struct Item { QString symbol; double turnover; };
        QVector<Item> items;
        for(const QJsonValue& v : array) {
            Item it;
            if(v.isString()) { it.symbol = v.toString(); it.turnover = 0; }
            else {
                const QJsonObject o = v.toObject();
                it.symbol = o.value("symbol").toString();
                it.turnover = o.value("turnover_24h").toDouble();
                const QString base = o.value("base").toString();
                if(!base.isEmpty()) lBaseNames.insert(o.value("symbol").toString().trimmed().toUpper(), base);
            }
            it.symbol = it.symbol.trimmed().toUpper();
            if(!it.symbol.isEmpty()) items.append(it);
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.turnover > b.turnover; });
        lOthers.clear();
        for(const Item& it : items)
            if(!lFavorites.contains(it.symbol) && !lOthers.contains(it.symbol)) lOthers << it.symbol;
        status->setText(QString("монет: %1").arg(lFavorites.size() + lOthers.size()));
        Rebuild();
    }, this);
}

QPushButton* SymbolPicker::MakeButton(const QString& symbol, bool favorite)
{
    auto* b = new QPushButton(lBaseNames.value(symbol, SymbolDisplayName(symbol)));
    b->setToolTip(symbol);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(symbol == current ? styleCurrent : favorite ? styleFavorite : styleNormal);
    connect(b, &QPushButton::clicked, this, [this, symbol]() { Choose(symbol); });
    return b;
}

void SymbolPicker::Rebuild()
{
    if(!favoritesGrid || !othersGrid) return;
    ClearGrid(favoritesGrid);
    ClearGrid(othersGrid);

    const QString f = search ? search->text().trimmed().toUpper() : QString();
    auto match = [this, &f](const QString& s) {
        return f.isEmpty() || s.contains(f) || lBaseNames.value(s, SymbolDisplayName(s)).toUpper().contains(f);
    };

    int n = 0;
    for(const QString& s : lFavorites) {
        if(!match(s)) continue;
        favoritesGrid->addWidget(MakeButton(s, true), n / kColumns, n % kColumns);
        ++n;
    }
    favoritesGrid->setColumnStretch(kColumns, 1);
    favoritesBox->setVisible(n > 0);

    n = 0;
    for(const QString& s : lOthers) {
        if(!match(s)) continue;
        othersGrid->addWidget(MakeButton(s, false), n / kColumns, n % kColumns);
        ++n;
    }
    othersGrid->setColumnStretch(kColumns, 1);
}

void SymbolPicker::Choose(const QString& symbol)
{
    emit Chosen(symbol);
    close();
}

void SymbolPicker::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Escape) { close(); return; }
    BaseWidget::keyPressEvent(event);
}

} // Trade
