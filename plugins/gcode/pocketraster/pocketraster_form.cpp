// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/*******************************************************************************
 * Author    :  Damir Bakiev                                                    *
 * Version   :  na                                                              *
 * Date      :  XXXXX XX, 2025                                                  *
 * Website   :  na                                                              *
 * Copyright :  Damir Bakiev 2016-2026                                          *
 * License   :                                                                  *
 * Use, modification & distribution is subject to Boost Software License Ver 1. *
 * http://www.boost.org/LICENSE_1_0.txt                                         *
 *******************************************************************************/

#include "pocketraster_form.h"
#include "gi_dbg.h"
#include "ui_pocketrasterform.h"

#include "abstract_file.h"
#include "gc_pathutils.h"
#include "gi_point.h"
#include "graphicsview.h"
#include "project.h"
#include "settings.h"
#include <QMessageBox>

#include <cmath>
#include <optional>

namespace PocketRaster {

namespace {

// Тела контуров -- ОБЪЕДИНЕНИЕМ, а не even-odd: контур панели с контурами плат
// внутри остаётся панелью, а не рамкой с платами-дырами. Вырезы внутри платы
// при этом засвечиваются тоже -- маске это безразлично.
Geo::Polygons rasterBodiesOf(const Geo::Polylines& contours) {
    std::vector<Geo::Polygon> bodies;
    for(const Geo::Polyline& contour: contours)
        if(Geo::Polygon body{contour}; !body.empty()) bodies.push_back(std::move(body));
    return Geo::Polygons{std::span<const Geo::Polygon>{bodies}};
}

struct RasterOutline {
    Geo::Polygons bodies;
    QStringList names;
};

// Слой контура платы -- со всех файлов проекта, видимых и скрытых, в любом
// виде: файл отдаёт его своими объектами, а не элементами сцены. Склейка --
// по файлу: обрывкам разных слоёв срастаться незачем.
RasterOutline rasterOutlineLayer() {
    RasterOutline outline;
    Geo::Polylines contours;
    for(AbstractFile* file: App::project().files()) {
        Geo::Polylines closed = boardContours(file->boardOutlineCurves());
        if(closed.empty()) continue;
        outline.names.push_back(file->shortName());
        contours.append_range(std::move(closed));
    }
    if(!contours.empty()) outline.bodies = rasterBodiesOf(contours);
    return outline;
}

// Граница платы, заданная кнопкой, -- точными контурами; иначе рамка проекта.
// Именно worckRect -- ОДНА плата, шаг раскладки: скрипт G-кода сам раскладывает
// УП по копиям, а рамка всей панели (LayoutFrames) засветила бы их внахлёст.
Geo::Polygons rasterFrameBodies() {
    if(const Geo::Polylines& border = App::project().boardBorder(); !border.empty())
        return rasterBodiesOf(border);
    const QRectF rect = App::project().worckRect();
    if(rect.isEmpty()) return {};
    return Geo::Polygons{Geo::Polygon{Geo::rectangle(rect.width(), rect.height(), rect.center())}};
}

// Точка заведомо внутри тела, а не на его контуре: contains точен, и вершина
// (площадка у самого края платы) давала бы ложное «вне границы».
std::optional<QPointF> rasterProbe(const Geo::Polygon& body) {
    const QRectF box = body.boundingRect();
    if(body.contains(box.center())) return box.center();
    // Кольцо (репер) и прочее с пустотой посередине -- шаг внутрь от середины
    // первого ребра внешней границы. Обход её канонический, против часовой --
    // тело слева, но проверяются обе стороны: дешевле, чем полагаться.
    const Geo::Polyline& outer = body.outer();
    if(outer.size() >= 2) {
        const QPointF from = outer[0], to = outer[1];
        const QPointF chord = to - from;
        const double length = std::hypot(chord.x(), chord.y());
        if(length > 0.0) {
            const QPointF right{chord.y() / length, -chord.x() / length};
            // Середина самого ребра: у дуги -- со стрелой прогиба вправо.
            const QPointF mid = (from + to) / 2.0 + right * (outer[0].bulge * length / 2.0);
            const double step = std::min(1e-3, length / 4.0);
            for(const double side: {-1.0, +1.0})
                if(const QPointF pt = mid + right * (side * step); body.contains(pt)) return pt;
        }
    }
    constexpr int grid = 7;
    for(int y = 1; y < grid; ++y)
        for(int x = 1; x < grid; ++x)
            if(const QPointF pt{box.left() + box.width() * x / grid, box.top() + box.height() * y / grid}; body.contains(pt))
                return pt;
    return std::nullopt;
}

// По пробе на каждое выделенное тело и на каждую открытую линию.
std::vector<QPointF> rasterProbes(const GCode::Params& gcp) {
    std::vector<QPointF> probes;
    for(const Geo::Polygon& body: gcp.closedCurves)
        if(const auto pt = rasterProbe(body)) probes.push_back(*pt);
    for(const Geo::Polyline& curve: gcp.openCurves)
        if(!curve.empty()) probes.push_back(curve.front());
    return probes;
}

// Тела региона, накрывшие хоть одну пробу, и накрыты ли все. Тела региона
// попарно не пересекаются, так что накрытая проба дальше не проверяется.
std::vector<Geo::Polygon> rasterCover(const Geo::Polygons& region, const std::vector<QPointF>& probes, bool& all) {
    std::vector<Geo::Polygon> kept;
    std::vector<uint8_t> hit(probes.size());
    for(const Geo::Polygon& body: region) {
        const QRectF box = body.boundingRect();
        bool any{};
        for(size_t i{}; i < probes.size(); ++i)
            if(!hit[i] && box.contains(probes[i]) && body.contains(probes[i])) hit[i] = 1, any = true;
        if(any) kept.push_back(body);
    }
    all = r::all_of(hit, [](uint8_t h) { return h != 0; });
    return kept;
}

} // namespace

Form::Form(GCode::Plugin* plugin)
    : GCode::Form{plugin, new Creator}
    , ui(new Ui::PocketRasterForm)
    , names{tr("Raster On"), tr("Raster Outside"), tr("Raster Inside")} {
    ui->setupUi(content);

    setWindowTitle(tr("Pocket Raster Toolpath"));

    MySettings settings;
    settings.beginGroup(u"PocketRasterForm"_s);
    settings.getValue(ui->cbxPass);
    settings.getValue(ui->dsbxAcc);
    settings.getValue(ui->dsbxAngle);
    settings.getValue(ui->rbClimb);
    settings.getValue(ui->rbConventional);
    settings.getValue(ui->rbFast);
    settings.getValue(ui->rbInside);
    settings.getValue(ui->rbNormal);
    settings.getValue(ui->rbOutside);
    settings.getValue(ui->cbxBorder);
    settings.endGroup();

    rb_clicked();

    connect(ui->rbClimb, &QRadioButton::clicked, this, &Form::rb_clicked);
    connect(ui->rbConventional, &QRadioButton::clicked, this, &Form::rb_clicked);
    connect(ui->rbInside, &QRadioButton::clicked, this, &Form::rb_clicked);
    connect(ui->rbOutside, &QRadioButton::clicked, this, &Form::rb_clicked);
    // Фрезеровка/HLDI -- только граница: rb_clicked() перебил бы имя УП
    // именем по умолчанию, а от типа оно не зависит.
    connect(ui->rbFast, &QRadioButton::clicked, this, &Form::updateBorder);
    connect(ui->rbNormal, &QRadioButton::clicked, this, &Form::updateBorder);
    connect(ui->cbxBorder, &QComboBox::currentIndexChanged, this, &Form::updateBorder);
    // Рамку задали кнопкой или авторасстановкой, открыли проект.
    connect(&App::project(), &Project::layoutFrameUpdate, this, &Form::updateBorder);

    connect(ui->toolHolder1, &ToolSelectorForm::updateName, this, &Form::updateName);

    connect(leName, &QLineEdit::textChanged, this, &Form::onNameTextChanged);

    //
}

Form::~Form() {

    MySettings settings;
    settings.beginGroup(u"PocketRasterForm"_s);
    settings.setValue(ui->cbxPass);
    settings.setValue(ui->dsbxAcc);
    settings.setValue(ui->dsbxAngle);
    settings.setValue(ui->rbClimb);
    settings.setValue(ui->rbConventional);
    settings.setValue(ui->rbFast);
    settings.setValue(ui->rbInside);
    settings.setValue(ui->rbNormal);
    settings.setValue(ui->rbOutside);
    settings.setValue(ui->cbxBorder);
    settings.endGroup();
    delete ui;
}

void Form::computePaths() {
    const auto tool{ui->toolHolder1->tool()};

    if(!tool.isValid()) {
        tool.errorMessageBox(this);
        return;
    }

    if(!getNewGcpWithGi()) return;

    // Граница растра HLDI снаружи -- здесь, в GUI-потоке: расчёт идёт рабочим,
    // и ни проекта, ни сцены ему не читать. Получит её готовой.
    if(borderEnabled()) {
        BorderChoice border = resolveBorder(&gcp);
        if(!border.question.isEmpty()
            && QMessageBox::question(this, tr("Raster border"), border.question,
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
                != QMessageBox::Yes)
            return;
        gcp.boundary = std::move(border.region);
        gcp.params[Creator::Border] = static_cast<qsizetype>(borderMode());
    }

    gcp.setConvent(ui->rbConventional->isChecked());
    gcp.setSide(side);
    gcp.tools.push_back(tool);

    gcp.params[Creator::UseAngle] = ui->dsbxAngle->value();
    gcp.params[GCode::Params::Depth] = dsbxDepth->value();
    gcp.params[Creator::Pass] = ui->cbxPass->currentIndex();
    if(ui->rbFast->isChecked()) {
        gcp.params[Creator::Fast] = true;
        gcp.params[Creator::AccDistance] = (tool.feedRate_mmPerSec() * tool.feedRate_mmPerSec()) / (2 * ui->dsbxAcc->value());
    }

    fileCount = 1;
    emit createToolpath(&gcp);
}

void Form::updateName() {
    const auto& tool{ui->toolHolder1->tool()};
    if(tool.type() != Tool::Laser)
        ui->rbNormal->setChecked(true);
    ui->rbFast->setEnabled(tool.type() == Tool::Laser);

    leName->setText(names[side]);
    // HLDI могло только что погаснуть (не лазер) -- setChecked сигнала clicked
    // не шлёт, и граница узнаёт об этом отсюда.
    updateBorder();
}

void Form::updatePixmap() {
    ui->lblPixmap->setPixmap(QIcon::fromTheme(pixmaps[direction]).pixmap(QSize(150, 150)));
}

void Form::rb_clicked() {

    if(ui->rbOutside->isChecked())
        side = GCode::Outer;
    else if(ui->rbInside->isChecked())
        side = GCode::Inner;

    if(ui->rbClimb->isChecked())
        direction = GCode::Climb;
    else if(ui->rbConventional->isChecked())
        direction = GCode::Conventional;

    updateName();
    updateButtonIconSize();

    updatePixmap();
}

void Form::resizeEvent(QResizeEvent* event) {
    updatePixmap();
    QWidget::resizeEvent(event);
}

void Form::showEvent(QShowEvent* event) {
    updatePixmap();
    // Базовая, а не QWidget: в GCode::Form::showEvent сбрасывается режим правки.
    GCode::Form::showEvent(event);
    // Пока форма была скрыта, могли загрузить слой контура или сменить рамку.
    updateBorder();
}

void Form::onNameTextChanged(const QString& arg1) { fileName_ = arg1; }

Form::Border Form::borderMode() const { return static_cast<Border>(std::max(ui->cbxBorder->currentIndex(), 0)); }

bool Form::borderEnabled() const { return side == GCode::Outer && ui->rbFast->isChecked(); }

bool Form::skipGi(const Gi::Item* gi) const {
    // «По выделенному» -- прежнее поведение целиком, вместе с приёмом «кольцо
    // контура в виде Normal плюс маска»: там кольцо как раз материал.
    if(!borderEnabled() || borderMode() == Border::Selection) return false;
    const AbstractFile* file = gi->file();
    return file && file->isBoardOutlineItem(gi);
}

Form::BorderChoice Form::resolveBorder(const GCode::Params* selection) const {
    const Border mode = borderMode();
    if(mode == Border::Selection)
        return {.label = tr("The raster ends about 1 mm beyond the selected objects.")};

    const QString notSet = tr("Not set: the raster ends about 1 mm beyond the selected objects.");
    auto sizeOf = [](const Geo::Polygons& region) {
        const QRectF rect = region.boundingRect();
        return std::pair{QString::number(rect.width(), 'f', 1), QString::number(rect.height(), 'f', 1)};
    };
    const std::vector<QPointF> probes = selection ? rasterProbes(*selection) : std::vector<QPointF>{};
    // Прежняя рамка: габарит выделенного плюс луч и миллиметр, как у
    // Creator::groupedPaths без границы. Ею граница расширяется, если часть
    // выделенного лежит вне её (реперы, объекты на техполях): засвечиваются
    // они так же, как и раньше, а не пропадают.
    auto legacyFrame = [&] {
        QRectF rect = selection->closedCurves.boundingRect();
        for(const Geo::Polyline& curve: selection->openCurves)
            rect = rect.united(curve.boundingRect());
        const double margin = ui->toolHolder1->tool().getDiameter(dsbxDepth->value()) + 1.0;
        return Geo::Polygons{Geo::Polygon{GCode::boundingFrame(rect, margin)}};
    };

    bool outsideOutline{};
    if(mode == Border::Auto || mode == Border::Outline) {
        const RasterOutline outline = rasterOutlineLayer();
        if(!outline.bodies.empty()) {
            const auto [width, height] = sizeOf(outline.bodies);
            const QString label = tr("Outline layer: %1, %2 × %3 mm").arg(outline.names.join(u", "_s), width, height);
            // Без проб (ни одного годного тела) судить не о чем -- контур целиком.
            if(!selection || probes.empty()) return {.region = outline.bodies, .label = label};
            // Нескольким платам (техполя, панель) -- каждой свой контур:
            // берутся те, в которых что-то выделено.
            bool all{};
            const std::vector<Geo::Polygon> kept = rasterCover(outline.bodies, probes, all);
            if(all && !kept.empty())
                return {.region = Geo::Polygons{std::span<const Geo::Polygon>{kept}}, .label = label};
            if(mode == Border::Outline) {
                Geo::Polygons region = kept.empty() ? outline.bodies : Geo::Polygons{std::span<const Geo::Polygon>{kept}};
                if(!all) region |= legacyFrame();
                return {.region = std::move(region), .label = label};
            }
            outsideOutline = true; // Auto: не всё внутри контура -- дальше рамка
        } else if(mode == Border::Outline) {
            return {
                .label = tr("No board outline layer: the raster ends about 1 mm beyond the selected objects."),
                .question = tr("No board outline layer is loaded: the raster ends about 1 mm beyond the selected objects.\n\n"
                               "Continue?"),
            };
        }
    }

    if(const Geo::Polygons frame = rasterFrameBodies(); !frame.empty()) {
        const auto [width, height] = sizeOf(frame);
        const QString label = tr("Board border (violet frame), %1 × %2 mm").arg(width, height);
        if(!selection) return {.region = frame, .label = label};
        bool all{};
        rasterCover(frame, probes, all);
        Geo::Polygons region = frame;
        if(!all) region |= legacyFrame();
        return {.region = std::move(region), .label = label};
    }

    if(outsideOutline)
        return {
            .label = notSet,
            .question = tr("Some selected objects lie outside the board outline layer, and the board border (violet frame) "
                           "is not set: the raster ends about 1 mm beyond the selected objects.\n\n"
                           "Continue?"),
        };
    return {
        .label = notSet,
        .question = tr("The board border is not set: the raster ends about 1 mm beyond the selected objects.\n"
                       "Set it with \"Set Board Border\" or load the board outline layer.\n\n"
                       "Continue?"),
    };
}

void Form::updateBorder() {
    const bool enabled = borderEnabled();
    ui->labelBorder->setEnabled(enabled);
    ui->cbxBorder->setEnabled(enabled);
    ui->lblBorder->setEnabled(enabled);
    // Скрытой форме считать незачем: покажут -- showEvent досчитает. Заодно
    // это и конструктор плагина: форма ещё не в доке.
    if(!isVisible()) return;
    ui->lblBorder->setText(resolveBorder(nullptr).label);
}

void Form::editFile(GCode::File* file) {
    const GCode::Params& params = file->params();
    auto value = [&params](const QString& key) {
        auto it = params.params.find(key);
        return it == params.params.end() ? GCode::Variant{} : it->second;
    };

    // Переключатели первыми: их rb_clicked() зовёт updateName() и перебивает
    // имя. База ставит его после.
    if(params.params.contains(GCode::Params::Side))
        (params.side() == GCode::Inner ? ui->rbInside : ui->rbOutside)->setChecked(true);
    if(params.params.contains(GCode::Params::Convent))
        (params.convent() ? ui->rbConventional : ui->rbClimb)->setChecked(true);

    // Инструмент -- копия, снятая при расчёте: в базе инструментов его могло уже
    // не остаться. До HLDI: смена инструмента зовёт updateName(), а тот у
    // не-лазера включает фрезеровку.
    if(!params.tools.empty()) ui->toolHolder1->setTool(params.tool());
    const bool fast = value(Creator::Fast).toBool();
    (fast ? ui->rbFast : ui->rbNormal)->setChecked(true);

    if(params.params.contains(Creator::UseAngle)) ui->dsbxAngle->setValue(value(Creator::UseAngle).toDouble());
    if(params.params.contains(Creator::Pass)) ui->cbxPass->setCurrentIndex(value(Creator::Pass).toInt());
    // Ускорения в УП нет -- только путь разгона v²/2a (computePaths). Обратно --
    // через подачу того же инструмента.
    if(const double accDistance = value(Creator::AccDistance).toDouble(); fast && accDistance > 0.0 && !params.tools.empty()) {
        const double feed = params.tool().feedRate_mmPerSec();
        ui->dsbxAcc->setValue(feed * feed / (2.0 * accDistance));
    }
    // У УП, посчитанных до выбора границы, ключа нет -- остаётся текущий выбор,
    // и «Обновить» посчитает её уже с границей.
    if(params.params.contains(Creator::Border)) ui->cbxBorder->setCurrentIndex(value(Creator::Border).toInt());

    rb_clicked();

    // База ставит глубину, имя, сторону платы и восстанавливает сцену.
    GCode::Form::editFile(file);
}

} // namespace PocketRaster

#include "moc_pocketraster_form.cpp"
