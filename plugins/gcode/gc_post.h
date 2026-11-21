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

#include <QDateTime>
#include <QJSEngine>
#include <QJSValue>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <vector>

namespace GCode {

// Постпроцессор -- диалект станка, описанный JS-файлом scripts/posts/<id>.js.
// Файл объявляет глобальный объект `post` (см. scripts/posts/README.md):
// имя, расширение, стиль комментариев, способ записи дуг, формат слов для
// линейных строк, дуг и циклов сверления, шапка/концовка, коды
// шпинделя/лазера, циклы сверления контроллера, единица выдержки P и смена
// инструмента в задании (toolChange, message, maxToolNumber).
//
// Диалект -- это ТОЛЬКО текст. Что и когда писать (G0 на безопасную высоту,
// врезание, переезд домой) решает C++/скрипт плагина; пост лишь говорит,
// какими словами.
class Post {
public:
    struct Info {
        QString id;   // имя файла без .js
        QString name; // post.name
    };

    enum class Arcs {
        IJ,     // G2/G3 X Y I J
        R,      // G2/G3 X Y R
        Linear, // хордами, G1
    };

    struct LineNumbers {
        bool enabled{};
        int start{10};
        int step{10};
    };

    // Циклы сверления контроллера (post.cycles): коды, которыми их писать.
    // Пустой код -- такого цикла у контроллера нет, его скрипт разворачивает
    // в G0/G1 сам (common_gcode.js, expandDrillCycle). Что писать и когда --
    // решает скрипт (drilling.js), пост лишь говорит, какими словами.
    struct Cycles {
        QString drill;          // G81: врезание на подаче, быстрый отвод
        QString dwell;          // G82: то же с выдержкой P внизу
        QString peck;           // G83: клевки Q с выводом из отверстия
        QString chipBreak;      // G73: клевки Q с коротким отскоком
        QString cancel;         // G80: отмена цикла
        QString retractInitial; // G98: отвод на исходный уровень
        QString retractR;       // G99: отвод на плоскость R
        // G83 контроллера после каждого клевка возвращается вниз НА ПОДАЧЕ от
        // плоскости отвода (grblHAL, mc_canned_drill): при G98 -- от исходного
        // уровня. Тогда «Авто» разворачивает клевки в G0/G1 -- развёртка
        // спускается быстрым ходом до дна прошлого клевка, это в разы быстрее.
        // Явно выбранный в инструменте G83 остаётся G83.
        bool peckRefeed{};
    };

    // Единица слова P выдержки -- у G4 и у G82: grblHAL и LinuxCNC считают
    // её в секундах, Fanuc/Haas и прошивки 3D-принтеров -- в миллисекундах.
    enum class DwellUnit {
        Seconds,
        Milliseconds,
    };

    // Контекст для функций поста: что известно о программе на момент вызова.
    struct Context {
        bool laser{};
        // Текст -- часть задания (несколько УП одним файлом, gc_job.h). Шапка
        // задания не пускает шпиндель: оператор вот-вот будет менять
        // инструмент, а пускает его блок смены (toolChange).
        bool job{};
        QString name;
        QString programName;
        int spindleSpeed{};
        double feedRate{};
        double plungeRate{};
        QJSValue tool;       // снимок инструмента (как file.tool в скрипте)
        QJSValue properties; // свойства проекта (как file.properties)
        // Смена инструмента в задании (toolChange): номер T, номер прежнего
        // (0 -- смен ещё не было), первая ли это смена и сообщение оператору
        // -- готовой строкой поста (message) и голым текстом (ASCII).
        int toolNumber{};
        int previousToolNumber{};
        bool first{};
        QString message;
        QString messageText;
    };

    Post();
    ~Post();

    static QString postsDir();
    static std::vector<Info> available();

    // Загрузить пост по id. При ошибке -- предупреждение и дефолты generic.
    bool load(const QString& id);
    // Перечитать, если файл на диске изменился.
    void reloadIfChanged();

    const QString& id() const { return id_; }
    const QString& name() const { return name_; }
    const QString& description() const { return description_; }
    const QString& extension() const { return extension_; }
    bool parenComments() const { return parenComments_; }
    Arcs arcs() const { return arcs_; }
    double arcTolerance() const { return arcTolerance_; }
    const QString& formatLinear(bool laser) const { return laser ? laserLinear_ : millingLinear_; }
    const QString& formatArc(bool laser) const { return laser ? laserArc_ : millingArc_; }
    // Формат строки цикла сверления (format.cycle); нет его -- линейный фрезы.
    const QString& formatCycle() const { return cycle_; }
    const LineNumbers& lineNumbers() const { return lineNumbers_; }
    // Циклы сверления; nullopt -- у контроллера их нет (поля cycles нет, или
    // в нём ни одного кода цикла, или пуст код отмены).
    const std::optional<Cycles>& cycles() const { return cycles_; }
    DwellUnit dwellUnit() const { return dwellUnit_; }

    QString comment(const QString& text) const;
    // Является ли строка комментарием в этом диалекте.
    bool isComment(const QString& line) const;

    // Свои движки у Post и у скрипта плагина, поэтому контекст собирается
    // на движке поста: ctx-объекты из чужого движка сюда не переносятся.
    QJSEngine& engine() { return *engine_; }

    QStringList header(const Context& ctx);
    QStringList footer(const Context& ctx);
    QString spindleOn(const Context& ctx);
    QString spindleOff(const Context& ctx);
    QString laserOn(const Context& ctx, bool dynamic);
    QString laserOff(const Context& ctx);
    // Пауза на seconds своим текстом поста: post.dwell(ctx, seconds) -- для
    // диалектов, где это не «G4 P» (у Sinumerik «G4 F»). Функции у поста нет
    // -- nullopt: «G4 P» в единицах dwellUnit() пишет вызывающий.
    std::optional<QString> dwell(const Context& ctx, double seconds);

    // Задание (gc_job.h). Сообщение оператору text (ASCII) строкой поста:
    // post.message(text) -- у grblHAL «(MSG, text)», его показывает
    // отправитель; функции нет -- обычный комментарий.
    QString message(const QString& text);
    // Блок смены инструмента: post.toolChange(ctx). Поля нет (пост, писанный
    // до заданий) -- ISO-смена: spindleOff, ctx.message, «T<n> M6», spindleOn.
    // Длин, G43/G49/G10 и касаний здесь нет и не будет: это дело отправителя.
    QStringList toolChange(const Context& ctx);
    // Наибольший номер T, который примет контроллер (post.maxToolNumber; у
    // grblHAL N_TOOLS = 8, T9 -- ошибка); 0 -- предела нет.
    int maxToolNumber() const { return maxToolNumber_; }

    // Итоговый текст файла: строки + нумерация N по посту.
    QString renderText(const std::vector<QString>& lines) const;

private:
    void resetDefaults();
    void readFields();
    QJSValue makeCtx(const Context& ctx);
    QStringList callLines(const QString& field, const QJSValue& ctx, const QJSValue& extra = {});

    std::unique_ptr<QJSEngine> engine_;
    QJSValue post_;
    QString id_, path_;
    QDateTime mtime_;

    QString name_, description_, extension_;
    bool parenComments_{};
    Arcs arcs_{Arcs::IJ};
    double arcTolerance_{0.01};
    QString millingLinear_, millingArc_, laserLinear_, laserArc_, cycle_;
    LineNumbers lineNumbers_;
    std::optional<Cycles> cycles_;
    DwellUnit dwellUnit_{DwellUnit::Seconds};
    int maxToolNumber_{};
};

} // namespace GCode
