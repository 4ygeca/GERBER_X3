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

bool updateRect() {
    QRectF rect(App::grView().getSelectedBoundingRect());
    if(rect.isEmpty()) {
        if(QMessageBox::question(nullptr, {},
               QObject::tr("There are no selected items to define the border.\n"
                           "The old border will be used."),
               QMessageBox::No, QMessageBox::Yes)
            == QMessageBox::No)
            return false;
    }
    App::layoutFrames().updateRect();
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

void Marker::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseMoveEvent(event);
    moved = true;
    setPos(App::settings().getSnappedPos(pos(), event->modifiers()));

    updateGCPForm();
}

void Marker::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseReleaseEvent(event);
    if(moved) GCode::regenerateGCodeFiles();
    moved = false;
}

void Marker::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if(!(flags() & QGraphicsItem::ItemIsMovable))
        return;
    resetPos();
    // QMatrix matrix(scene()->views().first()->matrix());
    // matrix.translate(-pos().x(), pos().y());
    // scene()->views().first()->setMatrix(matrix);
    updateGCPForm();
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

// Прежде булавка тащила за собой все четыре: противоположная по диагонали
// уходила на столько же в обратную сторону, остальные две достраивали
// прямоугольник, то есть расстановка раздувалась и сжималась вокруг
// неподвижного центра (да ещё и не пускалась за центр рамки платы). Задать
// так ось переворота в стороне от центра было нельзя вовсе.
//
// Теперь по умолчанию едет только пара: булавка и её зеркальный партнёр
// относительно оси переворота, ось при этом стоит. Shift -- одна булавка,
// без партнёра. Ctrl -- вся расстановка целиком, а с ней и ось.
void Pin::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    // Базовый mouseMoveEvent не зовётся: он двигает ещё и все выделенные
    // подвижные элементы сцены (булавка не выделяемая, и выделение при
    // нажатии на неё не снимается), а положение здесь всё равно своё.
    if(!(event->buttons() & Qt::LeftButton) || !(flags() & ItemIsMovable))
        return;
    if(!std::exchange(moved, true))
        showFlipAxis(true);

    // Всё -- от положений в момент нажатия, а не от прошлого события:
    // модификатор можно сменить посреди перетаскивания, и расстановка
    // пересчитается заново, без накопленного сдвига.
    QPointF pt = App::settings().getSnappedPos(
        lastPos_ + event->scenePos() - event->buttonDownScenePos(Qt::LeftButton), event->modifiers());

    const auto pins = App::pins();
    Positions pos;
    for(int i{}; i < 4; ++i)
        pos[i] = pins[i]->lastPos_;

    const int idx = index();
    if(event->modifiers() & Qt::ControlModifier) {
        const QPointF delta{pt - lastPos_};
        for(QPointF& p: pos)
            p += delta;
    } else if(event->modifiers() & Qt::ShiftModifier) {
        pos[idx] = pt;
    } else {
        // Через ось булавка не переходит: на самой оси пара сливается в одну
        // точку -- два штифта на оси переворота тоже рабочая схема. Сторону
        // булавки на оси решает исходная раскладка: 0 и 3 -- левые.
        const double axis = pressAxis_;
        const bool left = std::abs(lastPos_.x() - axis) > 1e-9 ? lastPos_.x() < axis : (idx == 0 || idx == 3);
        pt.rx() = left ? std::min(pt.x(), axis) : std::max(pt.x(), axis);
        pos[idx] = pt;
        pos[partner(idx)] = {2.0 * axis - pt.x(), pt.y()};
    }

    for(int i{}; i < 4; ++i)
        pins[i]->setPos(pos[i]);
    App::project().setPinsPos(pos.data());
    updateFlipAxis(flipAxisX(), pos);
}

void Pin::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseReleaseEvent(event);
    if(moved) {
        showFlipAxis(false);
        GCode::regenerateGCodeFiles();
    }
    moved = false;
}

void Pin::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if(!(flags() & QGraphicsItem::ItemIsMovable))
        return;
    resetPos();
    QGraphicsItem::mouseDoubleClickEvent(event);
}

void Pin::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    for(Pin* pin: App::pins())
        pin->lastPos_ = pin->pos();
    pressAxis_ = flipAxisX();
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

    menu.addAction(QIcon::fromTheme(u"document-edit"_s), tr("&Pin coordinates…"), [] {
        PinsDialog{App::grView.ptr(), PinsDialog::Coordinates}.exec();
    });
    action = menu.addAction(QIcon::fromTheme(u"drill"_s), tr("Snap pins to &drill holes…"), [] {
        PinsDialog{App::grView.ptr(), PinsDialog::SnapToHoles}.exec();
    });
    action->setEnabled(PinsDialog::hasHoles());
    menu.addSeparator();

    action = menu.addAction(tr("Fixed"), [](bool fl) {
        for (Pin* pin : {&App::pin0(), &App::pin1(), &App::pin2(), &App::pin3()}) pin->setFlag(QGraphicsItem::ItemIsMovable, !fl); });
    action->setCheckable(true);
    action->setChecked(!(App::pin0().flags() & QGraphicsItem::ItemIsMovable));

    action = menu.addAction(tr("Used"), [this](bool fl) {
        App::project().setPinUsed(fl, index_); update(); });
    action->setCheckable(true);
    action->setChecked(App::project().pinUsed(index_));

    menu.addSeparator();
    action = menu.addAction(QIcon::fromTheme(u"configure-shortcuts"_s), QObject::tr("&Settings"), [] {
        SettingsDialog(nullptr, SettingsDialog::Utils).exec();
    });
    menu.exec(event->screenPos());
}

int Pin::type() const { return Type::MarkPin; }

void Pin::setPinsPos(QPointF pos[]) {
    for(int i{}; i < 4; ++i)
        App::pins()[i]->setPos(pos[i]);
}

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
        + tr("Drag: the pin and its mirror partner\n"
             "Shift+drag: this pin only\n"
             "Ctrl+drag: all pins together"));
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

Pin::Symmetry Pin::symmetry(std::span<const QPointF, 4> pos, std::span<const bool, 4> used) {
    Symmetry sym;
    sym.axis = flipAxisX(pos, used);
    for(int i{}; i < 4; ++i) {
        if(!used[i]) continue;
        const QPointF mirrored{2.0 * sym.axis - pos[i].x(), pos[i].y()};
        // Ближайшая используемая булавка, партнёр -- первым: при равенстве
        // (пара слита в точку на оси) отчёт называет именно его.
        for(int j: {partner(i), i, partner(i) ^ 2, i ^ 2}) {
            if(!used[j]) continue;
            const double d = QLineF{mirrored, pos[j]}.length();
            if(sym.target[i] < 0 || d < sym.deviation[i] - 1e-9) {
                sym.target[i] = j;
                sym.deviation[i] = d;
            }
        }
        sym.maxDeviation = std::max(sym.maxDeviation, sym.deviation[i]);
    }
    return sym;
}

namespace {

class PinFlipAxis;
PinFlipAxis* pinFlipAxis{}; // создаётся при первом показе, владеет сцена

// Ось переворота -- вертикальный пунктир с запасом за крайние булавки.
// Постоянно не показывается: линия через всю плату только мешала бы.
class PinFlipAxis final : public QGraphicsItem {
public:
    PinFlipAxis() {
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(false);
        // Под булавками, над всем остальным. У булавок max - index, но на
        // таких порядках вычитание ничего не меняет -- у них просто max.
        setZValue(std::numeric_limits<double>::max() * 0.5);
        setVisible(false);
    }
    ~PinFlipAxis() override {
        if(pinFlipAxis == this) pinFlipAxis = nullptr;
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
        painter->drawLine(line_);
        painter->restore();
    }

    void setLine(const QLineF& line) {
        prepareGeometryChange();
        line_ = line;
        // Косметическое перо шириной в пиксель: запас в пару пикселей при
        // текущем масштабе, но не меньше миллиметра.
        const double margin = std::max({1.0, line.length() * 0.01, App::viewScaleFactor() * 4.0});
        rect_ = QRectF{line.p1(), line.p2()}.normalized().adjusted(-margin, -margin, margin, margin);
        update();
    }

private:
    QLineF line_;
    QRectF rect_;
};

} // namespace

void Pin::showFlipAxis(bool show) {
    if(!show) {
        if(pinFlipAxis) pinFlipAxis->hide();
        return;
    }
    if(!pinFlipAxis) {
        QGraphicsScene* scene = App::pin0().scene();
        if(!scene) return;
        scene->addItem(pinFlipAxis = new PinFlipAxis);
    }
    updateFlipAxis(flipAxisX(), positions());
    pinFlipAxis->show();
}

void Pin::updateFlipAxis(double axisX, std::span<const QPointF, 4> pos) {
    if(!pinFlipAxis) return;
    double minY{pos[0].y()}, maxY{minY};
    for(const QPointF& pt: pos) {
        minY = std::min(minY, pt.y());
        maxY = std::max(maxY, pt.y());
    }
    const double margin = std::max(5.0, (maxY - minY) * 0.1);
    pinFlipAxis->setLine({axisX, minY - margin, axisX, maxY + margin});
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
    painter->restore();
}

void LayoutFrames::updateRect(bool fl) {
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
    }
}

#include "moc_gi_point.cpp"
