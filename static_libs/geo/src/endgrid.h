#pragma once

// ВНУТРЕННИЙ заголовок библиотеки Geo: сетка концов путей для склеек
// (Geo::stitch, сшивка змейки в zigzagFill).
//
// Обе склейки ищут «кусок, чей конец ближе допуска к этой точке». Прежде --
// перебором всех кусков на каждый поиск, и на растре всей платы это сотни
// тысяч кусков: часы счёта. Сетка с ячейкой в два допуска отдаёт кандидатов
// из девяти ячеек вокруг точки; сам ли кандидат подходит, решает вызывающий
// ТЕМ ЖЕ сравнением расстояния, что и прежде, -- сетка лишь не даёт
// проверять заведомо далёкие куски, и порядок склейки от неё не зависит.

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Geo {

class EndGrid {
public:
    // Годится ли сетка для такого допуска и таких точек: координата в
    // ячейках обязана влезть в целое, NaN и бесконечность -- не точки. Иначе
    // склейки идут прежним перебором.
    static bool usable(double tolerance, double maxAbsCoordinate) {
        return std::isfinite(tolerance) && std::isfinite(maxAbsCoordinate)
            && maxAbsCoordinate / cellFor(tolerance) < 1e15;
    }

    // Охват для usable: наибольшая по модулю координата точек. Точка с NaN
    // или бесконечностью делает охват NaN навсегда -- std::max такую молча
    // пропустил бы, и сетка взялась бы за точку, у которой нет ячейки.
    static double extent(double acc, QPointF p) {
        if(!std::isfinite(p.x()) || !std::isfinite(p.y())) return std::numeric_limits<double>::quiet_NaN();
        return std::max({acc, std::abs(p.x()), std::abs(p.y())}); // NaN в acc остаётся NaN
    }

    explicit EndGrid(double tolerance)
        : cell_{cellFor(tolerance)} { }

    void insert(QPointF p, uint32_t id) { cells_[keyOf(p)].push_back(id); }

    void erase(QPointF p, uint32_t id) {
        auto it = cells_.find(keyOf(p));
        if(it == cells_.end()) return;
        auto& ids = it->second;
        if(auto at = std::ranges::find(ids, id); at != ids.end()) {
            *at = ids.back();
            ids.pop_back();
        }
    }

    // Все, кто лежит в девяти ячейках вокруг p (с повторами, если у куска
    // оба конца рядом).
    template <typename F>
    void near(QPointF p, F&& f) const {
        const Key at = keyOf(p);
        for(int64_t dx = -1; dx <= 1; ++dx)
            for(int64_t dy = -1; dy <= 1; ++dy)
                if(auto it = cells_.find({at.x + dx, at.y + dy}); it != cells_.end())
                    for(uint32_t id: it->second) f(id);
    }

private:
    // Ячейка -- два допуска: точки ближе допуска отстоят по каждой оси
    // меньше чем на полячейки и попадают в соседние ячейки, не дальше.
    // Нулевой допуск (склейка лишь совпавших) -- та же сетка, совпавшие
    // точки ложатся в одну ячейку.
    static double cellFor(double tolerance) { return 2.0 * std::max(tolerance, 1e-9); }

    struct Key {
        int64_t x, y;
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key& k) const {
            return std::hash<uint64_t>{}(static_cast<uint64_t>(k.x) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(k.y));
        }
    };

    Key keyOf(QPointF p) const {
        return {static_cast<int64_t>(std::floor(p.x() / cell_)), static_cast<int64_t>(std::floor(p.y() / cell_))};
    }

    double cell_;
    std::unordered_map<Key, std::vector<uint32_t>, Hash> cells_;
};

} // namespace Geo
