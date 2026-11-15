#include "basewidget.h"

#include "QtInterface/core/logger.h"
#include "QtInterface/style/theme.h"

namespace QtInterface {

BaseWidget::BaseWidget(QWidget* parent)
    : QWidget(parent)
    , mainLayout(nullptr)
    , isInited(false)
    , isInitInProgress(false)
{
}

BaseWidget::~BaseWidget() = default;

bool BaseWidget::Init()
{
    if(isInited)         return true;
    if(isInitInProgress) return LogErr("Init: повторный вход в Init()");
    isInitInProgress = true;

    bool ok = true;
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    if(!InitForChild()) {
        ok = LogErr("Init: InitForChild не удался");
    } else {
        mainLayout = new QGridLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        if(!SetupUIForChild(mainLayout))
            ok = LogErr("Init: SetupUIForChild не удался");
        else if(!ApplyStyleForChild(Theme::Instance()))
            ok = LogErr("Init: ApplyStyleForChild не удался");
    }

    if(ok) {
        connect(&Theme::Instance(), &Theme::Changed, this, &BaseWidget::OnThemeChanged);
        // подписка на обновления ближайшего BaseWidget-родителя
        for(QWidget* p = parentWidget(); p; p = p->parentWidget()) {
            if(auto* baseParent = qobject_cast<BaseWidget*>(p)) {
                ConnectToUpdates(baseParent);
                break;
            }
        }
    }

    isInitInProgress = false;
    isInited = ok;
    return ok;
}

bool BaseWidget::IsInited() const { return isInited; }

bool BaseWidget::AddWidget(QWidget* widget)
{
    if(!mainLayout) return LogErr("AddWidget: виджет ещё не собран (нет Init)");
    if(!widget)     return LogErr("AddWidget: nullptr");
    mainLayout->addWidget(widget, mainLayout->rowCount(), 0);
    return true;
}

void BaseWidget::ConnectToUpdates(BaseWidget* source)
{
    if(!source || source == this) return;
    connect(source, &BaseWidget::UpdateAll, this, &BaseWidget::OnUpdate, Qt::UniqueConnection);
}

QGridLayout* BaseWidget::GetMainLayout() const { return mainLayout; }

void BaseWidget::OnUpdate()
{
    if(!isInited) return;
    if(!UpdateForChild())
        LogErr("OnUpdate: UpdateForChild не удался");
    emit UpdateAll();
}

bool BaseWidget::LogErr(const QString& err) const
{
    LogError(GetLogSource(), err);
    return false;
}

void BaseWidget::LogMsg(const QString& msg) const
{
    LogInfo(GetLogSource(), msg);
}

QString BaseWidget::GetLogSource() const
{
    QString name = metaObject()->className();
    if(!objectName().isEmpty()) name += "(" + objectName() + ")";
    return name;
}

void BaseWidget::showEvent(QShowEvent* event)
{
    // Виджет показали, а Init() не вызывали: его создали через new, а не через Create<T>().
    Q_ASSERT_X(isInited, metaObject()->className(), "BaseWidget показан без Init(): создавай через QtInterface::Create<T>()");
    if(!isInited) LogErr("показан без Init(): создавай через QtInterface::Create<T>()");
    QWidget::showEvent(event);
}

void BaseWidget::OnThemeChanged()
{
    if(!ApplyStyleForChild(Theme::Instance()))
        LogErr("ApplyStyleForChild не удался при смене темы");
}

} // QtInterface
