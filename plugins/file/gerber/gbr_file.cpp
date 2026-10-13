// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  March 25, 2023                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2023                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 *******************************************************************************/
#include "gbr_file.h"
#include "gbr_attributes.h"
#include "gbr_node.h"
#include "gbrcomp_item.h"
#include "gbrcomp_onent.h"
#include "geo/boolean.h"
#include "geo/util.h"
#include "gi_datapath.h"
#include "gi_datasolid.h"
#include "graphicsview.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <forward_list>
#include <gi_dbg.h>
#include <optional>
#include <qglobal.h>
#include <utility>

namespace Gerber {

template <typename T>
struct EnumToStr {
    T e;
    template <auto>
    static constexpr std::string_view name() {
        return {__PRETTY_FUNCTION__};
    }

    static constexpr auto text = []<T... Ts> {
        return std::array{name<Ts>()...};
    }(std::integer_sequence<T, T{100}>{});
    constexpr operator std::string_view() const {
        return std::to_underlying(e) < text.size() ? text[std::to_underlying(e)] : std::string_view{};
    }
};

QDebug operator<<(QDebug debug, const State& state) {
    QDebugStateSaver saver{debug};
    debug.nospace() << u"State("_s
                    << u"D0"_s << state.dCode() << u", "_s
                    << u"G0"_s << state.gCode() << u", "_s
                    << u"Positive|Negative"_s.split(u'|').at(state.imgPolarity()) << u", "_s
                    << u"Linear|ClockwiseCircular|CounterClockwiseCircular"_s.split(u'|').at(state.interpolation() - 1) << u", "_s
                    << u"Aperture|Line|Region"_s.split(u'|').at(state.type()) << u", "_s
                    << u"Undef|Single|Multi"_s.split(u'|').at(state.quadrant()) << u", "_s
                    << u"Off|On"_s.split(u'|').at(state.region()) << u", "_s
                    << u"NoMirroring|X_Mirroring|Y_Mirroring|XY_Mirroring"_s.split(u'|').at(state.mirroring()) << u", "_s
                    << u"aperture"_s << state.aperture() << u", "_s
                    << state.curPos() << u", "_s
                    << u"scaling"_s << state.scaling() << u", "_s
                    << u"rotating"_s << state.rotating() << u", "_s
                    << ')';
    return debug;
}

namespace {

// X2-функции меди (Gerber X2, 5.6.10): площадки, проводники, заливки. Прочее
// -- графика на меди (NonConductor: текст и рисунки KiCad), сверловка,
// контуры компонентов и платы -- компенсации не подлежит.
bool copperFunction(int32_t function) {
    using A = Attr::Aperture;
    switch(function) {
    case A::ComponentPad:
    case A::SMDPad:
    case A::BGAPad:
    case A::ConnectorPad:
    case A::HeatsinkPad:
    case A::ViaPad:
    case A::TestPad:
    case A::CastellatedPad:
    case A::FiducialPad:
    case A::ReliefPad:
    case A::WasherPad:
    case A::AntiPad:
    case A::OtherPad:
    case A::Conductor:
    case A::EtchedComponent:
    case A::CopperBalancing:
    case A::Border:
    case A::OtherCopper    : return true;
    default                : return false;
    }
}

// Точка заведомо внутри тела объекта, а не на его контуре: центр вспышки,
// точки осевой трассы и середины её звеньев; иначе (регион, кольцо) -- сетка
// по габариту тела. Нужна там, где связь «объект -> слитый полигон» потеряна
// слиянием и восстанавливается по месту (File::copperNets, File::objectKeys).
std::optional<QPointF> interiorPoint(const GrObject& go) {
    std::vector<QPointF> pts;
    if(!std::isnan(go.pos.x())) pts.push_back(go.pos);
    if(!go.path.isClosed())
        for(size_t i{}; i < go.path.size(); ++i) {
            pts.push_back(go.path[i]);
            if(i + 1 < go.path.size()) pts.push_back((go.path[i] + go.path[i + 1]) / 2);
        }
    // Polygons::contains локализует точку во всём точном множестве и на
    // тысячах объектов стоит минуты -- спрашиваем тела по одному.
    const auto& bodies = go.fill.all();
    const auto inBody = [&](QPointF pt) { return r::any_of(bodies, [pt](const Geo::Polygon& p) { return p.contains(pt); }); };
    for(const QPointF& pt: pts)
        if(inBody(pt)) return pt;
    for(const Geo::Polygon& body: bodies) {
        const QRectF box = body.boundingRect();
        constexpr int grid = 7;
        for(int y = 1; y < grid; ++y)
            for(int x = 1; x < grid; ++x)
                if(const QPointF pt{box.left() + box.width() * x / grid, box.top() + box.height() * y / grid}; body.contains(pt))
                    return pt;
    }
    return std::nullopt;
}

} // namespace

int64_t EtchComp::objectKey(const GrObject& go) {
    // FNV-1a по 64-битным словам. Микроны -- от шума double: координаты из
    // файла от разбора к разбору и так одни и те же, а вот вывод их через
    // проект туда и обратно может разойтись в последнем знаке.
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](uint64_t value) {
        for(int i{}; i < 8; ++i, value >>= 8) {
            hash ^= value & 0xFF;
            hash *= 1099511628211ull;
        }
    };
    const auto um = [](double value) { return static_cast<uint64_t>(std::llround(value * 1000.0)); };

    mix(go.state.type());
    mix(go.state.imgPolarity());
    // D-код у области случаен (см. keyOf), её геометрию целиком задаёт контур.
    if(go.state.type() != Region) {
        // Имя апертуры -- с размерами (C(Ø0.25), R(1 x 2)); у макроса только
        // имя макроса, и два макроса одного имени в одной точке совпадут --
        // такого CAD не выгружает. minSize не годится: его считает ленивая
        // отрисовка апертуры, у штриховой до неё дело может и не дойти.
        if(const File* file = go.gFile) {
            const ApertureMap& apertures = *file->apertures();
            if(const auto it = apertures.find(go.state.aperture()); it != apertures.end() && it->second) {
                for(const QChar c: it->second->name()) mix(c.unicode());
                mix(um(it->second->drillDiameter()));
            }
        }
        mix(go.state.mirroring());
        mix(um(go.state.scaling()));
        mix(um(go.state.rotating()));
    }
    if(!std::isnan(go.pos.x())) {
        mix(um(go.pos.x()));
        mix(um(go.pos.y()));
    }
    for(const Geo::Vertex& vertex: go.path) {
        mix(um(vertex.x()));
        mix(um(vertex.y()));
        mix(um(vertex.bulge));
    }
    return static_cast<int64_t>(hash);
}

bool EtchComp::isExcluded(const GrObject& go) const {
    return !excluded.empty() && r::binary_search(excluded, objectKey(go));
}

bool EtchComp::copper(const GrObject& go, bool x2) const {
    // Исключённый объект -- сильнее выбора класса: ради текста на общих с
    // трассами апертурах исключение и заведено.
    if(isExcluded(go))
        return false;
    if(auto it = overrides.find(keyOf(go)); it != overrides.end())
        return it->second;
    if(go.state.aperFunction() >= 0)
        return copperFunction(go.state.aperFunction());
    // Цепь у выреза не в счёт: метки сверловки KiCad -- %LPC без функции, и
    // %TO.N у них остаётся от предыдущей площадки.
    if(go.state.imgPolarity() == Negative) {
        switch(clearMode) {
        case ExactClear : return false;
        case ShrinkClear: return true;
        default         : return !x2;
        }
    }
    if(go.state.net() >= 0)
        return true;
    return plainIsCopper;
}

File::File()
    : AbstractFile() {
    itemGroups_.append_range(std::array{new Gi::Group, new Gi::Group});
    layerTypes_ = {
        {Normal,     GbrObj::tr("Normal"),         GbrObj::tr("Normal view")                                                               },
        {ApPaths,    GbrObj::tr("Aperture paths"), GbrObj::tr("Displays only aperture paths of copper\nwithout width and without contacts")},
        {Components, GbrObj::tr("Components"),     GbrObj::tr("Show components")                                                           }
    };
}

File::~File() { }

const ApertureMap* File::apertures() const { return &apertures_; }

std::vector<GraphicObject> File::getDataForGC(std::span<Criteria> criterias, GCType gcType, bool test) const {
    std::vector<GraphicObject> retData;
    auto t = transform_.toQTransform(); // cached  QTransform
    // Компенсация подтрава и здесь, пообъектно: термобарьеры берут тела
    // площадок отсюда, а не из слитого слоя. Отбор (площадь) -- по исходному
    // размеру; сверловке раздувать нечего, она смотрит на размер апертуры.
    const bool etch = etch_.active() && gcType != GCType::Drill;
    const bool x2 = etch && hasX2();
    for(auto&& criterion: criterias) {
        for(const GrObject& go: graphicObjects_) {
            auto transformedGo = go * t; // return copy
            if(criterion.test(transformedGo)) {
                auto& g = retData.emplace_back(transformedGo);
                if(test)
                    return retData;

                if(etch && go.state.imgPolarity() == Positive && etch_.copper(go, x2))
                    g.fill = Geo::transformed(Geo::Inflate(go.fill, 2.0 * etch_.offset), t);

                switch(gcType) {
                case GCType::Drill: {
                    double drillDiameter{};
                    auto& ap = *apertures_.at(go.state.aperture());

                    auto name{ap.name()};
                    if(ap.withHole())
                        drillDiameter = ap.drillDiameter();
                    else
                        drillDiameter = ap.minSize();
                    drillDiameter *= std::min(transform_.scale.x(), transform_.scale.y());
                    name += QObject::tr(", drill Ø%1mm").arg(drillDiameter);
                    g.raw = drillDiameter;
                } break;
                default: break;
                }
            }
        }
    }

    return retData;
}

Geo::Polygons File::merge() const {
    // Компенсация подтрава -- своё слияние. Без неё путь прежний, и загрузка
    // стоит столько же, сколько стоила.
    if(etch_.active())
        return mergedCurves_ = mergeCompensated();

    Timer t;

    // Слой -- последовательность вспышек: положительные добавляют медь,
    // отрицательные её вычитают. Идущие подряд с одинаковой полярностью
    // собираются в одну пачку -- по булевой операции на пачку, а не на вспышку.
    constexpr auto samePolarity = +[](const GrObject& l, const GrObject& r) {
        return l.state.imgPolarity() == r.state.imgPolarity();
    };

#if 0 // FIXME
  auto gbgImage = [](QPainterPath&& path, QString&& out, bool hole) {
        QRectF box = path.boundingRect();
        if(box.isEmpty()) {
            qWarning() << "empty path";
            return;
        }
        const double margin = std::max(box.width(), box.height()) * 0.05;
        box.adjust(-margin, -margin, margin, margin);

        const int side     = qEnvironmentVariableIntValue("DUMP_SIDE") ?: 1200;
        const double scale = side / std::max(box.width(), box.height());

        QImage image{QSize(int(box.width() * scale), int(box.height() * scale)), QImage::Format_RGB32};
        image.fill(Qt::white);
        QPainter painter{&image};
        painter.setRenderHint(QPainter::Antialiasing);
        // Gerber Y goes up, raster Y goes down.
        painter.translate(0, image.height());
        painter.scale(scale, -scale);
        painter.translate(-box.left(), -box.top());
        painter.setPen(Qt::NoPen);
        painter.setBrush(hole ? QColor(0, 0, 120) : QColor(0, 120, 0));
        painter.drawPath(path);
        painter.end();

        out = out + u".png"_s;
        image.save(out);
    };
#endif

    mergedCurves_ = {};
    for(int i{}; auto&& gObjects: v::chunk_by(graphicObjects_, samePolarity)) {
        // Пачка собирается ГОТОВЫМИ ПОЛИГОНАМИ, а не плоским списком контуров:
        // union всё так же идёт разом, в несколько потоков и с пространственной
        // сортировкой (вспышек в слое десятки тысяч, и цепочка из стольких же
        // точных операций была бы неподъёмной), но вложенность каждого тела
        // остаётся при нём.
        //
        // Плоский список её терял: там вложенность выражена одной ориентацией
        // контура, и дырка одного объекта вычитала чужой объект, лежащий внутри
        // неё. Кольцевой штрих с окружностями внутри (репер на плате) так и
        // пропадал целиком -- дырка внешнего кольца съедала оба вложенных.

#if 0 // FIXME
        std::vector<Geo::Polygon> parts;
        for(int j{}; const GrObject& go: gObjects) {
            gbgImage(go.fill.toPath(), u"%1_3_go%2"_s.arg(i).arg(j), gObjects.front().state.imgPolarity());
            if(qEnvironmentVariableIsSet("GBR_DEBUG_MERGE")) {
                std::size_t holes{};
                for(const Geo::Polygon& p: go.fill.all()) holes += p.holes().size();
                qInfo().noquote() << u"%1_3_go%2"_s.arg(i).arg(j) << go.name
                                  << u"id"_s << go.id
                                  << u"ap"_s << go.state.aperture()
                                  << u"pol"_s << go.state.imgPolarity()
                                  << u"pos"_s << go.state.curPos()
                                  << u"n"_s << go.fill.size()
                                  << u"holes"_s << holes
                                  << u"bbox"_s << go.fill.boundingRect()
                                  << u"path.size"_s << go.path.size()
                                  << u"path.closed"_s << go.path.closed
                                  << u"path.width"_s << go.path.width;
                if(qEnvironmentVariableIsSet("GBR_DEBUG_MERGE_PATH")) {
                    QStringList sel = QString::fromLocal8Bit(qgetenv("GBR_DEBUG_MERGE_PATH")).split(u',');
                    if(sel.contains(u"%1_3_go%2"_s.arg(i).arg(j)))
                        for(const Geo::Vertex& v: go.path)
                            qInfo().noquote() << u"   "_s << v.x() << v.y() << u"b"_s << v.bulge;
                }
            }
            ++j;
            // if((i < 2) | go.fill.all().front().holes().size())
            parts.append_range(go.fill.all());
        }

        const Geo::Polygons part{std::span<const Geo::Polygon>{parts}};
#else
        // std::vector<Geo::Polygon> parts;
        // for(const GrObject& go: gObjects)
        //     parts.append_range(go.fill.all());

        std::vector parts{std::from_range,
            gObjects
                | v::transform(&GrObject::fill)
                | v::transform(&Geo::Polygons::all)
                | v::join};

#endif
        QElapsedTimer chunkTimer;
        if(qEnvironmentVariableIsSet("GBR_DEBUG_MERGE_TIME")) chunkTimer.start();

        const Geo::Polygons part{std::span<const Geo::Polygon>{parts}};

        if(qEnvironmentVariableIsSet("GBR_DEBUG_MERGE_TIME"))
            qInfo() << "chunk" << i << "objs" << gObjects.size() << "parts" << parts.size()
                    << "buildPart(ms)" << chunkTimer.restart();

        // gbgImage(mergedCurves_.toPath(), u"%1_0_merged"_s.arg(i), gObjects.front().state.imgPolarity());

        // gbgImage(part.toPath(), u"%1_1_part"_s.arg(i), gObjects.front().state.imgPolarity());

        if(gObjects.front().state.imgPolarity() == Positive)
            mergedCurves_ |= part;
        else
            mergedCurves_ -= part;

        if(qEnvironmentVariableIsSet("GBR_DEBUG_MERGE_TIME"))
            qInfo() << "chunk" << i << "union(ms)" << chunkTimer.elapsed();

        // gbgImage(mergedCurves_.toPath(), u"%1_2_merged"_s.arg(i), gObjects.front().state.imgPolarity());

        ++i;
    }

    return mergedCurves_;
}

// Компенсация -- на ГОТОВОМ изображении слоя, а не пообъектно: раздувание
// дистрибутивно над объединением, но не над разностью -- площадка, целиком
// накрытая поздним %LPC и раздутая порознь, вылезла бы из-под него обратно.
// Поэтому медь копится как есть и раздувается разом -- перед ТОЧНЫМ вырезом
// (метки сверловки KiCad) и в конце. Вырез меди (пустота -- зазор заливки
// старого образца) вычитается из неё ДО раздувания и сужается вместе с ней;
// уже раздутое (до точного выреза) он режет без сужения -- редкость, и это
// приближение. Графика на меди не раздувается вовсе.
Geo::Polygons File::mergeCompensated() const {
    Timer t;

    const bool x2 = hasX2();
    const double delta = 2.0 * etch_.offset; // у Geo::Inflate delta -- ПОЛНАЯ ширина
    const size_t count = graphicObjects_.size();

    std::vector<uint8_t> copper(count);
    for(size_t i{}; i < count; ++i)
        copper[i] = etch_.copper(graphicObjects_[i], x2);

    enum Kind {
        Dark,   // тёмное: медь раздувается, графика как есть
        Shrink, // вырез меди -- сужается вместе с ней
        Exact,  // точный вырез -- своего размера
    };
    auto kind = [&](size_t i) {
        if(graphicObjects_[i].state.imgPolarity() == Positive) return Dark;
        return copper[i] ? Shrink : Exact;
    };

    // Отверстия апертур (%ADD10C,1.6X0.8) -- метки под сверло, и раздувание
    // их сужает. С keepPadHoles они остаются своего размера, но лишь там, где
    // в исходном слое пусто: трасса, заходящая в центр площадки, дырку
    // накрывает, и резать её нельзя.
    Geo::Polylines holes;
    if(etch_.keepPadHoles) {
        for(size_t i{}; i < count; ++i) {
            const GrObject& go = graphicObjects_[i];
            if(go.state.type() != Aperture || kind(i) != Dark || !copper[i]) continue;
            if(auto it = apertures_.find(go.state.aperture()); it != apertures_.end() && it->second && it->second->withHole())
                holes.push_back(it->second->drawDrill(go.state));
        }
    }
    const bool keepHoles = !holes.empty();

    Geo::Polygons done;  // раздутая медь, уже обрезанная точными вырезами
    Geo::Polygons raw;   // то же без раздувания -- только для keepPadHoles
    Geo::Polygons cu;    // медь с последнего раздувания, ещё как есть
    Geo::Polygons other; // графика
    auto flush = [&] {
        if(cu.empty()) return;
        done |= Geo::Inflate(cu, delta);
        if(keepHoles) raw |= cu;
        cu = {};
    };

    // Пачки подряд идущих объектов одного рода -- по булевой операции на
    // пачку, тела собираются готовыми полигонами (см. merge).
    for(size_t i{}; i < count;) {
        const Kind k = kind(i);
        std::vector<Geo::Polygon> parts, otherParts;
        for(; i < count && kind(i) == k; ++i)
            (k == Dark && !copper[i] ? otherParts : parts).append_range(graphicObjects_[i].fill.all());
        const Geo::Polygons part{std::span<const Geo::Polygon>{parts}};

        switch(k) {
        case Dark:
            if(!part.empty()) cu |= part;
            if(!otherParts.empty()) other |= Geo::Polygons{std::span<const Geo::Polygon>{otherParts}};
            break;
        case Shrink:
            cu -= part;
            other -= part;
            done -= part;
            if(keepHoles) raw -= part;
            break;
        case Exact:
            flush();
            done -= part;
            other -= part;
            if(keepHoles) raw -= part;
            break;
        }
    }
    flush();

    if(keepHoles) {
        raw |= other;
        done -= Geo::Polygons{holes} - raw;
    }
    if(!other.empty()) done |= other;
    return done;
}

bool File::hasX2() const {
    return !nets_.empty() || r::any_of(graphicObjects_, [](const GrObject& go) { return go.state.aperFunction() >= 0; });
}

void File::setEtchComp(const EtchComp& etchComp) {
    if(etchComp == etch_) return;
    etch_ = etchComp;
    rebuild();
}

void File::rebuild() {
    // Слияние -- до сноса элементов: бросит -- на сцене останется прежний
    // слой, а недостроенный кэш не задержится и соберётся при обращении.
    try {
        mergedCurves_ = {};
        merge();
    } catch(...) {
        mergedCurves_ = {};
        throw;
    }
    groupedCurves_ = {};

    const bool visible = isVisible();
    // Высота над прочими файлами -- как была: выделенный в дереве файл поднят
    // (Node::setData), а addToScene опустил бы его на место.
    std::optional<double> z;
    for(const Gi::Group* group: itemGroups_)
        if(!z && !group->empty()) z = group->front()->zValue();
    for(Gi::Group* group: itemGroups_) {
        group->clear();
        // Цвет и перо группа раздаёт элементам лишь при СМЕНЕ (см.
        // Gi::Group::setBrushColor), а новые элементы о них не знают -- осевые
        // родятся белыми: без сброса setColor в createGi их бы не раскрасил.
        group->setBrushColor({});
        group->setPen({});
    }
    // Список дублей осевых: не очистить -- при skipDuplicates все осевые
    // сочтутся дублями прежних и пропадут.
    checkList.clear();

    createGi();
    addToScene();
    if(z)
        for(Gi::Group* group: itemGroups_) group->setZValue(*z);
    setTransform(transform_);
    setVisible(visible);
}

const QList<Comp::Component>& File::components() const { return components_; }

// Разбор вложенности вручную больше не нужен: Geo::Polygons и ЕСТЬ разобранный
// регион -- его собственные полигоны это медь, а полигоны того, что осталось от
// габаритной рамки после вычитания меди, -- вырезы. Прежде то же самое
// получалось объединением с рамкой по NonZero и обходом PolyTree.
Geo::Polygons& File::groupedPaths(File::Group group, bool /*fl*/) {
    if(!groupedCurves_.empty() && group_ == group)
        return groupedCurves_;

    group_ = group;
    const Geo::Polygons region = mergedCurves();

    if(group == CopperGroup) {
        groupedCurves_ = region;
    } else {
        // Поле рамки вокруг детали: миллиметр с каждой стороны, лишь бы вырезы
        // оказались внутри неё и отделились от бесконечности.
        constexpr double margin = 1.0;
        QRectF box = region.boundingRect();
        box.adjust(-margin, -margin, margin, margin);
        const Geo::Polygons frame{Geo::Polylines{Geo::rectangle(box.width(), box.height(), box.center())}};
        groupedCurves_ = frame - region;
    }

    return groupedCurves_;
}

bool File::flashedApertures() const {
    for(const auto& [_, aperture]: apertures_)
        if(aperture->flashed())
            return true;
    return false;
}

void File::setColor(const QColor& color) {
    color_ = color;
    itemGroups_[Normal]->setBrushColor(color_);
    itemGroups_[ApPaths]->setPen(QPen(color_, 0.0));
}

std::vector<const ::GraphicObject*> File::graphicObjects() const {
    std::vector<const ::GraphicObject*> go(graphicObjects_.size());
    size_t i{};
    for(auto& refGo: graphicObjects_)
        go[i++] = &refGo;
    return go;
}

void File::initFrom(AbstractFile* file_) {
    AbstractFile::initFrom(file_);
    static_cast<Node*>(node_)->file = this;
    // Перезагрузка с диска: компенсация подтрава -- настройка файла, как цвет
    // и трансформация, и переживает её. Разбор собрал слой без неё.
    if(auto* old = dynamic_cast<File*>(file_)) {
        etch_ = old->etch_;
        // Ручной выбор по D-коду переносится, только если под этим кодом всё та
        // же апертура: CAD при каждой выгрузке нумерует D10, D11... заново, и
        // чужой выбор молча лёг бы на другую апертуру (площадку вместо логотипа).
        // Ключи областей -- по функции X2, они от нумерации не зависят.
        std::erase_if(etch_.overrides, [old, this](const auto& entry) {
            const int32_t key = entry.first;
            if(key < 0) return false;
            const auto was = old->apertures_.find(key);
            const auto now = apertures_.find(key);
            return was == old->apertures_.end() || now == apertures_.end()
                || !was->second || !now->second || was->second->name() != now->second->name();
        });
        // Исключение -- по геометрии объекта: переживает перенумерацию, а с
        // объекта, который в CAD сдвинули или убрали, снимается само.
        if(!etch_.excluded.empty()) {
            std::vector<int64_t> keys{std::from_range, graphicObjects_ | v::transform(&EtchComp::objectKey)};
            r::sort(keys);
            std::erase_if(etch_.excluded, [&keys](int64_t key) { return !r::binary_search(keys, key); });
        }
        if(etch_.active()) {
            try {
                rebuild();
            } catch(...) {
                // Остаётся слой разбора -- без компенсации, и настройка с ним в лад.
                qWarning() << shortName() << u"-- etch compensation failed, reset to 0"_s;
                etch_.offset = 0.0;
            }
        }
    }
}

void File::postLoad() {
    for(GrObject& go: graphicObjects_) {
        go.gFile = this;
        go.state.file_ = this;
    }
    // Слой с компенсацией подтрава -- не из проекта, а слиянием заново.
    // Проект хранит слитый слой контурами в double, и при чтении каждый
    // контур заново проходит проверку точного домена (toGPoly). У раздутой
    // меди при малых зазорах дырки (зазоры между цепями) сходятся в точку
    // и касаются себя и соседей; после округления такой контур невалиден,
    // Polygon(outer, holes) молча его пропускает -- и дырка заливается
    // медью вместе со всеми островами в ней: после открытия проекта слой
    // сплошной. Сбой слияния -- остаётся прочитанное.
    if(etch_.active()) {
        Geo::Polygons merged = std::move(mergedCurves_);
        try {
            mergedCurves_ = {};
            merge();
            groupedCurves_ = {};
        } catch(...) {
            qWarning() << shortName() << u"-- etch compensation re-merge failed, the saved layer is used"_s;
            mergedCurves_ = std::move(merged);
        }
    }
    AbstractFile::postLoad();
}

FileTree::Node* File::node() {
    return node_ ? node_ : node_ = new Node{this};
}

QIcon File::icon() const {
    switch(itemsType_) {
    case File::ApPaths   : return decoration(color_, u'A');
    case File::Components: return decoration(color_, u'C');
    default              : return decoration(color_);
    }
}

void File::setItemType(int type) {
    if(itemsType_ == type)
        return;

    itemsType_ = type;

    itemGroups_[Normal]->setVisible(false);
    itemGroups_[ApPaths]->setVisible(false);
    itemGroups_[Components]->setVisible(false);

    itemGroups_[itemsType_]->setVisible(true /*visible_*/);
}

int File::itemsType() const { return itemsType_; }

// Слияние связь «объект -> полигон» теряет, поэтому цепь восстанавливается по
// месту: положительный объект целиком лежит в одном слитом полигоне (или съеден
// негативом), так что достаточно найти полигон, в котором лежит одна внутренняя
// точка объекта. contains строг -- граница снаружи, -- поэтому точка берётся
// заведомо внутри тела объекта, а не на его контуре.
std::vector<std::vector<int32_t>> File::copperNets(const Geo::Polygons& copper) const {
    if(nets_.empty()) return std::vector<std::vector<int32_t>>(copper.size());

    // Слитые полигоны велики (заливки -- тысячи дуг), и QPainterPath::contains
    // (кривые на каждый запрос) по ним стоит десятки секунд на плату. Отбор --
    // по ломаной, сплющенной один раз, а решает точный contains: у самого края
    // ломаная ошибается и записывала чужую цепь соседу, давая ложное
    // замыкание. Масштаб -- в микроны: Qt сплющивает с допуском порядка
    // единицы, в миллиметрах это полмиллиметра.
    constexpr double um = 1000.0;
    const QTransform toUm = QTransform::fromScale(um, um);
    // Контуры порознь: toPath кладёт внешний первым, дальше дырки. Одной
    // ломаной (toFillPolygon) нельзя -- швы между контурами сбивают счёт
    // обходов, и остров чужой цепи в дырке считался бы своим.
    std::vector<QList<QPolygonF>> paths{std::from_range,
        copper | v::transform([&](const Geo::Polygon& p) { return p.toPath().toSubpathPolygons(toUm); })};
    std::vector<QRectF> rects{std::from_range,
        paths | v::transform([](const QList<QPolygonF>& c) { return c.empty() ? QRectF{} : c.front().boundingRect(); })};
    const auto inside = [&](size_t k, QPointF pt) {
        const auto& c = paths[k];
        return rects[k].contains(pt) && c.front().containsPoint(pt, Qt::OddEvenFill)
            && r::none_of(c | v::drop(1), [pt](const QPolygonF& hole) { return hole.containsPoint(pt, Qt::OddEvenFill); });
    };
    std::vector<std::vector<int32_t>> nets(paths.size());

    for(const GrObject& go: graphicObjects_) {
        if(go.state.net() < 0 || go.state.imgPolarity() != Positive) continue;
        const auto pt = interiorPoint(go);
        if(!pt) continue;
        for(size_t k{}; k < paths.size(); ++k) {
            if(!inside(k, *pt * um) || !copper[k].contains(*pt)) continue;
            if(!r::contains(nets[k], go.state.net())) nets[k].push_back(go.state.net());
            break;
        }
    }

    return nets;
}

// Тот же приём, что и в copperNets: объект лежит в теле целиком (или съеден
// вырезом), так что хватает одной его внутренней точки. Тела элементов
// сцены -- в координатах сцены (DataFill::region), туда же и точка. Осевая
// же -- это путь ровно одного объекта (createGi), и сравнивается с ним как есть.
std::vector<int64_t> File::objectKeys(std::span<Gi::Item* const> items) const {
    std::vector<Geo::Polygon> bodies;
    std::vector<QRectF> boxes;
    Geo::Polylines paths;
    for(const Gi::Item* item: items) {
        if(item->file() != this) continue;
        switch(item->type()) {
        case Gi::Type::DataSolid:
            for(const Geo::Polygon& body: item->region()) {
                boxes.push_back(body.boundingRect());
                bodies.push_back(body);
            }
            break;
        case Gi::Type::DataPath: paths.append_range(item->curves(1)); break; // в своих координатах -- координатах файла
        default                : break;
        }
    }
    if(bodies.empty() && paths.empty()) return {};

    const QTransform t = transform_.toQTransform();
    const auto samePath = [](const Geo::Polyline& lhs, const Geo::Polyline& rhs) {
        constexpr double eps = 1e-9;
        return lhs.size() == rhs.size() && r::equal(lhs, rhs, [](const Geo::Vertex& a, const Geo::Vertex& b) {
            return std::abs(a.x() - b.x()) < eps && std::abs(a.y() - b.y()) < eps;
        });
    };

    std::vector<int64_t> keys;
    for(const GrObject& go: graphicObjects_) {
        if(go.state.imgPolarity() != Positive) continue;
        bool inside = !go.path.empty() && r::any_of(paths, [&](const Geo::Polyline& path) { return samePath(path, go.path); });
        if(!inside && !bodies.empty())
            if(const auto pt = interiorPoint(go)) {
                const QPointF scenePt = t.map(*pt);
                for(size_t k{}; k < bodies.size() && !inside; ++k)
                    inside = boxes[k].contains(scenePt) && bodies[k].contains(scenePt);
            }
        if(inside) keys.push_back(EtchComp::objectKey(go));
    }
    r::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

QString File::netsToolTip(std::span<const int32_t> nets) const {
    QStringList names;
    for(int32_t id: nets) names.push_back(netName(id));
    names.sort();
    if(names.size() > 1) names.prepend(u"⚠ "_s + GbrObj::tr("Short circuit:"));
    return names.join(u'\n');
}

void File::createGi() {
    netItems_.assign(nets_.size(), {});
    shorts_ = 0;

    if constexpr(1) { // fill copper
        const Geo::Polygons& copper = groupedPaths();
        const auto nets = copperNets(copper);
        for(size_t k{}; const Geo::Polygon& paths: copper) {
            // Gi::Debug(paths);
            Gi::Item* item = new Gi::DataFill{Geo::Polygons{paths}, this};
            item->setToolTip(netsToolTip(nets[k]));
            if(nets[k].size() > 1) ++shorts_; // см. netsToolTip
            for(int32_t net: nets[k]) netItems_[net].push_back(item);
            ++k;
            itemGroups_[Normal]->push_back(item);
        }
        itemGroups_[Normal]->shrink_to_fit();
    }
    if constexpr(1) { // add components
        for(const Comp::Component& component: std::as_const(components_))
            if(!component.referencePoint().isNull())
                itemGroups_[Components]->push_back(new Comp::Item{component, this});
        itemGroups_[Components]->shrink_to_fit();
    }
    if constexpr(1) { // add aperture paths
        auto contains = [&](const Geo::Polyline& path) -> bool {
            constexpr double k = 0.001;
            for(const auto& chPath: checkList) { // find copy
                size_t counter{};
                if(chPath.size() == path.size()) {
                    for(const auto& p1: chPath) {
                        for(const auto& p2: path) {
                            if((abs(p1.x() - p2.x()) < k) && (abs(p1.y() - p2.y()) < k)) {
                                ++counter;
                                break;
                            }
                        }
                    }
                }
                if(counter == path.size())
                    return true;
            }
            return false;
        };

        // Ветка упрощения регионов ушла: SimplifyPolygon был заглушкой и
        // наполнял пустой список, то есть контуры замкнутых регионов просто
        // терялись. Теперь они добавляются наравне с прочими.
        for(const GrObject& go: graphicObjects_) {
            if(go.path.empty()) continue;
            if(Settings::skipDuplicates()) {
                if(contains(go.path)) continue;
                checkList.push_front(go.path);
            }
            Gi::Item* item = new Gi::DataPath{{go.path}, this};
            item->setToolTip(netName(go.state.net()));
            if(go.state.net() >= 0) netItems_[go.state.net()].push_back(item);
            itemGroups_[ApPaths]->push_back(item);
        }

        itemGroups_[ApPaths]->shrink_to_fit();
    }

    bool zeroLine{};
    for(auto& [dCode, ap]: apertures_)
        if(zeroLine = (qFuzzyIsNull(ap->minSize()) && ap->used()); zeroLine)
            break;

    if(itemsType_ == NullType) {
        if /**/ (itemGroups_[Components]->size())
            itemsType_ = Components;
        else if(itemGroups_[Normal]->size()) // && !zeroLine)
            itemsType_ = Normal;
        else
            itemsType_ = ApPaths;
    }

    setColor(color_);

    layerTypes_[Normal].id = itemGroups_[Normal]->size() ? Normal : NullType;
    layerTypes_[ApPaths].id = itemGroups_[ApPaths]->size() ? ApPaths : NullType;
    layerTypes_[Components].id = itemGroups_[Components]->size() ? Components : NullType;

    itemGroups_[ApPaths]->setVisible(false);
    itemGroups_[Components]->setVisible(false);
    itemGroups_[Normal]->setVisible(false);

    itemGroups_[itemsType_]->setVisible(visible_);
}

} // namespace Gerber
