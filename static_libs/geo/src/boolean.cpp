// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// Раздувание и булевы операции поверх обёрток Geo::Polygon/Polygons
// (geo/polygon.h) -- то есть поверх точного CGAL-домена: центры и радиусы
// дуг проходят операции неизменными, дискретизации нет вовсе.
//
// Здесь нет ни одного вызова CGAL напрямую: всё выражается операциями
// региона (|, &, -, ^) и разложением офсета в сумму Минковского с кругом
// (geo/src/offsetcapsules.h).

#include "geo/boolean.h"
#include "cgal.h" // Cgal::weldTolerance
#include "endgrid.h"
#include "geo/cancel.h"
#include "geo/util.h"
#include "offsetcapsules.h"
#include "phasestats.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <set>

#include <QtGlobal>

#include <map>

namespace Geo {

namespace {

// Куски Минковского границы одной полилинии, раздутой на d.
//
// Исходная окружность -- особый случай: её куски (два полусектора) имели бы
// точно совпадающие радиальные торцы, худший вход для булева движка, тогда
// как раздутая окружность -- это просто диск радиуса R0 + d.
Polylines capsulesOf(const Polyline& poly, double d) {
    if(const auto circle = Offset::fullCircleOf(poly))
        return {Offset::disc(circle->first, circle->second + d)};
    return Offset::capsulesFor(poly, d);
}

// Раздутый регион: R (+) диск(d) = R объединить с кусками ВСЕЙ его границы
// -- и внешней, и границ отверстий. Отсюда и берётся то, что дырка при
// раздувании сжимается: куски её границы заполняют ободок изнутри, а дырка
// тоньше 2d исчезает целиком.
//
// Куски берутся из ПОДАННЫХ контуров, а не из границы самого региона:
// лишние (те, что легли внутри R) объединению не мешают, зато так
// раздуваются и открытые контуры, у которых площади нет вовсе.
Polygons grow(Polygons region, const Polylines& boundary, double d) {
    if(d <= 0.0) return region;
    Polylines parts;
    for(const Polyline& contour: boundary) {
        checkCancelled();
        for(Polyline& capsule: capsulesOf(contour, d))
            parts.push_back(std::move(capsule));
    }
    return region | Polygons{parts};
}

} // namespace

Polygons Inflate_::operator()(const Polylines& polylines, double delta) const {
    // Сжатие -- через границу САМОГО региона, а не через поданные контуры:
    // у наложившихся друг на друга контуров общий шов границей уже не
    // является, и вычитание его капсул прорезало бы регион насквозь. Для
    // раздувания та же разница безвредна (лишние капсулы и так внутри), а
    // капсулы открытых контуров там ещё и нужны.
    if(delta < 0.0) return operator()(Polygons{polylines}, delta);
    return grow(Polygons{polylines}, polylines, delta * 0.5);
}

Polygons Inflate_::operator()(const Polylines& polylines) const {
    // Каждая полилиния -- на свою собственную ширину; регион собирается
    // один на весь список, поэтому вложенность (тело/пустота) учитывается
    // сразу и целиком.
    Polygons region{polylines};
    Polylines parts;
    for(const Polyline& poly: polylines) {
        checkCancelled();
        const double d = std::abs(poly.width) * 0.5;
        if(d <= 0.0) continue;
        for(Polyline& capsule: capsulesOf(poly, d)) parts.push_back(std::move(capsule));
    }
    return parts.empty() ? region : region | Polygons{parts};
}

Polygons Inflate_::operator()(const Polygons& region, double delta, double coarse) const {
    const double d = std::abs(delta) * 0.5;
    if(d <= 0.0) return region;

    // Одна и та же полоса вдоль границы -- и наружу, и внутрь: снаружи она
    // прирастает к региону, изнутри съедается из него. Во втором случае
    // остаются ровно те точки региона, что дальше d от его границы, --
    // эрозия без всякого приближения (при coarse == 0; недобор черновой
    // полосы в обоих случаях сдвигает контур К границе, не от неё).
    const Polygons band = region.boundaryBand(d, coarse);
    PhaseScope stat{Phase::RegionOp};
    return delta > 0.0 ? region | band : region - band;
}

Polygons BooleanOp_::operator()(
    BooleanOp_::ClipType cliptype, BooleanOp_::FillRule fillrule,
    const Polylines& subjects, const Polylines& clips) {
    // Правило заливки применимо ровно здесь -- на СЫРЫХ контурах, пока
    // вложенность ещё не разобрана. Дальше её несёт сам регион.
    auto toRegion = [fillrule](const Polylines& contours) {
        return fillrule == FillRule::EvenOdd ? evenOdd(contours) : Polygons{contours};
    };
    return operator()(cliptype, fillrule, toRegion(subjects), toRegion(clips));
}

Polygons BooleanOp_::operator()(
    BooleanOp_::ClipType cliptype, BooleanOp_::FillRule fillrule,
    const Polygons& subjects, const Polygons& clips) {
    // Правило заливки задаётся уже самим регионом (вложенность разобрана
    // при его построении), так что различать здесь нечего. Кому нужен
    // even-odd от СЫРЫХ контуров -- см. Geo::evenOdd: перегрузка от Polylines
    // выше уже пропускает их через него.

    Polygons result = subjects;
    switch(cliptype) {
    case ClipType::NoClip      : break;
    case ClipType::Union       : result |= clips; break;
    case ClipType::Intersection: result &= clips; break;
    case ClipType::Difference  : result -= clips; break;
    case ClipType::Xor         : result ^= clips; break;
    }
    return result;
}

//------------------------------------------------------------------------------
// Нормализация чертёжной геометрии (см. geo/boolean.h).

namespace {

// Обрывок, вернувшийся в свою стартовую точку (в допуске glue), -- контур,
// если тела из него хватает: без хвоста-дубля три вершины, либо две, но с
// дугой: окружность в bulge-виде -- ровно две вершины с прогибом +-1, и
// именно так её приносит гербер (две половинки или один оборот). Две вершины
// без дуг -- линия туда-обратно, не контур. Порог «больше трёх вершин»
// здесь оставлял отверстия открытыми путями: профиль обходил их с обеих
// сторон, карман терял вовсе.
bool returnsToStart(const Polyline& p, double glue) {
    if(p.size() < 3 || distance(p.front(), p.back()) > glue) return false;
    const std::size_t body = p.size() - 1;
    return body >= 3 || std::ranges::any_of(p | std::views::take(body), &Vertex::isArc);
}

// Снимает хвост-дубль (и неточный -- в допуске он тот же старт; у KiCad стык
// линии и дуги расходится на десятки нанометров, и close() сам такой дубль
// не снимет -- остался бы шов-сегмент в доли микрона) и замыкает. Прогиб
// замыкающего сегмента лежит на предпоследней вершине, у хвоста его нет --
// терять нечего. У УЖЕ замкнутого контура хвост -- настоящая вершина, не
// дубль: там только close().
Polyline& closeRing(Polyline& p) {
    if(!p.closed && static_cast<const QPointF&>(p.front()) != p.back()) p.pop_back();
    return p.close();
}

} // namespace

namespace {

// Стык пары -- БЛИЖАЙШИЙ из четырёх (конец a к началу b, конец к концу,
// начало к концу, начало к началу), при равных -- первый в этом порядке.
//
// Прежде брался первый же стык в пределах клея, а не ближайший. Пока куски
// длиннее клея, в его пределах лежит лишь один стык, и разницы нет. Но
// скругление контура KiCad пишет отрезками по 0.0499 мм, короче клея 0.05 по
// умолчанию: у куска оба конца рядом с соседом, и «первый» стык брал ЧУЖОЙ
// конец -- кусок вставал задом наперёд, вершина соседа пропадала, контур
// панели из 65 вырезов собирался в 52 тела и 12 обрывков. «Внутри» обрывки не
// фрезерует, и вырезы со скруглениями молча пропадали из УП.
struct Joint {
    double distance; // NaN -- как бесконечность: такой стык не годится
    int kind;        // 0..3 -- порядок выше
};

Joint nearestJoint(const Polyline& a, const Polyline& b) {
    const double d[4]{distance(a.back(), b.front()), distance(a.back(), b.back()), distance(a.front(), b.back()),
        distance(a.front(), b.front())};
    Joint best{std::numeric_limits<double>::infinity(), 0};
    for(int kind{}; kind < 4; ++kind)
        if(d[kind] < best.distance) best = {d[kind], kind}; // строго: при равных -- прежний порядок
    return best;
}

// Склейка пары: к a приклеивается b ближайшим стыком (nearestJoint). Пара
// заведомо склеивается (stitchJoinable).
void stitchPair(Polyline& a, Polyline b) {
    switch(nearestJoint(a, b).kind) {
    case 0: break;
    case 1: b.reverse(); break;
    case 2: a.reverse(), b.reverse(); break;
    case 3: a.reverse(); break;
    }
    // Только Polyline::append: прогиб стыка -- у первой вершины куска, и
    // ручной insert(begin() + 1, …) его выбрасывал -- дуга габарита уходила
    // хордой, если кусок приклеивался перевёрнутым.
    a.append(b);
}

bool stitchJoinable(const Polyline& a, const Polyline& b, double glue) {
    auto joinable = [glue](QPointF p, QPointF q) { return distance(p, q) <= glue; };
    return joinable(a.back(), b.front()) || joinable(a.back(), b.back())
        || joinable(a.front(), b.back()) || joinable(a.front(), b.front());
}

bool stitchable(const Polyline& p) { return !p.closed && p.size() >= 2; }

// Пара куска -- та, чей стык ближе (nearestJoint); при равных -- меньший
// номер. Стык точнее клея у соседа по контуру всегда ближе, чем у куска через
// одного, -- даже когда куски короче клея и в его пределах лежат оба.
struct Partner {
    double distance{std::numeric_limits<double>::infinity()};
    std::size_t id{std::numeric_limits<std::size_t>::max()};
    void offer(double d, std::size_t candidate) {
        if(d < distance || (d == distance && candidate < id)) distance = d, id = candidate;
    }
};

// Склейка перебором: после каждой склейки -- снова с начала. Склеивается
// ПЕРВЫЙ открытый кусок, у которого вообще есть пара, с БЛИЖАЙШЕЙ из своих
// пар (Partner). Квадратична на каждую склейку -- для входа, с которым сетка
// не справится (NaN, бесконечность, гигантские координаты).
void stitchQuadratic(Polylines& polylines, double glue) {
    bool merged;
    do {
        merged = false;
        for(std::size_t i{}; i < polylines.size() && !merged; ++i) {
            if(!stitchable(polylines[i])) continue;
            Partner partner;
            for(std::size_t j{}; j < polylines.size(); ++j) {
                if(i == j || !stitchable(polylines[j])) continue;
                if(!stitchJoinable(polylines[i], polylines[j], glue)) continue;
                partner.offer(nearestJoint(polylines[i], polylines[j]).distance, j);
            }
            if(partner.id >= polylines.size()) continue;
            stitchPair(polylines[i], polylines[partner.id]);
            polylines.erase(polylines.begin() + static_cast<std::ptrdiff_t>(partner.id));
            merged = true;
        }
        checkCancelled(); // склейка квадратична по числу обрывков
    } while(merged);
}

// Та же склейка в ТОМ ЖЕ порядке, но без перебора. Порядок перебора задан
// правилом «первый кусок с парой и ближайшая из его пар» -- значит, следующую
// склейку определяет наименьший номер среди кусков, у которых пара есть.
// Их множество и держится, а пары ищутся сеткой концов: склейка меняет
// концы лишь двух кусков, и пересчитывать надо только тех, чьи концы лежат
// рядом с прежними или новыми концами этих двух. Номера -- исходные: erase
// порядка оставшихся не меняет, так что порядок списка и есть порядок
// номеров. На растре платы прежний перебор шёл часами (тысячи склеек по
// квадрату обрывков, каждый с копией пути), здесь -- доли секунды.
bool stitchFast(Polylines& polylines, double glue) {
    double extent{};
    for(const Polyline& p: polylines)
        if(stitchable(p)) extent = EndGrid::extent(EndGrid::extent(extent, p.front()), p.back());
    if(!EndGrid::usable(glue, extent) || polylines.size() >= std::numeric_limits<uint32_t>::max()) return false;

    const auto count = static_cast<uint32_t>(polylines.size());
    std::vector<char> alive(count, 1);
    EndGrid grid{glue};
    auto insert = [&](uint32_t id) {
        grid.insert(polylines[id].front(), id);
        grid.insert(polylines[id].back(), id);
    };
    auto erase = [&](uint32_t id) {
        grid.erase(polylines[id].front(), id);
        grid.erase(polylines[id].back(), id);
    };
    for(uint32_t id{}; id < count; ++id)
        if(stitchable(polylines[id])) insert(id);

    constexpr uint32_t none = std::numeric_limits<uint32_t>::max();
    // Ближайшая пара куска i (Partner) -- та, что нашёл бы перебор.
    auto partnerOf = [&](uint32_t i) {
        Partner best;
        for(const QPointF& end: {QPointF{polylines[i].front()}, QPointF{polylines[i].back()}})
            grid.near(end, [&](uint32_t id) {
                if(id != i && stitchJoinable(polylines[i], polylines[id], glue))
                    best.offer(nearestJoint(polylines[i], polylines[id]).distance, id);
            });
        return best.id < count ? static_cast<uint32_t>(best.id) : none;
    };

    std::set<uint32_t> ready; // куски, у которых пара есть
    for(uint32_t id{}; id < count; ++id)
        if(stitchable(polylines[id]) && partnerOf(id) != none) ready.insert(id);

    std::vector<uint32_t> touched;
    auto touchNear = [&](uint32_t id) {
        for(const QPointF& end: {QPointF{polylines[id].front()}, QPointF{polylines[id].back()}})
            grid.near(end, [&](uint32_t other) { touched.push_back(other); });
    };
    while(!ready.empty()) {
        checkCancelled();
        const uint32_t i = *ready.begin();
        const uint32_t j = partnerOf(i);
        if(j == none) { // пару увела прежняя склейка
            ready.erase(ready.begin());
            continue;
        }
        touched.clear();
        touchNear(i);
        touchNear(j);
        erase(i);
        erase(j);
        stitchPair(polylines[i], std::move(polylines[j]));
        alive[j] = 0;
        ready.erase(j);
        insert(i);
        touchNear(i);
        touched.push_back(i);
        std::ranges::sort(touched);
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for(uint32_t id: touched) {
            if(!alive[id]) continue;
            if(partnerOf(id) != none) ready.insert(id);
            else ready.erase(id);
        }
    }

    std::size_t kept{};
    for(uint32_t id{}; id < count; ++id)
        if(alive[id]) {
            if(kept != id) polylines[kept] = std::move(polylines[id]);
            ++kept;
        }
    polylines.resize(kept);
    return true;
}

} // namespace

void stitch(Polylines& polylines, double glue) {
    if(!stitchFast(polylines, glue)) stitchQuadratic(polylines, glue);

    // Замкнувшийся сам на себя обрывок -- это контур, а не линия.
    for(Polyline& polyline: polylines)
        if(returnsToStart(polyline, glue)) closeRing(polyline);
}

namespace {

// Тот же ли это контур: та же замкнутая граница, возможно начатая с другого
// узла -- стартовая вершина в чертеже произвольна. Оба контура уже приведены
// к одному обходу (ccw в evenOdd), так что встречный дубликат сюда попадает
// тоже. Сверяются и прогибы: у двух разных дуг через одни и те же вершины
// совпадать нечему.
bool sameContour(const Polyline& a, const Polyline& b, double tolerance) {
    if(a.size() != b.size()) return false;
    const std::size_t n = a.size();

    auto matchFrom = [&](std::size_t shift) {
        for(std::size_t i{}; i < n; ++i) {
            const Vertex& va = a[i];
            const Vertex& vb = b[(i + shift) % n];
            if(distance(va, vb) > tolerance) return false;
            if(std::abs(va.bulge - vb.bulge) > tolerance) return false;
        }
        return true;
    };

    for(std::size_t shift{}; shift < n; ++shift)
        if(distance(a.front(), b[shift]) <= tolerance && matchFrom(shift))
            return true;
    return false;
}

} // namespace

Polygons evenOdd(const Polylines& contours) {
    // Сперва отбор годных тел, и только потом XOR: дубликаты надо отсеять до
    // счёта чётности, иначе контур, нарисованный ДВАЖДЫ (обычный мусор
    // чертежа -- новая линия обведена поверх старой), взаимно уничтожился бы:
    // два накрытия -- пустота. Для чертежа же это всё одна линия, поэтому из
    // совпадающих контуров остаётся один; нечётные копии безвредны и так, но
    // правило одно на всех -- так предсказуемее.
    Polylines bodies;
    for(const Polyline& contour: contours) {
        // Порога по числу вершин здесь быть не может: в bulge-виде окружность
        // -- это ДВЕ вершины с прогибом 1 (полный оборот прогибом не
        // выражается, см. Geo::circle), и «меньше трёх -- не контур» выкидывало
        // бы ровно её. Годность решает isExactContour ниже.
        if(!contour.isClosed() || contour.size() < 2) continue;
        // Ориентация не значит НИЧЕГО: каждый контур входит телом, а дырки
        // получаются сами -- вложенный контур накрывает точку второй раз.
        Polyline body = contour;
        if(body.signedArea() < 0) body.reverse();
        if(!isExactContour(body)) continue; // вырожденный или самокасающийся

        // Допуск дубликата -- сварочный допуск выхода из точного домена: чьи
        // вершины неотличимы на выходе, те и есть одна линия.
        auto seenAlready = [&body](const Polyline& seen) {
            return sameContour(seen, body, exitWeldTolerance);
        };
        if(std::ranges::any_of(bodies, seenAlready)) continue;

        bodies.push_back(std::move(body));
        checkCancelled(); // сверка с уже отобранными квадратична
    }

    Polygons region;
    for(Polyline& body: bodies) {
        region ^= Polygons{Polylines{std::move(body)}};
        checkCancelled();
    }
    return region;
}

Polylines splitSelfTouching(const Polyline& contour) {
    if(!contour.closed || contour.size() < 3) return {contour};

    using Key = std::pair<double, double>;
    auto keyOf = [](const Vertex& v) { return Key{v.x(), v.y()}; };

    // Шип: в вершине b контур разворачивается и идёт назад по той же прямой
    // -- конец короткого плеча лежит на длинном. Площади у шипа нет, а
    // самокасание есть. KiCad оставляет такие в заливке зон: шаг в одну
    // единицу файла (10 нм) назад на стыке прямой со скруглением.
    auto isSpike = [](const Vertex& a, const Vertex& b, const Vertex& c) {
        if(a.isArc() || b.isArc()) return false; // только прямые плечи
        const QPointF ab = a - b, cb = c - b;
        if(QPointF::dotProduct(ab, cb) <= 0.0) return false; // не разворот
        const double longer = std::max(std::hypot(ab.x(), ab.y()), std::hypot(cb.x(), cb.y()));
        return std::abs(ab.x() * cb.y() - ab.y() * cb.x()) <= longer * Cgal::weldTolerance;
    };
    auto trimSpikes = [&](Polyline& loop) {
        for(std::size_t i{}; loop.size() >= 3 && i < loop.size();) {
            const std::size_t n = loop.size();
            if(isSpike(loop[(i + n - 1) % n], loop[i], loop[(i + 1) % n])) {
                loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
                i = i ? i - 1 : 0; // сосед слева мог сам стать шипом
            } else
                ++i;
        }
        // Стык через начало: срезанный в конце шип меняет соседа первой вершины.
        while(loop.size() >= 3 && isSpike(loop.back(), loop.front(), loop[1]))
            loop.erase(loop.begin());
    };

    Polylines loops;
    auto take = [&](Polyline&& loop) {
        trimSpikes(loop);
        if(loop.size() < 2) return;
        loop.closed = true;
        loop.width = contour.width;
        // Перемычка туда и обратно: площади нет, а с лишней вершиной на
        // одном из ходов она выходит ниточкой -- отсюда порог, а не ноль.
        if(std::abs(loop.signedArea()) <= loop.perimeter() * exitWeldTolerance) return;
        loops.push_back(std::move(loop));
    };

    // Незамкнутый пока хвост обхода. Точки в нём попарно различны: всякий
    // повтор тут же снимает петлю.
    Polyline stack;
    std::map<Key, std::size_t> seen; // точка -> её место в stack
    for(const Vertex& v: contour) {
        const auto it = seen.find(keyOf(v));
        if(it == seen.end()) {
            seen.emplace(keyOf(v), stack.size());
            stack.push_back(v);
            continue;
        }
        const auto from = stack.begin() + static_cast<std::ptrdiff_t>(it->second);
        Polyline loop(from, stack.end());
        for(auto gone = from + 1; gone != stack.end(); ++gone) seen.erase(keyOf(*gone));
        stack.erase(from + 1, stack.end());
        // Вершина стыка остаётся в хвосте, но ребро из неё теперь другое --
        // то, что выходит из её ПОВТОРА.
        stack.back().bulge = v.bulge;
        take(std::move(loop));
    }
    take(std::move(stack));
    return loops;
}

Normalized normalize(Polylines polylines, double glue) {
    std::erase_if(polylines, [](const Polyline& p) { return p.size() < 2; });

    auto isClosed = [glue](const Polyline& p) { return p.closed || returnsToStart(p, glue); };

    // Сперва отбираем уже замкнутое, потом склеиваем остальное: склейка
    // квадратична, и гонять её по тому, что и так замкнуто, незачем.
    Polylines closed;
    for(std::size_t i{}; i < polylines.size();)
        if(isClosed(polylines[i])) {
            closeRing(closed.emplace_back(std::move(polylines[i])));
            polylines.erase(polylines.begin() + i);
        } else
            ++i;

    stitch(polylines, glue);

    Normalized result;
    for(Polyline& p: polylines)
        if(isClosed(p)) closeRing(closed.emplace_back(std::move(p)));
        else result.open.emplace_back(std::move(p));

    result.region = evenOdd(closed);
    return result;
}

} // namespace Geo
