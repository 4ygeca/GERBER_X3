/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2025                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#pragma once

#include "abstract_file.h"
#include "gc_post.h"
#include "gc_types.h"
#include "geo/util.h"

#include <QList>
#include <QString>

class Project;

namespace GCode {

class GcFileProxy;
class File;

// Текст G-кода у файлов проекта устарел: side -- только у файлов этой
// стороны (булавки сдвигают лишь нижнюю), NullSide -- у всех. Вызывать, как
// только меняется параметр, влияющий на текст G-кода (сторона платы, safeZ/
// clearence/plunge, положение Home/Zero/Pin и т.п.). Пересобирается СРАЗУ
// только текст УП с открытым окном просмотра (окно получает новый, под
// курсором ожидания), прочие лишь помечены устаревшими (File::invalidate) и
// пересоберутся при первом чтении (File::ensureLines) -- в окне, при
// сохранении, в буфер обмена. Пересчёт ВСЕХ УП -- это секунды (HLDI, растр:
// новый движок JS на файл), а нужен текст лишь тому, кто его читает. Только
// GUI-поток.
void regenerateGCodeFiles(Side side = NullSide);

// То же, но окна просмотра обновляются отложенно и одним пересчётом на
// серию: запросы до его начала сливаются, каждый новый отодвигает его, и пока
// кнопка мыши нажата или поле формы свойств ещё правят, он ждёт. Для правок
// мышью и полями: пересчёт большой УП -- секунды, и в обработчике отпускания
// он вешал бы программу, не дав даже перерисовать сцену. Отметка «устарел»
// ставится сразу: прочитать текст до пересчёта значит прочитать его уже
// пересобранным. Только GUI-поток.
void scheduleRegenerateGCodeFiles(Side side = NullSide);

// Пересобрать устаревший текст у files перед чтением. Курсор ожидания --
// один на всю пачку и только если пересобирать есть что: у свежих
// File::ensureLines -- пустой ход. Только GUI-поток.
void ensureGCodeLines(const std::vector<File*>& files);

// Показать в открытых окнах просмотра files актуальный текст (устаревший --
// пересобрать), под одним курсором ожидания. У кого окна нет -- ничего: текст
// пересоберёт первый, кто его прочтёт. Только GUI-поток.
void refreshGCodeViewers(std::vector<File*> files);

class File : public AbstractFile {
    // friend class ::Project;
    friend class GcFileProxy;

protected:
    // Производные от gcp (заполняет конструктор от Params) — не сериализуются:
    // после загрузки проекта их восстанавливает postLoad() из прочитанного gcp.
    // regenerate() их только читает.
    [[= Serial::skip]] double feedRate{};
    [[= Serial::skip]] double plungeRate{};
    [[= Serial::skip]] int spindleSpeed{};
    [[= Serial::skip]] int toolType{};
    [[= Serial::skip]] QString strFeed;
    [[= Serial::skip]] QString strPlungeFeed;
    [[= Serial::skip]] QString strSpindle;
    Params gcp; ////

    // Имя, которое дал программе пользователь (текст поля Name в форме), без
    // суффикса инструмента. Отдельно от name_, потому что тот работает на две
    // ставки: до сохранения это метка в дереве, после save() -- путь на диске.
    // Опираться на name_ при поиске коллизий и при восстановлении формы нельзя.
    QString programName_;

    // Группа УП в дереве проекта (папка в «GCode», см. GCode::layoutTree);
    // пустая -- вне групп. Хранится в самой УП, а не в дереве: дерево из неё и
    // складывается, в том числе при открытии проекта. В старых проектах ключа
    // нет -- УП вне групп, как и было.
    QString group_;

    // Видимость файлов проекта на момент расчёта: id файла -> был ли видим.
    // Только хранится: открытие на правку его целиком НЕ применяет -- в нём и
    // прочие УП, и id, которые проект раздаёт повторно (Form::restoreContext).
    std::map<int32_t, bool> visibility_;

    // Текст УП (lines_) устарел: сменился параметр, от которого он зависит, а
    // пересборка отложена до первого чтения (ensureLines). Хранится в проекте
    // вместе с текстом: проект с устаревшим текстом и открывается устаревшим,
    // а сохранение проекта ничего не пересобирает. В старых проектах ключа нет
    // -- текст там пересобирался сразу, он свежий.
    bool stale_{};

public:
    File(Params&& newGcp)
        : feedRate{newGcp.feedRate()}
        , plungeRate{newGcp.plungeRate()}
        , spindleSpeed{newGcp.spindleSpeed()}
        , toolType{newGcp.toolType()}
        , strFeed{u'F' + formatValue(feedRate)}
        , strPlungeFeed{u'F' + formatValue(plungeRate)}
        , strSpindle{u'S' + formatValue(spindleSpeed)}
        , gcp{std::move(newGcp)} {
        setSide(gcp.params[Params::FileSide]);
        // Текста ещё нет -- его соберёт конструктор производного класса
        // (regenerate). Отметка, которую ставит смена стороны, у новой УП
        // ни к чему: устаревшим текст делают лишь правки уже созданной.
        stale_ = false;
    }

    File() = default;
    ~File() override = default;

    // Восстановление после чтения (зовёт движок Serial; прячет одноимённый крюк
    // AbstractFile, поэтому зовёт его сам). Поля подач/оборотов/типа -- skip,
    // проект их не хранит, а инструмент в gcp хранит: без этого УП из
    // открытого проекта при любом regenerate() теряла слова F и S ("M3 S0"),
    // а лазер считала сверлом (toolType 0 == Tool::Drill). Поля -- до базы:
    // та зовёт createGi().
    void postLoad() {
        const auto it = gcp.params.find(Params::MultiToolIndex);
        const qsizetype index = it != gcp.params.end() ? it->second.toInt() : -1;
        if(0 <= index && std::cmp_less(index, gcp.tools.size())) {
            feedRate = gcp.feedRate();
            plungeRate = gcp.plungeRate();
            spindleSpeed = gcp.spindleSpeed();
            toolType = gcp.toolType();
            strFeed = u'F' + formatValue(feedRate);
            strPlungeFeed = u'F' + formatValue(plungeRate);
            strSpindle = u'S' + formatValue(spindleSpeed);
        }
        AbstractFile::postLoad();
    }

    std::vector<QString> gCodeText() const { return lines_; }
    // Текст устарел -- см. stale_. Читать lines_ (lines(), lines2()) напрямую
    // можно только после ensureLines: иначе окно, файл на диске или буфер
    // получили бы текст для прежних стороны, высот, Home/Zero, булавок.
    bool isStale() const { return stale_; }
    void invalidate() { stale_ = true; }
    // Пересобрать текст, если устарел. Только GUI-поток: устаревшим текст
    // делают лишь правки в GUI, а файлы рабочего потока расчёта рождаются
    // свежими (их конструкторы зовут regenerate()).
    void ensureLines() {
        if(stale_) regenerate();
    }
    const Tool& tool() const { return gcp.tool(); }
    const Params& params() const { return gcp; }

    const QString& programName() const { return programName_; }
    void setProgramName(const QString& name) { programName_ = name; }

    const QString& group() const { return group_; }
    void setGroup(const QString& group) { group_ = group; }

    const std::map<int32_t, bool>& visibility() const { return visibility_; }
    void setVisibility(std::map<int32_t, bool> visibility) { visibility_ = std::move(visibility); }

    static QString getLastDir();
    static void setLastDir(QString dirPath);

    bool save(const QString& name);

    // Пересобрать lines_ (текст G-кода) из текущих параметров: стороны платы,
    // безопасной высоты, зазора, глубины подвода и т.п. У уже созданного файла
    // после смены такого параметра -- лениво: invalidate, а пересборка -- в
    // ensureLines у читающего. Части текста: «сохранить всё в один файл» пишет
    // шапку только у первого файла, а концовку -- только у последнего.
    // Неполный текст свежим не считается: после него отметка stale_ взведена.
    enum Section : unsigned {
        Header = 1,
        Body = 2,
        Footer = 4,
        All = Header | Body | Footer,
    };
    void regenerate(unsigned sections = All);

    // Текст УП без комментариев -- в системный буфер обмена; устаревший
    // сперва пересобирается. Только из GUI-потока; зовёт форма, когда новая
    // УП готова (Form::fileHandler, синхронные ветки сверловки). Из
    // regenerate() -- нельзя: того зовут и в рабочем потоке, и на каждый
    // пересчёт всех УП.
    void copyToClipboard();

    static void ensureDefaultScripts();

    // Расширение прокси для скрипта: объект, доступный как file.ext. Плагин
    // отдаёт сюда то, чего в общем прокси нет (Profile -- геометрию мостов).
    virtual QObject* createJsExtension(GcFileProxy& /*proxy*/, QJSEngine& /*engine*/) { return nullptr; }

    // Комментарий в диалекте текущего поста.
    static QString comment(const QString& text);
    // Итоговый текст файла (склейка строк, нумерация N по посту).
    static QString renderText(const std::vector<QString>& lines);

    FileTree::Node* node() override;

    // Сторона хранится ДВАЖДЫ: полем файла и параметром УП. Поле читают все, а
    // параметр переживает сохранение проекта -- из него сторону берёт
    // конструктор при загрузке. Разъехаться им нельзя: выбор в дереве иначе не
    // дожил бы до следующего открытия. Другая сторона -- другой текст (снизу он
    // зеркальный): смена делает его устаревшим.
    void setSide(Side side) override;

    // Базовый initFrom переносит то, что нужно при замене УП на месте (id, узел
    // дерева, сторону, цвет, тип, трансформ); здесь к нему добавляется группа:
    // пересчитанная УП остаётся там, куда её положил пользователь. А вот
    // programName_ и visibility_ переносить НЕЛЬЗЯ -- у нового файла они уже
    // свои, выставленные формой до замены. Сторону база переносит полем, мимо
    // setSide, -- её догоняют здесь параметр УП и отметка «устарел».
    void initFrom(AbstractFile* file) override;

protected:
    void startPath(const QPointF& point);
    void endPath();

    std::vector<Geo::Polylines> mirrorAndOffsetCurves(const QPointF& offset);
    Geo::Polylines mirrorAndOffsetCurves(const QPointF& offset, Geo::Polylines paths_);

    [[= Serial::skip]] std::vector<QSharedPointer<QColor>> debugColor;

    enum {
        AlwaysG,
        AlwaysX,
        AlwaysY,
        AlwaysZ,
        AlwaysI,
        AlwaysJ,
        AlwaysR,
        AlwaysS,
        AlwaysF,

        SpaceG,
        SpaceX,
        SpaceY,
        SpaceZ,
        SpaceI,
        SpaceJ,
        SpaceR,
        SpaceS,
        SpaceF,

        Size
    };

    // У линейных строк (G0/G1) и у дуг (G2/G3) свой диалект: пост задаёт два
    // формата слов, formated() выбирает набор по слову G строки.
    enum FormatKind {
        Linear,
        Arc,
        KindCount
    };

    // Рабочее состояние генератора текста УП: пересобирается regenerate(),
    // в проект не пишется.
    [[= Serial::skip]] Geo::Polylines g0path_;
    [[= Serial::skip]] double z_{};

    // Пост на время regenerate(): берётся один раз в его начале. Settings::post()
    // на каждом вызове сверяет время изменения файла поста с диском (stat), а
    // нужен пост на каждую дугу и каждый комментарий. Поле у каждого файла своё,
    // а не общая метка «давно ли сверяли» в Post: созданные расчётом файлы
    // пересобираются в его рабочем потоке одновременно с GUI, и общую метку
    // писали бы оба потока. Файл же в каждый момент в руках одного потока.
    [[= Serial::skip]] Post* post_{};
    // Пост пересчёта, вне его -- Settings::post(), как прежде.
    Post& currentPost();

    static inline QString lastDir;
    static inline bool redirected;
    static inline constexpr auto CMD_LIST = u"GXYZIJRSF"_sv;
    // Из них координатные -- те, что живут на сетке вывода (GCode::Units).
    static inline constexpr auto COORD_LIST = u"XYZIJR"_sv;

    std::vector<double> getDepths();

    [[= Serial::skip]] bool formatFlags[KindCount][Size]{};
    [[= Serial::skip]] Code gCode_ = GNull;
    [[= Serial::skip]] QString jsError_; // последняя ошибка скрипта, для комментария в УП
    static void parseFormat(const QString& format, bool (&flags)[Size]);

    // Слово УП: буква и то, что за ней. Координатные несут с собой своё ЧИСЛО
    // в единицах вывода -- повтор подавляется по нему, а не по тексту: текст
    // это лишь запись числа, и решать по записи значит решать по округлению.
    struct Word {
        QString text;
        Units units{};
        bool numeric{};
        Word(QString text)
            : text{std::move(text)} { }
        Word(QChar letter, Units units)
            : text{letter + format(units)}
            , units{units}
            , numeric{true} { }
        operator const QString&() const { return text; }
    };

    // Последнее НАПИСАННОЕ значение каждого слова: у координатных числом, у
    // прочих (G, S, F -- там значение и есть текст) текстом.
    [[= Serial::skip]] Units lastUnits[SpaceG]{};
    [[= Serial::skip]] bool lastWritten[SpaceG]{};
    [[= Serial::skip]] QString lastValues[SpaceG];

    std::vector<QString> savePath(const Geo::Polyline& srcCurve, double perimeter = {}, double depth = {});

    QString formated(const std::vector<Word>& data);
    // Готовые слова из JS-скрипта. Координатные разбираются обратно в число
    // и печатаются заново -- чтобы вывод скрипта шёл по той же сетке и с тем
    // же подавлением повторов, что и вывод C++.
    QString formatedText(const std::vector<QString>& data);

    struct Coord {
        QChar c;
        Word operator()(double mm) const { return {c, toUnits(mm)}; }
        operator Word() const { return {c, Units{}}; }
    } static constexpr i{u'I'}, j{u'J'}, rad{u'R'}, x{u'X'}, y{u'Y'}, z{u'Z'};

    // Обороты шпинделя длиной не являются -- своя запись, не по сетке вывода.
    struct {
        QChar c;
        Word operator()(double val) const { return {c + formatValue(val)}; }
    } static constexpr speed{u'S'};

    QString g0();
    QString g1();
    QString g2();
    QString g3();
    void extracted(const Geo::Vertex& v);
    QString g(const Geo::Vertex& v);

    // QString i(double val) { return u'I' + format(val); }
    // QString j(double val) { return u'J' + format(val); }
    // QString x(double val) { return u'X' + format(val); }
    // QString y(double val) { return u'Y' + format(val); }
    // QString z(double val) { return u'Z' + format(val); }
    // QString speed(int val) { return u'S' + QString::number(val); }

    // Текст координаты -- ровно запись её числа: цифры до точки, значащие
    // после и ни одного хвостового нуля.
    static QString format(Units units);
    static QString format(double mm) { return format(toUnits(mm)); }

    // Число, длиной НЕ являющееся: подача (мм/мин), обороты. Сетка вывода
    // задана допуском геометрии, и к минутам с оборотами он отношения не
    // имеет -- у них своя запись, до трёх знаков без хвостовых нулей.
    static QString formatValue(double val);

    void initSave();
    void writeHeader();
    void addSourceInfo();
    void addInfo();
    void writeFooter();
    bool runJsScript(const QString& scriptPath);
    // Контекст для функций поста.
    Post::Context postContext();
    // Одна дуга в текущем диалекте (I/J, R или хордами).
    void arcLines(std::vector<QString>& lines, const Geo::Vertex& fr, const Geo::Vertex& to, const Geo::Arc& arc);

    // AbstractFile interfaces
    // FileTree::Node* node() override;

    /////////////////////////////////////////////////////////////

    void createGiDrill();
    void createGiLaser();
    void createGiPocket();
    void createGiProfile();
    void createGiRaster();
};

} // namespace GCode
