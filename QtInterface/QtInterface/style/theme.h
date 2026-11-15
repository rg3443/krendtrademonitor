#ifndef QTINTERFACE_THEME_H
#define QTINTERFACE_THEME_H

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QApplication;

namespace QtInterface {

    //! \brief Цвета темы. По умолчанию — стиль ZIS из QtShell.
    struct ThemeColors {
        QColor background          = QColor("#e0e1e1"); //!< \brief Фон окон и панелей.
        QColor base                = QColor("#f4f5f5"); //!< \brief Фон таблиц и полей ввода.
        QColor alternateBase       = QColor("#e9eaea"); //!< \brief Фон чередующихся строк.
        QColor text                = QColor("#1f2626"); //!< \brief Основной текст.
        QColor mutedText           = QColor("#6b7676"); //!< \brief Второстепенный текст.
        QColor accent              = QColor("#ffa800"); //!< \brief Акцент (вкладки, прогресс).
        QColor selection           = QColor("#495454"); //!< \brief Фон выделения.
        QColor selectionText       = QColor("#ffffff"); //!< \brief Текст выделения.
        QColor border              = QColor("#b9bdbd"); //!< \brief Рамки.
        QColor good                = QColor("#2e8b57"); //!< \brief «Всё хорошо».
        QColor warning             = QColor("#d98e00"); //!< \brief «Внимание».
        QColor bad                 = QColor("#c0392b"); //!< \brief «Ошибка».
    };

    /*!
     *  \brief Тема приложения.
     *  В отличие от WidgetStyle из QtShell, стиль ставится один раз на всё приложение
     *  (палитра + таблица стилей). Виджеты получают тему в ApplyStyleForChild()
     *  только для точечных правок, а при смене темы — заново (сигнал Changed).
     */
    class Theme : public QObject {
    Q_OBJECT
    public:
        //! \brief Единственный экземпляр.
        static Theme&           Instance();

        //! \brief Поставить тему на приложение (стиль Fusion + палитра + QSS).
        void                    ApplyToApp(QApplication& app);

        //! \brief Сменить цвета; если тема уже применена — применить заново и оповестить виджеты.
        void                    SetColors(const ThemeColors& colors);
        const ThemeColors&      GetColors() const;

        //! \brief Палитра из текущих цветов.
        QPalette                GetPalette() const;

        //! \brief Таблица стилей приложения из текущих цветов.
        QString                 GetStyleSheet() const;

    signals:
        //! \brief Тема изменилась — виджетам пора переприменить свои правки.
        void Changed();

    private:
        Theme();

        ThemeColors     colors;
        QApplication*   app;
    };
}

#endif // QTINTERFACE_THEME_H
