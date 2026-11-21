/********************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  ХХ ХХХ 2026                                                 *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 ********************************************************************************/
#pragma once

#include "gc_file.h"
#include "project.h"

namespace Drilling {

constexpr auto DRILLING = "Drilling"_hash32;

// Строка таблицы инструментов, из которой посчитана УП. Ключ -- ИМЯ: индекс
// строки зависит от порядка апертур в файле и от того, какой файл выбран в
// combobox'е, а имя переживает и то, и другое.
struct RowRef {
    QString name;
    Tool::ID toolId{};
    bool useForCalc{};
    // Инструмент правлен только для этой УП: при правке строка берёт его копию
    // из самой УП (gcp.tools), а не из базы. В старых проектах ключа нет -- false.
    bool overridden{};
};

class[[= Serial::name("Drilling")]] File final : public GCode::File {
public:
    void serialize(Serial::Writer& sb) const override { Serial::writeInto(sb, *this); }
    using GCode::File::File;
    explicit File(GCode::Params&& newGcp)
        : GCode::File{std::move(newGcp)} {
        if(gcp.tools.front().diameter())
            regenerate();
    }
    // Чем заполнить таблицу при открытии УП на правку. Хранится в самой УП, а
    // не в Params: Variant умеет только числа и UsedItems, строки в него не лягут.
    const std::vector<RowRef>& rows() const { return rows_; }
    void setRows(std::vector<RowRef> rows) { rows_ = std::move(rows); }
    int worckType() const { return worckType_; }
    void setWorckType(int type) { worckType_ = type; }
    int32_t srcFileId() const { return srcFileId_; }
    void setSrcFileId(int32_t id) { srcFileId_ = id; }

    QIcon icon() const override { return QIcon::fromTheme(u"drill-path"_s); }
    uint32_t type() const override { return DRILLING; }
    // Пазы фрезеруются вдоль путей (drilling.js) -- и рисуются, как профиль:
    // полосой в диаметр фрезы. Метки сверловки на вершинах пути лгали бы.
    void createGi() override {
        if(gcp.slotPaths())
            createGiProfile();
        else
            createGiDrill();
        itemGroup()->setVisible(true);
    }

    // Точки сверлятся, пазы фрезеруются вдоль путей (drilling.js) -- и режимы
    // у них свои: сверловки и фрезеровки.
    Tool::Operation operation() const override {
        if(gcp.tools.empty()) return GCode::File::operation();
        return Tool::programOperation(gcp.tool().type(), !gcp.slotPaths());
    }
    // Точки сверловки -- вершины первого пути первой группы, и drilling.js
    // сверлит их на каждом тайле раскладки, если она не отключена (NotTile).
    int drillHits() const override {
        if(gcp.slotPaths() || gcp.toolPathss.empty() || gcp.toolPathss.front().empty()) return 0;
        const auto points = static_cast<int>(gcp.toolPathss.front().front().size());
        if(gcp.params.contains(GCode::Params::NotTile)) return points;
        const Project& project = App::project();
        return points * static_cast<int>(std::max(1u, project.stepsX()) * std::max(1u, project.stepsY()));
    }

    // Пазы из проектов до появления ключа SlotPaths. Точки сверловки -- всегда
    // ОДНА незамкнутая полилиния (форма, булавки), так что несколько путей или
    // замкнутый контур -- это пазы. Одиночный прямой паз от пары отверстий не
    // отличить: такая УП остаётся сверловкой, как и считалась. Ключ -- до
    // базы: её postLoad зовёт createGi().
    void postLoad() {
        if(!gcp.slotPaths() && !gcp.toolPathss.empty()) {
            const Geo::Polylines& paths = gcp.toolPathss.front();
            if(paths.size() > 1 || (paths.size() == 1 && paths.front().isClosed()))
                gcp.params[GCode::Params::SlotPaths] = 1;
        }
        GCode::File::postLoad();
    }

private:
    std::vector<RowRef> rows_;
    int worckType_{};
    int32_t srcFileId_{-1};
};

} // namespace Drilling
