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
    static const QRegularExpression re("^[\\w\\-. ]+\\.(md|txt)$", QRegularExpression::UseUnicodePropertiesOption);
    QString reason;
    if(name.contains('/') || name.contains('\\') || name.contains("..")) reason = "без папок и «..»";
    else if(!re.match(name).hasMatch())                                   reason = "нужно имя вида 05-news.md или notes.txt";
    else if(name.compare("README.md", Qt::CaseInsensitive) == 0)          reason = "README.md — служебный, его стратег не читает";
    if(why) *why = reason;
    return reason.isEmpty();
}

} // Trade
