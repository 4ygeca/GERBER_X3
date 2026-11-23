// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
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
#include "gc_jsproxy.h"
#include "gc_file.h"

#include "app.h"
#include "project.h"

#include "serial.h"
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <cmath>
#include <vector>

namespace GCode {

GcFileProxy::GcFileProxy(File* file, QJSEngine* engine, QObject* parent)
    : QObject{parent}
    , file_{file}
    , engine_{engine} {
}

QJSValue GcFileProxy::toolObject() const {
    // Tool::write обходит поля рефлексией (см. tool.cpp), ключи -- имена самих
    // членов. Наружу уходит снимок: писать в него из скрипта смысла нет.
    QJsonObject json;
    file_->gcp.tool().write(json);
    return engine_->toScriptValue(json.toVariantMap());
}

bool GcFileProxy::laser() const { return file_->toolType == Tool::Laser; }
bool GcFileProxy::spiralRamp() const { return file_->gcp.spiralRamp(); }
double GcFileProxy::toolDiameter() const { return file_->gcp.getToolDiameter(); }
double GcFileProxy::toolLength() const { return file_->gcp.tool().lenght(); }
double GcFileProxy::toolOneTurnCut() const { return file_->gcp.tool().oneTurnCut(); }
double GcFileProxy::feedRate() const { return file_->feedRate; }
double GcFileProxy::plungeRate() const { return file_->plungeRate; }
int GcFileProxy::spindleSpeed() const { return file_->spindleSpeed; }
QString GcFileProxy::strFeed() const { return file_->strFeed; }
QString GcFileProxy::strPlungeFeed() const { return file_->strPlungeFeed; }
QString GcFileProxy::strSpindle() const { return file_->strSpindle; }
bool GcFileProxy::inside() const { return !file_->gcp.params.contains(GCode::Params::Side) || file_->gcp.side() != GCode::Outer; }
bool GcFileProxy::climb() const { return !file_->gcp.params.contains(GCode::Params::Convent) || !file_->gcp.convent(); }
bool GcFileProxy::leftHand() const { return file_->gcp.leftHand(); }
double GcFileProxy::threadPitch() const { return file_->gcp.tool().passDepth(); }
double GcFileProxy::threadHoleDiam() const { return file_->gcp.tool().holeDiam(); }
bool GcFileProxy::circle() const { return file_->gcp.circle(); }
bool GcFileProxy::chamfer() const { return file_->gcp.chamfer(); }
int GcFileProxy::starts() const { return file_->gcp.starts(); }
QJSValue GcFileProxy::properties() const { return propertiesValue(*engine_); }
bool GcFileProxy::notTile() const { return file_->gcp.params.contains(Params::NotTile); }
QString GcFileProxy::name() const { return file_->shortName(); }
QString GcFileProxy::programName() const { return file_->programName_; }
bool GcFileProxy::slotPaths() const { return file_->gcp.slotPaths(); }

QJSValue GcFileProxy::cycles() const {
    // Пост живёт на своём движке: объект собирается заново на движке скрипта.
    const auto& cycles = file_->currentPost().cycles();
    if(!cycles) return QJSValue{QJSValue::UndefinedValue};
    QJSValue obj = engine_->newObject();
    auto setCode = [&obj](const QString& key, const QString& code) {
        if(!code.isEmpty()) obj.setProperty(key, code);
    };
    setCode(u"drill"_s, cycles->drill);
    setCode(u"dwell"_s, cycles->dwell);
    setCode(u"peck"_s, cycles->peck);
    setCode(u"chipBreak"_s, cycles->chipBreak);
    setCode(u"cancel"_s, cycles->cancel);
    // Пустой код отвода -- пустая строка, а не дыра: скрипт проверяет его так
    // же, как код цикла.
    QJSValue retract = engine_->newObject();
    retract.setProperty(u"initial"_s, cycles->retractInitial);
    retract.setProperty(u"r"_s, cycles->retractR);
    obj.setProperty(u"retract"_s, retract);
    if(cycles->peckRefeed) obj.setProperty(u"peckRefeed"_s, true);
    return obj;
}

QString GcFileProxy::dwellUnit() const {
    return file_->currentPost().dwellUnit() == Post::DwellUnit::Milliseconds ? u"ms"_s : u"s"_s;
}

namespace {

// Serial пишет QPointF массивом [x, y], QRectF -- [x, y, w, h]. Скрипту
// удобнее объекты с именованными полями.
QVariant asPoint(const QVariant& v) {
    const auto list = v.toList();
    if(list.size() != 2) return v;
    return QVariantMap{
        {u"x"_s, list[0]},
        {u"y"_s, list[1]}
    };
}
QVariant asRect(const QVariant& v) {
    const auto list = v.toList();
    if(list.size() != 4) return v;
    return QVariantMap{
        {u"x"_s,      list[0]},
        {u"y"_s,      list[1]},
        {u"width"_s,  list[2]},
        {u"height"_s, list[3]}
    };
}

} // namespace

QJSValue GcFileProxy::propertiesValue(QJSEngine& engine) {
    Serial::Writer sb;
    Serial::write(sb, App::project().header());
    std::string_view view;
    if(auto err = sb.view().get(view); err) {
        qWarning() << "GCode JS: project header:" << simdjson::error_message(err);
        return engine.newObject();
    }
    QVariantMap map = QJsonDocument::fromJson(QByteArray{view.data(), static_cast<qsizetype>(view.size())}).object().toVariantMap();
    for(const QString& key: {u"home"_s, u"zero"_s})
        if(map.contains(key)) map[key] = asPoint(map[key]);
    for(const QString& key: {u"workRect"_s, u"viewRect"_s})
        if(map.contains(key)) map[key] = asRect(map[key]);
    if(map.contains(u"pins"_s)) {
        QVariantList pins = map[u"pins"_s].toList();
        for(QVariant& pin: pins) pin = asPoint(pin);
        map[u"pins"_s] = pins;
    }
    return engine.toScriptValue(map);
}
double GcFileProxy::zVal() const { return file_->z_; }
void GcFileProxy::setZ(double z) { file_->z_ = z; }

namespace {

// Сборщик массивов путей на стороне JS: тот же вид, что собирал C++ --
// массив групп, в группе массивы вершин с полями closed и perimeter, вершина
// {x, y, bulge, type, cx, cy}, -- но из одного буфера чисел. Свойство за
// свойством из C++ (QJSValue::setProperty) это было ~10 млн вызовов на растр
// HLDI всей платы и 16 секунд; здесь объекты строит сам движок. Числа идут
// как есть, double в double: значения те же до бита, включая -0.
// Раскладка буфера: число групп; на группу -- число путей; на путь -- число
// вершин, closed (0/1), perimeter и по шесть чисел на вершину.
constexpr auto gcJsPathsBuilder = uR"((function (buffer) {
    var d = new Float64Array(buffer);
    var p = 0;
    var pathss = new Array(d[p++]);
    for (var i = 0; i < pathss.length; i++) {
        var paths = new Array(d[p++]);
        for (var j = 0; j < paths.length; j++) {
            var curve = new Array(d[p++]);
            curve.closed = d[p++] !== 0;
            curve.perimeter = d[p++];
            for (var k = 0; k < curve.length; k++, p += 6)
                curve[k] = { x: d[p], y: d[p + 1], bulge: d[p + 2], type: d[p + 3], cx: d[p + 4], cy: d[p + 5] };
            paths[j] = curve;
        }
        pathss[i] = paths;
    }
    return pathss;
}))"_sv;

} // namespace

QJSValue GcFileProxy::getToolPaths(double ox, double oy) {
    offset_ = {ox, oy};
    cachedPathss_ = file_->mirrorAndOffsetCurves(offset_);

    if(!pathsBuilder_.isCallable()) {
        pathsBuilder_ = engine_->evaluate(gcJsPathsBuilder.toString());
        if(!pathsBuilder_.isCallable()) {
            qWarning() << "GCode JS: paths builder:" << pathsBuilder_.toString();
            return toolPathsValue();
        }
    }

    std::size_t size = 1;
    for(const Geo::Polylines& paths: cachedPathss_) {
        size += 1;
        for(const Geo::Polyline& curve: paths) size += 3 + 6 * curve.size();
    }
    std::vector<double> data;
    data.reserve(size);
    data.push_back(static_cast<double>(cachedPathss_.size()));
    for(const Geo::Polylines& paths: cachedPathss_) {
        data.push_back(static_cast<double>(paths.size()));
        for(const Geo::Polyline& curve: paths) {
            data.append_range(std::array{static_cast<double>(curve.size()), curve.isClosed() ? 1.0 : 0.0, curve.perimeter()});
            for(std::size_t k{}; k < curve.size(); ++k) {
                const Geo::Vertex& v = curve[k];
                // type/cx/cy описывают сегмент, ПРИХОДЯЩИЙ в вершину, -- см.
                // toolPathsValue.
                auto arc = k ? Geo::arcOf(curve[k - 1], curve[k], curve[k - 1].bulge) : std::nullopt;
                data.append_range(std::array{v.x(), v.y(), v.bulge, arc ? static_cast<double>(int(arc->dir())) : 0.0,
                    arc ? arc->center.x() : 0.0, arc ? arc->center.y() : 0.0});
            }
        }
    }
    const QByteArray buffer{reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size() * sizeof(double))};
    QJSValue pathss = pathsBuilder_.call({engine_->toScriptValue(buffer)});
    // Буфер обязан прийти в движок ArrayBuffer'ом; что бы ни пошло не так,
    // прежний путь медленнее, но верен.
    if(pathss.isError() || !pathss.isArray() || pathss.property(u"length"_s).toUInt() != cachedPathss_.size()) {
        qWarning() << "GCode JS: paths builder:" << pathss.toString();
        return toolPathsValue();
    }
    return pathss;
}

QJSValue GcFileProxy::toolPathsValue() const {
    QJSValue outerArr = engine_->newArray(static_cast<uint>(cachedPathss_.size()));
    for(uint i{}; i < cachedPathss_.size(); ++i) {
        const Geo::Polylines& paths = cachedPathss_[i];
        QJSValue pathsArr = engine_->newArray(static_cast<uint>(paths.size()));
        for(uint j{}; j < paths.size(); ++j) {
            const Geo::Polyline& curve = paths[j];
            QJSValue curveArr = engine_->newArray(static_cast<uint>(curve.size()));
            curveArr.setProperty(u"closed"_s, curve.isClosed());
            curveArr.setProperty(u"perimeter"_s, curve.perimeter());
            for(uint k{}; k < curve.size(); ++k) {
                const Geo::Vertex& v = curve[k];
                QJSValue vtx = engine_->newObject();
                vtx.setProperty(u"x"_s, v.x());
                vtx.setProperty(u"y"_s, v.y());
                vtx.setProperty(u"bulge"_s, v.bulge);
                // type/cx/cy описывают сегмент, ПРИХОДЯЩИЙ в вершину -- так их
                // и видели скрипты, когда дуга хранилась центром на конечной
                // вершине; менять контракт из-за смены внутреннего вида нельзя
                auto arc = k ? Geo::arcOf(curve[k - 1], curve[k], curve[k - 1].bulge) : std::nullopt;
                vtx.setProperty(u"type"_s, arc ? int(arc->dir()) : 0);
                vtx.setProperty(u"cx"_s, arc ? arc->center.x() : 0.0);
                vtx.setProperty(u"cy"_s, arc ? arc->center.y() : 0.0);
                curveArr.setProperty(k, vtx);
            }
            pathsArr.setProperty(j, curveArr);
        }
        outerArr.setProperty(i, pathsArr);
    }
    return outerArr;
}

QJSValue GcFileProxy::getDepths() {
    auto depths = file_->getDepths();
    QJSValue arr = engine_->newArray(static_cast<uint>(depths.size()));
    for(uint i{}; i < depths.size(); ++i)
        arr.setProperty(i, depths[i]);
    return arr;
}

void GcFileProxy::startPath(double x, double y) {
    file_->startPath({x, y});
}

void GcFileProxy::endPath() {
    file_->endPath();
}

void GcFileProxy::addLine(const QString& line) {
    file_->lines_.emplace_back(line);
}

QString GcFileProxy::comment(const QString& text) { return File::comment(text); }
QString GcFileProxy::spindleOn() { return App::gcSettings().post().spindleOn(file_->postContext()); }
QString GcFileProxy::spindleOff() { return App::gcSettings().post().spindleOff(file_->postContext()); }
QString GcFileProxy::laserOn(bool dynamic) { return App::gcSettings().post().laserOn(file_->postContext(), dynamic); }
QString GcFileProxy::laserOff() { return App::gcSettings().post().laserOff(file_->postContext()); }

QString GcFileProxy::dwell(double seconds) {
    if(auto text = file_->currentPost().dwell(file_->postContext(), seconds)) return *text;
    return u"G4 "_s + fmtDwell(seconds);
}

QJSValue GcFileProxy::savePathLines(int pi, int ci, bool rev, double perimeter, double depth) {
    if(pi < 0 || pi >= static_cast<int>(cachedPathss_.size())) return {};
    const Geo::Polylines& paths = cachedPathss_[static_cast<size_t>(pi)];
    if(ci < 0 || ci >= static_cast<int>(paths.size())) return {};

    const Geo::Polyline& orig = paths[static_cast<size_t>(ci)];
    return linesFor(rev ? orig.reversed() : orig, perimeter, depth);
}

QJSValue GcFileProxy::linesFor(const Geo::Polyline& curve, double perimeter, double depth) {
    auto lines = file_->savePath(curve, perimeter, depth);
    QJSValue arr = engine_->newArray(static_cast<uint>(lines.size()));
    for(uint i{}; i < lines.size(); ++i)
        arr.setProperty(i, lines[i]);
    return arr;
}

int GcFileProxy::addPassLines(int pi, const QJSValue& moveWords) {
    if(pi < 0 || pi >= static_cast<int>(cachedPathss_.size())) return 0;
    const Geo::Polylines& paths = cachedPathss_[static_cast<size_t>(pi)];

    // Слова хода -- заготовка строки; места "X" и "Y" на каждой вершине
    // занимают её координаты.
    std::vector<QString> data;
    std::vector<std::size_t> xAt, yAt;
    const uint count = moveWords.property(u"length"_s).toUInt();
    data.reserve(count);
    for(uint k{}; k < count; ++k) {
        QString word = moveWords.property(k).toString();
        if(word == u"X"_s) xAt.push_back(data.size());
        if(word == u"Y"_s) yAt.push_back(data.size());
        data.push_back(std::move(word));
    }

    // Код хода. Скрипт, пишущий ходы сам, зовёт file.g1() на каждую строку
    // хода, и форматтер всякий раз видит перед ней G1; здесь слова собраны
    // один раз, а строки засветки между ходами код меняют (savePath) --
    // поэтому перед каждой строкой хода код возвращается тот, что поставили
    // слова. Строк без слова G здесь нет, так что на сам текст это не
    // влияет, но состояние форматтера после прохода -- то же, что у скрипта.
    const Code moveCode = file_->gCode_;
    const std::size_t before = file_->lines_.size();
    for(std::size_t j{}; j < paths.size(); ++j) {
        const Geo::Polyline& path = paths[j];
        if(j % 2) { // засветка -- как savePathLines(pi, j, false, 0, 0) и addLine каждой строки
            for(QString& line: file_->savePath(path, 0.0, 0.0)) file_->lines_.emplace_back(std::move(line));
            continue;
        }
        if(path.empty()) continue;
        // Ход: строка на вершину, сдвигающую головку в выводе -- сравнение по
        // тексту X и Y, как у laserOffPath в pocketraster.js.
        QString last = fmtX(path.front().x()) + fmtY(path.front().y());
        for(std::size_t k = 1; k < path.size(); ++k) {
            const QString x = fmtX(path[k].x()), y = fmtY(path[k].y());
            if(x + y == last) continue;
            last = x + y;
            for(std::size_t w: xAt) data[w] = x;
            for(std::size_t w: yAt) data[w] = y;
            file_->gCode_ = moveCode;
            file_->lines_.emplace_back(file_->formatedText(data));
        }
    }
    return static_cast<int>(file_->lines_.size() - before);
}

QString GcFileProxy::formatted(const QJSValue& parts) {
    std::vector<QString> data;
    const uint len = parts.property(u"length"_s).toUInt();
    data.reserve(len);
    for(uint i{}; i < len; ++i)
        data.emplace_back(parts.property(i).toString());
    return file_->formatedText(data);
}

void GcFileProxy::forceWords(const QString& letters) { file_->forceWords(letters); }

QString GcFileProxy::g0() { return file_->g0(); }
QString GcFileProxy::g1() { return file_->g1(); }
QString GcFileProxy::g2() { return file_->g2(); }
QString GcFileProxy::g3() { return file_->g3(); }

QString GcFileProxy::fmtX(double v) { return File::x(v); }
QString GcFileProxy::fmtY(double v) { return File::y(v); }
QString GcFileProxy::fmtZ(double v) { return File::z(v); }
QString GcFileProxy::fmtI(double v) { return File::i(v); }
QString GcFileProxy::fmtJ(double v) { return File::j(v); }
QString GcFileProxy::fmtS(int v) { return File::speed(static_cast<double>(v)); }
QString GcFileProxy::fmtF(double v) { return u'F' + File::formatValue(v); }
QString GcFileProxy::fmtR(double v) { return File::rad(v); }
QString GcFileProxy::fmtQ(double v) { return File::q(v); }
QString GcFileProxy::fmtP(double v) { return u'P' + File::formatValue(v); }

QString GcFileProxy::fmtDwell(double seconds) {
    // Миллисекунды -- целым и без точки: у Fanuc и Haas P с точкой -- уже
    // секунды, а без неё -- миллисекунды.
    if(file_->currentPost().dwellUnit() == Post::DwellUnit::Milliseconds)
        return u'P' + QString::number(std::llround(seconds * 1000.0));
    return fmtP(seconds);
}

} // namespace GCode
