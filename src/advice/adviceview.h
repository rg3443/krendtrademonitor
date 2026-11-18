#ifndef KT_ADVICEVIEW_H
#define KT_ADVICEVIEW_H

#include "QtInterface/core/basewidget.h"

#include <QHash>
#include <QPointer>

class QLabel;
class QPushButton;

namespace QtInterface {
    class ApiClient;
    class TableWidget;
}

namespace Trade {

    class AdviceEditor;
    class AdviceModel;

    /*!
     *  \brief Конфигурация бота: файлы-рекомендации стратегу (advice/ на сервере).
     *  Таблица файлов (GET /api/advice), напротив каждого — «Открыть»: окно AdviceEditor
     *  с текстом и кнопкой «Сохранить»; «Удалить» — удалить файл на сервере
     *  (с подтверждением). «Новый…» создаёт файл.
     *  Все файлы целиком попадают в системный промпт Claude при каждом пересмотре плана.
     */
    class AdviceView : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        AdviceView(QWidget* parent, QtInterface::ApiClient* api);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        bool                    UpdateForChild() override;

    private:
        void                    Request();
        void                    Open(const QString& name, bool isNew);
        void                    CreateNew();
        //! \brief DELETE /api/advice/{name}?mtime=… (mtime из списка; 409 — файл изменили после чтения).
        void                    Delete(const QString& name);

        QtInterface::ApiClient*     api;
        AdviceModel*                model;
        QtInterface::TableWidget*   table;
        QLabel*                     summary;
        QPushButton*                refreshButton;
        QPushButton*                newButton;
        bool                        isRequestInFlight;
        bool                        isFirstData;
        QHash<QString, QPointer<AdviceEditor>> lEditors;    //!< \brief открытые окна по имени файла
    };
}

#endif // KT_ADVICEVIEW_H
