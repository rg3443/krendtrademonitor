#ifndef QTINTERFACE_BASEWIDGET_H
#define QTINTERFACE_BASEWIDGET_H

#include <QGridLayout>
#include <QWidget>

#include <type_traits>
#include <utility>

namespace QtInterface {

    class Theme;

    /*!
     *  \brief Основа для всех виджетов-компонентов (жизненный цикл из QtShell).
     *
     *  Init() по шаблонному методу вызывает:
     *      1) InitForChild()            — создать объекты и связать сигналы;
     *      2) SetupUIForChild(layout)   — разложить их по главному layout;
     *      3) ApplyStyleForChild(theme) — точечные правки стиля (общий стиль ставит Theme).
     *
     *  Init() НЕ вызывается в конструкторе: пока работает конструктор базы, объект
     *  ещё не стал наследником, и виртуальные *ForChild наследников не вызовутся.
     *  Поэтому виджеты создаются только через фабрику Create<T>(...), которая
     *  вызывает Init(), когда объект собран целиком.
     *
     *  Наследник, расширяющий родителя, переопределяет те же методы и при
     *  необходимости вызывает родительскую версию: Parent::SetupUIForChild(layout).
     *
     *  Ошибки: методы возвращают false и пишут в Logger через LogErr(). Исключений нет.
     *  Память: дочерние объекты создаются с родителем, ручных delete нет.
     */
    class BaseWidget : public QWidget {
    Q_OBJECT
    public:
        explicit BaseWidget(QWidget* parent = nullptr);
        ~BaseWidget() override;

        /*!
         *  \brief Собрать виджет: InitForChild → SetupUIForChild → ApplyStyleForChild.
         *  Повторный вызов ничего не делает. Обычно вызывается фабрикой Create<T>().
         *  \return false, если один из шагов не удался (подробности в журнале)
         */
        bool                    Init();

        //! \brief Init() уже прошёл успешно.
        bool                    IsInited() const;

        //! \brief Добавить виджет в главный layout (в следующую строку).
        bool                    AddWidget(QWidget* widget);

        //! \brief Получать UpdateAll от source (по нему вызывается OnUpdate()).
        void                    ConnectToUpdates(BaseWidget* source);

        QGridLayout*            GetMainLayout() const;

    public slots:
        /*!
         *  \brief Обновить виджет: UpdateForChild(), затем UpdateAll для подписчиков.
         *  При Init() виджет сам подписывается на UpdateAll ближайшего BaseWidget-родителя,
         *  так что вызов OnUpdate() у корня обновляет всё дерево.
         */
        void                    OnUpdate();

    signals:
        //! \brief Обновить подписчиков (дочерние BaseWidget).
        void UpdateAll();

    protected:
        //! \brief Наследник создаёт свои объекты и связывает сигналы.
        virtual bool            InitForChild() { return true; }
        //! \brief Наследник раскладывает свои объекты по главному layout.
        virtual bool            SetupUIForChild(QGridLayout* mainLayout) { Q_UNUSED(mainLayout); return true; }
        //! \brief Наследник делает точечные правки стиля. Вызывается при Init и при смене темы.
        virtual bool            ApplyStyleForChild(const Theme& theme) { Q_UNUSED(theme); return true; }
        //! \brief Наследник обновляет свои данные/вид. Вызывается из OnUpdate().
        virtual bool            UpdateForChild() { return true; }

        //! \brief Записать ошибку в журнал от имени класса виджета.
        //! \return всегда false — удобно для `return LogErr("...")`
        bool                    LogErr(const QString& err) const;
        //! \brief Записать информационное сообщение от имени класса виджета.
        void                    LogMsg(const QString& msg) const;

        //! \brief Имя класса для журнала.
        QString                 GetLogSource() const;

        void                    showEvent(QShowEvent* event) override;

    private:
        void                    OnThemeChanged();

        QGridLayout*    mainLayout;
        bool            isInited;
        bool            isInitInProgress;
    };

    /*!
     *  \brief Создать виджет и сразу собрать его (Init()).
     *  Init() вызывается, когда объект собран целиком, поэтому
     *  виртуальные *ForChild работают на любой глубине наследования.
     *  \code
     *  auto* table = QtInterface::Create<TableWidget>(this);
     *  auto* bots  = QtInterface::Create<BotsView>(this, api, poller);
     *  \endcode
     */
    template<class T, class... Args>
    T* Create(Args&&... args) {
        static_assert(std::is_base_of<BaseWidget, T>::value, "Create<T>: T должен наследоваться от BaseWidget");
        T* widget = new T(std::forward<Args>(args)...);
        widget->Init();
        return widget;
    }
}

#endif // QTINTERFACE_BASEWIDGET_H
