#ifndef KT_SYMBOLPICKER_H
#define KT_SYMBOLPICKER_H

#include "QtInterface/core/basewidget.h"

#include <QHash>
#include <QStringList>

class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace QtInterface {
    class ApiClient;
}

namespace Trade {

    /*!
     *  \brief Окно «Токены»: квадратная сетка кнопок с именами монет.
     *  Сверху — избранные (BTC, ETH, PEPE…), ниже — остальные по обороту.
     *  Список берётся из GET /api/symbols; если сервер его не знает — из запасного
     *  списка в trademonitor.json. Строка поиска фильтрует кнопки, Enter выбирает первую.
     *  По нажатию кнопки шлёт Chosen(symbol) и закрывается.
     *  \code
     *  auto* picker = QtInterface::Create<SymbolPicker>(this, api, favorites, fallback, current);
     *  connect(picker, &SymbolPicker::Chosen, this, &MarketView::SetSymbol);
     *  picker->show();
     *  \endcode
     */
    class SymbolPicker : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        SymbolPicker(QWidget* parent, QtInterface::ApiClient* api, const QStringList& favorites,
                     const QStringList& fallback, const QString& current);

    signals:
        void Chosen(const QString& symbol);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    ApplyStyleForChild(const QtInterface::Theme& theme) override;
        void                    keyPressEvent(QKeyEvent* event) override;

    private:
        void                    Request();
        void                    Rebuild();
        QPushButton*            MakeButton(const QString& symbol, bool favorite);
        void                    Choose(const QString& symbol);

        QtInterface::ApiClient* api;
        QStringList             lFavorites;
        QStringList             lOthers;
        QHash<QString, QString> lBaseNames;     //!< \brief символ → имя монеты от сервера (base)
        QString                 current;
        QLineEdit*              search;
        QLabel*                 status;
        QWidget*                favoritesBox;
        QWidget*                othersBox;
        QGridLayout*            favoritesGrid;
        QGridLayout*            othersGrid;
        QString                 styleFavorite, styleNormal, styleCurrent;
    };
}

#endif // KT_SYMBOLPICKER_H
