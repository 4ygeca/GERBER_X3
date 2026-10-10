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
// Заголовки Qt -- первыми (см. gi.h).
#include <QCoreApplication>
#include <QDir>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QRectF>
#include <QSaveFile>
#include <QSettings>
#include <QTransform>

#include "plot.h"

#include "abstract_file.h"
#include "geo/boolean.h"
#include "geo/polygon.h"
#include "geo/util.h"
#include "gi.h"
#include "gi_drill.h"
#include "gi_group.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <string>
#include <string_view>

namespace Plot {

namespace {

// Скрытый в дереве файл на время сбора показывается: ЧТО у файла выводить,
// знает только он сам -- какая группа у Gerber на экране (медь или пути
// апертур), какие слои DXF/TopoR видимы и в каком режиме. Сбор идёт без цикла
// событий, так что на экране это не мелькает, а видимость возвращается той же
// virtual setVisible -- со всеми её особенностями у DXF/TopoR.
class ShownForPlot {
public:
    explicit ShownForPlot(AbstractFile* file)
        : file_{file}
        , hidden_{!file->isVisible()} {
        if(hidden_) file_->setVisible(true);
    }
    ~ShownForPlot() {
        if(hidden_) file_->setVisible(false);
    }
    ShownForPlot(const ShownForPlot&) = delete;
    ShownForPlot& operator=(const ShownForPlot&) = delete;

private:
    AbstractFile* file_;
    bool hidden_;
};

template <typename Fn>
void forVisibleItems(const AbstractFile* file, Fn&& fn) {
    for(Gi::Group* group: file->itemGroups()) {
        if(!group) continue;
        for(const auto& item: *group)
            if(item && item->isVisible()) fn(*item);
    }
}

// Геометрия элемента в координатах СЦЕНЫ. curves()/region() уже несут
// трансформацию элемента (её ставит AbstractFile::setTransform), а pos у
// элементов данных нулевой -- но если кто-то элемент сдвинул, сдвиг тоже
// учитывается: на экране он есть.
Geo::Polylines sceneCurves(const Gi::Item& item) {
    Geo::Polylines curves = item.curves();
    if(const QPointF pos = item.pos(); !pos.isNull()) Geo::translate(curves, pos);
    return curves;
}

Geo::Polygons sceneRegion(const Gi::Item& item) {
    Geo::Polygons region = item.region();
    if(const QPointF pos = item.pos(); !pos.isNull()) region = Geo::translated(region, pos);
    return region;
}

struct Source {
    Geo::Polygons region; // сцена, мм, Y вверх
    bool drill{};         // одни отверстия -- слой сверловки
};

// Всё, что файл показывает, одним точным регионом:
//   * заливка (Gi::DataFill) -- точным регионом элемента; тела объединяются,
//     а не складываются контурами: у DXF/TopoR тела соседних слоёв одного
//     файла могут перекрываться, и even-odd на выходе вырезал бы перекрытие;
//   * отверстия (Gi::Drill) -- телами, как у сверловки в Excellon::merge;
//   * пути без заливки (Gi::DataPath) -- штрихом ширины lineWidth, тоже
//     точным регионом, чтобы негатив, зеркало и вычитание отверстий
//     работали с ними так же, как с медью.
Source collect(AbstractFile* file, double lineWidth) {
    const ShownForPlot shown{file};

    std::vector<Geo::Polygon> bodies;
    Geo::Polylines holes;
    Geo::Polylines paths;
    forVisibleItems(file, [&](const Gi::Item& item) {
        const int type = item.type();
        if(type == Gi::Type::DataSolid)
            bodies.append_range(sceneRegion(item).all());
        else if(type == Gi::Type::Drill)
            holes.append_range(sceneCurves(item));
        else if(type == Gi::Type::DataPath)
            paths.append_range(sceneCurves(item));
        // Прочее (компоненты, превью, ошибки) -- не данные слоя.
    });

    Source src;
    if(!bodies.empty()) src.region = Geo::Polygons{std::span<const Geo::Polygon>{bodies}};
    if(!holes.empty()) src.region |= Geo::Polygons{holes};
    src.drill = bodies.empty() && !holes.empty();

    if(lineWidth > 0.0 && !paths.empty()) {
        // У файла со своей заливкой замкнутые пути -- это границы той же
        // заливки (DXF/TopoR в режиме "заливка и контуры"): их обводка лишь
        // раздула бы медь на полширины штриха. Открытые пути заливки не имеют
        // и выводятся всегда.
        const bool filled = !src.region.empty();
        Geo::Polylines strokes;
        Geo::Polylines dots;
        for(Geo::Polyline& path: paths) {
            if(path.empty()) continue;
            if(path.size() == 1) { // точка -- след круглого пера
                dots.push_back(Geo::circle(lineWidth, path.front()));
                continue;
            }
            if(path.closed) {
                if(filled) continue;
                // Замкнутый путь -- кольцом, а не телом: размыкаем его в
                // стартовой вершине, прогиб замыкающего сегмента остаётся на
                // последней вершине.
                const QPointF start = path.front();
                path.closed = false;
                path.emplace_back(start);
            }
            strokes.push_back(std::move(path));
        }
        if(!strokes.empty()) src.region |= Geo::Inflate(strokes, lineWidth);
        if(!dots.empty()) src.region |= Geo::Polygons{dots};
    }
    return src;
}

// Отверстия файла сверловки для меток на остальных слоях: в натуральную
// величину либо малыми метками-кернами. Малая метка отверстия -- кружок в его
// центре, паза -- сам паз, суженный до диаметра метки (та же ось и те же
// скругления). Метка не крупнее самого отверстия.
Geo::Polygons drillHoles(AbstractFile* file, const Options& opt) {
    const ShownForPlot shown{file};

    Geo::Polylines contours;
    std::vector<Geo::Polygon> thinSlots; // не "slots": это макрос Qt
    forVisibleItems(file, [&](Gi::Item& item) {
        if(item.type() != Gi::Type::Drill) return;
        Geo::Polylines hole = sceneCurves(item);
        if(hole.empty()) return;
        if(opt.drillMarks != DrillMarks::Small) {
            contours.append_range(std::move(hole));
            return;
        }
        auto& drill = static_cast<Gi::Drill&>(item);
        QRectF box;
        for(const Geo::Polyline& contour: hole) box |= contour.boundingRect();
        if(!drill.isSlot()) {
            if(opt.smallDrill >= std::min(box.width(), box.height()))
                contours.append_range(std::move(hole));
            else
                contours.push_back(Geo::circle(opt.smallDrill, box.center()));
            return;
        }
        // Ширина паза на сцене: диаметр сверла в масштабе трансформации файла.
        const double width = drill.diameter() * std::sqrt(std::abs(item.transform().determinant()));
        if(opt.smallDrill >= width) {
            contours.append_range(std::move(hole));
            return;
        }
        const Geo::Polygons thin = Geo::Inflate(Geo::Polygons{hole}, opt.smallDrill - width);
        thinSlots.append_range(thin.all());
    });

    Geo::Polygons marks{contours};
    if(!thinSlots.empty()) marks |= Geo::Polygons{std::span<const Geo::Polygon>{thinSlots}};
    return marks;
}

// Метки совмещения: крест с кольцом в каждом углу поля, в координатах
// страницы. Одинаковы во всех выводах и симметричны относительно центра
// страницы -- зеркало их не сдвигает, и по ним совмещается любая пара слоёв,
// в том числе в программах, которые при импорте выравнивают по габариту
// содержимого, а не по холсту.
Geo::Polylines registrationMarks(QSizeF page, double margin) {
    // В углу поля, не задевая слой: половина метки меньше половины поля.
    const double size = std::min(5.0, margin * 0.8);
    if(size < 1.0) return {};
    constexpr double line = 0.15;
    const double ring = size * 0.5;
    const double half = margin * 0.5;
    const QPointF corners[]{
        {half,                half                },
        {page.width() - half, half                },
        {half,                page.height() - half},
        {page.width() - half, page.height() - half},
    };
    Geo::Polygons marks;
    for(const QPointF& center: corners) {
        marks |= Geo::Polygons{
            Geo::Polylines{Geo::rectangle(size, line, center), Geo::rectangle(line, size, center)}
        };
        marks |= Geo::Polygons{
            Geo::Polygon{Geo::circle(ring + line, center), Geo::Polylines{Geo::circle(ring - line, center)}}
        };
    }
    return marks.contours();
}

Page tiledPage(const Sheet& sheet, int columns, double gap, const QString& name) {
    Page page;
    page.fileBase = name;
    const int count = static_cast<int>(sheet.plates.size());
    if(count == 0) return page;
    columns = std::clamp(columns, 1, count);
    const int rows = (count + columns - 1) / columns;
    const double width = sheet.page.width();
    const double height = sheet.page.height();
    page.size = QSizeF{columns * width + (columns - 1) * gap, rows * height + (rows - 1) * gap};
    for(int i{}; i < count; ++i) {
        const int row = i / columns;
        const int column = i % columns;
        // Y вверх: первый ряд -- верхний.
        page.items.push_back(Placed{
            &sheet.plates[static_cast<std::size_t>(i)],
            QPointF{column * (width + gap), (rows - 1 - row) * (height + gap)}
        });
    }
    return page;
}

double overflowRatio(QSizeF size, QSizeF area) {
    if(area.width() <= 0.0 || area.height() <= 0.0) return std::numeric_limits<double>::infinity();
    return std::max(size.width() / area.width(), size.height() / area.height());
}

// Сколько слоёв в ряд, чтобы общий лист лучше всего лёг в область печати.
int bestColumns(const Sheet& sheet, double gap, QSizeF area) {
    const int count = static_cast<int>(sheet.plates.size());
    int best = std::max(count, 1);
    double bestRatio = std::numeric_limits<double>::infinity();
    for(int columns = count; columns >= 1; --columns) {
        const double ratio = overflowRatio(tiledPage(sheet, columns, gap, {}).size, area);
        if(ratio < bestRatio) {
            bestRatio = ratio;
            best = columns;
        }
    }
    return best;
}

// Страница в координатах painter'а: 1 ед. = 1 мм, Y вниз, (0, 0) -- левый
// верхний угол страницы. Чёрным, непрозрачно: никаких полупрозрачных цветов
// слоёв, сглаживание не нужно -- принтер и PDF сами знают своё разрешение.
void paintPage(QPainter& painter, const Page& page) {
    painter.setPen(Qt::NoPen);
    for(const Placed& placed: page.items) {
        painter.save();
        painter.translate(placed.offset.x(), page.size.height() - placed.offset.y());
        painter.scale(1.0, -1.0); // слой хранится с Y вверх
        painter.fillPath(placed.plate->path, QBrush{Qt::black});
        painter.restore();
    }
}

std::vector<Page> printPages(const Sheet& sheet, const Options& opt, QSizeF area) {
    std::vector<Page> pages;
    if(opt.layout == Layout::Tiled) {
        const double gap = std::max(opt.gap, 0.0);
        pages.push_back(tiledPage(sheet, bestColumns(sheet, gap, area), gap, {}));
    } else {
        for(const Plate& plate: sheet.plates)
            pages.push_back(Page{plate.fileBase, sheet.page, {Placed{&plate, QPointF{}}}});
    }
    return pages;
}

//------------------------------------------------------------------------------
// Запись. Числа -- через std::format: он не зависит от локали, десятичный
// разделитель всегда точка.

std::string number(double value, int precision = 5) {
    std::string text = std::format("{:.{}f}", value, precision);
    if(const auto dot = text.find('.'); dot != std::string::npos) {
        auto last = text.find_last_not_of('0');
        if(last == dot) --last; // "12." -> "12"
        text.resize(last + 1);
    }
    if(text == "-0") text = "0";
    return text;
}

std::string xmlEscaped(const QString& text) {
    std::string out;
    for(const char ch: text.toStdString()) {
        switch(ch) {
        case '&' : out += "&amp;"; break;
        case '<' : out += "&lt;"; break;
        case '>' : out += "&gt;"; break;
        case '"' : out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default  : out += ch; break;
        }
    }
    return out;
}

bool isAsciiAlnum(char16_t ch) {
    return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') || (ch >= u'0' && ch <= u'9');
}

// Идентификатор XML (NCName) из имени файла: только ASCII-буквы, цифры, '_',
// '-' и '.', не с цифры. CorelDRAW и Inkscape берут его именем объекта.
std::string svgId(const QString& name, std::set<std::string>& used) {
    std::string base;
    for(const QChar ch: name) {
        const char16_t code = ch.unicode();
        const bool ok = isAsciiAlnum(code) || code == u'_' || code == u'-' || code == u'.';
        base += ok ? static_cast<char>(code) : '_';
    }
    if(base.empty() || !(std::isalpha(static_cast<unsigned char>(base.front())) || base.front() == '_'))
        base.insert(base.begin(), '_');
    std::string unique = base;
    for(int n = 2; !used.insert(unique).second; ++n)
        unique = base + '_' + std::to_string(n);
    return unique;
}

// Имя слоя DXF R12: до 31 символа из A-Z, 0-9, '_', '-', '$'.
std::string dxfLayerName(const QString& name, std::set<std::string>& used) {
    constexpr std::size_t maxLength = 31;
    std::string base;
    for(const QChar ch: name) {
        const char16_t code = ch.unicode();
        const bool ok = isAsciiAlnum(code) || code == u'_' || code == u'-' || code == u'$';
        base += ok ? static_cast<char>(std::toupper(static_cast<unsigned char>(code))) : '_';
    }
    if(base.empty()) base = "LAYER";
    base.resize(std::min(base.size(), maxLength));
    std::string unique = base;
    for(int n = 2; !used.insert(unique).second; ++n) {
        const std::string suffix = "_" + std::to_string(n);
        unique = base.substr(0, maxLength - suffix.size()) + suffix;
    }
    return unique;
}

void appendPoint(std::string& out, QPointF point) {
    out += number(point.x());
    out += ' ';
    out += number(point.y());
}

// Контур -- командами пути SVG; дуги точные ('A'), не ломаные. Контуры уже в
// координатах SVG (Y вниз), где знаковый угол дуги > 0 означает рост угла от
// +X к +Y -- это и есть sweep-flag = 1.
void appendContour(std::string& out, const Geo::Polyline& contour) {
    if(contour.size() < 2) return;
    out += 'M';
    appendPoint(out, contour.front());
    // У замкнутого последним идёт замыкающий сегмент -- и дугой, если он дуга.
    for(auto&& [from, to]: Geo::segments(contour)) {
        if(const auto arc = Geo::arcOf(from, to, from.bulge)) {
            std::format_to(std::back_inserter(out), "A{0} {0} 0 {1} {2} ", number(arc->radius),
                std::abs(arc->theta) > std::numbers::pi ? 1 : 0, arc->theta > 0.0 ? 1 : 0);
        } else {
            out += 'L';
        }
        appendPoint(out, to);
    }
    if(contour.closed) out += 'Z';
}

// SVG: размеры в мм и viewBox в тех же числах -- 1 единица = 1 мм в любой
// программе, без догадок о 72/90/96 точках на дюйм. Слой -- один путь
// even-odd: CorelDRAW импортирует его одной составной кривой.
std::string svg(const Page& page) {
    const double width = page.size.width();
    const double height = page.size.height();
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    out += "<!-- GGEasy plot: 1 user unit = 1 mm -->\n";
    std::format_to(std::back_inserter(out),
        "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"{0}mm\" height=\"{1}mm\" viewBox=\"0 0 {0} {1}\">\n",
        number(width), number(height));
    std::set<std::string> ids;
    for(const Placed& placed: page.items) {
        // Y вниз от верхнего края листа; зеркальный определитель Geo::transform
        // учитывает сам -- меняет знак прогибов.
        Geo::Polylines contours = placed.plate->contours;
        Geo::transform(contours, QTransform{1.0, 0.0, 0.0, -1.0, placed.offset.x(), height - placed.offset.y()});
        out += "<g id=\"";
        out += svgId(placed.plate->name, ids);
        out += "\"><title>";
        out += xmlEscaped(placed.plate->name);
        out += "</title><path fill=\"#000000\" fill-rule=\"evenodd\" stroke=\"none\" d=\"";
        for(const Geo::Polyline& contour: contours) appendContour(out, contour);
        out += "\"/></g>\n";
    }
    out += "</svg>\n";
    return out;
}

// DXF R12 (AC1009): POLYLINE/VERTEX с прогибом (42), Y вверх, мм. Заливки в
// R12 нет -- только контуры; слой на исходный файл.
std::string dxf(const Page& page) {
    std::string out;
    const auto group = [&out](int code, std::string_view value) {
        out += std::to_string(code);
        out += "\r\n";
        out.append(value);
        out += "\r\n";
    };
    const auto real = [&group](int code, double value) { group(code, number(value, 6)); };
    // Точка: 10/20/30 -- X/Y/Z.
    const auto point = [&real](double x, double y) {
        real(10, x);
        real(20, y);
        real(30, 0.0);
    };

    std::set<std::string> used;
    std::vector<std::string> layers;
    for(const Placed& placed: page.items) layers.push_back(dxfLayerName(placed.plate->name, used));

    const double width = page.size.width();
    const double height = page.size.height();

    group(0, "SECTION");
    group(2, "HEADER");
    group(9, "$ACADVER");
    group(1, "AC1009");
    group(9, "$INSBASE");
    point(0.0, 0.0);
    group(9, "$EXTMIN");
    point(0.0, 0.0);
    group(9, "$EXTMAX");
    point(width, height);
    group(9, "$LIMMIN");
    real(10, 0.0);
    real(20, 0.0);
    group(9, "$LIMMAX");
    real(10, width);
    real(20, height);
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "TABLES");
    group(0, "TABLE");
    group(2, "LTYPE");
    group(70, "1");
    group(0, "LTYPE");
    group(2, "CONTINUOUS");
    group(70, "0");
    group(3, "Solid line");
    group(72, "65");
    group(73, "0");
    real(40, 0.0);
    group(0, "ENDTAB");
    group(0, "TABLE");
    group(2, "LAYER");
    group(70, std::to_string(layers.size()));
    for(const std::string& layer: layers) {
        group(0, "LAYER");
        group(2, layer);
        group(70, "0");
        group(62, "7");
        group(6, "CONTINUOUS");
    }
    group(0, "ENDTAB");
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "ENTITIES");
    for(std::size_t i{}; i < page.items.size(); ++i) {
        const Placed& placed = page.items[i];
        const std::string& layer = layers[i];
        Geo::Polylines contours = placed.plate->contours;
        Geo::translate(contours, placed.offset);
        for(const Geo::Polyline& contour: contours) {
            if(contour.size() < 2) continue;
            group(0, "POLYLINE");
            group(8, layer);
            group(66, "1");
            point(0.0, 0.0);
            group(70, contour.closed ? "1" : "0");
            for(const Geo::Vertex& vertex: contour) {
                group(0, "VERTEX");
                group(8, layer);
                point(vertex.x(), vertex.y());
                if(vertex.isArc()) group(42, number(vertex.bulge, 10));
            }
            group(0, "SEQEND");
            group(8, layer);
        }
    }
    group(0, "ENDSEC");
    group(0, "EOF");
    return out;
}

// PDF точного размера: страница -- ровно раскладка (ExactMatch, иначе размер
// в пределах ±3 pt притянулся бы к стандартному), поля нулевые, painter
// масштабирован в мм. Округление страницы до целых пунктов меняет лишь поле
// у нижнего края, но не масштаб.
QString writePdf(const Page& page, const QString& path) {
    QPdfWriter writer{path};
    writer.setCreator(u"GGEasy"_s);
    writer.setTitle(page.fileBase);
    writer.setResolution(1200);
    // Размер задаётся книжным, а ориентация -- явно: так лист выходит ровно
    // W x H, как бы QPageSize ни трактовал широкий нестандартный размер.
    const bool landscape = page.size.width() > page.size.height();
    const QSizeF portrait = landscape ? page.size.transposed() : page.size;
    const QPageLayout layout{
        QPageSize{portrait, QPageSize::Millimeter, QString{}, QPageSize::ExactMatch},
        landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
        QMarginsF{},
        QPageLayout::Millimeter,
    };
    if(!writer.setPageLayout(layout))
        return QCoreApplication::translate("Plot", "Cannot set the page size %1 x %2 mm.")
            .arg(page.size.width(), 0, 'f', 3)
            .arg(page.size.height(), 0, 'f', 3);
    QPainter painter;
    if(!painter.begin(&writer))
        return QCoreApplication::translate("Plot", "Cannot write file %1.").arg(QDir::toNativeSeparators(path));
    const double scale = writer.resolution() / 25.4; // точек на мм
    painter.scale(scale, scale);
    paintPage(painter, page);
    if(!painter.end())
        return QCoreApplication::translate("Plot", "Cannot write file %1.").arg(QDir::toNativeSeparators(path));
    return {};
}

} // namespace

//------------------------------------------------------------------------------

void Options::load() {
    QSettings settings;
    settings.beginGroup(u"Plot"_s);
    format = static_cast<Format>(std::clamp(settings.value(u"format"_s, static_cast<int>(format)).toInt(), 0, 2));
    layout = static_cast<Layout>(std::clamp(settings.value(u"layout"_s, static_cast<int>(layout)).toInt(), 0, 1));
    gap = settings.value(u"gap"_s, gap).toDouble();
    margin = settings.value(u"margin"_s, margin).toDouble();
    regMarks = settings.value(u"regMarks"_s, regMarks).toBool();
    drillMarks = static_cast<DrillMarks>(std::clamp(settings.value(u"drillMarks"_s, static_cast<int>(drillMarks)).toInt(), 0, 2));
    drillFile = settings.value(u"drillFile"_s, drillFile).toString();
    smallDrill = settings.value(u"smallDrill"_s, smallDrill).toDouble();
    lineWidth = settings.value(u"lineWidth"_s, lineWidth).toDouble();
    calibX = settings.value(u"calibX"_s, calibX).toDouble();
    calibY = settings.value(u"calibY"_s, calibY).toDouble();
    outDir = settings.value(u"outDir"_s, outDir).toString();
    settings.endGroup();
}

void Options::save() const {
    QSettings settings;
    settings.beginGroup(u"Plot"_s);
    settings.setValue(u"format"_s, static_cast<int>(format));
    settings.setValue(u"layout"_s, static_cast<int>(layout));
    settings.setValue(u"gap"_s, gap);
    settings.setValue(u"margin"_s, margin);
    settings.setValue(u"regMarks"_s, regMarks);
    settings.setValue(u"drillMarks"_s, static_cast<int>(drillMarks));
    settings.setValue(u"drillFile"_s, drillFile);
    settings.setValue(u"smallDrill"_s, smallDrill);
    settings.setValue(u"lineWidth"_s, lineWidth);
    settings.setValue(u"calibX"_s, calibX);
    settings.setValue(u"calibY"_s, calibY);
    settings.setValue(u"outDir"_s, outDir);
    settings.endGroup();
}

bool hasDrills(const AbstractFile* file) {
    if(!file) return false;
    for(Gi::Group* group: file->itemGroups()) {
        if(!group) continue;
        for(const auto& item: *group)
            if(item && item->type() == Gi::Type::Drill) return true;
    }
    return false;
}

Sheet build(std::span<const Job> jobs, AbstractFile* drillFile, const Options& opt) {
    Sheet sheet;

    Geo::Polygons holes;
    if(opt.drillMarks != DrillMarks::None && drillFile)
        holes = drillHoles(drillFile, opt);

    struct Layer {
        const Job* job;
        Geo::Polygons region;
    };
    std::vector<Layer> layers;
    QRectF extent;
    for(const Job& job: jobs) {
        if(!job.file) continue;
        Source src = collect(job.file, opt.lineWidth);
        // Метки сверловки -- просвет в меди (точная разность). Самой
        // сверловки они не касаются: напечатанная отдельно, она и есть
        // отверстия -- краской.
        if(!src.drill && !holes.empty() && !src.region.empty()) src.region -= holes;
        if(src.region.empty()) {
            sheet.empty.push_back(job.file->shortName());
            continue;
        }
        extent |= src.region.boundingRect();
        layers.push_back(Layer{&job, std::move(src.region)});
    }
    if(layers.empty()) return sheet;

    // ОДНА страница на все слои: отдельные файлы совмещаются наложением, а
    // зеркало у всех вокруг одного центра.
    const double margin = std::max(opt.margin, 0.0);
    const QRectF page = extent.adjusted(-margin, -margin, margin, margin);
    sheet.page = page.size();
    const double width = sheet.page.width();
    const double height = sheet.page.height();

    const Geo::Polylines marks = opt.regMarks ? registrationMarks(sheet.page, margin) : Geo::Polylines{};
    // Сцена (Y вверх) -> страница (Y вверх, начало -- левый нижний угол).
    // У QRectF со сценовым Y вверх top() -- это МИНИМАЛЬНЫЙ y.
    const QTransform toPage = QTransform::fromTranslate(-page.left(), -page.top());
    // Зеркало вокруг центра страницы: x' = W - (x - left) = right - x.
    const QTransform toPageMirrored{-1.0, 0.0, 0.0, 1.0, page.right(), -page.top()};

    std::set<QString> names;
    sheet.plates.reserve(layers.size());
    for(const Layer& layer: layers) {
        const Job& job = *layer.job;
        Plate plate;
        plate.name = job.file->shortName();
        const QString base = plate.name
            + (job.mirror ? u"_mirror"_s : QString{})
            + (job.negative ? u"_neg"_s : QString{});
        plate.fileBase = base;
        for(int n = 2; !names.insert(plate.fileBase).second; ++n)
            plate.fileBase = base + u"_"_s + QString::number(n);

        Geo::Polylines contours = layer.region.contours();
        Geo::transform(contours, job.mirror ? toPageMirrored : toPage);
        // Метки и слой не пересекаются (метки -- в поле), так что even-odd
        // их не путает.
        contours.append_range(marks);
        // Негатив: рамка страницы первым контуром, и even-odd сам делает
        // "рамка минус медь" -- контуры региона друг друга не пересекают и
        // все лежат внутри рамки, булева операция не нужна.
        if(job.negative)
            contours.insert(contours.begin(), Geo::rectangle(width, height, QPointF{width * 0.5, height * 0.5}));

        plate.path = Geo::toPath(contours);
        plate.path.setFillRule(Qt::OddEvenFill);
        plate.contours = std::move(contours);
        sheet.plates.push_back(std::move(plate));
    }
    return sheet;
}

std::vector<Page> filePages(const Sheet& sheet, const Options& opt, const QString& combinedName) {
    std::vector<Page> pages;
    if(sheet.plates.empty()) return pages;
    if(opt.layout == Layout::Tiled) {
        // В файл -- одним рядом: лист файла не ограничен бумагой.
        pages.push_back(tiledPage(sheet, static_cast<int>(sheet.plates.size()), std::max(opt.gap, 0.0), combinedName));
        return pages;
    }
    for(const Plate& plate: sheet.plates)
        pages.push_back(Page{plate.fileBase, sheet.page, {Placed{&plate, QPointF{}}}});
    return pages;
}

QString extension(Format format) {
    switch(format) {
    case Format::Dxf: return u".dxf"_s;
    case Format::Pdf: return u".pdf"_s;
    case Format::Svg:
    default         : return u".svg"_s;
    }
}

QString write(const Page& page, const QString& path, Format format) {
    if(format == Format::Pdf) return writePdf(page, path);

    const std::string data = format == Format::Dxf ? dxf(page) : svg(page);
    // Через временный файл: прерванная запись не оставит полфайла на месте
    // прежнего.
    QSaveFile file{path};
    if(!file.open(QIODevice::WriteOnly)) return file.errorString();
    const auto size = static_cast<qint64>(data.size());
    if(file.write(data.data(), size) != size || !file.commit()) return file.errorString();
    return {};
}

double fitRatio(const Sheet& sheet, const Options& opt, QSizeF area) {
    if(sheet.plates.empty()) return 0.0;
    // Область печати -- в мм бумаги, раскладка -- в мм платы: калибровка
    // растягивает вторую.
    const QSizeF sheetArea{area.width() * 100.0 / std::max(opt.calibX, 1.0),
        area.height() * 100.0 / std::max(opt.calibY, 1.0)};
    double ratio = 0.0;
    for(const Page& page: printPages(sheet, opt, sheetArea))
        ratio = std::max(ratio, overflowRatio(page.size, sheetArea));
    return ratio;
}

QString print(QPrinter* printer, const Sheet& sheet, const Options& opt) {
    if(!printer || sheet.plates.empty()) return {};
    QPainter painter;
    if(!painter.begin(printer)) return QCoreApplication::translate("Plot", "Cannot start printing.");

    // Точек устройства на мм -- у самого устройства, на каждый вызов: после
    // выбора другого принтера разрешение другое. Калибровка -- поправка
    // принтера по осям (как Fine scale adjust в KiCad).
    const double scaleX = std::max(opt.calibX, 1.0) / 100.0;
    const double scaleY = std::max(opt.calibY, 1.0) / 100.0;
    const double kx = printer->logicalDpiX() / 25.4 * scaleX;
    const double ky = printer->logicalDpiY() / 25.4 * scaleY;
    // Область печати (страница минус поля) в мм платы. (0, 0) устройства --
    // её левый верхний угол.
    const QSizeF area{printer->width() / kx, printer->height() / ky};

    QString warning;
    const std::vector<Page> pages = printPages(sheet, opt, area);
    for(std::size_t i{}; i < pages.size(); ++i) {
        if(i > 0 && !printer->newPage()) break;
        const Page& page = pages[i];
        // По центру области печати, БЕЗ вписывания: что не влезло -- не
        // влезло, масштаб не трогаем.
        const double dx = (printer->width() - page.size.width() * kx) * 0.5;
        const double dy = (printer->height() - page.size.height() * ky) * 0.5;
        if(dx < 0.0 || dy < 0.0)
            warning = QCoreApplication::translate("Plot",
                "The plot (%1 x %2 mm) does not fit the printable area (%3 x %4 mm) and is cut off. "
                "Choose a larger paper size or another orientation.")
                          .arg(page.size.width() * scaleX, 0, 'f', 1)
                          .arg(page.size.height() * scaleY, 0, 'f', 1)
                          .arg(area.width() * scaleX, 0, 'f', 1)
                          .arg(area.height() * scaleY, 0, 'f', 1);
        painter.setTransform(QTransform{kx, 0.0, 0.0, ky, dx, dy});
        paintPage(painter, page);
    }
    painter.end();
    return warning;
}

} // namespace Plot
