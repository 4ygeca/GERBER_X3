// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
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
#include "gc_job.h"
#include "gc_file.h"
#include "gc_jobdialog.h"
#include "gc_plugin.h"
#include "gc_post.h"

#include "abstract_file.h"
#include "app.h"
#include "gi.h"
#include "project.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QTextStream>
#include <map>
#include <set>

namespace GCode {

namespace {

// Имена здесь -- с приставкой job: unity build склеивает .cpp цели пачками,
// и безымянное пространство у них на всех одно.

// Тип УП профиля -- Profile::PROFILE: заголовок плагина ядру не виден.
constexpr auto jobProfileType = "Profile"_hash32;

File* jobFile(int32_t id) { return dynamic_cast<File*>(App::project().file(id)); }

bool jobHasToolPaths(const Params& params) {
    return r::any_of(params.toolPathss, [](const Geo::Polylines& paths) {
        return r::any_of(paths, [](const Geo::Polyline& path) { return !path.empty(); });
    });
}

int jobToolRank(Tool::Type type) {
    switch(type) {
    case Tool::Drill   : return 0;
    case Tool::EndMill : return 1;
    case Tool::Engraver: return 2;
    default            : return 3;
    }
}

int jobOperationRank(Tool::Operation operation) {
    switch(operation) {
    case Tool::Operation::Drilling: return 0;
    case Tool::Operation::Milling : return 1;
    default                       : return 2;
    }
}

// Слово операции для строки STEP. Вырезка -- своим словом: её плагин
// отправителя может, например, переспросить перед последним шагом.
QString jobOperationKeyword(const JobStep& step) {
    if(step.cutout) return u"CUTOUT"_s;
    switch(step.operation) {
    case Tool::Operation::Drilling : return u"DRILLING"_s;
    case Tool::Operation::Milling  : return u"MILLING"_s;
    case Tool::Operation::Laser    : return u"LASER"_s;
    case Tool::Operation::Threading: return u"THREADING"_s;
    }
    return u"MILLING"_s;
}

// Сообщение оператору перед сменой: «T2 D10L100 DRILL D=1.00». Только ASCII
// (код уже очищен, слово типа и число -- свои): имя инструмента бывает
// кириллицей и с пробелами, его в сообщении нет -- оно есть в сведениях УП.
QString jobMessage(const JobStep& step, int toolNumber) {
    return u"T%1 %2 %3 D=%4"_s.arg(toolNumber).arg(step.code, jobToolKeyword(step.toolType), jobDiameter(step.diameter));
}

JobStep jobStep(const File* file) {
    const Tool& tool = file->tool();
    JobStep step;
    step.id = file->id();
    step.identity = tool.identityKey();
    step.code = jobToolCode(tool);
    step.toolName = tool.name();
    step.programName = file->programName().isEmpty() ? file->shortName() : file->programName();
    step.toolType = tool.type();
    step.diameter = tool.diameter();
    step.operation = file->operation();
    step.spindleSpeed = file->params().spindleSpeed();
    // Сверловка идёт на подаче врезания (циклы, развёртка), прочее -- на
    // подаче фрезеровки: в диалоге -- та, с которой шаг и работает.
    step.feed = step.operation == Tool::Operation::Drilling ? tool.plungeRate() : tool.feedRate();
    step.hits = file->drillHits();
    step.laser = tool.type() == Tool::Laser;
    step.cutout = step.autoCutout = isCutoutProgram(file);
    return step;
}

// Имя для имени файла: символы, которых в имени файла быть не может, -- '_'.
QString jobFileNamePart(QString name) {
    for(QChar& c: name)
        if(u"/\\:*?\"<>|"_s.contains(c) || c.unicode() < 0x20) c = u'_';
    return name.trimmed();
}

} // namespace

QString jobToolCode(const Tool& tool) {
    // Заданный код -- как есть, если годен (его проверяет и правка базы).
    // Негодный (tools.json правили руками) -- код по геометрии: в комментарии
    // и сообщение он идёт без скобок и ';', иначе строку обрезало бы.
    if(const QString code = tool.code(); !code.isEmpty() && Tool::isValidCode(code)) return code;
    if(const QString code = tool.codeAuto(); !code.isEmpty() && Tool::isValidCode(code)) return code;
    return u"TOOL"_s;
}

QString jobToolKeyword(Tool::Type type) {
    switch(type) {
    case Tool::Drill     : return u"DRILL"_s;
    case Tool::EndMill   : return u"ENDMILL"_s;
    case Tool::Engraver  : return u"ENGRAVER"_s;
    case Tool::Laser     : return u"LASER"_s;
    case Tool::ThreadMill: return u"THREADMILL"_s;
    default              : return u"TOOL"_s;
    }
}

QString jobDiameter(double diameter) {
    // QString::number от локали не зависит: точка всегда. Три знака --
    // столько держит база, третий нулевой -- долой: 0.80, 1.00, 0.125.
    QString text = QString::number(diameter, 'f', 3);
    if(text.endsWith(u'0')) text.chop(1);
    return text;
}

bool isCutoutProgram(const File* file) {
    if(!file || file->type() != jobProfileType) return false;
    const Params& params = file->params();
    if(params.tools.empty() || params.tool().type() == Tool::Laser) return false;
    if(!params.params.contains(Params::Side) || params.side() != Outer) return false;
    const auto it = params.params.find(Params::GrItems);
    if(it == params.params.end()) return false;
    const auto* items = std::get_if<UsedItems>(&static_cast<const V&>(it->second));
    if(!items) return false;
    const Project& project = App::project();
    // Ключ -- пара {id файла, тип элементов}, значение -- id элементов сцены.
    for(const auto& [key, giIds]: *items) {
        if(key.empty()) continue;
        const AbstractFile* source = App::project().file(key.front());
        if(!source) continue; // исходный файл уже закрыт
        if(project.isBoardOutlineFile(source->id())) return true;
        for(const auto& gi: *source->itemGroup(key.size() > 1 ? key[1] : -1))
            if(gi && r::contains(giIds, gi->id()) && source->isBoardOutlineItem(gi.get())) return true;
    }
    return false;
}

int jobPartRank(const File* file) { return isCutoutProgram(file) ? 3 : jobOperationRank(file->operation()); }

std::optional<Job> Job::fromPrograms(const std::vector<int32_t>& ids, QString& error) {
    if(ids.empty()) {
        error = QObject::tr("No tool paths are selected.\n"
                            "Mark the tool paths of the job with their checkboxes in the tree, or select several with Ctrl or Shift, or a whole group.");
        return std::nullopt;
    }

    Job job;
    bool sides[2]{};
    for(int32_t id: ids) {
        File* file = jobFile(id);
        if(!file) continue;
        // Без траекторий (или без плагина, который пишет текст) у УП нет
        // тела -- в задании ей делать нечего.
        const Params& params = file->params();
        if(params.tools.empty() || !jobHasToolPaths(params) || !App::gCodePlugin(file->type())) {
            job.skipped.push_back(file->shortName());
            continue;
        }
        job.steps.push_back(jobStep(file));
        job.steps.back().treeIndex = static_cast<int>(job.steps.size());
        sides[file->side() == Bottom ? Bottom : Top] = true;
    }

    if(job.steps.empty()) {
        error = QObject::tr("The selected tool paths have no toolpaths: there is nothing to save.");
        return std::nullopt;
    }
    if(sides[Top] && sides[Bottom]) {
        error = QObject::tr("The selected tool paths are on both sides of the board.\n"
                            "A job is one side: select the tool paths of one side.");
        return std::nullopt;
    }
    const auto lasers = r::count_if(job.steps, &JobStep::laser);
    if(lasers && std::cmp_not_equal(lasers, job.steps.size())) {
        error = QObject::tr("Laser and spindle tool paths cannot be in one job: the machine works with them in different modes.\n"
                            "Save them as separate jobs.");
        return std::nullopt;
    }

    job.side = sides[Bottom] ? Bottom : Top;
    job.laser = lasers > 0;
    // Номера T у лазера станку не уходят (смен нет) -- и предел им ни к чему.
    job.maxToolNumber = job.laser ? 0 : jobMaxToolNumber();
    job.loadLengthSettings();
    job.applyDefaultOrder();
    return job;
}

namespace {
const auto jobLengthsKey = u"GCode/JobLengths"_s;
const auto jobZeroProbeKey = u"GCode/JobZeroProbe"_s;
const auto jobToolTableFileKey = u"GCode/JobToolTableFile"_s;
const auto jobTableToolsKey = u"GCode/JobTableTools"_s;
} // namespace

void Job::loadLengthSettings() {
    const QSettings settings;
    // Пост, который длин не умеет, -- всегда по-старому, что бы ни
    // запомнилось от прошлого поста.
    const bool can = App::gcSettings().post().toolLengths();
    lengths = can && settings.value(jobLengthsKey, 0).toInt() == static_cast<int>(JobLengths::Controller)
        ? JobLengths::Controller
        : JobLengths::Sender;
    zeroProbe = settings.value(jobZeroProbeKey, true).toBool();
    toolTableFile = settings.value(jobToolTableFileKey, false).toBool();
    tableTools.clear();
    for(const QString& identity: settings.value(jobTableToolsKey).toStringList()) tableTools.insert(identity);
}

void Job::saveLengthSettings() const {
    QSettings settings;
    settings.setValue(jobLengthsKey, static_cast<int>(lengths));
    settings.setValue(jobZeroProbeKey, zeroProbe);
    settings.setValue(jobToolTableFileKey, toolTableFile);
    // Отметки «из таблицы» прочих инструментов (не этого задания) -- как были:
    // их память у инструмента, а не у задания.
    QStringList identities = settings.value(jobTableToolsKey).toStringList();
    for(const JobStep& step: steps) identities.removeAll(step.identity);
    for(const QString& identity: tableTools)
        if(r::contains(steps, identity, &JobStep::identity)) identities.push_back(identity);
    settings.setValue(jobTableToolsKey, identities);
}

bool Job::usesTable() const {
    return controllerLengths() && r::any_of(steps, [this](const JobStep& step) { return tableTools.contains(step.identity); });
}

QStringList Job::lengthProblems() const {
    QStringList problems;
    if(!controllerLengths() || steps.empty()) return problems;
    const JobPlan p = plan();
    // Z0 задаёт первый инструмент. Если он «щуп», Z0 -- его кончик, а длины
    // таблицы мерены против другой точки и другого инструмента: инструмент
    // «из таблицы» встал бы не на Z0 -- воздух или фреза в стол.
    if(usesTable() && !tableTools.contains(steps.front().identity)) {
        QStringList codes;
        for(size_t i{}; i < steps.size(); ++i)
            if(tableTools.contains(steps[i].identity) && !codes.contains(u"T%1"_s.arg(p.toolNumbers[i])))
                codes << u"T%1"_s.arg(p.toolNumbers[i]);
        problems << QObject::tr("The first tool is probed: its touch sets Z0 of the work, but %1 take their lengths "
                                "from the controller table, measured against another tool -- Z0 would be wrong for them. "
                                "Take the first tool from the table as well, or probe all tools.")
                        .arg(codes.join(u", "_s));
    }
    // Номера пошли по кругу: в ячейке одна длина, а инструментов на неё
    // несколько. «Щуп» перепишет её посреди задания (G10 L10), файл замера
    // запишет туда лишь один -- инструменту «из таблицы» общая ячейка не годится.
    std::map<int, std::vector<QString>> cells; // номер -> инструменты (identity)
    for(size_t i{}; i < steps.size(); ++i)
        if(auto& tools = cells[p.toolNumbers[i]]; !r::contains(tools, steps[i].identity)) tools.push_back(steps[i].identity);
    for(const auto& [number, tools]: cells)
        if(tools.size() > 1 && r::any_of(tools, [this](const QString& identity) { return tableTools.contains(identity); }))
            problems << QObject::tr("T%1: %2 tools share cell %1 of the controller table (the tool numbers wrap), "
                                    "and a cell holds one length. Probe these tools.")
                            .arg(number)
                            .arg(tools.size());
    return problems;
}

static const auto jobMaxToolNumberKey = u"GCode/JobMaxToolNumber"_s;

int jobMaxToolNumber() { return std::max(0, QSettings{}.value(jobMaxToolNumberKey, 0).toInt()); }

void setJobMaxToolNumber(int maxToolNumber) { QSettings{}.setValue(jobMaxToolNumberKey, std::max(0, maxToolNumber)); }
void Job::applyDefaultOrder() {
    // Равные по правилу шаги -- в порядке дерева, как бы их ни переставили.
    r::stable_sort(steps, {}, &JobStep::treeIndex);
    std::vector<JobOrderKey> keys;
    keys.reserve(steps.size());
    for(const JobStep& step: steps)
        keys.push_back({step.identity, jobToolRank(step.toolType), step.diameter, jobOperationRank(step.operation), step.cutout});
    std::vector<JobStep> ordered;
    ordered.reserve(steps.size());
    for(int index: jobDefaultOrder(keys)) ordered.push_back(steps[index]);
    steps = std::move(ordered);
}

JobPlan Job::plan() const {
    std::vector<QString> identities;
    identities.reserve(steps.size());
    for(const JobStep& step: steps) identities.push_back(step.identity);
    return jobPlan(identities, maxToolNumber);
}

QString Job::defaultFileName() const {
    // Все УП из одной группы -- её имя: задание обычно ею и собрано («до
    // металлизации»). Иначе -- имя проекта.
    std::set<QString> groups;
    for(const JobStep& step: steps)
        if(const File* file = jobFile(step.id)) groups.insert(file->group());
    QString base;
    if(groups.size() == 1) base = jobFileNamePart(*groups.begin());
    if(base.isEmpty()) base = jobFileNamePart(QFileInfo{App::project().name()}.completeBaseName());
    if(base.isEmpty()) base = u"job"_s;
    return base + (side == Bottom ? u"_BS."_s : u"_TS."_s) + App::gcSettings().fileExtension();
}

std::vector<QString> Job::text(const QString& name) const {
    // УП -- заново по id: пока был открыт диалог, расчёт мог заменить её
    // объект на месте, а то и удалить её.
    std::vector<std::pair<const JobStep*, File*>> parts;
    for(const JobStep& step: steps)
        if(File* file = jobFile(step.id)) parts.emplace_back(&step, file);
    if(parts.empty()) return {};

    std::vector<QString> identities;
    for(const auto& [step, file]: parts) identities.push_back(step->identity);
    const JobPlan plan = jobPlan(identities, maxToolNumber);

    // Сведения задания и таблица инструментов -- комментариями поста.
    // Ключевые слова не переводятся: их разбирает плагин отправителя
    // (грамматика -- scripts/posts/README.md, «Jobs»).
    std::vector<QString> info;
    // Версия формата -- первой строкой: по ней плагин отправителя узнаёт, что
    // за файл, и замечает, если грамматика ниже когда-нибудь сменится.
    info.push_back(File::comment(u"FORMAT GGEASY-JOB 1"_s));
    info.push_back(File::comment(u"JOB "_s + name));
    info.push_back(File::comment(side == Bottom ? u"SIDE BOTTOM"_s : u"SIDE TOP"_s));
    const QString material = App::toolHolder().materialName(App::project().material());
    info.push_back(File::comment(u"MATERIAL "_s + (material.isEmpty() ? u"-"_s : material)));
    info.push_back(File::comment(u"STEPS %1"_s.arg(static_cast<int>(parts.size()))));
    for(size_t i{}; i < parts.size(); ++i) {
        const JobStep& step = *parts[i].first;
        info.push_back(File::comment(u"STEP %1 T%2 %3 %4"_s.arg(static_cast<int>(i + 1)).arg(plan.toolNumbers[i]).arg(jobOperationKeyword(step), step.programName)));
    }
    info.push_back(File::comment(u"TOOLS %1"_s.arg(plan.tools)));
    // По инструменту -- строка, в порядке первого применения и с его номером.
    std::vector<QString> listed;
    for(size_t i{}; i < parts.size(); ++i) {
        const JobStep& step = *parts[i].first;
        if(r::contains(listed, step.identity)) continue;
        listed.push_back(step.identity);
        int hits{};
        for(const auto& [other, file]: parts)
            if(other->identity == step.identity) hits += other->hits;
        QString line = u"T%1 %2 D=%3 %4"_s.arg(plan.toolNumbers[i]).arg(step.code, jobDiameter(step.diameter), jobToolKeyword(step.toolType));
        if(hits > 0) line += u" HITS=%1"_s.arg(hits);
        info.push_back(File::comment(line));
    }
    if(plan.wrapped) {
        const auto warning = u"WARNING %1 tools, the controller takes T1..T%2: tool numbers repeat, the tool code identifies the tool"_s;
        info.push_back(File::comment(warning.arg(plan.tools).arg(maxToolNumber)));
    }

    // Длины из таблицы контроллера и щупом (окно задания). Z0 детали задаёт
    // первая смена: инструмент «из таблицы» -- касанием с его длиной (G43 H,
    // если просили замер в начале), «щуп» -- касанием своего кончика. Дальше
    // «из таблицы» -- G43 H, «щуп» -- касание: при инструментах из таблицы
    // длина в его ячейку против уже заданного Z0, без них -- снова Z0 его
    // кончиком (таблицу такое задание не трогает вовсе).
    const bool controller = controllerLengths();
    const bool table = usesTable();

    // Шапка первой УП -- без пуска шпинделя (ctx.job), тела всех, концовка
    // последней -- один раз.
    std::vector<QString> lines = parts.front().second->jobHeaderText(info);
    for(size_t i{}; i < parts.size(); ++i) {
        const auto [step, file] = parts[i];
        if(plan.toolChange[i]) {
            const QString message = jobMessage(*step, plan.toolNumbers[i]);
            if(laser) // лазер один, сколько бы «инструментов» (пятен) ни было: только отметка
                lines.push_back(File::comment(message));
            else {
                File::ToolChange change{
                    .toolNumber = plan.toolNumbers[i],
                    .previousToolNumber = i ? plan.toolNumbers[i - 1] : 0,
                    .first = i == 0,
                    .code = step->code,
                    .message = message,
                    .length = {},
                    .probe = false,
                    .setZero = false,
                };
                if(controller) {
                    const bool fromTable = tableTools.contains(step->identity);
                    change.length = fromTable ? u"table"_s : u"probe"_s;
                    change.probe = !fromTable || (i == 0 && zeroProbe);
                    change.setZero = fromTable ? change.probe : !table;
                }
                lines.append_range(file->toolChangeText(change));
            }
        } else if(!laser && step->spindleSpeed != parts[i - 1].first->spindleSpeed) {
            // Тот же инструмент, другие режимы: без смены, только обороты.
            lines.push_back(file->spindleOnText());
        }
        lines.append_range(file->sectionsText(File::Body | File::Job));
    }
    lines.append_range(parts.back().second->sectionsText(File::Footer | File::Job));
    std::erase_if(lines, std::bind(&QString::isEmpty, _1));
    return lines;
}

std::vector<QString> Job::toolTableText(const QString& name) const {
    if(!usesTable()) return {};
    const JobPlan p = plan();
    std::vector<Post::TableTool> tools;
    std::set<QString> listed;
    for(size_t i{}; i < steps.size(); ++i)
        if(tableTools.contains(steps[i].identity) && listed.insert(steps[i].identity).second)
            tools.push_back({p.toolNumbers[i], jobMessage(steps[i], p.toolNumbers[i])});
    r::sort(tools, {}, &Post::TableTool::number);
    const QStringList lines = App::gcSettings().post().toolTable(name, maxToolNumber, tools);
    return {lines.begin(), lines.end()};
}

void saveJob(const std::vector<int32_t>& ids, QWidget* owner) {
    const QString title = QObject::tr("Save as Job");
    QString error;
    auto job = Job::fromPrograms(ids, error);
    if(!job) {
        QMessageBox::information(owner, title, error);
        return;
    }
    // Нижняя сторона зеркалится по оси булавок: прежде записи -- тот же
    // вопрос о расстановке, что и у прочих сохранений УП.
    if(App::project().pinsPlacedMessage()) return;

    JobDialog dialog{std::move(*job), owner};
    if(dialog.exec() != QDialog::Accepted) return;
    const Job& accepted = dialog.job();

    const QString name = QFileDialog::getSaveFileName(owner, QObject::tr("Save Job"),
        File::getLastDir().append(accepted.defaultFileName()),
        QObject::tr("GCode (*.%1)").arg(App::gcSettings().fileExtension()));
    if(name.isEmpty()) return;
    File::setLastDir(name);

    // Пересборка текстов всех УП задания -- у больших это секунды.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<QString> lines = accepted.text(QFileInfo{name}.completeBaseName());
    QSaveFile file{name};
    bool written{};
    if(!lines.empty() && file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        {
            QTextStream out{&file};
            out.setEncoding(QStringConverter::Utf8);
            out << File::renderText(lines);
        } // поток дописан до commit: тот закрывает файл
        written = file.commit();
    }
    // Файл замера таблицы инструментов -- рядом, с тем же именем и
    // приставкой: панель_TS.nc и панель_TS_tool_table.nc.
    QString tableName;
    bool tableWritten{true};
    QString tableError;
    if(written && accepted.toolTableFile) {
        const QFileInfo info{name};
        const std::vector<QString> table = accepted.toolTableText(info.completeBaseName());
        if(!table.empty()) {
            tableName = info.dir().filePath(info.completeBaseName() + u"_tool_table."_s + info.suffix());
            QSaveFile tableFile{tableName};
            tableWritten = false;
            if(tableFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                {
                    QTextStream out{&tableFile};
                    out.setEncoding(QStringConverter::Utf8);
                    // Как написал пост, с пустыми строками между
                    // инструментами: файл читают глазами.
                    for(const QString& line: table) out << line << u'\n';
                }
                tableWritten = tableFile.commit();
            }
            if(!tableWritten) tableError = tableFile.errorString();
        }
    }
    QGuiApplication::restoreOverrideCursor();
    if(!written)
        QMessageBox::warning(owner, title,
            QObject::tr("Cannot write the job file \"%1\".\n%2").arg(QDir::toNativeSeparators(name), file.errorString()));
    else if(!tableWritten)
        QMessageBox::warning(owner, title,
            QObject::tr("Cannot write the tool table file \"%1\".\n%2").arg(QDir::toNativeSeparators(tableName), tableError));
}

} // namespace GCode
