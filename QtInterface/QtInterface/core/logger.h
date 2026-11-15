#ifndef QTINTERFACE_LOGGER_H
#define QTINTERFACE_LOGGER_H

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

namespace QtInterface {

    //! \brief Уровень записи журнала.
    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    //! \brief Одна запись журнала.
    struct LogEntry {
        QDateTime time;     //!< \brief Когда записано.
        LogLevel  level;    //!< \brief Уровень.
        QString   source;   //!< \brief Кто написал (обычно имя класса).
        QString   text;     //!< \brief Текст.
    };

    /*!
     *  \brief Единый журнал приложения.
     *  Заменяет скрытый QTextEdit в каждом виджете QtShell: все пишут сюда,
     *  а показывает журнал LogWidget (или кто угодно, подписавшись на EntryAdded).
     *  Писать можно из любого потока.
     */
    class Logger : public QObject {
    Q_OBJECT
    public:
        //! \brief Единственный экземпляр.
        static Logger&          Instance();

        //! \brief Записать в журнал (и продублировать в qDebug).
        void                    Log(LogLevel level, const QString& source, const QString& text);

        //! \brief Последние записи (не больше GetCapacity()).
        QVector<LogEntry>       GetHistory() const;

        //! \brief Сколько последних записей хранить в памяти.
        void                    SetCapacity(int capacity);
        int                     GetCapacity() const;

        //! \brief Короткое имя уровня: "DBG", "INF", "WRN", "ERR".
        static QString          LevelName(LogLevel level);

    signals:
        //! \brief Новая запись. Из чужого потока доставляется в поток получателя.
        void EntryAdded(const QtInterface::LogEntry& entry);

    private:
        Logger();

        mutable QMutex      mutex;
        QVector<LogEntry>   lEntries;
        int                 capacity;
    };

    //! \brief Короткие обёртки над Logger::Instance().Log().
    void LogDebug(const QString& source, const QString& text);
    void LogInfo(const QString& source, const QString& text);
    void LogWarning(const QString& source, const QString& text);
    void LogError(const QString& source, const QString& text);
}

Q_DECLARE_METATYPE(QtInterface::LogEntry)

#endif // QTINTERFACE_LOGGER_H
