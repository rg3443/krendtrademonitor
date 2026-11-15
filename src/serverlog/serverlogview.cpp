#include "serverlogview.h"

#include "QtInterface/net/apiclient.h"
#include "QtInterface/net/poller.h"
#include "QtInterface/style/theme.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>

using namespace QtInterface;

namespace Trade {

namespace {
    const int kFirstLines = 500;        //!< \brief сколько строк взять при первом запросе
    const int kMaxBlocks  = 20000;      //!< \brief столько строк держим в окне
    const int kTailKeep   = 50;
}

ServerLogView::ServerLogView(QWidget* parent, ApiClient* api_, Poller* poller_)
    : BaseWidget(parent)
    , api(api_)
    , poller(poller_)
    , isRequestInFlight(false)
    , generation(0)
    , offset(-1)
    , shownLines(0)
    , grep(nullptr)
    , pause(nullptr)
    , clearButton(nullptr)
    , status(nullptr)
    , text(nullptr)
{
}

bool ServerLogView::InitForChild()
{
    if(!api || !poller) return LogErr("InitForChild: нет ApiClient или Poller");

    grep = new QLineEdit(this);
    grep->setPlaceholderText("фильтр по подстроке (на сервере): ERROR, engine, risk…");
    grep->setClearButtonEnabled(true);
    auto* debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(400);
    connect(grep, &QLineEdit::textChanged, debounce, QOverload<>::of(&QTimer::start));
    connect(debounce, &QTimer::timeout, this, [this]() { Reset(); Request(); });

    pause = new QCheckBox("пауза", this);
    pause->setToolTip("Не запрашивать новые строки");
    connect(pause, &QCheckBox::toggled, this, [this](bool paused) { if(!paused) Request(); UpdateStatus(); });

    clearButton = new QPushButton("Перечитать", this);
    clearButton->setToolTip("Очистить окно и заново взять хвост лога");
    connect(clearButton, &QPushButton::clicked, this, [this]() { Reset(); Request(); });

    status = new QLabel(this);

    text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setMaximumBlockCount(kMaxBlocks);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    connect(poller, &Poller::Poll, this, &ServerLogView::Request);
    return true;
}

bool ServerLogView::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout();
    top->addWidget(grep, 1);
    top->addWidget(pause);
    top->addWidget(clearButton);
    top->addWidget(status);
    mainLayout->addLayout(top, 0, 0);
    mainLayout->addWidget(text, 1, 0);
    mainLayout->setRowStretch(1, 1);
    Request();
    return true;
}

bool ServerLogView::ApplyStyleForChild(const Theme& theme)
{
    cError = theme.GetColors().bad.name();
    cWarning = theme.GetColors().warning.name();
    cMuted = theme.GetColors().mutedText.name();
    UpdateStatus();
    return true;
}

bool ServerLogView::UpdateForChild()
{
    Request();
    return true;
}

void ServerLogView::Reset()
{
    ++generation;
    isRequestInFlight = false;
    offset = -1;
    lTail.clear();
    shownLines = 0;
    text->clear();
}

void ServerLogView::Request()
{
    if(isRequestInFlight || pause->isChecked()) return;
    isRequestInFlight = true;
    const int gen = generation;

    QString path = "/api/logs?lines=" + QString::number(offset >= 0 ? 2000 : kFirstLines);
    if(offset >= 0) path += "&after=" + QString::number(offset);
    const QString g = grep->text().trimmed();
    if(!g.isEmpty()) path += "&grep=" + QString(QUrl::toPercentEncoding(g));

    api->Get(path, [this, gen](const ApiReply& reply) {
        if(gen != generation) return;
        isRequestInFlight = false;
        if(!reply.ok) { UpdateStatus(reply.error); return; }

        const QJsonObject o = reply.json.object();
        QStringList lines;
        for(const QJsonValue& v : o.value("lines").toArray()) lines << v.toString();

        if(o.contains("offset")) {                  // сервер с курсором — пришли только новые строки
            const qint64 next = o.value("offset").toVariant().toLongLong();
            if(offset >= 0 && (o.value("rotated").toBool() || next < offset)) {      // лог провернулся (ротация)
                text->appendPlainText("──────── лог начат заново (ротация) ────────");
            }
            offset = next;
            Append(lines);
        } else {
            Append(NewLinesByOverlap(lines));
        }
        UpdateStatus();
    }, this);
}

QStringList ServerLogView::NewLinesByOverlap(const QStringList& lines) const
{
    if(lTail.isEmpty()) return lines;
    // ищем последний кусок уже показанных строк в новом хвосте (с конца)
    const int probe = qMin(qMin(lTail.size(), 5), lines.size());
    if(probe == 0) return QStringList();
    const QStringList last = lTail.mid(lTail.size() - probe);
    for(int i = lines.size() - probe; i >= 0; --i) {
        bool same = true;
        for(int k = 0; k < probe && same; ++k) same = lines[i + k] == last[k];
        if(same) return lines.mid(i + probe);
    }
    return lines;       // нахлёста нет: строк пришло больше, чем окно хвоста, — показываем все
}

void ServerLogView::Append(const QStringList& lines)
{
    if(lines.isEmpty()) return;
    QScrollBar* bar = text->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;
    const int keep = bar->value();

    for(const QString& line : lines) {
        QString color;
        if(line.contains(" ERROR ") || line.contains(" CRITICAL ") || line.startsWith("Traceback")) color = cError;
        else if(line.contains(" WARNING ")) color = cWarning;
        if(color.isEmpty()) text->appendPlainText(line);
        else text->appendHtml(QString("<span style='color:%1'>%2</span>").arg(color, line.toHtmlEscaped()));
    }
    shownLines += lines.size();
    lTail += lines;
    if(lTail.size() > kTailKeep) lTail = lTail.mid(lTail.size() - kTailKeep);

    bar->setValue(atBottom ? bar->maximum() : keep);
}

void ServerLogView::UpdateStatus(const QString& error)
{
    if(!status) return;
    if(!error.isEmpty()) {
        status->setText(QString("<span style='color:%1'>%2</span>").arg(cError, error.toHtmlEscaped()));
        return;
    }
    status->setText(QString("<span style='color:%1'>строк: %2%3 · %4</span>")
                        .arg(cMuted).arg(shownLines)
                        .arg(pause && pause->isChecked() ? " · пауза" : "")
                        .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
}

} // Trade
