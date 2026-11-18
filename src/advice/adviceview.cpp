#include "adviceview.h"

#include "advice/adviceeditor.h"
#include "advice/advicefile.h"
#include "QtInterface/components/tablewidget.h"
#include "QtInterface/delegates/cellcomponents.h"
#include "QtInterface/models/basetablemodel.h"
#include "QtInterface/net/apiclient.h"
#include "QtInterface/style/theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QUrl>

using namespace QtInterface;

namespace Trade {

namespace {
    //! \brief Вопрос с русскими кнопками (переводы Qt не поставляются). \return true — «да».
    bool Ask(QWidget* parent, const QString& title, const QString& text, const QString& yesText, const QString& noText)
    {
        QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
        QPushButton* yes = box.addButton(yesText, QMessageBox::DestructiveRole);
        QPushButton* no = box.addButton(noText, QMessageBox::RejectRole);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        return box.clickedButton() == yes;
    }
}

//! \brief Таблица файлов-рекомендаций.
class AdviceModel : public BaseTableModel<AdviceFile, QString> {
public:
    enum Column { ColName, ColSize, ColMtime, ColOpen, ColDelete };

    explicit AdviceModel(QObject* parent)
        : BaseTableModel<AdviceFile, QString>([](const AdviceFile& f) { return f.name; }, parent)
    {
        AddColumn({ "Файл", [](const AdviceFile& f) { return f.name; } });
        AddColumn({ "Размер", [](const AdviceFile& f) { return f.size; },
                    [](const AdviceFile& f) { return f.size > 0 ? QString::number(f.size) + " Б" : QString("—"); },
                    {}, Qt::AlignRight | Qt::AlignVCenter });
        AddColumn({ "Изменён", [](const AdviceFile& f) { return f.mtime; },
                    [](const AdviceFile& f) {
                        return f.mtime > 0 ? QDateTime::fromMSecsSinceEpoch(qint64(f.mtime * 1000)).toString("dd.MM.yyyy HH:mm")
                                           : QString("—");
                    } });
        AddColumn({ "", [](const AdviceFile&) { return QString("Открыть"); } });
        AddColumn({ "", [](const AdviceFile&) { return QString("Удалить"); } });
    }
};

AdviceView::AdviceView(QWidget* parent, ApiClient* api_)
    : BaseWidget(parent)
    , api(api_)
    , model(nullptr)
    , table(nullptr)
    , summary(nullptr)
    , refreshButton(nullptr)
    , newButton(nullptr)
    , isRequestInFlight(false)
    , isFirstData(true)
{
}

bool AdviceView::InitForChild()
{
    if(!api) return LogErr("InitForChild: нет ApiClient");

    model = new AdviceModel(this);
    table = Create<TableWidget>(this);
    table->SetModel(model);
    table->GetDelegate()->BindColumn(AdviceModel::ColOpen, Cells::Button("Открыть", [this](const QModelIndex& index) {
        Open(index.sibling(index.row(), AdviceModel::ColName).data(Qt::EditRole).toString(), false);
    }));
    table->GetDelegate()->BindColumn(AdviceModel::ColDelete, Cells::Button("Удалить", [this](const QModelIndex& index) {
        Delete(index.sibling(index.row(), AdviceModel::ColName).data(Qt::EditRole).toString());
    }));
    connect(table->GetTable(), &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        if(index.column() == AdviceModel::ColDelete) return;
        Open(index.sibling(index.row(), AdviceModel::ColName).data(Qt::EditRole).toString(), false);
    });

    summary = new QLabel(this);
    summary->setWordWrap(true);

    refreshButton = new QPushButton("Обновить", this);
    connect(refreshButton, &QPushButton::clicked, this, &AdviceView::Request);
    newButton = new QPushButton("Новый…", this);
    newButton->setToolTip("Создать файл-рекомендацию на сервере");
    connect(newButton, &QPushButton::clicked, this, &AdviceView::CreateNew);

    Request();
    return true;
}

bool AdviceView::SetupUIForChild(QGridLayout* mainLayout)
{
    mainLayout->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout();
    top->addWidget(summary, 1);
    top->addWidget(newButton);
    top->addWidget(refreshButton);
    mainLayout->addLayout(top, 0, 0);
    mainLayout->addWidget(table, 1, 0);
    mainLayout->setRowStretch(1, 1);
    return true;
}

bool AdviceView::UpdateForChild()
{
    Request();
    return true;
}

void AdviceView::Request()
{
    if(isRequestInFlight) return;
    isRequestInFlight = true;
    api->Get("/api/advice", [this](const ApiReply& reply) {
        isRequestInFlight = false;
        const ThemeColors& c = Theme::Instance().GetColors();
        if(!reply.ok) {
            summary->setText(QString("<span style='color:%1'>Список рекомендаций не получен: %2</span>")
                                 .arg(c.bad.name(), reply.error.toHtmlEscaped()));
            return;
        }
        QString error;
        std::vector<AdviceFile> files = AdviceFile::ListFromJson(reply.json, &error);
        if(!error.isEmpty()) { summary->setText(error); LogErr(error); return; }
        const int count = static_cast<int>(files.size());
        model->SetRows(std::move(files));
        if(isFirstData && model->rowCount() > 0) {
            table->AlignColumns();
            QHeaderView* header = table->GetTable()->horizontalHeader();
            header->setStretchLastSection(false);
            header->setSectionResizeMode(AdviceModel::ColName, QHeaderView::Stretch);
            isFirstData = false;
        }
        summary->setText(QString("Рекомендаций: <b>%1</b> <span style='color:%2'>— все файлы целиком уходят Claude "
                                 "в каждом пересмотре плана; лимиты риска они не отменяют</span>")
                             .arg(count).arg(c.mutedText.name()));
    }, this);
}

void AdviceView::Open(const QString& name, bool isNew)
{
    if(name.isEmpty()) return;
    if(QPointer<AdviceEditor> e = lEditors.value(name)) {
        e->raise();
        e->activateWindow();
        return;
    }
    auto* editor = Create<AdviceEditor>(this, api, name, isNew);
    connect(editor, &AdviceEditor::Saved, this, &AdviceView::Request);
    connect(editor, &AdviceEditor::Renamed, this, [this, editor](const QString& oldName, const QString& newName) {
        if(lEditors.value(oldName) == editor) lEditors.remove(oldName);
        lEditors.insert(newName, editor);
    });
    lEditors.insert(name, editor);
    editor->show();
}

void AdviceView::CreateNew()
{
    QString name = "05-.md";
    QString label = "Имя файла (.md или .txt, латиницей):";
    for(;;) {       // не годится — спросить снова, подставив латинский вариант («05тест.md» → «05test.md»)
        bool ok = false;
        name = QInputDialog::getText(this, "Новая рекомендация", label, QLineEdit::Normal, name, &ok).trimmed();
        if(!ok || name.isEmpty()) return;
        if(!name.endsWith(".md", Qt::CaseInsensitive) && !name.endsWith(".txt", Qt::CaseInsensitive)) name += ".md";
        QString why;
        if(AdviceFile::IsValidName(name, &why)) break;
        label = "Имя «" + name + "» не годится: " + why + ".\n\nПредлагаю латиницей — поправьте, если нужно:";
        name = AdviceFile::SuggestName(name);
    }
    if(model->FindRow(name) >= 0) {
        QMessageBox::information(this, "Новая рекомендация", "«" + name + "» уже есть — открываю его.");
        Open(name, false);
        return;
    }
    Open(name, true);
}

void AdviceView::Delete(const QString& name)
{
    if(name.isEmpty()) return;
    const AdviceFile* file = model->GetRow(model->FindRow(name));
    if(!file) return;
    const double mtime = file->mtime;      // версия из GET /api/advice: если файл изменили после — сервер ответит 409

    QString text = "Удалить «" + name + "» с сервера?\n\nСтратег перестанет получать эту рекомендацию "
                   "со следующего плана. Отменить удаление нельзя.";
    QPointer<AdviceEditor> editor = lEditors.value(name);
    if(editor && editor->IsModified())
        text += "\n\nОкно с этим файлом открыто — несохранённые правки в нём пропадут.";
    else if(editor)
        text += "\n\nОткрытое окно с этим файлом закроется.";
    if(!Ask(this, "Удалить рекомендацию", text, "Удалить", "Отмена")) return;

    QString path = "/api/advice/" + QString(QUrl::toPercentEncoding(name));
    if(mtime > 0) path += "?mtime=" + QString::number(mtime, 'g', 17);
    api->Delete(path, [this, name](const ApiReply& reply) {
        const ThemeColors& c = Theme::Instance().GetColors();
        if(reply.httpStatus == 409) {
            QMessageBox::warning(this, "Удалить рекомендацию",
                                 "«" + name + "» изменили на сервере после загрузки списка — не удалено.\n"
                                 "Список обновлён: проверьте файл и удалите ещё раз, если нужно.");
            Request();
            return;
        }
        if(!reply.ok && reply.httpStatus != 404) {      // 404 — файла уже нет, цель достигнута
            summary->setText(QString("<span style='color:%1'>«%2» не удалён: %3</span>")
                                 .arg(c.bad.name(), name.toHtmlEscaped(), reply.error.toHtmlEscaped()));
            return;
        }
        LogMsg("рекомендация удалена на сервере: " + name);
        if(QPointer<AdviceEditor> e = lEditors.take(name))
            e->deleteLater();       // мимо closeEvent: про правки уже спросили
        Request();
    }, this);
}

} // Trade
