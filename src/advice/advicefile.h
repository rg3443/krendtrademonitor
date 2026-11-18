#ifndef KT_ADVICEFILE_H
#define KT_ADVICEFILE_H

#include <QJsonDocument>
#include <QString>

#include <vector>

namespace Trade {

    //! \brief Файл-рекомендация из advice/ на сервере (GET /api/advice).
    struct AdviceFile {
        QString name;           //!< \brief Имя файла: 01-general.md
        qint64  size  = 0;      //!< \brief Байт.
        double  mtime = 0;      //!< \brief Изменён, Unix-секунды.

        /*!
         *  \brief Разобрать ответ сервера: [{"name","size","mtime"}] или ["01-general.md", ...].
         *  \param error  сюда — текст ошибки, если формат не тот
         */
        static std::vector<AdviceFile>  ListFromJson(const QJsonDocument& json, QString* error);

        /*!
         *  \brief Подобрать годное имя: кириллица — латиницей, прочие символы — «-», без «.md» — допишет.
         *  «05тест.md» → «05test.md». Если и так ничего не выходит — «advice.md».
         */
        static QString                  SuggestName(const QString& name);

        /*!
         *  \brief Имя годится для сервера — то же правило, что у него (ADVICE_RE в bot/api.py):
         *  латиница, цифры, «.», «-», «_», пробел; начинается с буквы или цифры; *.md / *.txt; не README.md.
         */
        static bool                     IsValidName(const QString& name, QString* why = nullptr);
    };
}

#endif // KT_ADVICEFILE_H
