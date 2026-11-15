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

using namespace QtInterface;

namespace Trade {

//! \brief Таблица файлов-рекомендаций.
class AdviceModel : public BaseTableModel<AdviceFile, QString> {
public:
    enum Column { ColName, ColSize, ColMtime, ColOpen };

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
    connect(table->GetTable(), &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
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
    lEditors.insert(name, editor);
    editor->show();
}

void AdviceView::CreateNew()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "Новая рекомендация", "Имя файла (.md или .txt):",
                                         QLineEdit::Normal, "05-.md", &ok).trimmed();
    if(!ok || name.isEmpty()) return;
    if(!name.contains('.')) name += ".md";
    QString why;
    if(!AdviceFile::IsValidName(name, &why)) {
        QMessageBox::warning(this, "Новая рекомендация", "Имя «" + name + "» не годится: " + why);
        return;
    }
    if(model->FindRow(name) >= 0) {
        QMessageBox::information(this, "Новая рекомендация", "«" + name + "» уже есть — открываю его.");
        Open(name, false);
        return;
    }
    Open(name, true);
}

} // Trade
