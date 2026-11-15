#include "candlechart.h"

#include "QtInterface/style/theme.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace QtInterface;

namespace Trade {

namespace {
    const int kAxisRight  = 78;     //!< \brief ширина оси цены
    const int kAxisBottom = 22;     //!< \brief высота оси времени
    const int kPadTop     = 8;
    const int kPadLeft    = 6;

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
    , livePrice(0)
    , isMouseInside(false)
{
}

bool CandleChart::InitForChild()
{
    setMouseTracking(true);
    setMinimumSize(320, 200);
    return true;
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
    update();
    return true;
}

QSize CandleChart::sizeHint() const { return QSize(800, 420); }

void CandleChart::SetCandles(const QVector<Candle>& candles, int intervalSec_)
{
    lCandles = candles;
    intervalSec = intervalSec_ > 0 ? intervalSec_ : 60;
    // живую цену, пришедшую позже последней свечи, не теряем
    if(livePrice > 0 && !lCandles.isEmpty()) {
        Candle& last = lCandles.last();
        last.close = livePrice;
        last.high = std::max(last.high, livePrice);
        last.low = std::min(last.low, livePrice);
    }
    update();
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
            if(lCandles.size() > 2) lCandles.removeFirst();     // окно не растёт
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

void CandleChart::Clear()
{
    lCandles.clear();
    lLevels.clear();
    lMarkers.clear();
    livePrice = 0;
    update();
}

bool CandleChart::MakeFrame(Frame& f) const
{
    if(lCandles.isEmpty()) return false;
    f.plot = QRect(kPadLeft, kPadTop, width() - kPadLeft - kAxisRight, height() - kPadTop - kAxisBottom);
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
        DrawLevels(p, f);
        DrawCandles(p, f);
        DrawMarkers(p, f);
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
            const QRectF box(f.plot.left() + 4, y, w, fm.height());
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
    const QRectF box(f.plot.left() + 4, f.plot.top() + 4, fm.horizontalAdvance(text) + 12, fm.height() + 6);
    QColor bg = colorBg;
    bg.setAlpha(230);
    p.fillRect(box, bg);
    p.setPen(QPen(colorGrid, 1));
    p.drawRect(box);
    p.setPen(hit->close >= hit->open ? colorUp : colorDown);
    p.drawText(box, Qt::AlignCenter, text);
}

void CandleChart::mouseMoveEvent(QMouseEvent* event)
{
    mousePos = event->pos();
    isMouseInside = true;
    update();
    BaseWidget::mouseMoveEvent(event);
}

void CandleChart::leaveEvent(QEvent* event)
{
    isMouseInside = false;
    update();
    BaseWidget::leaveEvent(event);
}

} // Trade
