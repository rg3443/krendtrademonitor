#include "candle.h"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Trade {

QVector<Candle> Candle::ListFromJson(const QJsonArray& array)
{
    QVector<Candle> out;
    out.reserve(array.size());
    for(const QJsonValue& v : array) {
        Candle c;
        if(v.isObject()) {
            const QJsonObject o = v.toObject();
            c.ts     = o.value("ts").toDouble();
            c.open   = o.value("open").toDouble();
            c.high   = o.value("high").toDouble();
            c.low    = o.value("low").toDouble();
            c.close  = o.value("close").toDouble();
            c.volume = o.value("volume").toDouble();
        } else if(v.isArray()) {      // на всякий случай: [ts, o, h, l, c, v]
            const QJsonArray a = v.toArray();
            if(a.size() < 5) continue;
            c.ts = a[0].toDouble(); c.open = a[1].toDouble(); c.high = a[2].toDouble();
            c.low = a[3].toDouble(); c.close = a[4].toDouble(); c.volume = a.size() > 5 ? a[5].toDouble() : 0;
        } else {
            continue;
        }
        if(c.ts <= 0 || c.high <= 0 || c.low <= 0) continue;
        out.append(c);
    }
    std::sort(out.begin(), out.end(), [](const Candle& a, const Candle& b) { return a.ts < b.ts; });
    return out;
}

int IntervalSeconds(const QString& interval)
{
    static const QRegularExpression re("^(\\d+)([smhd])$");
    const QRegularExpressionMatch m = re.match(interval.trimmed().toLower());
    if(!m.hasMatch()) return 0;
    const int n = m.captured(1).toInt();
    switch(m.captured(2).at(0).toLatin1()) {
        case 's': return n;
        case 'm': return n * 60;
        case 'h': return n * 3600;
        case 'd': return n * 86400;
        default:  return 0;
    }
}

QString FormatPrice(double price)
{
    const double a = std::fabs(price);
    int decimals;
    if(a >= 10000)    decimals = 1;
    else if(a >= 100) decimals = 2;
    else if(a >= 1)   decimals = 4;
    else if(a > 0)    decimals = qBound(4, 3 - static_cast<int>(std::floor(std::log10(a))), 10);   // 4 значащие цифры
    else              decimals = 2;
    return QString::number(price, 'f', decimals);
}

QString SymbolDisplayName(const QString& symbol)
{
    QString s = symbol.toUpper();
    for(const char* quote : { "USDT", "USDC", "PERP", "USD" }) {
        if(s.endsWith(quote) && s.size() > static_cast<int>(strlen(quote))) { s.chop(static_cast<int>(strlen(quote))); break; }
    }
    static const QRegularExpression lead("^(1000000|10000|1000)(?=[A-Z])");
    static const QRegularExpression tail("(?<=[A-Z])(1000000|10000|1000)$");
    s.remove(lead);
    s.remove(tail);
    return s.isEmpty() ? symbol : s;
}

} // Trade
