#ifndef KT_CANDLECHART_H
#define KT_CANDLECHART_H

#include "market/candle.h"
#include "QtInterface/core/basewidget.h"

#include <QColor>
#include <QPoint>

namespace Trade {

    /*!
     *  \brief Свечной график на QPainter: свечи, ось цены справа, ось времени снизу,
     *  линия текущей цены, горизонтальные уровни (вход, SL, TP, ликвидация) и
     *  отметки сделок. Под курсором — перекрестие и OHLC свечи.
     *
     *  Уровень далеко за пределами свечей не сжимает график: он показывается
     *  подписью со стрелкой у края.
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
        //! \brief Текущая цена: обновляет последнюю свечу (или начинает новую) и линию цены.
        void                    SetLivePrice(double price, double ts);
        void                    SetLevels(const QVector<Level>& levels);
        void                    SetMarkers(const QVector<Marker>& markers);
        //! \brief Текст поверх графика (ошибка, «загрузка…»); пустой — убрать.
        void                    SetMessage(const QString& message);
        //! \brief Убрать всё (при смене монеты).
        void                    Clear();

        const QVector<Candle>&  GetCandles() const;

    protected:
        bool                    InitForChild() override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;

        void                    paintEvent(QPaintEvent* event) override;
        void                    mouseMoveEvent(QMouseEvent* event) override;
        void                    leaveEvent(QEvent* event) override;
        QSize                   sizeHint() const override;

    private:
        struct Frame;           //!< \brief Геометрия одного кадра (область, масштабы).
        bool                    MakeFrame(Frame& f) const;
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
        double                  livePrice;
        QString                 message;
        QPoint                  mousePos;
        bool                    isMouseInside;

        QColor                  colorUp, colorDown, colorGrid, colorText, colorMuted, colorBg, colorAccent;
    };
}

#endif // KT_CANDLECHART_H
