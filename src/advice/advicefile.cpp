#include "advicefile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace Trade {

std::vector<AdviceFile> AdviceFile::ListFromJson(const QJsonDocument& json, QString* error)
{
    std::vector<AdviceFile> out;
    if(!json.isArray()) {
        if(error) *error = "список рекомендаций: ожидался массив";
        return out;
    }
    for(const QJsonValue& v : json.array()) {
        AdviceFile f;
        if(v.isString()) {
            f.name = v.toString();
        } else {
            const QJsonObject o = v.toObject();
            f.name  = o.value("name").toString();
            f.size  = o.value("size").toVariant().toLongLong();
            f.mtime = o.value("mtime").toDouble();
        }
        if(!f.name.isEmpty()) out.push_back(f);
    }
    return out;
}

bool AdviceFile::IsValidName(const QString& name, QString* why)
{
    // как ADVICE_RE на сервере: иначе он ответит 400 только при сохранении
    static const QRegularExpression re("^[A-Za-z0-9][A-Za-z0-9._ -]{0,80}\\.(md|txt)$");
    QString reason;
    if(name.contains('/') || name.contains('\\') || name.contains("..")) reason = "без папок и «..»";
    else if(!re.match(name).hasMatch())
        reason = "сервер принимает только латиницу, цифры, «.», «-», «_» и пробел, в начале — буква или цифра, "
                 "окончание .md или .txt (например, 05-news.md)";
    else if(name.compare("README.md", Qt::CaseInsensitive) == 0)          reason = "README.md — служебный, его стратег не читает";
    if(why) *why = reason;
    return reason.isEmpty();
}

QString AdviceFile::SuggestName(const QString& name)
{
    static const char* const kTranslit[] = {        // а … я
        "a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p", "r", "s", "t", "u",
        "f", "h", "ts", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya",
    };
    QString base = name.trimmed();
    QString ext = "md";
    const int dot = base.lastIndexOf('.');
    if(dot > 0) {
        const QString e = base.mid(dot + 1).toLower();
        if(e == "md" || e == "txt") { ext = e; base = base.left(dot); }
    }
    QString out;
    for(const QChar ch : base) {
        const ushort u = ch.toLower().unicode();
        if(u == 0x0451) out += "e";                                     // ё
        else if(u >= 0x0430 && u <= 0x044F) {
            QString t = kTranslit[u - 0x0430];
            if(ch.isUpper() && !t.isEmpty()) t[0] = t[0].toUpper();
            out += t;
        }
        else if(ch.unicode() < 128 && (ch.isLetterOrNumber() || ch == '.' || ch == '_' || ch == '-' || ch == ' ')) out += ch;
        else out += '-';
    }
    out.replace(QRegularExpression("\\.{2,}"), ".");                     // «..» сервер не пускает
    out.replace(QRegularExpression("-{2,}"), "-");
    while(!out.isEmpty() && !out.at(0).isLetterOrNumber()) out.remove(0, 1);
    while(!out.isEmpty() && (out.endsWith('-') || out.endsWith('.') || out.endsWith(' '))) out.chop(1);
    out = out.left(80);
    if(out.isEmpty()) out = "advice";
    QString result = out + "." + ext;
    if(!IsValidName(result)) result = "advice." + ext;                  // например, вышло README.md
    return result;
}

} // Trade
