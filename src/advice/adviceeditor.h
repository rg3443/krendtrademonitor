#ifndef KT_ADVICEEDITOR_H
#define KT_ADVICEEDITOR_H

#include "QtInterface/core/basewidget.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace QtInterface {
    class ApiClient;
}

namespace Trade {

    /*!
     *  \brief Окно с текстом одной рекомендации.
     *
     *  Открывается с текстом из GET /api/advice/{name}. «Сохранить» (или Ctrl+S) отправляет
     *  PUT /api/advice/{name} с mtime прочитанной версии (новый файл — POST /api/advice): если файл успели изменить на
     *  сервере (например, /advice из Telegram), сервер ответит 409 и окно предложит
     *  перечитать или перезаписать. Закрытие крестиком НЕ сохраняет; если есть
     *  несохранённые правки — спрашивает, точно ли их выбросить.
     */
    class AdviceEditor : public QtInterface::BaseWidget {
    Q_OBJECT
    public:
        /*!
         *  \param name     имя файла на сервере
         *  \param isNew    файла ещё нет: создать при первом сохранении
         */
        AdviceEditor(QWidget* parent, QtInterface::ApiClient* api, const QString& name, bool isNew);

        QString                 GetName() const;
        //! \brief В окне есть несохранённые правки.
        bool                    IsModified() const;

    signals:
        //! \brief Файл сохранён на сервере (списку пора обновиться).
        void Saved(const QString& name);
        //! \brief Новый файл переименован перед сохранением (сервер не принял прежнее имя).
        void Renamed(const QString& oldName, const QString& newName);

    protected:
        bool                    InitForChild() override;
        bool                    SetupUIForChild(QGridLayout* mainLayout) override;
        void                    closeEvent(QCloseEvent* event) override;

    private:
        void                    Load();
        void                    Save(bool force);
        //! \brief Спросить другое имя для нового файла. \return true — имя сменилось.
        bool                    AskNewName(const QString& reason);
        void                    SetModified(bool modified);
        void                    SetStatus(const QString& text, const QColor& color);

        QtInterface::ApiClient* api;
        QString                 name;
        bool                    isNew;
        bool                    isBusy;
        double                  mtime;          //!< \brief версия, которую правим (0 — неизвестна)
        QPlainTextEdit*         text;
        QPushButton*            saveButton;
        QPushButton*            reloadButton;
        QLabel*                 status;
    };
}

#endif // KT_ADVICEEDITOR_H
