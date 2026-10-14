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
#include "gi_point.h"

#include "drill/drill_file.h"
#include "gc_file.h"
#include "gc_propertiesform.h"
#include "gc_types.h"
#include "geo/boolean.h"
#include "gi.h"
#include "gi_pinsdialog.h"
#include "project.h"
#include "settingsdialog.h"
#include "tool_database.h"

#include <QGraphicsSceneEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStyleOptionGraphicsItem>
#include <QTimer>

#include <cmath>
#include <set>

bool updateRect() {
    QRectF rect(App::grView().getSelectedBoundingRect());
    if(rect.isEmpty()) {
        // Кнопки -- набором Yes|No, а умолчание -- Yes: прежний вызов
        // question(..., No, Yes) читался как «кнопка No, умолчание Yes», и
        // согласиться в окне было просто нечем.
        if(QMessageBox::question(nullptr, {},
               QObject::tr("There are no selected items to define the border.\n"
                           "The old border will be used."),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
            != QMessageBox::Yes)
            return false;
    }
    App::layoutFrames().updateRect();
    return true;
}

Geo::Polylines uniqueCurves(Geo::Polylines curves) {
    // Ключ -- вершины в нанометрах: шум double у одинаковых штрихов уходит, а
    // настоящая геометрия на таком шаге не различается. Направление --
    // каноническое: из двух обходов берётся меньший ключ, так что отрезок и
    // он же навстречу -- одно.
    using Key = std::vector<int64_t>;
    auto keyOf = [](const Geo::Polyline& curve) {
        constexpr double perMm = 1e6;
        Key key;
        key.reserve(curve.size() * 3 + 1);
        key.push_back(curve.closed);
        for(size_t i{}; i < curve.size(); ++i) {
            const Geo::Vertex& vertex = curve[i];
            key.push_back(std::llround(vertex.x() * perMm));
            key.push_back(std::llround(vertex.y() * perMm));
            // Хвостовой прогиб открытой кривой не описывает никакого сегмента.
            const bool tail = !curve.closed && i + 1 == curve.size();
            key.push_back(tail ? 0 : std::llround(vertex.bulge * perMm));
        }
        return key;
    };
    std::set<Key> seen;
    std::erase_if(curves, [&](const Geo::Polyline& curve) {
        return !seen.insert(std::min(keyOf(curve), keyOf(curve.reversed()))).second;
    });
    return curves;
}

Geo::Polylines boardContours(Geo::Polylines curves) {
    std::erase_if(curves, [](const Geo::Polyline& curve) { return curve.size() < 2; });
    curves = uniqueCurves(std::move(curves));

    // Осевые путей апертур -- открытые обрывки: контур платы в Гербере это
    // десятки отдельных штрихов. Клей -- тот же, что у выделения цепочки по
    // Shift+щелчку (Gi::DataPath) и у сырых путей в расчёте УП.
    const double glue = App::project().glue();
    Geo::stitch(curves, glue);

    for(Geo::Polyline& curve: curves) {
        if(curve.size() < 3) continue;
        // stitch замыкает только то, где вершин больше трёх, а полная
        // окружность одной дугой (G75) приходит тремя: две дуговые вершины и
        // повтор начала -- им парсер её размыкает. Две полуокружности
        // отдельными штрихами после склейки -- те же три.
        if(!curve.closed && Geo::distance(curve.front(), curve.back()) <= glue)
            curve.closed = true;
        // Повтор начала в хвосте замкнутого -- долой. Polyline::close снимает
        // лишь точный повтор, а при ненулевом клее конец отстоит от начала:
        // остался бы отрезок короче клея, и точная геометрия на нём спотыкается.
        if(curve.closed && Geo::distance(curve.front(), curve.back()) <= glue)
            curve.pop_back();
    }

    // Годится только то, что возьмёт точная геометрия: замкнутое, невырожденное,
    // без самокасаний. Негодный контур Geo::Polygon молча превратил бы в пустоту.
    std::erase_if(curves, [](const Geo::Polyline& curve) { return !Geo::isExactContour(curve); });
    return curves;
}

bool setBoardBorderFromSelection() {
    QWidget* const parent = App::grView().window();
    const QString title = QObject::tr("Board border");

    Geo::Polylines curves;
    for(const Gi::Item* gi: App::grView().selectedItems<Gi::Item>()) {
        const int type = gi->type();
        // Отверстия и всё служебное (траектории УП, превью, ошибки) контуром
        // платы не бывают.
        if(type == Gi::Type::Drill || (type >= Gi::Type::Path_ && type < Gi::Type::ShapeBegin)) continue;
        if(type == Gi::Type::DataSolid) {
            // Заливка (слой в виде Normal) -- внешние края тел. У обводки
            // контура это наружный край штриха, на полширины линии дальше
            // настоящего края платы: точнее -- путями апертур.
            for(const Geo::Polygon& body: gi->region())
                curves.push_back(body.outer());
        } else
            curves.append_range(gi->curves()); // в сцене: трансформация файла уже применена
    }

    const QString hint = QObject::tr("Shift+click on an aperture-path segment selects the whole connected outline.");
    if(curves.empty()) {
        QMessageBox::information(parent, title,
            QObject::tr("Select the closed board contour, e.g. the board outline layer shown as aperture paths.")
                + u"\n"_s + hint);
        return false;
    }

    Geo::Polylines contours = boardContours(std::move(curves));
    if(contours.empty()) {
        QMessageBox::warning(parent, title,
            QObject::tr("The selected objects do not form a closed contour.\n"
                        "Gaps between the ends must not exceed the Glue setting (G-Code Properties): %1 mm.")
                    .arg(App::project().glue())
                + u"\n"_s + hint);
        return false;
    }

    // Рамка проекта -- ещё и шаг раскладки (скрипт G-кода раскладывает УП по
    // workRect): сменится габарит -- уедут все копии на плате-панели.
    QRectF rect;
    for(const Geo::Polyline& contour: contours)
        rect = rect.isNull() ? contour.boundingRect() : rect.united(contour.boundingRect());
    Project& project = App::project();
    if(project.stepsX() * project.stepsY() > 1 && rect != project.worckRect()
        && QMessageBox::question(parent, title,
               QObject::tr("The board border is also the step of the step-and-repeat layout (%1 × %2), "
                           "so the copies will move.\nSet the border anyway?")
                   .arg(project.stepsX())
                   .arg(project.stepsY()),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
        return false;

    project.setBoardBorder(std::move(contours));
    return true;
}

namespace Gi {

Marker::Marker(Type type)
    : QGraphicsObject{nullptr}
    , type_{type} {
    setAcceptHoverEvents(true);
    if(type_ == Home) {
        App::home.set(this);
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 0, 90);
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 270, -90);
        setToolTip(QObject::tr("G-Code Home Point"));
    } else {
        App::zero.set(this);
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 90, 90);
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 360, -90);
        setToolTip(QObject::tr("G-Code Zero Point"));
    }
    baseShape_.addEllipse(QRectF(QPointF(-3, -3), QSizeF(6, 6)));
    applyScaleMode();
}

// Постоянный экранный размер -- штатным флагом Qt, а не painter->scale() в
// paint(). Прежний вариант рисовал в 10*sf раз крупнее, чем объявляли
// boundingRect() и shape(): за пределами габарита оставались следы
// перерисовки, мышь промахивалась мимо видимого глифа, а сама трансформация
// утекала в следующий элемент -- вид выставляет DontSavePainterState, и
// save()/restore() здесь не было.
//
// Масштаб 10 и отражение по Y повторяют прежний вид: сцена перевёрнута по Y
// (scale(1,-1) в конструкторе вида), а untransformable-элемент рисуется в
// ориентации устройства.
static const QTransform kScreenUnits = QTransform::fromScale(10, -10);

void Marker::applyScaleMode() {
    prepareGeometryChange();
    const bool screenSize = App::settings().scaleHZMarkers();
    setFlag(ItemIgnoresTransformations, screenSize);
    path_ = screenSize ? kScreenUnits.map(basePath_) : basePath_;
    shape_ = screenSize ? kScreenUnits.map(baseShape_) : baseShape_;
    dotRadius_ = screenSize ? 20 : 2;
    rect_ = path_.boundingRect();
}

Marker::~Marker() {
    (type_ == Home) ? App::home.set(nullptr) : App::zero.set(nullptr);
}

QRectF Marker::boundingRect() const {
    if(App::drawPdf())
        return {};
    return rect_;
}

void Marker::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* /*widget*/) {
    if(App::drawPdf())
        return;

    QColor c(type_ == Home ? App::settings().guiColor(GuiColors::Home) : App::settings().guiColor(GuiColors::Zero));
    if(option->state & QStyle::State_MouseOver)
        c.setAlpha(200);
    if(!(flags() & QGraphicsItem::ItemIsMovable))
        c.setAlpha(static_cast<int>(c.alpha() * 0.5));
    painter->setPen(Qt::NoPen);
    painter->setBrush(c);
    painter->drawPath(path_);
    painter->setPen(c);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPointF{}, dotRadius_, dotRadius_);
}

// Именно path_, а не shape_: попадание всегда считалось по самому глифу
// (shape_ у Marker не использовался).
QPainterPath Marker::shape() const { return App::drawPdf() ? QPainterPath() : path_; }

int Marker::type() const { return static_cast<int>(type_ ? Gi::Type::MarkHome : Gi::Type::MarkZero); }

void Marker::resetPos(bool flUpdateRect) {
    if(flUpdateRect && !updateRect())
        return;

    const QRectF rect(App::layoutFrames().boundingRect());

    static constexpr std::array corner{
        &QRectF::bottomLeft,
        &QRectF::bottomRight,
        &QRectF::topLeft,
        &QRectF::topRight,
        &QRectF::center,
        &QRectF::center,
    };

    if(type_ == Home)
        if(App::settings().mkrHomePos() == corner.size())
            setPos({});
        else
            setPos((rect.*corner[App::settings().mkrHomePos()])() + App::settings().mkrHomeOffset());
    else if(App::settings().mkrZeroPos() == corner.size())
        setPos({});
    else
        setPos((rect.*corner[App::settings().mkrZeroPos()])() + App::settings().mkrZeroOffset());

    updateGCPForm();

    if(type_ == Home)
        App::project().setHomePos(pos());
    else
        App::project().setZeroPos(pos());
}

void Marker::setPosX(double x) {
    QPointF point(pos());
    if(qFuzzyCompare(point.x(), x))
        return;
    point.setX(x);
    setPos(point);
    // Как и в setPosY: положение в проекте -- то, что уходит в файл проекта и
    // в свойства УП для скриптов (properties.home/zero). Без этого правка X в
    // поле формы терялась при сохранении проекта.
    if(type_ == Home)
        App::project().setHomePos(pos());
    else
        App::project().setZeroPos(pos());
}

void Marker::setPosY(double y) {
    QPointF point(pos());
    if(qFuzzyCompare(point.y(), y))
        return;
    point.setY(y);
    setPos(point);
    if(type_ == Home)
        App::project().setHomePos(pos());
    else
        App::project().setZeroPos(pos());
}

void Marker::updateGCPForm() {
    if(App::gcPropertiesForm.ptr())
        App::gcPropertiesForm().updatePosDsbxs();

    if(type_ == Zero) {
        App::project().setZeroPos(pos());
        for(auto pin: App::pins())
            pin->updateToolTip();

    } else {
        App::project().setHomePos(pos());
    }
}

namespace {
// Положение маркера в момент нажатия: по нему отпускание решает, сдвинулся ли
// он на самом деле. Одно на оба маркера -- тащат всегда только один.
QPointF markerPressPos;
} // namespace

void Marker::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if(!moved) markerPressPos = pos(); // первое движение -- маркер ещё там, где нажали
    QGraphicsItem::mouseMoveEvent(event);
    moved = true;
    setPos(App::settings().getSnappedPos(pos(), event->modifiers()));

    updateGCPForm();
}

void Marker::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseReleaseEvent(event);
    // УП пересобираются здесь, а не на каждом движении, и только если маркер
    // и правда сдвинулся: щелчок с дрожанием мыши в пиксель (а с привязкой к
    // сетке -- и побольше) оставляет его на месте. Zero сдвигает все УП, Home
    // -- концовку лазерных.
    if(std::exchange(moved, false) && pos() != markerPressPos)
        GCode::scheduleRegenerateGCodeFiles();
}

void Marker::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if(!(flags() & QGraphicsItem::ItemIsMovable))
        return;
    // Двойной щелчок ставит маркер по рамке, а рамку -- по выделенному
    // (updateRect). УП зависят от обоих: от рамки -- раскладка копий
    // мультипликации. Не сдвинулось ни то, ни другое -- и пересчёт ни к чему.
    const QPointF before = pos();
    const QRectF workRect = App::project().worckRect();
    resetPos();
    // QMatrix matrix(scene()->views().first()->matrix());
    // matrix.translate(-pos().x(), pos().y());
    // scene()->views().first()->setMatrix(matrix);
    updateGCPForm();
    if(pos() != before || App::project().worckRect() != workRect)
        GCode::scheduleRegenerateGCodeFiles();
    QGraphicsItem::mouseDoubleClickEvent(event);
}
void Marker::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    auto action = menu.addAction(QObject::tr("Fixed"), this, [this](bool fl) {
        setFlag(QGraphicsItem::ItemIsMovable, !fl);
    });
    action->setCheckable(true);
    action->setChecked(!(flags() & QGraphicsItem::ItemIsMovable));
    menu.addSeparator();
    action = menu.addAction(QIcon::fromTheme(u"configure-shortcuts"_s), QObject::tr("&Settings"), [] {
        SettingsDialog(nullptr, SettingsDialog::Utils).exec();
    });
    menu.exec(event->screenPos());
}

////////////////////////////////////////////////
/// \brief Pin::Pin
/// \param parent
///
Pin::Pin()
    : QGraphicsObject{nullptr}
    , index_(ctr_++) {
    setObjectName(u"Pin"_s);
    setAcceptHoverEvents(true);

    if(index_ % 2) {
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 0, 90);
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 270, -90);
    } else {
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 90, 90);
        basePath_.arcTo(QRectF(QPointF(-3, -3), QSizeF(6, 6)), 360, -90);
    }
    baseShape_.addEllipse(QRectF(QPointF(-3, -3), QSizeF(6, 6)));
    applyScaleMode();

    setZValue(std::numeric_limits<double>::max() - index_);
}

void applyMarkersScaleMode() {
    if(auto* p = App::home.ptr()) p->applyScaleMode();
    if(auto* p = App::zero.ptr()) p->applyScaleMode();
    for(auto* p: {App::pin0.ptr(), App::pin1.ptr(), App::pin2.ptr(), App::pin3.ptr()})
        if(p) p->applyScaleMode();
}

void Pin::applyScaleMode() { // см. Marker::applyScaleMode
    prepareGeometryChange();
    const bool screenSize = App::settings().scalePinMarkers();
    setFlag(ItemIgnoresTransformations, screenSize);
    path_ = screenSize ? kScreenUnits.map(basePath_) : basePath_;
    shape_ = screenSize ? kScreenUnits.map(baseShape_) : baseShape_;
    dotRadius_ = screenSize ? 20 : 2;
    rect_ = path_.boundingRect();
}

Pin::~Pin() { }

QRectF Pin::boundingRect() const {
    if(App::drawPdf())
        return {};
    return rect_;
}

void Pin::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* /*widget*/) {
    if(App::drawPdf())
        return;

    QColor c(App::project().pinUsed(index_) ? App::settings().guiColor(GuiColors::Pin) : QColor(127, 127, 127, 127));
    if(option->state & QStyle::State_MouseOver)
        c.setAlpha(200);
    if(!(flags() & QGraphicsItem::ItemIsMovable))
        c.setAlpha(static_cast<int>(c.alpha() * 0.5));
    // c.setAlpha(50);
    painter->setPen(Qt::NoPen);
    painter->setBrush(c);
    painter->drawPath(path_);
    painter->setPen(c);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPointF{}, dotRadius_, dotRadius_);
}

QPainterPath Pin::shape() const {
    if(App::drawPdf())
        return {};
    return shape_;
}

// Симметрия держится всегда (см. описание Pin), а что едет вслед за
// булавкой -- решает режим проекта:
//  * ось -- пара: булавка и её зеркальный партнёр относительно оси
//    переворота, ось при этом стоит;
//  * точка -- все четыре: противоположная по диагонали уходит на столько же
//    в обратную сторону, остальные две достраивают прямоугольник (так
//    булавки двигались исходно), точка стоит.
// Ctrl -- вся расстановка целиком, а с ней и ось/точка. Одной булавки, без
// партнёра, больше нет: она ломала симметрию, а на несимметричные штифты
// перевёрнутая плата не садится.
void Pin::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    // Базовый mouseMoveEvent не зовётся: он двигает ещё и все выделенные
    // подвижные элементы сцены (булавка не выделяемая, и выделение при
    // нажатии на неё не снимается), а положение здесь всё равно своё.
    if(!(event->buttons() & Qt::LeftButton) || !(flags() & ItemIsMovable))
        return;
    if(!std::exchange(moved, true))
        showSymmetryMark(true);

    // Всё -- от положений в момент нажатия, а не от прошлого события:
    // модификатор можно сменить посреди перетаскивания, и расстановка
    // пересчитается заново, без накопленного сдвига.
    QPointF pt = App::settings().getSnappedPos(
        lastPos_ + event->scenePos() - event->buttonDownScenePos(Qt::LeftButton), event->modifiers());

    const auto pins = App::pins();
    Positions pos;
    for(int i{}; i < 4; ++i)
        pos[i] = pins[i]->lastPos_;

    const bool pointMode = pointSymmetry();
    QPointF c = pressCenter_;
    const int idx = index();
    if(event->modifiers() & Qt::ControlModifier) {
        const QPointF delta{pt - lastPos_};
        for(QPointF& p: pos)
            p += delta;
        c += delta;
    } else {
        // Через ось (в режиме точки -- и через горизонталь точки) булавка не
        // переходит: на самой линии булавки сливаются в одну точку -- два
        // штифта на оси переворота тоже рабочая схема. Сторону булавки на
        // линии решает исходная раскладка: 0 и 3 -- левые, 0 и 1 -- нижние
        // (сцена перевёрнута по Y: нижние -- с меньшей Y).
        const bool left = std::abs(lastPos_.x() - c.x()) > 1e-9 ? lastPos_.x() < c.x() : (idx == 0 || idx == 3);
        pt.rx() = left ? std::min(pt.x(), c.x()) : std::max(pt.x(), c.x());
        if(pointMode) {
            const bool bottom = std::abs(lastPos_.y() - c.y()) > 1e-9 ? lastPos_.y() < c.y() : (idx == 0 || idx == 1);
            pt.ry() = bottom ? std::min(pt.y(), c.y()) : std::max(pt.y(), c.y());
        }
        pos = place(pos, idx, pt, c, pointMode);
    }

    for(int i{}; i < 4; ++i)
        pins[i]->setPos(pos[i]);
    App::project().setPinsPos(pos.data());
    updateSymmetryMark(pointMode, c, pos);
}

void Pin::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseReleaseEvent(event);
    if(!std::exchange(moved, false))
        return;
    showSymmetryMark(false);
    flipAxisMoved(pressFlipX_);
}

void Pin::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if(!(flags() & QGraphicsItem::ItemIsMovable))
        return;
    // Пересчёт -- здесь, а не в resetPos: тот зовётся и из Project::close
    // (через layoutFrameUpdate), и из "Авторасстановки", где УП и так
    // пересчитывают переставленные следом Home и Zero.
    const double before = flipAxisX();
    resetPos();
    flipAxisMoved(before);
    QGraphicsItem::mouseDoubleClickEvent(event);
}

void Pin::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Несимметричная расстановка (из прежних версий, где булавку можно было
    // тащить одну) выравнивается под режим с первым же движением, а не при
    // открытии проекта: так сами не съедут ни зафиксированные, ни уже
    // просверленные булавки.
    const UsedFlags used = usedFlags();
    const Positions base = conform(positions(), used, pointSymmetry());
    const auto pins = App::pins();
    for(int i{}; i < 4; ++i)
        pins[i]->lastPos_ = base[i];
    pressCenter_ = center(base, used);
    pressFlipX_ = flipAxisX(); // по этой оси посчитаны нынешние УП
    QGraphicsItem::mousePressEvent(event);
}
void Pin::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;

    auto action = menu.addAction(QIcon::fromTheme(u"drill-path"_s), tr("&Create path for Pins"), [] {
        ToolDatabase tdb{
            App::grView.ptr(), std::array{Tool::Drill, Tool::EndMill}
        };
        if(tdb.exec()) {
            Tool tool(tdb.tool());

            QPolygonF dst;

            for(Pin* pin: {&App::pin0(), &App::pin1(), &App::pin2(), &App::pin3()}) {
                pin->setFlag(QGraphicsItem::ItemIsMovable, false);
                QPointF point(pin->pos());
                if(dst.contains(point))
                    continue;
                if(App::project().pinUsed(pin->index_))
                    dst.push_back(point);
            }

            QSettings settings;
            settings.beginGroup(u"Pin"_s);
            bool ok;
            double depth = QInputDialog::getDouble(
                nullptr,                               // parent
                {},                                    // title
                tr("Set Depth"),                       // label
                settings.value(u"depth"_s).toDouble(), // value
                0,                                     // minValue
                20,                                    // maxValue
                1,                                     // decimals
                &ok,                                   // ok
                Qt::WindowFlags(),                     // flags
                1                                      // step
            );
            if(!ok)
                return;

            if(depth == 0.0)
                return;

            settings.setValue(u"depth"_s, depth);
            settings.endGroup();

            GCode::Params gcp{tool, depth};
            gcp.params[GCode::Params::NotTile];
            // Точки сверловки -- одна незамкнутая полилиния, а не тело: сверлу
            // важен только порядок центров отверстий.
            gcp.toolPathss = {Geo::Polylines{Geo::Polyline{dst}}};

            auto drillls = new Drilling::File{std::move(gcp)};
            drillls->setFileName(tr("Pin_") + tool.nameEnc());
            App::project().addFile(drillls);
        }
    });

    // Зафиксированные (после "Создать УП для булавок" -- программа сверловки
    // уже стоит на их местах; и при каждом запуске -- флаг не хранится) не
    // двигаются и отсюда, как и перетаскиванием: смена режима тоже может их
    // сдвинуть (conform). Пункты погашены, а подсказка говорит, что снять.
    const bool movable = flags() & QGraphicsItem::ItemIsMovable;
    const QString fixedHint = movable ? QString{} : tr("Uncheck \"Fixed\" to edit the pins.");
    menu.setToolTipsVisible(true);
    // Одно окно на координаты и привязку к отверстиям: прежние два пункта
    // открывали один и тот же диалог.
    action = menu.addAction(QIcon::fromTheme(u"document-edit"_s), tr("Pin &coordinates and snapping…"), [] {
        PinsDialog{App::grView.ptr()}.exec();
    });
    action->setEnabled(movable);
    action->setToolTip(fixedHint);
    action = menu.addAction(tr("Symmetry &point"), [](bool point) { setPointSymmetry(point); });
    action->setCheckable(true);
    action->setChecked(pointSymmetry());
    action->setEnabled(movable);
    action->setToolTip(fixedHint);
    menu.addSeparator();

    action = menu.addAction(tr("Fixed"), [](bool fl) {
        for (Pin* pin : {&App::pin0(), &App::pin1(), &App::pin2(), &App::pin3()}) pin->setFlag(QGraphicsItem::ItemIsMovable, !fl); });
    action->setCheckable(true);
    action->setChecked(!(App::pin0().flags() & QGraphicsItem::ItemIsMovable));

    action = menu.addAction(tr("Used"), [this](bool fl) {
        const double axis = flipAxisX();
        App::project().setPinUsed(fl, index_);
        update();
        // Ось переворота считается по используемым булавкам -- а по ней
        // зеркалится нижняя сторона в УП.
        flipAxisMoved(axis);
    });
    action->setCheckable(true);
    action->setChecked(App::project().pinUsed(index_));

    menu.addSeparator();
    action = menu.addAction(QIcon::fromTheme(u"configure-shortcuts"_s), QObject::tr("&Settings"), [] {
        SettingsDialog(nullptr, SettingsDialog::Utils).exec();
    });
    menu.exec(event->screenPos());
}

int Pin::type() const { return Type::MarkPin; }

void Pin::resetPos(bool fl) {
    if(fl)
        if(!updateRect())
            return;

    const QPointF offset(App::settings().mkrPinOffset());
    const QRectF rect(App::layoutFrames().boundingRect()); // App::project().worckRect()

    QPointF pt[]{
        QPointF(rect.topLeft() + QPointF(-offset.x(), -offset.y())),
        QPointF(rect.topRight() + QPointF(+offset.x(), -offset.y())),
        QPointF(rect.bottomRight() + QPointF(+offset.x(), +offset.y())),
        QPointF(rect.bottomLeft() + QPointF(-offset.x(), +offset.y())),
    };

    const QPointF center(rect.center());

    if(pt[0].x() > center.x()) pt[0].setX(center.x());
    if(pt[0].y() > center.y()) pt[0].setY(center.y());
    if(pt[1].x() < center.x()) pt[1].setX(center.x());
    if(pt[1].y() > center.y()) pt[1].setY(center.y());
    if(pt[2].x() < center.x()) pt[2].setX(center.x());
    if(pt[2].y() < center.y()) pt[2].setY(center.y());
    if(pt[3].x() > center.x()) pt[3].setX(center.x());
    if(pt[3].y() < center.y()) pt[3].setY(center.y());

    for(int i{}; i < 4; ++i)
        App::pins()[i]->setPos(pt[i]);

    App::project().setPinsPos(pt);
}

void Pin::setPos(const QPointF pos[]) {
    for(int i{}; i < 4; ++i)
        App::pins()[i]->setPos(pos[i]);
}

void Pin::updateToolTip() {
    const QPointF p(pos() - App::zero().pos());
    setToolTip(QObject::tr("Pin %1\nX %2:Y %3")
                   .arg(index_ + 1)
                   .arg(p.x())
                   .arg(p.y())
        + u"\n\n"_s
        + (pointSymmetry()
                ? tr("Drag: all four pins about the symmetry point\n"
                     "Ctrl+drag: all pins together with the symmetry point")
                : tr("Drag: the pin and its mirror partner (the flip axis stays)\n"
                     "Ctrl+drag: all pins together with the flip axis")));
}

void Pin::setPos(const QPointF& pos) {
    QGraphicsItem::setPos(pos);
    updateToolTip();
}

Pin::Positions Pin::positions() {
    const auto pins = App::pins();
    Positions pos;
    for(int i{}; i < 4; ++i)
        pos[i] = pins[i]->pos();
    return pos;
}

Pin::UsedFlags Pin::usedFlags() {
    UsedFlags used;
    for(int i{}; i < 4; ++i)
        used[i] = App::project().pinUsed(i);
    return used;
}

double Pin::flipAxisX() { return flipAxisX(positions(), usedFlags()); }

double Pin::flipAxisX(std::span<const QPointF, 4> pos, std::span<const bool, 4> used) {
    double sum[3]{};
    int count[3]{};
    for(int i: {0, 2}) {
        const int tier = int(used[i]) + int(used[partner(i)]);
        sum[tier] += (pos[i].x() + pos[partner(i)].x()) * 0.5;
        ++count[tier];
    }
    for(int tier = 2; tier > 0; --tier)
        if(count[tier]) return sum[tier] / count[tier];
    return sum[0] / count[0]; // ни одна не используется -- обе пары
}

QPointF Pin::center(std::span<const QPointF, 4> pos, std::span<const bool, 4> used) {
    // Y -- середина между серединами пар: у симметричной расстановки это и
    // центр прямоугольника, и горизонталь точки.
    const double bottom = (pos[0].y() + pos[1].y()) * 0.5;
    const double top = (pos[2].y() + pos[3].y()) * 0.5;
    return {flipAxisX(pos, used), (bottom + top) * 0.5};
}

Pin::Positions Pin::place(Positions pos, int idx, QPointF pt, QPointF c, bool pointMode) {
    pos[idx] = pt;
    pos[partner(idx)] = {2.0 * c.x() - pt.x(), pt.y()}; // зеркально через ось
    if(pointMode) {
        pos[idx ^ 2] = 2.0 * c - pt;                   // по диагонали -- через точку
        pos[idx ^ 3] = {pt.x(), 2.0 * c.y() - pt.y()}; // та же X -- через горизонталь точки
    }
    return pos;
}

bool Pin::isSymmetric(std::span<const QPointF, 4> pos, std::span<const bool, 4> used, bool pointMode, double eps) {
    const QPointF c = center(pos, used);
    const auto same = [eps](QPointF a, QPointF b) { return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps; };
    for(int i{}; i < 4; ++i) {
        if(!same(pos[partner(i)], {2.0 * c.x() - pos[i].x(), pos[i].y()}))
            return false;
        if(pointMode && !same(pos[i ^ 2], 2.0 * c - pos[i]))
            return false;
    }
    return true;
}

Pin::Positions Pin::conform(Positions pos, std::span<const bool, 4> used, bool pointMode) {
    // Пересчёт симметричной дал бы сдвиги в последний знак: проект числился
    // бы изменённым, а булавка уходила бы с отверстия, на котором стоит.
    if(isSymmetric(pos, used, pointMode))
        return pos;
    // Ось -- прежняя: по ней посчитаны УП нижней стороны.
    const double axis = flipAxisX(pos, used);
    for(int i: {0, 2}) {
        const int j = partner(i);
        if(used[i] != used[j]) {
            // Используется одна -- она и стоит (могла встать на отверстие),
            // неиспользуемая отражается к ней.
            const int u = used[i] ? i : j;
            pos[partner(u)] = {2.0 * axis - pos[u].x(), pos[u].y()};
        } else {
            // Обе или ни одной -- пара встаёт на ось, сохранив ширину и
            // среднюю высоту.
            const double halfWidth = (pos[j].x() - pos[i].x()) * 0.5;
            const double y = (pos[i].y() + pos[j].y()) * 0.5;
            pos[i] = {axis - halfWidth, y};
            pos[j] = {axis + halfWidth, y};
        }
    }
    if(pointMode) {
        // Прямоугольник: ширину задаёт пара, где используемых больше (при
        // равенстве -- нижняя), вторая встаёт по диагонали через точку,
        // сохранив свою высоту.
        const auto tier = [&](int i) { return int(used[i]) + int(used[partner(i)]); };
        const int d = tier(2) > tier(0) ? 2 : 0;
        const QPointF c{axis, center(pos, used).y()};
        for(int k: {d, partner(d)})
            pos[k ^ 2] = 2.0 * c - pos[k];
    }
    return pos;
}

void Pin::flipAxisMoved(double before) {
    // Пересчёт всех УП (скрипт на каждую, да ещё копия в буфер обмена) идёт
    // секундами -- это и была пауза после отпускания булавки, хотя пара или
    // точка, двигаясь вокруг стоящей оси, программ не меняют.
    if(std::abs(flipAxisX() - before) > 1e-9)
        GCode::regenerateGCodeFiles();
}

bool Pin::pointSymmetry() { return App::project().pinsPointSymmetry(); }

namespace {

class PinSymmetryMark;
PinSymmetryMark* pinSymmetryMark{}; // создаётся при первом показе, владеет сцена
// Счётчик показов и скрытий метки: отложенное скрытие после смены режима
// не гасит метку, которую с тех пор показали заново (тащат булавку, открыт
// диалог).
unsigned pinSymmetryMarkShows{};

// Метка симметрии булавок -- пунктир с запасом за крайние булавки:
// вертикаль оси переворота, в режиме точки ещё и горизонталь через точку
// (перекрестье). Постоянно не показывается: линии через всю плату только
// мешали бы.
class PinSymmetryMark final : public QGraphicsItem {
public:
    PinSymmetryMark() {
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(false);
        // Под булавками, над всем остальным. У булавок max - index, но на
        // таких порядках вычитание ничего не меняет -- у них просто max.
        setZValue(std::numeric_limits<double>::max() * 0.5);
        setVisible(false);
    }
    ~PinSymmetryMark() override {
        if(pinSymmetryMark == this) pinSymmetryMark = nullptr;
    }

    int type() const override { return Gi::Type::MarkPinAxis; }
    QRectF boundingRect() const override { return App::drawPdf() ? QRectF{} : rect_; }
    QPainterPath shape() const override { return {}; } // мышь проходит насквозь

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/, QWidget* /*widget*/) override {
        if(App::drawPdf()) return;
        QColor color{App::settings().guiColor(GuiColors::Pin)};
        color.setAlpha(255);
        QPen pen{color, 0.0, Qt::DashLine};
        pen.setCosmetic(true);
        painter->save(); // вид рисует с DontSavePainterState
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(vLine_);
        if(cross_) painter->drawLine(hLine_);
        painter->restore();
    }

    // hLine рисуется только при cross (режим точки).
    void setLines(const QLineF& vLine, const QLineF& hLine, bool cross) {
        prepareGeometryChange();
        vLine_ = vLine;
        hLine_ = hLine;
        cross_ = cross;
        QRectF rect = QRectF{vLine.p1(), vLine.p2()}.normalized();
        if(cross) rect |= QRectF{hLine.p1(), hLine.p2()}.normalized();
        // Косметическое перо шириной в пиксель: запас в пару пикселей при
        // текущем масштабе, но не меньше миллиметра.
        const double margin = std::max({1.0, std::max(rect.width(), rect.height()) * 0.01, App::viewScaleFactor() * 4.0});
        rect_ = rect.adjusted(-margin, -margin, margin, margin);
        update();
    }

private:
    QLineF vLine_;
    QLineF hLine_;
    QRectF rect_;
    bool cross_{};
};

} // namespace

void Pin::showSymmetryMark(bool show) {
    ++pinSymmetryMarkShows;
    if(!show) {
        if(pinSymmetryMark) pinSymmetryMark->hide();
        return;
    }
    if(!pinSymmetryMark) {
        QGraphicsScene* scene = App::pin0().scene();
        if(!scene) return;
        scene->addItem(pinSymmetryMark = new PinSymmetryMark);
    }
    const UsedFlags used = usedFlags();
    const Positions pos = positions();
    updateSymmetryMark(pointSymmetry(), center(pos, used), pos);
    pinSymmetryMark->show();
}

void Pin::updateSymmetryMark(bool pointMode, QPointF c, std::span<const QPointF, 4> pos) {
    if(!pinSymmetryMark) return;
    double minX{pos[0].x()}, maxX{minX}, minY{pos[0].y()}, maxY{minY};
    for(const QPointF& pt: pos) {
        minX = std::min(minX, pt.x());
        maxX = std::max(maxX, pt.x());
        minY = std::min(minY, pt.y());
        maxY = std::max(maxY, pt.y());
    }
    const double marginX = std::max(5.0, (maxX - minX) * 0.1);
    const double marginY = std::max(5.0, (maxY - minY) * 0.1);
    pinSymmetryMark->setLines({c.x(), minY - marginY, c.x(), maxY + marginY},
        {minX - marginX, c.y(), maxX + marginX, c.y()}, pointMode);
}

void Pin::setPointSymmetry(bool point) {
    if(pointSymmetry() == point)
        return;
    const double before = flipAxisX();
    App::project().setPinsPointSymmetry(point);
    // Расстановка, симметричная относительно оси, относительно точки может
    // быть и несимметрична (трапеция) -- выровнять сразу, чтобы режим и то,
    // что видно на сцене, не расходились до первого движения.
    const Positions pos = conform(positions(), usedFlags(), point);
    if(pos != positions()) {
        Pin::setPos(pos.data());
        App::project().setPinsPos(pos.data());
    }
    for(Pin* pin: App::pins())
        pin->updateToolTip(); // подсказка описывает режим
    // Метку -- ненадолго: видно, вокруг чего теперь ходят булавки.
    showSymmetryMark(true);
    QTimer::singleShot(1500, &App::pin0(), [shows = pinSymmetryMarkShows] {
        if(shows == pinSymmetryMarkShows) showSymmetryMark(false);
    });
    flipAxisMoved(before);
}

} // namespace Gi

////////////////////////////////////////////////
LayoutFrames::LayoutFrames()
    : QGraphicsObject{nullptr} {
    setZValue(-std::numeric_limits<double>::max());
    setFlag(ItemIsSelectable, false);
    App::layoutFrames.set(this);
}

LayoutFrames::~LayoutFrames() {
    App::layoutFrames.set(nullptr);
}

int LayoutFrames::type() const {
    return Gi ::Type::MarkLayoutFrames;
}

QRectF LayoutFrames::boundingRect() const {
    if(App::drawPdf())
        return {};
    return rect_;
}

void LayoutFrames::paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/, QWidget* /*widget*/) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setBrush(Qt::NoBrush);

    const double sf = 1.0 / QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
    QPen pen{
        QColor{255, 0, 255},
        2.0 * sf
    };
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);

    painter->drawPath(path_);

    if(!borderPath_.isEmpty()) {
        // Точный контур границы -- косметическим пунктиром в пиксель: видно,
        // ЧТО задано кнопкой, и уголков рамки он не заслоняет.
        QPen dash{
            QColor{255, 0, 255},
            1.0, Qt::DashLine
        };
        dash.setCosmetic(true);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(dash);
        painter->drawPath(borderPath_);
    }
    painter->restore();
}

void LayoutFrames::updateRect(bool fl) {
    // Габарит меняется: индекс BSP сцены должен узнать об этом ДО смены
    // rect_, иначе держит прежний прямоугольник -- следы перерисовки там, где
    // рамки уже нет, и пропуски там, где она теперь.
    prepareGeometryChange();
    borderPath_ = App::project().boardBorder().toPath();

    QPainterPath path;
    QRectF rect(App::project().worckRect());
    rect_ = rect;

    const int stepsY(App::project().stepsY()), stepsX(App::project().stepsX());
    const double spaceY_(App::project().spaceY()), spaceX_(App::project().spaceX());

    rect_.setHeight(rect_.height() * stepsY + spaceY_ * (stepsY - 1));
    rect_.setWidth(rect_.width() * stepsX + spaceX_ * (stepsX - 1));

    for(int x{}; x < stepsX; ++x) {
        for(int y{}; y < stepsY; ++y) {
            if(x || y) {
                path.addRect(rect.translated(
                    (rect.width() + spaceX_) * x,
                    (rect.height() + spaceY_) * y));
            } else {
                const double spaceX = qFuzzyIsNull(spaceX_) ? 2 : spaceX_ * 0.5;
                const double spaceY = qFuzzyIsNull(spaceY_) ? 2 : spaceY_ * 0.5;

                path.moveTo(rect.bottomLeft() + QPointF(-spaceX, 0));
                path.lineTo(rect.bottomLeft());
                path.lineTo(rect.bottomLeft() + QPointF(0, +spaceY));

                path.moveTo(rect.bottomRight() + QPointF(+spaceX, 0));
                path.lineTo(rect.bottomRight());
                path.lineTo(rect.bottomRight() + QPointF(0, +spaceY));

                path.moveTo(rect.topLeft() + QPointF(-spaceX, 0));
                path.lineTo(rect.topLeft());
                path.lineTo(rect.topLeft() + QPointF(0, -spaceY));

                path.moveTo(rect.topRight() + QPointF(+spaceX, 0));
                path.lineTo(rect.topRight());
                path.lineTo(rect.topRight() + QPointF(0, -spaceY));

                {
                    const double mid = rect.height() / 2;
                    const double len = rect.height() / 20;

                    path.moveTo(rect.topLeft() + QPointF(0, mid - len));
                    path.lineTo(rect.topLeft() + QPointF(0, mid + len));

                    path.moveTo(rect.topRight() + QPointF(0, mid - len));
                    path.lineTo(rect.topRight() + QPointF(0, mid + len));

                    path.moveTo(rect.topLeft() + QPointF(0, mid));
                    path.lineTo(rect.topLeft() + QPointF(-spaceX, mid));

                    path.moveTo(rect.topRight() + QPointF(0, mid));
                    path.lineTo(rect.topRight() + QPointF(+spaceX, mid));
                }

                {
                    const double mid = rect.width() / 2;
                    const double len = rect.width() / 20;

                    path.moveTo(rect.bottomLeft() + QPointF(mid - len, 0));
                    path.lineTo(rect.bottomLeft() + QPointF(mid + len, 0));

                    path.moveTo(rect.topLeft() + QPointF(mid - len, 0));
                    path.lineTo(rect.topLeft() + QPointF(mid + len, 0));

                    path.moveTo(rect.bottomLeft() + QPointF(mid, 0));
                    path.lineTo(rect.bottomLeft() + QPointF(mid, +spaceY));

                    path.moveTo(rect.topLeft() + QPointF(mid, 0));
                    path.lineTo(rect.topLeft() + QPointF(mid, -spaceY));
                }
            }
        }
    }
    path_ = std::move(path);
    QGraphicsItem::update();
    if(fl) {
        App::home().resetPos(false);
        App::zero().resetPos(false);
        Gi ::Pin::resetPos(false);
        // Шаг или число копий мультипликации: по ним скрипты раскладывают
        // копии, а маркеры и булавки только что встали по новой рамке.
        GCode::scheduleRegenerateGCodeFiles();
    }
}

#include "moc_gi_point.cpp"
