#include "adviceeditor.h"

#include "QtInterface/net/apiclient.h"
#include "QtInterface/style/theme.h"

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>

using namespace QtInterface;

namespace Trade {

namespace {
    //! \brief Вопрос с русскими кнопками (переводы Qt не поставляются). \return true — «да».
    bool Ask(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
             const QString& yesText, const QString& noText)
    {
        QMessageBox box(icon, title, text, QMessageBox::NoButton, parent);
        QPushButton* yes = box.addButton(yesText, QMessageBox::AcceptRole);
        QPushButton* no = box.addButton(noText, QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        return box.clickedButton() == yes;
    }
}

AdviceEditor::AdviceEditor(QWidget* parent, ApiClient* api_, const QString& name_, bool isNew_)
    : BaseWidget(parent)
    , api(api_)
    , name(name_)
    , isNew(isNew_)
    , isBusy(false)
    , mtime(0)
    , text(nullptr)
    , saveButton(nullptr)
    , reloadButton(nullptr)
    , status(nullptr)
{
}

QString AdviceEditor::GetName() const { return name; }

bool AdviceEditor::InitForChild()
{
    if(!api) return LogErr("InitForChild: нет ApiClient");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(name);

    text = new QPlainTextEdit(this);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setTabStopDistance(4 * text->fontMetrics().horizontalAdvance(' '));
    connect(text->document(), &QTextDocument::modificationChanged, this, &AdviceEditor::SetModified);

    saveButton = new QPushButton("Сохранить", this);
    saveButton->setToolTip("Записать файл на сервер (Ctrl+S). Стратег прочитает его при следующем плане.");
    connect(saveButton, &QPushButton::clicked, this, [this]() { Save(false); });

    reloadButton = new QPushButton("Перечитать", this);
    reloadButton->setToolTip("Взять текст с сервера заново (правки в окне пропадут)");
    connect(reloadButton, &QPushButton::clicked, this, [this]() {
        if(text->document()->isModified() &&
           !Ask(this, QMessageBox::Question, "Перечитать", "Выбросить правки в окне и взять текст с сервера?",
                "Перечитать", "Отмена"))
            return;
        Load();
    });
    reloadButton->setEnabled(!isNew);

    auto* saveAction = new QAction(this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() { Save(false); });
    addAction(saveAction);

    status = new QLabel(this);

    if(isNew) {
        SetStatus("новый файл — появится на сервере после «Сохранить»", Theme::Instance().GetColors().warning);
        SetModified(true);
    } else {
        Load();
    }
    return true;
}

bool AdviceEditor::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->addWidget(text, 0, 0);
    auto* bottom = new QHBoxLayout();
    bottom->addWidget(status, 1);
    bottom->addWidget(reloadButton);
    bottom->addWidget(saveButton);
    mainLayout->addLayout(bottom, 1, 0);
    mainLayout->setRowStretch(0, 1);
    resize(820, 640);
    return true;
}

void AdviceEditor::Load()
{
    isBusy = true;
    text->setReadOnly(true);
    saveButton->setEnabled(false);
    SetStatus("загружаю…", Theme::Instance().GetColors().mutedText);

    api->Get("/api/advice/" + QString(QUrl::toPercentEncoding(name)), [this](const ApiReply& reply) {
        isBusy = false;
        text->setReadOnly(false);
        if(!reply.ok) {
            SetStatus("не загрузить: " + reply.error, Theme::Instance().GetColors().bad);
            return;
        }
        QString body;
        if(reply.json.isObject()) {
            const QJsonObject o = reply.json.object();
            body = o.value("text").toString();
            mtime = o.value("mtime").toDouble();
        } else {
            body = QString::fromUtf8(reply.body);       // сервер отдал файл как есть
            mtime = 0;
        }
        text->setPlainText(body);
        text->document()->setModified(false);
        SetModified(false);
        SetStatus(mtime > 0 ? "изменён на сервере " + QDateTime::fromMSecsSinceEpoch(qint64(mtime * 1000)).toString("dd.MM.yyyy HH:mm:ss")
                            : QString("загружено"),
                  Theme::Instance().GetColors().mutedText);
    }, this);
}

void AdviceEditor::Save(bool force)
{
    if(isBusy) return;
    isBusy = true;
    saveButton->setEnabled(false);
    SetStatus("сохраняю…", Theme::Instance().GetColors().mutedText);

    if(force) {
        // перезапись поверх чужой правки: взять текущий mtime с сервера и сохранить с ним
        api->Get("/api/advice/" + QString(QUrl::toPercentEncoding(name)), [this](const ApiReply& reply) {
            isBusy = false;
            if(!reply.ok) { SetModified(true); SetStatus("не сохранено: " + reply.error, Theme::Instance().GetColors().bad); return; }
            mtime = reply.json.object().value("mtime").toDouble();
            Save(false);
        }, this);
        return;
    }

    const QString path = "/api/advice/" + QString(QUrl::toPercentEncoding(name));
    auto onReply = [this](const ApiReply& reply) {
        isBusy = false;
        const ThemeColors& c = Theme::Instance().GetColors();
        if(reply.httpStatus == 409 && isNew) {
            SetModified(true);
            SetStatus("не сохранено: файл «" + name + "» уже есть на сервере", c.warning);
            return;
        }
        if(reply.httpStatus == 409) {
            SetModified(true);
            const bool overwrite = Ask(this, QMessageBox::Warning, "Файл изменился",
                "«" + name + "» изменили на сервере после того, как он был открыт.\n\n"
                "Перезаписать его текстом из этого окна?", "Перезаписать", "Не сохранять");
            if(overwrite) Save(true);
            else SetStatus("не сохранено: файл изменился на сервере (можно «Перечитать»)", c.warning);
            return;
        }
        if(!reply.ok) {
            SetModified(true);
            SetStatus("не сохранено: " + reply.error, c.bad);
            return;
        }
        const double newMtime = reply.json.object().value("mtime").toDouble();
        mtime = newMtime > 0 ? newMtime : 0;
        isNew = false;
        reloadButton->setEnabled(true);
        text->document()->setModified(false);
        SetModified(false);
        SetStatus("сохранено " + QDateTime::currentDateTime().toString("HH:mm:ss"), c.good);
        LogMsg("рекомендация сохранена на сервере: " + name);
        emit Saved(name);
    };

    if(isNew)   // новый файл — POST /api/advice (409, если такой уже есть)
        api->Post("/api/advice", QJsonDocument(QJsonObject{ { "name", name }, { "text", text->toPlainText() } }), onReply, this);
    else        // правка — PUT с mtime прочитанной версии (409, если файл изменили после чтения)
        api->Put(path, QJsonDocument(QJsonObject{ { "text", text->toPlainText() }, { "mtime", mtime } }), onReply, this);
}

void AdviceEditor::SetModified(bool modified)
{
    saveButton->setEnabled(modified && !isBusy);
    setWindowTitle((modified ? "● " : "") + name + " — рекомендация");
}

void AdviceEditor::SetStatus(const QString& message, const QColor& color)
{
    status->setText(QString("<span style='color:%1'>%2</span>").arg(color.name(), message.toHtmlEscaped()));
}

void AdviceEditor::closeEvent(QCloseEvent* event)
{
    // крестик не сохраняет — только переспрашивает, если есть правки
    if(text && (text->document()->isModified() || (isNew && !text->toPlainText().isEmpty()))) {
        if(!Ask(this, QMessageBox::Question, "Несохранённые правки",
                "Закрыть «" + name + "» без сохранения? Правки пропадут.", "Закрыть без сохранения", "Остаться")) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // Trade
