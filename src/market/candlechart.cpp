#include "candlechart.h"

#include "QtInterface/style/theme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace QtInterface;

namespace Trade {

namespace {
    const int kAxisRight  = 78;     //!< \brief ширина оси цены
    const int kAxisBottom = 22;     //!< \brief высота оси времени
    const int kPadTop     = 8;
    const int kPadLeft    = 6;

    const int kZoomButton    = 30;      //!< \brief сторона кнопки «−» / «+»
    const int kZoomGap       = 4;       //!< \brief отступ кнопок от края области свечей и между ними
    const int kSetBoxWidth   = 150;     //!< \brief ширина списка наборов векторов
    const int kWidthBoxWidth = 64;      //!< \brief ширина списка толщин
    const int kVectorWidths[] = { 1, 2, 3, 4, 6 };     //!< \brief толщины векторов, px
    const char* const kNewSetItem = "\x01new";  //!< \brief data пункта «Новый набор…» (имя файла таким не бывает)
    const double kLiqMaxLength = 0.3;   //!< \brief черточка самого крупного уровня — доля ширины графика
    const double kLiqMinShare  = 0.01;  //!< \brief уровни мельче этой доли от крупнейшего не рисуются
    const double kMinVectorPx = 4;      //!< \brief короче — второй клик считается промахом, не концом вектора

    //! \brief «Красивый» шаг сетки (1, 2, 5 × 10^k), чтобы делений было около target.
    double NiceStep(double span, int target)
    {
        if(span <= 0 || target <= 0) return 1;
        const double raw = span / target;
        const double mag = std::pow(10.0, std::floor(std::log10(raw)));
        const double norm = raw / mag;
        const double nice = norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10;
        return nice * mag;
    }

    QDateTime FromTs(double ts) { return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ts * 1000)); }

    //! \brief $7 600 000 → «$7.6 млн».
    QString FormatUsd(double usd)
    {
        const double a = std::fabs(usd);
        if(a >= 1e9) return QString("$%1 млрд").arg(usd / 1e9, 0, 'f', 2);
        if(a >= 1e6) return QString("$%1 млн").arg(usd / 1e6, 0, 'f', a >= 1e8 ? 0 : 1);
        if(a >= 1e3) return QString("$%1 тыс").arg(usd / 1e3, 0, 'f', 0);
        return QString("$%1").arg(usd, 0, 'f', 0);
    }

    /*!
     *  \brief Конец луча из a через b: где он выходит за rect (не ближе b).
     *  Луч рисуется до края области свечей, а не до точки второго клика.
     */
    QPointF RayEnd(const QPointF& a, const QPointF& b, const QRectF& rect)
    {
        const QPointF d = b - a;
        if(std::hypot(d.x(), d.y()) < 1e-9) return b;
        const double inf = std::numeric_limits<double>::infinity();
        // по каждой оси — при каком t луч доходит до края в сторону движения
        const double tx = d.x() > 0 ? (rect.right() - a.x()) / d.x() : d.x() < 0 ? (rect.left() - a.x()) / d.x() : inf;
        const double ty = d.y() > 0 ? (rect.bottom() - a.y()) / d.y() : d.y() < 0 ? (rect.top() - a.y()) / d.y() : inf;
        return a + d * std::max(1.0, std::min(tx, ty));
    }
}

struct CandleChart::Frame {
    QRect   plot;
    double  tMin = 0, tMax = 1;
    double  pMin = 0, pMax = 1;
    double  slot = 1;           //!< \brief ширина одной свечи в пикселях (с промежутком)

    double X(double ts) const    { return plot.left() + (ts - tMin) / (tMax - tMin) * plot.width(); }
    double Y(double price) const { return plot.bottom() - (price - pMin) / (pMax - pMin) * plot.height(); }
    double PriceAt(double y) const { return pMin + (plot.bottom() - y) / plot.height() * (pMax - pMin); }
    bool   InRange(double price) const { return price >= pMin && price <= pMax; }
};

CandleChart::CandleChart(QWidget* parent)
    : BaseWidget(parent)
    , intervalSec(60)
    , windowSize(0)
    , livePrice(0)
    , isMouseInside(false)
    , zoomOutButton(nullptr)
    , zoomInButton(nullptr)
    , rulerButton(nullptr)
    , vectorSetBox(nullptr)
    , hasVectorStart(false)
    , startTs(0)
    , startPrice(0)
    , colorButton(nullptr)
    , widthBox(nullptr)
    , newVectorColor("#3d8bfd")
    , hoverVector(-1)
    , liqButton(nullptr)
    , liqStep(0)
    , liqMaxUsd(0)
    , hoverLiquidation(-1)
{
}

bool CandleChart::InitForChild()
{
    setMouseTracking(true);
    setMinimumSize(320, 200);

    zoomOutButton = new QPushButton("−", this);
    zoomInButton  = new QPushButton("+", this);
    for(QPushButton* b : { zoomOutButton, zoomInButton }) {
        b->setFixedSize(kZoomButton, kZoomButton);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRepeat(true);             // зажал — масштаб меняется дальше
        b->setAutoRepeatDelay(400);
        b->setAutoRepeatInterval(150);
        b->installEventFilter(this);        // курсор над кнопкой — перекрестие убрать
        b->raise();                         // поверх остальных детей графика
    }
    connect(zoomOutButton, &QPushButton::clicked, this, [this]() { emit ZoomRequested(false); });
    connect(zoomInButton,  &QPushButton::clicked, this, [this]() { emit ZoomRequested(true); });

    rulerButton = new QPushButton(this);
    rulerButton->setFixedSize(kZoomButton, kZoomButton);
    rulerButton->setCursor(Qt::PointingHandCursor);
    rulerButton->setFocusPolicy(Qt::NoFocus);
    rulerButton->setCheckable(true);
    rulerButton->setIconSize(QSize(20, 20));
    rulerButton->setToolTip("Нарисовать вектор: клик — начало, второй клик — конец\n"
                            "Esc или правый клик — отмена");
    rulerButton->installEventFilter(this);
    connect(rulerButton, &QPushButton::toggled, this, &CandleChart::SetRulerActive);

    vectorSetBox = new QComboBox(this);
    vectorSetBox->setFixedSize(kSetBoxWidth, kZoomButton);
    vectorSetBox->setFocusPolicy(Qt::NoFocus);
    vectorSetBox->setToolTip("Набор векторов: что показывать на графике и куда сохранять новые");
    vectorSetBox->installEventFilter(this);
    connect(vectorSetBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString data = vectorSetBox->itemData(index).toString();
        if(data == kNewSetItem) {
            // вернуть список к прежнему набору: новый появится, когда владелец его создаст
            const QSignalBlocker block(vectorSetBox);
            vectorSetBox->setCurrentIndex(vectorSetBox->property("current").toInt());
            emit NewVectorSetRequested();
            return;
        }
        vectorSetBox->setProperty("current", index);
        emit VectorSetSelected(data);
    });

    colorButton = new QPushButton(this);
    colorButton->setFixedSize(kZoomButton, kZoomButton);
    colorButton->setCursor(Qt::PointingHandCursor);
    colorButton->setFocusPolicy(Qt::NoFocus);
    colorButton->setIconSize(QSize(18, 18));
    colorButton->setToolTip("Цвет новых векторов");
    colorButton->installEventFilter(this);
    connect(colorButton, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(newVectorColor, this, "Цвет новых векторов");
        if(!c.isValid()) return;
        SetNewVectorStyle(c, GetNewVectorWidth());
        emit NewVectorStyleChanged(newVectorColor, GetNewVectorWidth());
    });

    widthBox = new QComboBox(this);
    widthBox->setFixedSize(kWidthBoxWidth, kZoomButton);
    widthBox->setFocusPolicy(Qt::NoFocus);
    widthBox->setToolTip("Толщина новых векторов");
    widthBox->installEventFilter(this);
    for(int w : kVectorWidths) widthBox->addItem(QString("%1 px").arg(w), w);
    widthBox->setCurrentIndex(widthBox->findData(2));

    // «череп» — выключатель карты ликвидаций: нажат — красный, на графике черточки
    liqButton = new QPushButton(QString(QChar(0x2620)), this);
    liqButton->setObjectName("liqButton");
    liqButton->setFixedSize(kZoomButton, kZoomButton);
    liqButton->setCursor(Qt::PointingHandCursor);
    liqButton->setFocusPolicy(Qt::NoFocus);
    liqButton->setCheckable(true);
    liqButton->installEventFilter(this);
    connect(liqButton, &QPushButton::toggled, this, [this](bool active) {
        hoverLiquidation = -1;
        UpdateLiquidationButtonTip();
        update();
        emit LiquidationsToggled(active);
    });
    UpdateLiquidationButtonTip();
    connect(widthBox, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        emit NewVectorStyleChanged(newVectorColor, GetNewVectorWidth());
    });

    setFocusPolicy(Qt::ClickFocus);         // Esc отменяет начатый вектор
    UpdateRulerIcon();
    UpdateColorButton();
    return true;
}

void CandleChart::SetNewVectorStyle(const QColor& color, double width)
{
    if(color.isValid()) newVectorColor = color;
    if(widthBox) {
        int best = 0;       // ближайшая толщина из списка
        for(int i = 1; i < widthBox->count(); ++i)
            if(std::fabs(widthBox->itemData(i).toDouble() - width) < std::fabs(widthBox->itemData(best).toDouble() - width))
                best = i;
        widthBox->setCurrentIndex(best);
    }
    UpdateColorButton();
}

QColor CandleChart::GetNewVectorColor() const { return newVectorColor; }

double CandleChart::GetNewVectorWidth() const { return widthBox ? widthBox->currentData().toDouble() : 2; }

void CandleChart::UpdateColorButton()
{
    if(!colorButton) return;
    const qreal dpr = colorButton->devicePixelRatioF();
    QPixmap pix(QSize(18, 18) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(colorText.isValid() ? colorText : QColor(Qt::black), 1));
    p.setBrush(newVectorColor);
    p.drawRoundedRect(QRectF(1.5, 1.5, 15, 15), 3, 3);
    p.end();
    colorButton->setIcon(QIcon(pix));
    colorButton->setToolTip("Цвет новых векторов: " + newVectorColor.name());
}

int CandleChart::VectorAt(const QPoint& pos) const
{
    Frame f;
    if(lVectors.isEmpty() || !MakeFrame(f) || !f.plot.contains(pos)) return -1;
    for(int i = lVectors.size() - 1; i >= 0; --i) {
        const ChartVector& v = lVectors[i];
        const QPointF a(f.X(v.t1), f.Y(v.p1));
        const QPointF b = RayEnd(a, QPointF(f.X(v.t2), f.Y(v.p2)), f.plot);
        // расстояние от точки до отрезка ab (луч, обрезанный краем графика)
        const QPointF ab = b - a, ap = QPointF(pos) - a;
        const double len2 = QPointF::dotProduct(ab, ab);
        const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(ap, ab) / len2, 0.0, 1.0) : 0.0;
        const QPointF d = QPointF(pos) - (a + ab * t);
        if(std::hypot(d.x(), d.y()) <= std::max(5.0, v.width / 2 + 3)) return i;
    }
    return -1;
}

void CandleChart::ShowVectorMenu(int index, const QPoint& globalPos)
{
    if(index < 0 || index >= lVectors.size()) return;
    const ChartVector v = lVectors[index];      // копия: меню модальное, список могут обновить
    QMenu menu(this);
    QAction* colorAction = menu.addAction("Цвет…");
    QMenu* widthMenu = menu.addMenu("Толщина");
    for(int w : kVectorWidths) {
        QAction* a = widthMenu->addAction(QString("%1 px").arg(w));
        a->setCheckable(true);
        a->setChecked(std::fabs(v.width - w) < 0.01);
        a->setData(w);
    }
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("Удалить");

    QAction* chosen = menu.exec(globalPos);
    hoverVector = -1;
    update();
    if(!chosen) return;
    if(chosen == deleteAction) {
        emit VectorDeleteRequested(v.id);
        return;
    }
    ChartVector changed = v;
    if(chosen == colorAction) {
        const QColor c = QColorDialog::getColor(v.color, this, "Цвет вектора");
        if(!c.isValid() || c == v.color) return;
        changed.color = c;
    } else if(chosen->data().isValid()) {
        changed.width = chosen->data().toDouble();
        if(std::fabs(changed.width - v.width) < 0.01) return;
    }
    emit VectorChanged(changed);
}

void CandleChart::resizeEvent(QResizeEvent* event)
{
    BaseWidget::resizeEvent(event);
    LayoutToolbar();
}

void CandleChart::LayoutToolbar()
{
    if(!zoomOutButton) return;
    const QRect plot = GetPlotRect();
    int x = plot.left() + kZoomGap;
    const int y = plot.top() + kZoomGap;
    QWidget* const order[] = { zoomOutButton, zoomInButton, rulerButton, vectorSetBox, colorButton, widthBox,
                               liqButton };
    for(QWidget* w : order) {
        w->move(x, y);
        w->raise();                         // поверх остальных детей графика
        x += w->width() + kZoomGap;
    }
}

int CandleChart::GetToolbarInset() const
{
    return liqButton ? liqButton->geometry().right() + kZoomGap * 2 - GetPlotRect().left() : 8;
}

void CandleChart::UpdateRulerIcon()
{
    if(!rulerButton) return;
    // линейка наискосок: корпус и риски
    const qreal dpr = rulerButton->devicePixelRatioF();
    QPixmap pix(QSize(20, 20) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(10, 10);
    p.rotate(-45);
    const QColor ink = colorText.isValid() ? colorText : QColor(Qt::black);
    p.setPen(QPen(ink, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(-11, -4, 22, 8), 1.5, 1.5);
    for(int i = 0; i < 7; ++i) {
        const double x = -8 + i * 2.7;
        p.drawLine(QPointF(x, -4), QPointF(x, i % 2 == 0 ? -0.5 : -2));
    }
    p.end();
    rulerButton->setIcon(QIcon(pix));
}

void CandleChart::SetRulerActive(bool active)
{
    if(rulerButton && rulerButton->isChecked() != active) {
        const QSignalBlocker block(rulerButton);
        rulerButton->setChecked(active);
    }
    hasVectorStart = false;
    hoverVector = -1;
    setToolTip(QString());
    setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
    if(active) setFocus(Qt::OtherFocusReason);
    update();
}

void CandleChart::CancelDrawing() { SetRulerActive(false); }

void CandleChart::SetVectors(const QVector<ChartVector>& vectors)
{
    lVectors = vectors;
    hoverVector = -1;           // индексы сменились
    update();
}

void CandleChart::SetVectorSets(const QStringList& names, const QString& current)
{
    if(!vectorSetBox) return;
    const QSignalBlocker block(vectorSetBox);
    vectorSetBox->clear();
    for(const QString& name : names) vectorSetBox->addItem(name, name);
    vectorSetBox->insertSeparator(vectorSetBox->count());
    vectorSetBox->addItem("Новый набор…", QString(kNewSetItem));
    const int index = std::max(0, vectorSetBox->findData(current));
    vectorSetBox->setCurrentIndex(index);
    vectorSetBox->setProperty("current", index);
}

void CandleChart::SetZoomState(bool canZoomIn, bool canZoomOut, const QString& info)
{
    if(!zoomOutButton) return;
    // setEnabled/setToolTip — только при изменении: не сбивать нажатие и показанную подсказку
    if(zoomInButton->isEnabled() != canZoomIn) zoomInButton->setEnabled(canZoomIn);
    if(zoomOutButton->isEnabled() != canZoomOut) zoomOutButton->setEnabled(canZoomOut);
    const QString suffix = info.isEmpty() ? QString() : "\n" + info;
    const QString inTip  = (canZoomIn ? "Крупнее: меньше свечей" : "Меньше свечей нельзя") + suffix;
    const QString outTip = (canZoomOut ? "Мельче: больше свечей" : "Больше свечей нельзя") + suffix;
    if(zoomInButton->toolTip() != inTip) zoomInButton->setToolTip(inTip);
    if(zoomOutButton->toolTip() != outTip) zoomOutButton->setToolTip(outTip);
}

bool CandleChart::ApplyStyleForChild(const Theme& theme)
{
    const ThemeColors& c = theme.GetColors();
    colorUp     = c.good;
    colorDown   = c.bad;
    colorGrid   = c.border;
    colorText   = c.text;
    colorMuted  = c.mutedText;
    colorBg     = c.base;
    colorAccent = c.accent;
    // непрозрачные кнопки со своим видом: видно наведение, нажатие и «выключено»
    const QString style = QString(
        "QPushButton { border: 1px solid %1; border-radius: 4px; background: %2; color: %3;"
        " font-size: 18px; font-weight: bold; padding: 0 0 2px 0; }"
        " QPushButton:hover { background: %4; color: %5; border-color: %4; }"
        " QPushButton:pressed { background: %6; color: %5; border-color: %6; }"
        " QPushButton:disabled { background: %2; color: %7; border-color: %7; }"
        " QPushButton:checked { background: %4; border-color: %4; }")
        .arg(c.border.name(), c.base.name(), c.text.name(), c.accent.name(), c.selectionText.name(),
             c.accent.darker(125).name(), c.border.name());
    if(zoomOutButton) {
        zoomOutButton->setStyleSheet(style);
        zoomInButton->setStyleSheet(style);
        rulerButton->setStyleSheet(style);
        colorButton->setStyleSheet(style);
        // «череп» включён — красный, как лампочка
        liqButton->setStyleSheet(style + QString(" QPushButton { font-size: 17px; font-weight: normal; }"
                                                 " QPushButton:checked { background: %1; border-color: %1; color: #ffffff; }"
                                                 " QPushButton:checked:hover { background: %2; border-color: %2; }")
                                             .arg(c.bad.name(), c.bad.lighter(115).name()));
        UpdateRulerIcon();
        UpdateColorButton();
    }
    update();
    return true;
}

bool CandleChart::eventFilter(QObject* watched, QEvent* event)
{
    const bool isToolbar = watched == zoomOutButton || watched == zoomInButton || watched == rulerButton
                           || watched == vectorSetBox || watched == colorButton || watched == widthBox
                           || watched == liqButton;
    if(isToolbar && event->type() == QEvent::Enter && isMouseInside) {
        isMouseInside = false;
        hoverVector = -1;
        update();
    }
    return BaseWidget::eventFilter(watched, event);
}

QSize CandleChart::sizeHint() const { return QSize(800, 420); }

void CandleChart::SetCandles(const QVector<Candle>& candles, int intervalSec_)
{
    lCandles = candles;
    windowSize = lCandles.size();
    intervalSec = intervalSec_ > 0 ? intervalSec_ : 60;
    ApplyLivePriceToLast();
    update();
}

void CandleChart::ApplyLivePriceToLast()
{
    // живую цену, пришедшую позже последней свечи с сервера, не теряем
    if(livePrice <= 0 || lCandles.isEmpty()) return;
    Candle& last = lCandles.last();
    last.close = livePrice;
    last.high = std::max(last.high, livePrice);
    last.low = std::min(last.low, livePrice);
}

bool CandleChart::MergeCandles(const QVector<Candle>& tail, int intervalSec_)
{
    if(lCandles.isEmpty() || tail.isEmpty() || intervalSec_ != intervalSec) return false;
    const double eps = intervalSec * 0.01;      // время свечей с сервера — ровное, но double
    QVector<Candle> merged = lCandles;          // на копии: не склеилось — график как был
    for(const Candle& c : tail) {
        if(c.ts < merged.first().ts - eps) continue;           // старее окна — не нужна
        // с конца: свежие свечи почти всегда последние
        int i = merged.size() - 1;
        while(i >= 0 && merged[i].ts > c.ts + eps) --i;
        if(i >= 0 && std::fabs(merged[i].ts - c.ts) <= eps) merged[i] = c;        // обновилась
        else merged.insert(i + 1, c);                                             // новая (или пропущенная)
    }
    // дыра в ряду (монитор «спал», сеть пропадала) — склеить нельзя, нужна полная загрузка
    const int from = std::max(1, merged.size() - tail.size() - 2);
    for(int i = from; i < merged.size(); ++i)
        if(merged[i].ts - merged[i - 1].ts > intervalSec * 1.5) return false;
    while(windowSize > 0 && merged.size() > windowSize) merged.removeFirst();        // окно не растёт
    lCandles = merged;
    ApplyLivePriceToLast();
    update();
    return true;
}

void CandleChart::SetLivePrice(double price, double ts)
{
    if(price <= 0) return;
    livePrice = price;
    if(!lCandles.isEmpty()) {
        Candle& last = lCandles.last();
        if(ts >= last.ts + intervalSec) {      // началась новая свеча — до следующего опроса рисуем её сами
            Candle c;
            c.ts = last.ts + std::floor((ts - last.ts) / intervalSec) * intervalSec;
            c.open = c.high = c.low = c.close = price;
            lCandles.append(c);
            while(windowSize > 0 && lCandles.size() > windowSize) lCandles.removeFirst();   // окно не растёт
        } else if(ts >= last.ts || ts <= 0) {
            last.close = price;
            last.high = std::max(last.high, price);
            last.low = std::min(last.low, price);
        }
    }
    update();
}

void CandleChart::SetLevels(const QVector<Level>& levels)   { lLevels = levels; update(); }
void CandleChart::SetMarkers(const QVector<Marker>& markers) { lMarkers = markers; update(); }
void CandleChart::SetMessage(const QString& message_)       { message = message_; update(); }
const QVector<Candle>& CandleChart::GetCandles() const      { return lCandles; }

QRect CandleChart::GetPlotRect() const
{
    return QRect(kPadLeft, kPadTop, width() - kPadLeft - kAxisRight, height() - kPadTop - kAxisBottom);
}

void CandleChart::Clear()
{
    lCandles.clear();
    windowSize = 0;
    lLevels.clear();
    lMarkers.clear();
    lLiquidations.clear();      // карта ликвидаций — прежней монеты
    liqMaxUsd = 0;
    hoverLiquidation = -1;
    livePrice = 0;
    CancelDrawing();            // векторы не трогаем: их задаёт владелец через SetVectors
    update();
}

bool CandleChart::MakeFrame(Frame& f) const
{
    if(lCandles.isEmpty()) return false;
    f.plot = GetPlotRect();
    if(f.plot.width() < 40 || f.plot.height() < 40) return false;

    f.tMin = lCandles.first().ts;
    f.tMax = lCandles.last().ts + intervalSec;
    if(f.tMax <= f.tMin) f.tMax = f.tMin + intervalSec;
    f.slot = f.plot.width() / ((f.tMax - f.tMin) / intervalSec);

    double lo = lCandles.first().low, hi = lCandles.first().high;
    for(const Candle& c : lCandles) { lo = std::min(lo, c.low); hi = std::max(hi, c.high); }
    if(livePrice > 0) { lo = std::min(lo, livePrice); hi = std::max(hi, livePrice); }

    // Уровни близко к свечам включаем в масштаб; далёкие (ликвидация) — только подписью у края.
    const double span0 = std::max(hi - lo, hi * 0.0005);
    for(const Level& l : lLevels) {
        if(l.price <= 0) continue;
        if(l.price < lo && lo - l.price <= span0 * 0.6) lo = l.price;
        if(l.price > hi && l.price - hi <= span0 * 0.6) hi = l.price;
    }
    double span = std::max(hi - lo, hi * 0.0005);
    f.pMin = lo - span * 0.06;
    f.pMax = hi + span * 0.06;
    return true;
}

void CandleChart::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), colorBg);
    p.setRenderHint(QPainter::Antialiasing, true);

    Frame f;
    if(MakeFrame(f)) {
        DrawGrid(p, f);
        DrawLiquidations(p, f);     // под свечами и уровнями: это фон-оценка
        DrawLevels(p, f);
        DrawCandles(p, f);
        DrawMarkers(p, f);
        DrawVectors(p, f);
        DrawLivePrice(p, f);
        if(isMouseInside && f.plot.contains(mousePos)) DrawCrosshair(p, f);
    }

    const QString text = !message.isEmpty() ? message : (lCandles.isEmpty() ? QString("нет данных") : QString());
    if(!text.isEmpty()) {
        QFont font = p.font();
        font.setPointSizeF(font.pointSizeF() * 1.2);
        p.setFont(font);
        p.setPen(colorMuted);
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, text);
    }
}

void CandleChart::DrawGrid(QPainter& p, const Frame& f) const
{
    const QFontMetrics fm(p.font());
    p.setPen(QPen(colorGrid, 1));
    p.drawRect(f.plot);

    // цена
    const double step = NiceStep(f.pMax - f.pMin, std::max(2, f.plot.height() / 50));
    for(double v = std::ceil(f.pMin / step) * step; v <= f.pMax; v += step) {
        const double y = f.Y(v);
        p.setPen(QPen(colorGrid, 1, Qt::DotLine));
        p.drawLine(QPointF(f.plot.left(), y), QPointF(f.plot.right(), y));
        p.setPen(colorMuted);
        p.drawText(QRectF(f.plot.right() + 6, y - fm.height() / 2.0, kAxisRight - 8, fm.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, FormatPrice(v));
    }

    // время: шаг из «круглых» значений, около 1 подписи на 90 px
    static const int kSteps[] = { 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400 };
    const double span = f.tMax - f.tMin;
    const int want = std::max(2, f.plot.width() / 90);
    int tStep = kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1];
    for(int s : kSteps) if(span / s <= want) { tStep = s; break; }
    const qint64 offset = FromTs(f.tMin).offsetFromUtc();       // деления по местному времени
    for(double t = std::ceil((f.tMin + offset) / tStep) * tStep - offset; t <= f.tMax; t += tStep) {
        const double x = f.X(t);
        p.setPen(QPen(colorGrid, 1, Qt::DotLine));
        p.drawLine(QPointF(x, f.plot.top()), QPointF(x, f.plot.bottom()));
        const QDateTime dt = FromTs(t);
        const QString label = (dt.time().hour() == 0 && dt.time().minute() == 0 && span > 6 * 3600)
                                  ? dt.toString("dd.MM") : dt.toString("HH:mm");
        p.setPen(colorMuted);
        p.drawText(QRectF(x - 40, f.plot.bottom() + 3, 80, kAxisBottom - 3), Qt::AlignHCenter | Qt::AlignTop, label);
    }
}

void CandleChart::DrawCandles(QPainter& p, const Frame& f) const
{
    const double bodyW = std::max(1.0, f.slot * 0.7);
    p.setRenderHint(QPainter::Antialiasing, false);
    for(const Candle& c : lCandles) {
        const QColor color = c.close >= c.open ? colorUp : colorDown;
        const double xc = f.X(c.ts + intervalSec / 2.0);
        p.setPen(QPen(color, 1));
        p.drawLine(QPointF(xc, f.Y(c.high)), QPointF(xc, f.Y(c.low)));
        const double top = f.Y(std::max(c.open, c.close));
        const double bottom = f.Y(std::min(c.open, c.close));
        const QRectF body(xc - bodyW / 2, top, bodyW, std::max(1.0, bottom - top));
        p.fillRect(body, color);
    }
    p.setRenderHint(QPainter::Antialiasing, true);
}

void CandleChart::DrawLevels(QPainter& p, const Frame& f) const
{
    const QFontMetrics fm(p.font());
    int above = 0, below = 0;       // сколько подписей уже стоит у верхнего/нижнего края
    for(const Level& l : lLevels) {
        if(l.price <= 0) continue;
        const QString text = l.label + " " + FormatPrice(l.price);
        if(f.InRange(l.price)) {
            const double y = f.Y(l.price);
            p.setPen(QPen(l.color, 1, l.dashed ? Qt::DashLine : Qt::SolidLine));
            p.drawLine(QPointF(f.plot.left(), y), QPointF(f.plot.right(), y));
            const int w = fm.horizontalAdvance(text) + 8;
            const QRectF box(f.plot.left() + 4, y - fm.height() - 1, w, fm.height());
            p.fillRect(box, colorBg);
            p.setPen(l.color);
            p.drawText(box, Qt::AlignCenter, text);
        } else {
            const bool up = l.price > f.pMax;
            const QString t = (up ? "↑ " : "↓ ") + text;
            const int w = fm.horizontalAdvance(t) + 8;
            const int row = up ? above++ : below++;
            const double y = up ? f.plot.top() + 2 + row * (fm.height() + 2)
                                : f.plot.bottom() - fm.height() - 2 - row * (fm.height() + 2);
            const double x = f.plot.left() + (up ? GetToolbarInset() : 4);    // сверху слева — панель кнопок
            const QRectF box(x, y, w, fm.height());
            p.fillRect(box, colorBg);
            p.setPen(l.color);
            p.drawText(box, Qt::AlignCenter, t);
        }
    }
}

void CandleChart::DrawMarkers(QPainter& p, const Frame& f) const
{
    const double r = 5;
    for(const Marker& m : lMarkers) {
        if(m.ts < f.tMin || m.ts > f.tMax || !f.InRange(m.price)) continue;
        const QPointF c(f.X(m.ts), f.Y(m.price));
        const QColor color = m.isLong ? colorUp : colorDown;
        p.setPen(QPen(colorText, 1));
        p.setBrush(color);
        if(m.isEntry) {
            QPainterPath path;
            if(m.isLong) { path.moveTo(c.x(), c.y() - r); path.lineTo(c.x() - r, c.y() + r); path.lineTo(c.x() + r, c.y() + r); }
            else         { path.moveTo(c.x(), c.y() + r); path.lineTo(c.x() - r, c.y() - r); path.lineTo(c.x() + r, c.y() - r); }
            path.closeSubpath();
            p.drawPath(path);
        } else {
            p.setBrush(colorBg);
            p.setPen(QPen(color, 2));
            p.drawEllipse(c, r - 1, r - 1);
        }
    }
    p.setBrush(Qt::NoBrush);
}

void CandleChart::DrawLivePrice(QPainter& p, const Frame& f) const
{
    const double price = livePrice > 0 ? livePrice : lCandles.last().close;
    if(!f.InRange(price)) return;
    const bool up = price >= lCandles.last().open;
    const QColor color = up ? colorUp : colorDown;
    const double y = f.Y(price);
    p.setPen(QPen(color, 1, Qt::DotLine));
    p.drawLine(QPointF(f.plot.left(), y), QPointF(f.plot.right(), y));
    DrawPriceTag(p, f, price, color);
}

void CandleChart::DrawPriceTag(QPainter& p, const Frame& f, double price, const QColor& bg, const QString& prefix) const
{
    const QFontMetrics fm(p.font());
    const double y = f.Y(price);
    const QRectF box(f.plot.right() + 1, y - fm.height() / 2.0 - 1, kAxisRight - 2, fm.height() + 2);
    p.fillRect(box, bg);
    p.setPen(Qt::white);
    p.drawText(box.adjusted(4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, prefix + FormatPrice(price));
}

void CandleChart::DrawCrosshair(QPainter& p, const Frame& f) const
{
    p.setPen(QPen(colorMuted, 1, Qt::DashLine));
    p.drawLine(QPointF(f.plot.left(), mousePos.y()), QPointF(f.plot.right(), mousePos.y()));
    p.drawLine(QPointF(mousePos.x(), f.plot.top()), QPointF(mousePos.x(), f.plot.bottom()));
    DrawPriceTag(p, f, f.PriceAt(mousePos.y()), colorMuted);

    // свеча под курсором
    const double ts = f.tMin + (mousePos.x() - f.plot.left()) / double(f.plot.width()) * (f.tMax - f.tMin);
    const Candle* hit = nullptr;
    for(const Candle& c : lCandles)
        if(ts >= c.ts && ts < c.ts + intervalSec) { hit = &c; break; }
    if(!hit) return;

    const double change = hit->open > 0 ? (hit->close - hit->open) / hit->open * 100 : 0;
    const QString text = QString("%1   O %2  H %3  L %4  C %5   %6%7%")
        .arg(FromTs(hit->ts).toString("dd.MM HH:mm"), FormatPrice(hit->open), FormatPrice(hit->high),
             FormatPrice(hit->low), FormatPrice(hit->close), change >= 0 ? "+" : "")
        .arg(change, 0, 'f', 2);
    const QFontMetrics fm(p.font());
    const QRectF box(f.plot.left() + GetToolbarInset(), f.plot.top() + 4, fm.horizontalAdvance(text) + 12, fm.height() + 6);
    QColor bg = colorBg;
    bg.setAlpha(230);
    p.fillRect(box, bg);
    p.setPen(QPen(colorGrid, 1));
    p.drawRect(box);
    p.setPen(hit->close >= hit->open ? colorUp : colorDown);
    p.drawText(box, Qt::AlignCenter, text);
}

void CandleChart::DrawArrow(QPainter& p, const QPointF& from, const QPointF& to, const QColor& color,
                            double width, Qt::PenStyle style) const
{
    const QLineF line(from, to);
    if(line.length() < 1) return;
    p.setPen(QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawLine(line);
    // наконечник: треугольник у конца, размер растёт с толщиной
    const double head = std::min(line.length() * 0.5, 7.0 + width * 2.0);
    const double angle = std::atan2(from.y() - to.y(), from.x() - to.x());
    const double spread = 0.45;     // полуугол раствора, рад
    QPainterPath path;
    path.moveTo(to);
    path.lineTo(to + QPointF(std::cos(angle + spread) * head, std::sin(angle + spread) * head));
    path.lineTo(to + QPointF(std::cos(angle - spread) * head, std::sin(angle - spread) * head));
    path.closeSubpath();
    p.setPen(QPen(color, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    p.drawPath(path);
    p.setBrush(Qt::NoBrush);
}

void CandleChart::DrawVectors(QPainter& p, const Frame& f) const
{
    p.save();
    p.setClipRect(f.plot);          // векторы за краем окна обрезаются, а не лезут на оси
    for(int i = 0; i < lVectors.size(); ++i) {
        const ChartVector& v = lVectors[i];
        const QPointF a(f.X(v.t1), f.Y(v.p1)), b(f.X(v.t2), f.Y(v.p2));
        const QPointF end = RayEnd(a, b, f.plot);       // луч: из a через b до края, стрелка у края
        if(i == hoverVector) {      // под курсором: полупрозрачный ореол и точки кликов — правый клик даст меню
            QColor halo = v.color;
            halo.setAlpha(70);
            DrawArrow(p, a, end, halo, v.width + 6);
        }
        DrawArrow(p, a, end, v.color, v.width);
        if(i == hoverVector) {
            p.setPen(QPen(colorBg, 1));
            p.setBrush(v.color);
            p.drawEllipse(a, v.width + 2.5, v.width + 2.5);
            p.drawEllipse(b, v.width + 2.5, v.width + 2.5);
            p.setBrush(Qt::NoBrush);
        }
    }
    // начатый вектор: пунктирный луч от первой точки через курсор
    if(hasVectorStart) {
        const QPointF start(f.X(startTs), f.Y(startPrice));
        p.setPen(Qt::NoPen);
        p.setBrush(colorAccent);
        p.drawEllipse(start, 3.5, 3.5);
        if(isMouseInside && QLineF(start, mousePos).length() >= kMinVectorPx)
            DrawArrow(p, start, RayEnd(start, mousePos, f.plot), colorAccent, 2, Qt::DashLine);
    }
    p.restore();
}

void CandleChart::mousePressEvent(QMouseEvent* event)
{
    if(!rulerButton || !rulerButton->isChecked()) {
        const int index = event->button() == Qt::RightButton ? VectorAt(event->pos()) : -1;
        if(index >= 0) ShowVectorMenu(index, event->globalPos());
        else BaseWidget::mousePressEvent(event);
        return;
    }
    if(event->button() == Qt::RightButton) { CancelDrawing(); return; }
    if(event->button() != Qt::LeftButton) return;

    Frame f;
    if(!MakeFrame(f) || !f.plot.contains(event->pos())) return;     // клик мимо свечей — ждём дальше
    const double ts = f.tMin + (event->pos().x() - f.plot.left()) / double(f.plot.width()) * (f.tMax - f.tMin);
    const double price = f.PriceAt(event->pos().y());
    if(!hasVectorStart) {
        hasVectorStart = true;
        startTs = ts;
        startPrice = price;
        update();
        return;
    }
    if(QLineF(QPointF(f.X(startTs), f.Y(startPrice)), event->pos()).length() < kMinVectorPx) return;
    const double t1 = startTs, p1 = startPrice;
    SetRulerActive(false);          // один вектор на одно нажатие линейки
    emit VectorDrawn(t1, p1, ts, price);
}

void CandleChart::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_Escape && rulerButton && rulerButton->isChecked()) {
        CancelDrawing();
        return;
    }
    BaseWidget::keyPressEvent(event);
}

void CandleChart::mouseMoveEvent(QMouseEvent* event)
{
    mousePos = event->pos();
    isMouseInside = true;
    if(!rulerButton || !rulerButton->isChecked()) {
        const int hit = VectorAt(mousePos);
        if(hit != hoverVector) {
            hoverVector = hit;
            setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            setToolTip(hit >= 0 ? QString("Правый клик — цвет, толщина, удалить") : QString());
        }
    }
    // черточка ликвидаций под курсором — подсказка сразу, без задержки; вектор важнее
    const int liq = hoverVector < 0 ? LiquidationAt(mousePos) : -1;
    if(liq != hoverLiquidation) {
        hoverLiquidation = liq;
        UpdateLiquidationToolTip();
    }
    update();
    BaseWidget::mouseMoveEvent(event);
}

void CandleChart::leaveEvent(QEvent* event)
{
    isMouseInside = false;
    hoverVector = -1;
    if(hoverLiquidation >= 0) {
        hoverLiquidation = -1;
        QToolTip::hideText();
    }
    update();
    BaseWidget::leaveEvent(event);
}

void CandleChart::SetLiquidations(const QVector<LiqLevel>& levels, double step)
{
    lLiquidations = levels;
    liqStep = step > 0 ? step : 0;
    liqMaxUsd = 0;
    for(const LiqLevel& l : lLiquidations) liqMaxUsd = std::max(liqMaxUsd, l.longUsd + l.shortUsd);
    hoverLiquidation = -1;
    update();
}

void CandleChart::SetLiquidationsActive(bool active)
{
    if(!liqButton || liqButton->isChecked() == active) return;
    const QSignalBlocker block(liqButton);
    liqButton->setChecked(active);
    hoverLiquidation = -1;
    UpdateLiquidationButtonTip();
    update();
}

bool CandleChart::IsLiquidationsActive() const { return liqButton && liqButton->isChecked(); }

void CandleChart::SetLiquidationsInfo(const QString& info)
{
    liqInfo = info;
    UpdateLiquidationButtonTip();
}

QRectF CandleChart::LiquidationRect(const Frame& f, int index) const
{
    const LiqLevel& l = lLiquidations[index];
    const double usd = l.longUsd + l.shortUsd;
    if(liqMaxUsd <= 0 || usd < liqMaxUsd * kLiqMinShare || !f.InRange(l.price)) return QRectF();
    // длина — от объёма (корень: мелкие уровни не пропадают), толщина — шаг уровней на экране
    const double length = std::max(6.0, f.plot.width() * kLiqMaxLength * std::sqrt(usd / liqMaxUsd));
    const double stepPx = liqStep > 0 ? std::fabs(f.Y(l.price) - f.Y(l.price + liqStep)) : 3;
    const double height = std::clamp(stepPx * 0.75, 2.0, 8.0);
    return QRectF(f.plot.right() - length, f.Y(l.price) - height / 2, length, height);
}

void CandleChart::DrawLiquidations(QPainter& p, const Frame& f) const
{
    if(!IsLiquidationsActive() || lLiquidations.isEmpty()) return;
    p.save();
    p.setClipRect(f.plot);
    p.setPen(Qt::NoPen);
    for(int i = 0; i < lLiquidations.size(); ++i) {
        const QRectF r = LiquidationRect(f, i);
        if(r.isEmpty()) continue;
        const LiqLevel& l = lLiquidations[i];
        // зелёные — лонги (сгорят при падении), красные — шорты (при росте); ярче — крупнее
        QColor color = l.longUsd >= l.shortUsd ? colorUp : colorDown;
        const double share = (l.longUsd + l.shortUsd) / liqMaxUsd;
        color.setAlpha(i == hoverLiquidation ? 255 : int(70 + 150 * std::sqrt(share)));
        p.fillRect(r, color);
    }
    p.restore();
}

int CandleChart::LiquidationAt(const QPoint& pos) const
{
    Frame f;
    if(!IsLiquidationsActive() || lLiquidations.isEmpty() || !MakeFrame(f) || !f.plot.contains(pos)) return -1;
    int best = -1;
    double bestDist = 0;
    for(int i = 0; i < lLiquidations.size(); ++i) {
        const QRectF r = LiquidationRect(f, i);
        if(r.isEmpty() || !r.adjusted(0, -3, 0, 3).contains(pos)) continue;
        const double dist = std::fabs(r.center().y() - pos.y());       // соседние черточки близко: ближайшая
        if(best < 0 || dist < bestDist) { best = i; bestDist = dist; }
    }
    return best;
}

void CandleChart::UpdateLiquidationButtonTip()
{
    if(!liqButton) return;
    QString tip = IsLiquidationsActive() ? "Карта ликвидаций включена — нажать, чтобы убрать"
                                         : "Показать карту ликвидаций (оценка)";
    tip += "\nЗелёные черточки — лонги сгорят при падении до цены, красные — шорты при росте.\n"
           "Длина — объём. Наведите на черточку — сколько там ликвидирует.";
    if(!liqInfo.isEmpty()) tip += "\n" + liqInfo;
    if(liqButton->toolTip() != tip) liqButton->setToolTip(tip);
}

void CandleChart::UpdateLiquidationToolTip()
{
    if(hoverLiquidation < 0 || hoverLiquidation >= lLiquidations.size()) {
        QToolTip::hideText();
        return;
    }
    const LiqLevel& l = lLiquidations[hoverLiquidation];
    const double now = livePrice > 0 ? livePrice : (lCandles.isEmpty() ? 0 : lCandles.last().close);
    QString text = QString("<b>%1</b>").arg(FormatPrice(l.price));
    if(now > 0) text += QString(" (%1%2% от цены)").arg(l.price >= now ? "+" : "").arg((l.price - now) / now * 100, 0, 'f', 2);
    if(l.longUsd > 0)  text += "<br>лонги: ≈ <b>" + FormatUsd(l.longUsd) + "</b> — сгорят при падении сюда";
    if(l.shortUsd > 0) text += "<br>шорты: ≈ <b>" + FormatUsd(l.shortUsd) + "</b> — сгорят при росте сюда";
    text += "<br><span style='color:gray'>оценка по открытому интересу OKX, плечи угаданы</span>";
    QToolTip::showText(mapToGlobal(mousePos), text, this);
}

} // Trade
