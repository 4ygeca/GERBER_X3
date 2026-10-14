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
#include "ex_file.h"

#include "gi_drill.h"

#include "ex_node.h"

#include "geo/boolean.h"
#include "geo/util.h"

namespace Excellon {

File::File()
    : AbstractFile()
    , format_(Settings::format()) {
    format_.file = this;
}

File::~File() { }

Format File::format() const { return format_; }

void File::setFormat(const Format& value) {
    (format_ = value).file = this;
    for(Hole& hole: *this) {
        hole.state.updatePos();
        hole.item->updatePath(hole.state.path.size() ? hole.state.path
                                                     : QPolygonF{hole.state.pos},
            hole.state.currentToolDiameter());
    }
}

double File::tool(unsigned t) const {
    if(tools_.contains(t))
        return format_.unitMode == Inches ? tools_.at(t) * 25.4 : tools_.at(t);
    return {};
}

Tools File::tools() const {
    Tools tools(tools_);
    if(format_.unitMode == Inches)
        for(auto& [_, tool]: tools)
            tool *= 25.4;
    return tools;
}

// Сверления и пазы -- независимые тела, лежащие поодиночке; регион слоя это
// просто их объединение. Контуры берутся у самих элементов сцены: Gi::Drill
// строит их точно (окружность у сверла, полоса Geo::Inflate у паза), и второй
// раз считать то же самое здесь незачем.
Geo::Polygons File::merge() const {
    Geo::Polylines contours;
    for(auto&& item: *itemGroup())
        contours.append_range(item->curves());
    mergedCurves_ = Geo::Polygons{contours};
    return mergedCurves_;
}

void File::createGi() {
    for(Hole& hole: *this)
        itemGroup()->push_back(hole.item = new Gi::Drill{
                                   hole.state.path.size() ? hole.state.path
                                                          : QPolygonF{hole.state.pos},
                                   hole.state.currentToolDiameter(),
                                   this,
                                   static_cast<Tool::ID>(hole.state.toolId)});
    itemGroup()->setVisible(true);
}

void File::initFrom(AbstractFile* file) {
    AbstractFile::initFrom(file);
    setFormat(static_cast<File*>(file)->format());
    static_cast<Excellon::Node*>(node_)->file = this;
}

FileTree::Node* File::node() {
    return node_ ? node_ : node_ = new Excellon::Node{this};
}

std::vector<GraphicObject> File::getDataForGC(std::span<Criteria> criterias, GCType /*gcType*/, bool test) const {
    std::vector<GraphicObject> retData;
    const QTransform t = transform_.toQTransform();
    for(const Excellon::Hole& hole: *this) {
        // Диаметр -- уже в миллиметрах: у дюймового файла tool() пересчитывает
        // сам, а сырой tools_ отдал бы дюймы, и сверло уехало бы в 25.4 раза.
        const double diam = tool(hole.state.toolId);
        GraphicObject go;
        if(hole.state.path.size() < 2) { // сверление
            go.pos = hole.state.pos;
            go.path = Geo::Polyline{Geo::Vertex{go.pos}};
            go.fill = Geo::Polygons{Geo::Polylines{Geo::circle(diam, go.pos)}};
        } else { // паз: полоса вдоль прохода шириной ровно в диаметр сверла
            go.path = Geo::Polyline{hole.state.path};
            go.fill = Geo::Inflate(Geo::Polylines{go.path}, diam);
        }
        go.name = u"T%1|Ø%2"_s.arg(+hole.state.toolId).arg(diam);
        go.type = GraphicObject::FlStamp;
        go.raw = diam;

        auto transformedGo = go * t;
        for(auto&& criterion: criterias)
            if(criterion.test(transformedGo)) {
                retData.emplace_back(std::move(transformedGo));
                if(test) return retData;
                break;
            }
    }
    return retData;
}

double File::coordinateResolution() const {
    // Число с точкой (X12.345) несёт свой шаг -- знаки после точки, формат
    // при разборе оно не читает. Но нули в хвосте дробной части не пишут:
    // X12.7 рядом с X12.345 -- тот же шаг 0.001. Поэтому знаков -- максимум
    // по всему файлу, а не у отдельного числа.
    int pointDecimals = -1;
    bool implicitDecimals{}; // число без точки: знаки задаёт формат
    auto scan = [&](const QString& raw) {
        if(raw.isEmpty()) return;
        if(const qsizetype dot = raw.indexOf(u'.'); dot >= 0)
            pointDecimals = std::max(pointDecimals, static_cast<int>(raw.size() - dot - 1));
        else
            implicitDecimals = true;
    };
    for(const Hole& hole: *this) {
        scan(hole.state.rawPos.x);
        scan(hole.state.rawPos.y);
        for(const State::Pos& raw: hole.state.rawPosList) {
            scan(raw.x);
            scan(raw.y);
        }
    }
    // Без точки и без знаков в формате (decimal == 0) шаг неизвестен: целые
    // миллиметры или дюймы -- это скорее не заданный формат, чем сетка.
    double step{};
    if(implicitDecimals && format_.decimal > 0)
        step = std::pow(10.0, -format_.decimal);
    // Записаны по-разному -- проверке нужен худший, то есть крупный, шаг.
    if(pointDecimals >= 0)
        step = std::max(step, std::pow(10.0, -pointDecimals));
    if(!(step > 0.0))
        return 0.0;
    return format_.unitMode == Inches ? step * 25.4 : step;
}

} // namespace Excellon
