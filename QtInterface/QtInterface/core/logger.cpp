#include "logger.h"

#include <QDebug>
#include <QMutexLocker>

namespace QtInterface {

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
    : capacity(2000)
{
    qRegisterMetaType<QtInterface::LogEntry>("QtInterface::LogEntry");
}

void Logger::Log(LogLevel level, const QString& source, const QString& text)
{
    LogEntry entry { QDateTime::currentDateTime(), level, source, text };
    {
        QMutexLocker lock(&mutex);
        lEntries.append(entry);
        if(lEntries.size() > capacity)
            lEntries.remove(0, lEntries.size() - capacity);
    }

    const QString line = QString("[%1] %2: %3").arg(LevelName(level), source, text);
    switch(level) {
        case LogLevel::Debug:   qDebug().noquote()    << line; break;
        case LogLevel::Info:    qInfo().noquote()     << line; break;
        case LogLevel::Warning: qWarning().noquote()  << line; break;
        case LogLevel::Error:   qCritical().noquote() << line; break;
    }

    emit EntryAdded(entry);
}

QVector<LogEntry> Logger::GetHistory() const
{
    QMutexLocker lock(&mutex);
    return lEntries;
}

void Logger::SetCapacity(int capacity_)
{
    QMutexLocker lock(&mutex);
    capacity = qMax(1, capacity_);
    if(lEntries.size() > capacity)
        lEntries.remove(0, lEntries.size() - capacity);
}

int Logger::GetCapacity() const
{
    QMutexLocker lock(&mutex);
    return capacity;
}

QString Logger::LevelName(LogLevel level)
{
    switch(level) {
        case LogLevel::Debug:   return "DBG";
        case LogLevel::Info:    return "INF";
        case LogLevel::Warning: return "WRN";
        case LogLevel::Error:   return "ERR";
    }
    return "???";
}

void LogDebug(const QString& source, const QString& text)   { Logger::Instance().Log(LogLevel::Debug, source, text); }
void LogInfo(const QString& source, const QString& text)    { Logger::Instance().Log(LogLevel::Info, source, text); }
void LogWarning(const QString& source, const QString& text) { Logger::Instance().Log(LogLevel::Warning, source, text); }
void LogError(const QString& source, const QString& text)   { Logger::Instance().Log(LogLevel::Error, source, text); }

} // QtInterface
