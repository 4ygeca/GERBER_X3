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
#include "gc_post.h"
#include "gc_types.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace GCode {

namespace {

// Одноразовая загрузка файла поста в движок: глобальный объект `post`.
QJSValue evalPost(QJSEngine& engine, const QString& path) {
    QFile f{path};
    if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "GCode post: cannot open" << path;
        return {};
    }
    auto result = engine.evaluate(QString::fromUtf8(f.readAll()), path);
    if(result.isError()) {
        qWarning() << "GCode post error in" << path
                   << "line" << result.property(u"lineNumber"_s).toInt()
                   << ":" << result.toString();
        return {};
    }
    auto post = engine.globalObject().property(u"post"_s);
    if(!post.isObject()) {
        qWarning() << "GCode post:" << path << "does not define object `post`";
        return {};
    }
    return post;
}

QString strOr(const QJSValue& obj, const QString& key, const QString& def) {
    auto v = obj.property(key);
    return v.isString() ? v.toString() : def;
}

} // namespace

Post::Post()
    : engine_{std::make_unique<QJSEngine>()} {
    engine_->installExtensions(QJSEngine::ConsoleExtension);
    resetDefaults();
}

Post::~Post() = default;

QString Post::postsDir() {
    return QCoreApplication::applicationDirPath() + u"/scripts/posts"_s;
}

std::vector<Post::Info> Post::available() {
    std::vector<Info> list;
    QDir dir{postsDir()};
    for(const QString& fileName: dir.entryList({u"*.js"_s}, QDir::Files, QDir::Name)) {
        Info info{QFileInfo{fileName}.completeBaseName(), {}};
        QJSEngine engine;
        auto post = evalPost(engine, dir.filePath(fileName));
        info.name = strOr(post, u"name"_s, info.id);
        list.push_back(std::move(info));
    }
    // generic -- первым: он же дефолт.
    if(auto it = std::ranges::find(list, u"generic"_s, &Info::id); it != list.end())
        std::rotate(list.begin(), it, it + 1);
    return list;
}

void Post::resetDefaults() {
    // Ровно то, что было в полях настроек до появления постов.
    name_ = u"Generic ISO"_s;
    description_.clear();
    extension_ = u"tap"_s;
    parenComments_ = false;
    arcs_ = Arcs::IJ;
    arcTolerance_ = 0.01;
    millingLinear_ = u"G?X?Y?Z?F?S?"_s;
    millingArc_ = u"G?X?Y?I+J+Z?F?S?"_s;
    laserLinear_ = millingLinear_;
    laserArc_ = millingArc_;
    cycle_ = millingLinear_;
    lineNumbers_ = {};
    // Циклов у generic нет: сверловка разворачивается в G0/G1, как и прежде.
    cycles_.reset();
    dwellUnit_ = DwellUnit::Seconds;
    post_ = QJSValue{};
}

bool Post::load(const QString& id) {
    id_ = id;
    path_ = postsDir() + u'/' + id + u".js"_s;
    resetDefaults();
    mtime_ = QFileInfo{path_}.lastModified();

    // Свежий движок: старые глобалы предыдущего поста не должны просачиваться.
    engine_ = std::make_unique<QJSEngine>();
    engine_->installExtensions(QJSEngine::ConsoleExtension);
    post_ = evalPost(*engine_, path_);
    if(!post_.isObject()) {
        qWarning() << "GCode post: falling back to built-in generic defaults";
        return false;
    }
    readFields();
    return true;
}

void Post::reloadIfChanged() {
    if(path_.isEmpty()) return;
    if(QFileInfo{path_}.lastModified() != mtime_)
        load(id_);
}

void Post::readFields() {
    name_ = strOr(post_, u"name"_s, id_);
    description_ = strOr(post_, u"description"_s, {});
    extension_ = strOr(post_, u"extension"_s, extension_);
    parenComments_ = strOr(post_, u"comment"_s, u";"_s).startsWith(u'(');

    const QString arcs = strOr(post_, u"arcs"_s, u"ij"_s).toLower();
    arcs_ = arcs == u"r"_s ? Arcs::R : arcs == u"linear"_s ? Arcs::Linear
                                                           : Arcs::IJ;
    if(auto v = post_.property(u"arcTolerance"_s); v.isNumber() && v.toNumber() > 0.0)
        arcTolerance_ = v.toNumber();

    auto format = post_.property(u"format"_s);
    auto readPair = [&format](const QString& key, QString& linear, QString& arc) {
        auto obj = format.property(key);
        if(obj.isString()) { // одна строка на всё -- как в старых настройках
            linear = arc = obj.toString();
            return;
        }
        if(!obj.isObject()) return;
        linear = strOr(obj, u"linear"_s, linear);
        arc = strOr(obj, u"arc"_s, arc);
    };
    if(format.isObject()) {
        readPair(u"milling"_s, millingLinear_, millingArc_);
        laserLinear_ = millingLinear_, laserArc_ = millingArc_; // лазер наследует фрезу
        readPair(u"laser"_s, laserLinear_, laserArc_);
    }
    // Строка цикла -- свой формат; нет его -- линейный фрезы. Буквы R, Q, P,
    // которых в нём нет, пишутся как «?» -- только изменившиеся.
    cycle_ = strOr(format, u"cycle"_s, millingLinear_);

    // Циклы -- только с кодом отмены и хотя бы одним кодом цикла: без G80
    // цикл не закрыть, а пустой объект -- то же, что его отсутствие.
    if(auto obj = post_.property(u"cycles"_s); obj.isObject()) {
        Cycles cycles;
        cycles.drill = strOr(obj, u"drill"_s, {});
        cycles.dwell = strOr(obj, u"dwell"_s, {});
        cycles.peck = strOr(obj, u"peck"_s, {});
        cycles.chipBreak = strOr(obj, u"chipBreak"_s, {});
        cycles.cancel = strOr(obj, u"cancel"_s, u"G80"_s);
        // Нет retract -- G98/G99, как почти у всех. Пустая строка -- такого
        // отвода у контроллера нет: скрипт тогда разворачивает цикл сам.
        const auto retract = obj.property(u"retract"_s);
        cycles.retractInitial = strOr(retract, u"initial"_s, u"G98"_s);
        cycles.retractR = strOr(retract, u"r"_s, u"G99"_s);
        cycles.peckRefeed = obj.property(u"peckRefeed"_s).toBool();
        const bool anyCycle = !cycles.drill.isEmpty() || !cycles.dwell.isEmpty()
            || !cycles.peck.isEmpty() || !cycles.chipBreak.isEmpty();
        if(anyCycle && !cycles.cancel.isEmpty())
            cycles_ = std::move(cycles);
        else
            qWarning() << "GCode post" << id_ << ": `cycles` has no cycle codes or an empty `cancel` -- ignored";
    }

    // Единица P -- поле самого поста: она нужна и без циклов (G4 развёртки
    // G82). Записанная внутри cycles тоже годится.
    const QString dwellUnit = strOr(post_, u"dwellUnit"_s, strOr(post_.property(u"cycles"_s), u"dwellUnit"_s, u"s"_s));
    dwellUnit_ = dwellUnit.trimmed().toLower() == u"ms"_s ? DwellUnit::Milliseconds : DwellUnit::Seconds;

    auto ln = post_.property(u"lineNumbers"_s);
    if(ln.isObject()) {
        lineNumbers_.enabled = true;
        if(auto v = ln.property(u"start"_s); v.isNumber()) lineNumbers_.start = v.toInt();
        if(auto v = ln.property(u"step"_s); v.isNumber()) lineNumbers_.step = std::max(1, v.toInt());
    } else
        lineNumbers_.enabled = ln.toBool();
}

QString Post::comment(const QString& text) const {
    if(parenComments_) {
        QString safe = text;
        safe.replace(u'(', u'[').replace(u')', u']');
        return u'(' + safe + u')';
    }
    return u';' + text;
}

bool Post::isComment(const QString& line) const {
    return line.startsWith(parenComments_ ? u'(' : u';');
}

QJSValue Post::makeCtx(const Context& ctx) {
    QJSValue obj = engine_->newObject();
    obj.setProperty(u"laser"_s, ctx.laser);
    obj.setProperty(u"job"_s, ctx.job);
    obj.setProperty(u"name"_s, ctx.name);
    obj.setProperty(u"programName"_s, ctx.programName);
    obj.setProperty(u"spindleSpeed"_s, ctx.spindleSpeed);
    obj.setProperty(u"feedRate"_s, ctx.feedRate);
    obj.setProperty(u"plungeRate"_s, ctx.plungeRate);
    obj.setProperty(u"tool"_s, ctx.tool);
    obj.setProperty(u"properties"_s, ctx.properties);
    obj.setProperty(u"toolNumber"_s, ctx.toolNumber);
    obj.setProperty(u"previousToolNumber"_s, ctx.previousToolNumber);
    obj.setProperty(u"first"_s, ctx.first);
    obj.setProperty(u"message"_s, ctx.message);
    obj.setProperty(u"messageText"_s, ctx.messageText);
    return obj;
}

// Поле поста может быть строкой, массивом строк или функцией(ctx, extra),
// возвращающей строку либо массив строк. Пустое поле -- пусто.
QStringList Post::callLines(const QString& field, const QJSValue& ctx, const QJSValue& extra) {
    if(!post_.isObject()) return {};
    QJSValue value = post_.property(field);
    if(value.isCallable()) {
        QJSValueList args{ctx};
        if(!extra.isUndefined()) args.push_back(extra);
        // this -- сам пост: функция поля берёт свои настройки (post.spinUp)
        // и через this, как метод объекта.
        value = value.callWithInstance(post_, args);
        if(value.isError()) {
            qWarning() << "GCode post" << id_ << field << "error line"
                       << value.property(u"lineNumber"_s).toInt() << ":" << value.toString();
            return {};
        }
    }
    QStringList lines;
    if(value.isArray()) {
        const int len = value.property(u"length"_s).toInt();
        for(int i{}; i < len; ++i)
            lines << value.property(i).toString();
    } else if(value.isString())
        lines << value.toString().split(u'\n');
    return lines;
}

QStringList Post::header(const Context& ctx) {
    if(!post_.isObject()) // дефолт generic; шапка задания шпиндель не пускает
        return ctx.laser || ctx.job ? QStringList{u"G21 G17 G90"_s}
                                    : QStringList{u"G21 G17 G90"_s, u"M3 S"_s + QString::number(ctx.spindleSpeed)};
    return callLines(u"header"_s, makeCtx(ctx));
}

QStringList Post::footer(const Context& ctx) {
    if(!post_.isObject())
        return ctx.laser ? QStringList{u"M30"_s} : QStringList{u"M5"_s, u"M30"_s};
    return callLines(u"footer"_s, makeCtx(ctx));
}

QString Post::spindleOn(const Context& ctx) {
    if(!post_.isObject()) return u"M3 S"_s + QString::number(ctx.spindleSpeed);
    return callLines(u"spindleOn"_s, makeCtx(ctx)).join(u'\n');
}

QString Post::spindleOff(const Context& ctx) {
    if(!post_.isObject()) return u"M5"_s;
    return callLines(u"spindleOff"_s, makeCtx(ctx)).join(u'\n');
}

QString Post::laserOn(const Context& ctx, bool dynamic) {
    if(!post_.isObject()) return dynamic ? u"M4"_s : u"M3"_s;
    return callLines(u"laserOn"_s, makeCtx(ctx), QJSValue{dynamic}).join(u'\n');
}

QString Post::laserOff(const Context& ctx) {
    if(!post_.isObject()) return u"M5"_s;
    return callLines(u"laserOff"_s, makeCtx(ctx)).join(u'\n');
}

std::optional<QString> Post::dwell(const Context& ctx, double seconds) {
    // Только функция: строка-константа паузе ни к чему -- время у неё своё
    // на каждый вызов.
    if(!post_.isObject() || !post_.property(u"dwell"_s).isCallable()) return std::nullopt;
    return callLines(u"dwell"_s, makeCtx(ctx), QJSValue{seconds}).join(u'\n');
}

QString Post::message(const QString& text) {
    // Только функция, как у dwell: текст у сообщения свой на каждую смену.
    if(post_.isObject())
        if(QJSValue fn = post_.property(u"message"_s); fn.isCallable()) {
            const QJSValue line = fn.callWithInstance(post_, {text});
            if(!line.isError()) return line.toString();
            qWarning() << "GCode post" << id_ << "message error line"
                       << line.property(u"lineNumber"_s).toInt() << ":" << line.toString();
        }
    return comment(text);
}

QStringList Post::toolChange(const Context& ctx) {
    // Своё поле -- как написано, даже пустое: пустой массив значит «смену
    // не писать вовсе», это выбор поста.
    if(post_.isObject() && !post_.property(u"toolChange"_s).isUndefined())
        return callLines(u"toolChange"_s, makeCtx(ctx));
    return {spindleOff(ctx), ctx.message, u"T%1 M6"_s.arg(ctx.toolNumber), spindleOn(ctx)};
}

QString Post::renderText(const std::vector<QString>& lines) const {
    QString str;
    int n = lineNumbers_.start;
    for(const QString& line: lines) {
        if(line.isEmpty()) continue;
        // Строки, которые нумеровать нельзя: '%', комментарии и уже
        // пронумерованные постом (N99999 у Heidenhain).
        const bool numbered = line.size() > 1 && line[0].toUpper() == u'N' && line[1].isDigit();
        if(lineNumbers_.enabled && !line.startsWith(u'%') && !numbered && !isComment(line)) {
            str += u'N' + QString::number(n) + u' ';
            n += lineNumbers_.step;
        }
        str += line;
        if(!str.endsWith(u'\n')) str += u'\n';
    }
    return str;
}

Post& Settings::post() {
    if(!post_) {
        post_ = std::make_unique<Post>();
        post_->load(postId_);
    } else if(post_->id() != postId_)
        post_->load(postId_);
    else
        post_->reloadIfChanged();
    return *post_;
}

} // namespace GCode
