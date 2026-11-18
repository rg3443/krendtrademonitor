#ifndef KT_VECTORSTORE_H
#define KT_VECTORSTORE_H

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Trade {

    //! \brief Вектор теханализа на графике: из точки (t1, p1) в точку (t2, p2).
    struct ChartVector {
        QString id;                 //!< \brief уникален в наборе
        QString symbol;             //!< \brief монета, на графике которой нарисован: BTCUSDT
        double  t1 = 0, p1 = 0;     //!< \brief начало: Unix-секунды, цена
        double  t2 = 0, p2 = 0;     //!< \brief конец (сюда смотрит стрелка)
        QColor  color;
        double  width = 2;          //!< \brief толщина линии, px
        double  created = 0;        //!< \brief когда нарисован, Unix-секунды
    };

    /*!
     *  \brief Наборы векторов на диске: один набор — один JSON-файл
     *  <папка>/<имя набора>.json:
     *  \code
     *  { "name": "основной", "vectors": [ { "id": "…", "symbol": "BTCUSDT",
     *    "t1": 1794708000, "p1": 64100.5, "t2": 1794711600, "p2": 64480,
     *    "color": "#3d8bfd", "width": 2, "created": 1794712000.1 } ] }
     *  \endcode
     *  Запись — через QSaveFile: файл подменяется целиком, оборванная запись его не портит.
     */
    class VectorStore {
    public:
        explicit VectorStore(const QString& dir);

        QString                 GetDir() const;
        //! \brief Имена наборов (файлов *.json), по алфавиту.
        QStringList             ListSets() const;
        bool                    HasSet(const QString& name) const;
        //! \brief Создать пустой набор; уже есть — не ошибка.
        bool                    CreateSet(const QString& name, QString* error);
        QVector<ChartVector>    Load(const QString& name, QString* error) const;
        //! \brief Дописать вектор в набор (id и created проставятся, если пустые).
        bool                    Append(const QString& name, ChartVector vector, QString* error);
        //! \brief Заменить вектор с тем же id (цвет, толщина…). Нет такого — ошибка.
        bool                    Update(const QString& name, const ChartVector& vector, QString* error);
        //! \brief Удалить вектор по id. Нет такого — не ошибка (уже удалён).
        bool                    Remove(const QString& name, const QString& id, QString* error);

        //! \brief Имя годится для файла: 1–40 символов, буквы, цифры, пробел, - _ .
        static bool             IsValidSetName(const QString& name, QString* why = nullptr);

    private:
        QString                 PathOf(const QString& name) const;
        bool                    Save(const QString& name, const QVector<ChartVector>& vectors, QString* error) const;

        QString                 dir;
    };
}

#endif // KT_VECTORSTORE_H
