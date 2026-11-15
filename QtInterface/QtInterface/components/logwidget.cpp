#include "logwidget.h"

#include "QtInterface/style/theme.h"

#include <QFontDatabase>
#include <QPlainTextEdit>

namespace QtInterface {

LogWidget::LogWidget(QWidget* parent)
    : BaseWidget(parent)
    , text(nullptr)
    , minLevel(LogLevel::Info)
{
}

bool LogWidget::InitForChild()
{
    text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    text->setMaximumBlockCount(Logger::Instance().GetCapacity());
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(&Logger::Instance(), &Logger::EntryAdded, this, &LogWidget::Append);
    return true;
}

bool LogWidget::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->addWidget(text);
    return true;
}

bool LogWidget::ApplyStyleForChild(const Theme& theme)
{
    colorWarning = theme.GetColors().warning.name();
    colorError   = theme.GetColors().bad.name();
    colorMuted   = theme.GetColors().mutedText.name();
    Reload();
    return true;
}

void LogWidget::SetMinLevel(LogLevel level)
{
    minLevel = level;
    Reload();
}

void LogWidget::Reload()
{
    if(!text) return;
    text->clear();
    for(const LogEntry& entry : Logger::Instance().GetHistory()) Append(entry);
}

void LogWidget::Append(const LogEntry& entry)
{
    if(static_cast<int>(entry.level) < static_cast<int>(minLevel)) return;

    QString color;
    if(entry.level == LogLevel::Warning)    color = colorWarning;
    else if(entry.level == LogLevel::Error) color = colorError;

    const QString time = QString("<span style='color:%1'>%2</span>")
                             .arg(colorMuted, entry.time.toString("HH:mm:ss"));
    QString body = QString("%1 %2: %3").arg(Logger::LevelName(entry.level),
                                            entry.source.toHtmlEscaped(), entry.text.toHtmlEscaped());
    if(!color.isEmpty()) body = QString("<span style='color:%1'>%2</span>").arg(color, body);
    text->appendHtml(time + " " + body);
}

} // QtInterface
