#include "vectorstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace Trade {

namespace {
    bool SetError(QString* error, const QString& text)
    {
        if(error) *error = text;
        return false;
    }

    QJsonObject ToJson(const ChartVector& v)
    {
        return QJsonObject{
            { "id", v.id }, { "symbol", v.symbol },
            { "t1", v.t1 }, { "p1", v.p1 }, { "t2", v.t2 }, { "p2", v.p2 },
            { "color", v.color.name() }, { "width", v.width }, { "created", v.created },
        };
    }

    //! \brief \return false — элемент кривой, пропустить.
    bool FromJson(const QJsonObject& o, ChartVector& v)
    {
        v.id      = o.value("id").toString();
        v.symbol  = o.value("symbol").toString().toUpper();
        v.t1      = o.value("t1").toDouble();
        v.p1      = o.value("p1").toDouble();
        v.t2      = o.value("t2").toDouble();
        v.p2      = o.value("p2").toDouble();
        v.color   = QColor(o.value("color").toString());
        v.width   = o.value("width").toDouble(2);
        v.created = o.value("created").toDouble();
        if(!v.color.isValid()) v.color = QColor("#3d8bfd");
        if(!(v.width > 0) || v.width > 20) v.width = 2;
        return !v.symbol.isEmpty() && v.t1 > 0 && v.t2 > 0 && v.p1 > 0 && v.p2 > 0
               && std::isfinite(v.p1) && std::isfinite(v.p2);
    }
}

VectorStore::VectorStore(const QString& dir_)
    : dir(dir_)
{
}

QString VectorStore::GetDir() const { return dir; }

QString VectorStore::PathOf(const QString& name) const { return QDir(dir).filePath(name + ".json"); }

bool VectorStore::IsValidSetName(const QString& name, QString* why)
{
    static const QRegularExpression re("^[\\w][\\w .-]{0,39}$", QRegularExpression::UseUnicodePropertiesOption);
    if(re.match(name).hasMatch() && !name.endsWith('.') && !name.endsWith(' ')) return true;
    return SetError(why, "1–40 символов: буквы, цифры, пробел, «-», «_», «.»; в начале — буква или цифра");
}

QStringList VectorStore::ListSets() const
{
    QStringList out;
    for(const QFileInfo& f : QDir(dir).entryInfoList({ "*.json" }, QDir::Files, QDir::Name | QDir::IgnoreCase))
        out.append(f.completeBaseName());
    return out;
}

bool VectorStore::HasSet(const QString& name) const
{
    return IsValidSetName(name) && QFileInfo::exists(PathOf(name));
}

bool VectorStore::CreateSet(const QString& name, QString* error)
{
    QString why;
    if(!IsValidSetName(name, &why)) return SetError(error, "имя набора «" + name + "»: " + why);
    if(HasSet(name)) return true;
    if(!QDir().mkpath(dir)) return SetError(error, "не создать папку " + QDir::toNativeSeparators(dir));
    return Save(name, {}, error);
}

QVector<ChartVector> VectorStore::Load(const QString& name, QString* error) const
{
    QVector<ChartVector> out;
    if(!IsValidSetName(name)) { SetError(error, "имя набора «" + name + "» не годится"); return out; }
    QFile file(PathOf(name));
    if(!file.exists()) return out;          // набора ещё нет — пустой
    if(!file.open(QIODevice::ReadOnly)) {
        SetError(error, "не открыть " + QDir::toNativeSeparators(file.fileName()) + ": " + file.errorString());
        return out;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        SetError(error, "набор «" + name + "» испорчен: " + parseError.errorString());
        return out;
    }
    for(const QJsonValue& item : doc.object().value("vectors").toArray()) {
        ChartVector v;
        if(FromJson(item.toObject(), v)) out.append(v);
    }
    return out;
}

bool VectorStore::Append(const QString& name, ChartVector vector, QString* error)
{
    QString loadError;
    QVector<ChartVector> vectors = Load(name, &loadError);
    if(!loadError.isEmpty()) return SetError(error, loadError);     // испорченный файл не перезаписываем
    if(vector.id.isEmpty()) vector.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(vector.created <= 0) vector.created = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    vector.symbol = vector.symbol.toUpper();
    vectors.append(vector);
    if(!QDir().mkpath(dir)) return SetError(error, "не создать папку " + QDir::toNativeSeparators(dir));
    return Save(name, vectors, error);
}

bool VectorStore::Update(const QString& name, const ChartVector& vector, QString* error)
{
    QString loadError;
    QVector<ChartVector> vectors = Load(name, &loadError);
    if(!loadError.isEmpty()) return SetError(error, loadError);
    for(ChartVector& v : vectors) {
        if(v.id != vector.id) continue;
        v = vector;
        return Save(name, vectors, error);
    }
    return SetError(error, "вектора нет в наборе «" + name + "» (удалён?)");
}

bool VectorStore::Remove(const QString& name, const QString& id, QString* error)
{
    QString loadError;
    QVector<ChartVector> vectors = Load(name, &loadError);
    if(!loadError.isEmpty()) return SetError(error, loadError);
    const int before = vectors.size();
    vectors.erase(std::remove_if(vectors.begin(), vectors.end(), [&](const ChartVector& v) { return v.id == id; }),
                  vectors.end());
    return vectors.size() == before || Save(name, vectors, error);
}

bool VectorStore::Save(const QString& name, const QVector<ChartVector>& vectors, QString* error) const
{
    QJsonArray array;
    for(const ChartVector& v : vectors) array.append(ToJson(v));
    QSaveFile file(PathOf(name));
    if(!file.open(QIODevice::WriteOnly))
        return SetError(error, "не записать " + QDir::toNativeSeparators(file.fileName()) + ": " + file.errorString());
    file.write(QJsonDocument(QJsonObject{ { "name", name }, { "vectors", array } }).toJson(QJsonDocument::Indented));
    if(!file.commit())
        return SetError(error, "не записать " + QDir::toNativeSeparators(file.fileName()) + ": " + file.errorString());
    return true;
}

} // Trade
