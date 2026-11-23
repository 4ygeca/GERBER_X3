/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2026                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#pragma once

#include "gc_joborder.h"
#include "plugintypes.h" // Side
#include "tool.h"

#include <QString>
#include <QStringList>
#include <optional>
#include <set>
#include <vector>

class QWidget;

namespace GCode {

class File;

// Задание: УП, которые пользователь выбрал в дереве (например, до и после
// металлизации отверстий), одним станочным файлом одной стороны -- со
// сменами инструмента, номерами T и таблицей инструментов для отправителя.
// GGEasy решает, ЧТО и ЧЕМ. Как вести длины, выбирают в окне задания
// (JobLengths): по умолчанию касание и длины -- дело отправителя и
// контроллера ($341), в файле их нет; либо длины пишет само задание -- из
// таблицы инструментов контроллера (G43 H) и касанием щупа в X0 Y0.

// Шаг задания -- одна УП.
struct JobStep {
    int32_t id{-1};      // УП в проекте: к ней обращаются по id, объект за время диалога могли заменить
    QString identity;    // Tool::identityKey() -- «тот же физический инструмент»
    QString code;        // код инструмента для УП (jobToolCode): в таблице и в сообщении
    QString toolName;    // для людей: в диалоге (в сообщение не идёт -- там только ASCII)
    QString programName; // имя УП в дереве
    Tool::Type toolType{Tool::EndMill};
    double diameter{};
    Tool::Operation operation{Tool::Operation::Milling};
    int spindleSpeed{}; // у лазера -- мощность
    double feed{};      // подача шага: у сверловки -- врезания, иначе -- фрезеровки
    int hits{};         // отверстий сверловки, со всеми тайлами
    bool laser{};
    bool cutout{};     // вырезка заготовки -- последней
    bool autoCutout{}; // вырезку узнали сами (а не отметил пользователь)
    int treeIndex{};   // место в дереве -- основа порядка по умолчанию
};

// Кто ведёт длины инструментов задания.
enum class JobLengths {
    // Не задание: смену с касанием ведёт отправитель или контроллер (grblHAL
    // $341 = 1), в файле длин и касаний нет -- как было всегда.
    Sender,
    // Таблица инструментов контроллера и щуп (grblHAL $341 = 0, M6 -- пауза,
    // ячеек N_TOOLS): инструмент «из таблицы» -- G43 H<n> длиной, измеренной
    // заранее файлом замера; «щуп» -- касание в X0 Y0 в самой смене. Только
    // у поста, который это умеет (Post::toolLengths).
    Controller,
};

// Наибольший номер T в задании: 0 -- без предела (по умолчанию). Предел нужен
// лишь тогда, когда таблицу инструментов держит сам контроллер (grblHAL
// N_TOOLS = 8, T9 -- ошибка); отправителю с библиотекой инструментов (плагин
// ncSender) он ни к чему. Настройка окна задания, запоминается.
int jobMaxToolNumber();
void setJobMaxToolNumber(int maxToolNumber);
class Job {
public:
    // Задание из УП ids (в порядке дерева). УП без траекторий не входят --
    // их имена в skipped. Задания нет -- причина в error: ничего не выбрано,
    // разные стороны, лазер вместе со шпинделем, сохранять нечего.
    static std::optional<Job> fromPrograms(const std::vector<int32_t>& ids, QString& error);

    // Шаги -- в порядке исполнения. Правит его диалог задания.
    std::vector<JobStep> steps;
    QStringList skipped;
    Side side{Top};
    bool laser{};
    // Предел номеров T (jobMaxToolNumber, правится в окне задания); 0 --
    // предела нет. У лазера номера станку не уходят -- предела нет. При
    // таблице контроллера это и число её ячеек (N_TOOLS): лишние ячейки файл
    // замера обнуляет.
    int maxToolNumber{};

    // Длины (окно задания, запоминаются -- loadLengthSettings). Действуют
    // только при Controller и не у лазера.
    JobLengths lengths{JobLengths::Sender};
    // Замер Z0 щупом в X0 Y0 в начале, первым инструментом «из таблицы». У
    // первого инструмента «щуп» его касание и есть замер Z0.
    bool zeroProbe{true};
    // Рядом с заданием -- файл замера таблицы инструментов (toolTableText).
    bool toolTableFile{};
    // Инструменты (identity) «из таблицы» -- G43 H<n>; прочие -- «щуп».
    std::set<QString> tableTools;

    bool controllerLengths() const { return lengths == JobLengths::Controller && !laser; }
    // Есть ли в задании инструмент «из таблицы».
    bool usesTable() const;
    // Почему такие длины писать нельзя (пусто -- можно): Z0 от касания
    // первого инструмента «щуп» при инструментах «из таблицы»; ячейка, общая
    // у инструмента «из таблицы» с другим (номера пошли по кругу).
    QStringList lengthProblems() const;
    // Настройки длин -- из последнего задания (QSettings) и обратно.
    void loadLengthSettings();
    void saveLengthSettings() const;

    // Порядок по умолчанию (jobDefaultOrder) -- с текущими отметками вырезки,
    // от порядка дерева: ручные перестановки диалога основой не служат.
    void applyDefaultOrder();
    // Номера T и смены по текущему порядку.
    JobPlan plan() const;

    // Имя файла по умолчанию: общая группа УП, иначе имя проекта, и сторона.
    QString defaultFileName() const;
    // Текст файла задания name (имя -- в его сведения) по текущему посту.
    // Пересобирает тексты всех УП -- у больших это секунды.
    std::vector<QString> text(const QString& name) const;
    // Файл замера таблицы инструментов задания name (post.toolTable): каждый
    // инструмент «из таблицы» -- в свою ячейку, прочие ячейки до
    // maxToolNumber -- обнулить. Пусто -- писать нечего.
    std::vector<QString> toolTableText(const QString& name) const;
};

// «Сохранить как задание...»: проверка выбора, булавки, диалог шагов, имя
// файла и запись. owner -- главное окно, владелец всех диалогов (диалог без
// владельца на Windows отдаёт при закрытии активность чужому окну).
void saveJob(const std::vector<int32_t>& ids, QWidget* owner);

// Вырезка заготовки: профиль снаружи (Outer) по контуру платы -- слою,
// который пользователь взял за контур (Project::boardOutlineFiles), или
// элементам, которые файл сам узнаёт контуром (Edge.Cuts, X2 Profile).
bool isCutoutProgram(const File* file);
// Место части среди УП одного инструмента: сверловка 0, фрезеровка 1,
// вырезка 2 -- для «Сохранить выбранные» (те же правила, что у задания).
int jobPartRank(const File* file);

// Код инструмента для УП: Tool::code(), только [A-Za-z0-9._-] -- он идёт в
// сообщение (ASCII) и в комментарии, где скобка или ';' сломали бы строку.
QString jobToolCode(const Tool& tool);
// Слово типа: DRILL, ENDMILL, ENGRAVER, LASER, THREADMILL.
QString jobToolKeyword(Tool::Type type);
// Диаметр для УП: точка при любой локали, два-три знака -- 0.80, 1.00, 0.125.
QString jobDiameter(double diameter);

} // namespace GCode
