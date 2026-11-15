#ifndef QTINTERFACE_LOGWIDGET_H
#define QTINTERFACE_LOGWIDGET_H

#include "QtInterface/core/basewidget.h"
#include "QtInterface/core/logger.h"

class QPlainTextEdit;

namespace QtInterface {

    //! \brief Показывает журнал Logger: историю и новые записи. Только чтение.
    class LogWidget : public BaseWidget {
    Q_OBJECT
    public:
        explicit LogWidget(QWidget* parent = nullptr);

        //! \brief Не показывать записи ниже этого уровня (по умолчанию Info).
        void                    SetMinLevel(LogLevel level);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    ApplyStyleForChild(const Theme& theme) override;

    private:
        void                    Append(const LogEntry& entry);
        void                    Reload();

        QPlainTextEdit*         text;
        LogLevel                minLevel;
        QString                 colorWarning;
        QString                 colorError;
        QString                 colorMuted;
    };
}

#endif // QTINTERFACE_LOGWIDGET_H
