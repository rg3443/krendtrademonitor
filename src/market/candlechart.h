#ifndef KT_CANDLECHART_H
#define KT_CANDLECHART_H

#include "market/candle.h"
#include "market/vectorstore.h"
#include "QtInterface/core/basewidget.h"

#include <QColor>
#include <QPoint>
#include <QRect>

class QComboBox;
class QPushButton;

namespace Trade {

    /*!
     *  \brief Свечной график на QPainter: свечи, ось цены справа, ось времени снизу,
     *  линия текущей цены, горизонтальные уровни (вход, SL, TP, ликвидация) и
     *  отметки сделок. Под курсором — перекрестие и OHLC свечи.
     *
     *  Уровень далеко за пределами свечей не сжимает график: он показывается
     *  подписью со стрелкой у края.
     *
     *  Масштаб: кнопки «−» / «+» в левом верхнем углу области свечей шлют
     *  ZoomRequested — владелец графика меняет окно (сколько свечей) и
     *  сообщает, куда ещё можно, через SetZoomState.
     *
     *  Векторы теханализа: кнопка-линейка справа от масштаба ждёт два клика по
     *  графику — начало и конец (Esc или правый клик — отмена) — и шлёт VectorDrawn
     *  с координатами (время, цена). Вектор рисуется лучом: из первой точки через
     *  вторую до края области свечей, стрелка у края. Справа от линейки — список наборов векторов:
     *  выбор шлёт VectorSetSelected, пункт «Новый набор…» — NewVectorSetRequested.
     *  Что рисовать, задаёт владелец через SetVectors. Цвет и толщина новых векторов —
     *  кнопка-образец и список толщин в той же панели (NewVectorStyleChanged).
     *  Правый клик по вектору — меню: цвет, толщина, удалить (VectorChanged / VectorDeleteRequested).
     *
     *  Карта ликвидаций: «череп» в конце панели — выключатель (LiquidationsToggled). Включён —
     *  красный, у правого края графика черточки уровней из SetLiquidations: длина — объём,
     *  зелёные — лонги, красные — шорты; наведение — подсказка с суммой.
     *  \code
     *  auto* chart = QtInterface::Create<CandleChart>(this);
     *  chart->SetCandles(candles, 60);
     *  chart->SetLivePrice(64396.7, ts);
     *  chart->SetLevels({ { 63815, "SL", Qt::red } });
     *  \endcode
     */
    class CandleChart : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        //! \brief Горизонтальный уровень цены.
        struct Level {
            double  price = 0;
            QString label;
            QColor  color;
            bool    dashed = true;
        };
        //! \brief Оценка ликвидаций на уровне цены (из /api/liqmap, поле levels).
        struct LiqLevel {
            double  price = 0;
            double  longUsd = 0;        //!< \brief лонги: сгорят при падении до этой цены
            double  shortUsd = 0;       //!< \brief шорты: сгорят при росте до этой цены
        };
        //! \brief Отметка сделки: вход (треугольник) или выход (кружок).
        struct Marker {
            double  ts = 0;
            double  price = 0;
            bool    isEntry = true;
            bool    isLong = true;
            QString toolTip;
        };

        explicit CandleChart(QWidget* parent = nullptr);

        //! \brief Новый набор свечей (от старых к новым) и длина свечи в секундах.
        void                    SetCandles(const QVector<Candle>& candles, int intervalSec);
        /*!
         *  \brief Подставить свежие свечи в конец ряда: та же свеча (по времени) — заменяется,
         *  новая — добавляется, самые старые уходят, число свечей не меняется.
         *  \return false — склеить нельзя (дыра в ряду, другой размер свечи, ряда нет): нужен SetCandles
         */
        bool                    MergeCandles(const QVector<Candle>& tail, int intervalSec);
        //! \brief Текущая цена: обновляет последнюю свечу (или начинает новую) и линию цены.
        void                    SetLivePrice(double price, double ts);
        void                    SetLevels(const QVector<Level>& levels);
        void                    SetMarkers(const QVector<Marker>& markers);
        //! \brief Текст поверх графика (ошибка, «загрузка…»); пустой — убрать.
        void                    SetMessage(const QString& message);
        //! \brief Убрать всё (при смене монеты).
        void                    Clear();

        /*!
         *  \brief Состояние кнопок масштаба.
         *  \param canZoomIn   «+» доступна (можно меньше свечей)
         *  \param canZoomOut  «−» доступна (можно больше свечей)
         *  \param info        строка в подсказки кнопок: что сейчас на экране
         */
        void                    SetZoomState(bool canZoomIn, bool canZoomOut, const QString& info);

        //! \brief Векторы на графике (уже отобранные для этой монеты).
        void                    SetVectors(const QVector<ChartVector>& vectors);
        //! \brief Список наборов векторов и выбранный из них.
        void                    SetVectorSets(const QStringList& names, const QString& current);
        //! \brief Бросить начатый вектор и выключить линейку.
        void                    CancelDrawing();
        //! \brief Цвет и толщина, которые показывает панель для новых векторов.
        void                    SetNewVectorStyle(const QColor& color, double width);
        QColor                  GetNewVectorColor() const;
        double                  GetNewVectorWidth() const;

        //! \brief Уровни ликвидаций и шаг между ними ($): рисуются, пока включён «череп».
        void                    SetLiquidations(const QVector<LiqLevel>& levels, double step);
        //! \brief Включить/выключить «череп» (без сигнала LiquidationsToggled).
        void                    SetLiquidationsActive(bool active);
        bool                    IsLiquidationsActive() const;
        //! \brief Строка в подсказку «черепа»: источник, время, ошибка.
        void                    SetLiquidationsInfo(const QString& info);

        const QVector<Candle>&  GetCandles() const;
        //! \brief Область свечей внутри виджета (без осей цены и времени).
        QRect                   GetPlotRect() const;

    signals:
        //! \brief Нажата «+» (zoomIn: меньше свечей, крупнее) или «−» (больше свечей, мельче).
        void ZoomRequested(bool zoomIn);
        //! \brief Нарисован вектор: из (t1, p1) в (t2, p2), время — Unix-секунды.
        void VectorDrawn(double t1, double p1, double t2, double p2);
        //! \brief В списке выбран набор векторов.
        void VectorSetSelected(const QString& name);
        //! \brief В списке выбран «Новый набор…» (список уже вернулся к прежнему набору).
        void NewVectorSetRequested();
        //! \brief В панели выбраны цвет или толщина для новых векторов.
        void NewVectorStyleChanged(const QColor& color, double width);
        //! \brief Из меню вектора изменены цвет или толщина (id тот же).
        void VectorChanged(const ChartVector& vector);
        //! \brief Из меню вектора выбрано «Удалить».
        void VectorDeleteRequested(const QString& id);
        //! \brief «Череп» нажат: показать (true) или убрать карту ликвидаций.
        void LiquidationsToggled(bool active);

    protected:
        bool                    InitForChild() override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;

        bool                    eventFilter(QObject* watched, QEvent* event) override;
        void                    paintEvent(QPaintEvent* event) override;
        void                    mouseMoveEvent(QMouseEvent* event) override;
        void                    mousePressEvent(QMouseEvent* event) override;
        void                    keyPressEvent(QKeyEvent* event) override;
        void                    leaveEvent(QEvent* event) override;
        void                    resizeEvent(QResizeEvent* event) override;
        QSize                   sizeHint() const override;

    private:
        //! \brief Где кончается панель кнопок слева сверху (x, px): подписи там рисуются правее.
        int                     GetToolbarInset() const;
        void                    LayoutToolbar();
        void                    UpdateRulerIcon();
        void                    SetRulerActive(bool active);
        void                    UpdateColorButton();
        //! \brief Вектор под точкой (px), -1 — нет; при нескольких — верхний (нарисован последним).
        int                     VectorAt(const QPoint& pos) const;
        void                    ShowVectorMenu(int index, const QPoint& globalPos);
        //! \brief Черточка ликвидаций под точкой (px), -1 — нет.
        int                     LiquidationAt(const QPoint& pos) const;
        void                    UpdateLiquidationToolTip();     //!< \brief подсказка у черточки под курсором
        void                    UpdateLiquidationButtonTip();
        struct Frame;           //!< \brief Геометрия одного кадра (область, масштабы).
        bool                    MakeFrame(Frame& f) const;
        void                    ApplyLivePriceToLast();
        //! \brief Геометрия черточки уровня ликвидаций; пустой прямоугольник — не рисуется.
        QRectF                  LiquidationRect(const Frame& f, int index) const;
        void                    DrawLiquidations(QPainter& p, const Frame& f) const;
        void                    DrawVectors(QPainter& p, const Frame& f) const;
        void                    DrawArrow(QPainter& p, const QPointF& from, const QPointF& to, const QColor& color,
                                          double width, Qt::PenStyle style = Qt::SolidLine) const;
        void                    DrawGrid(QPainter& p, const Frame& f) const;
        void                    DrawCandles(QPainter& p, const Frame& f) const;
        void                    DrawLevels(QPainter& p, const Frame& f) const;
        void                    DrawMarkers(QPainter& p, const Frame& f) const;
        void                    DrawLivePrice(QPainter& p, const Frame& f) const;
        void                    DrawCrosshair(QPainter& p, const Frame& f) const;
        void                    DrawPriceTag(QPainter& p, const Frame& f, double price, const QColor& bg,
                                             const QString& prefix = QString()) const;

        QVector<Candle>         lCandles;
        QVector<Level>          lLevels;
        QVector<Marker>         lMarkers;
        int                     intervalSec;
        int                     windowSize;         //!< \brief свечей в окне после полной загрузки: ряд не растёт
        double                  livePrice;
        QString                 message;
        QPoint                  mousePos;
        bool                    isMouseInside;
        QPushButton*            zoomOutButton;      //!< \brief «−»: больше свечей
        QPushButton*            zoomInButton;       //!< \brief «+»: меньше свечей
        QPushButton*            rulerButton;        //!< \brief линейка: нарисовать вектор
        QComboBox*              vectorSetBox;       //!< \brief наборы векторов
        QPushButton*            colorButton;        //!< \brief образец цвета новых векторов
        QComboBox*              widthBox;           //!< \brief толщина новых векторов
        QColor                  newVectorColor;
        int                     hoverVector;        //!< \brief вектор под курсором (подсветка), -1 — нет
        QPushButton*            liqButton;          //!< \brief «череп»: карта ликвидаций вкл/выкл
        QVector<LiqLevel>       lLiquidations;
        double                  liqStep;            //!< \brief шаг уровней, $ (толщина черточки)
        double                  liqMaxUsd;          //!< \brief самый крупный уровень: от него длина черточек
        QString                 liqInfo;
        int                     hoverLiquidation;   //!< \brief черточка под курсором, -1 — нет
        QVector<ChartVector>    lVectors;
        bool                    hasVectorStart;     //!< \brief первый клик сделан, ждём второй
        double                  startTs;
        double                  startPrice;

        QColor                  colorUp, colorDown, colorGrid, colorText, colorMuted, colorBg, colorAccent;
    };
}

#endif // KT_CANDLECHART_H
