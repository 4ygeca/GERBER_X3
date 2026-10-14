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
#include "graphicsview.h"
#include "gi.h"
#include "gi_datasolid.h"
#include "gi_drill.h"
#include "gi_point.h"
#include "gridtick.h"

#include "openglcheck.h"
#include "project.h"
#include "ruler.h"
#include "utils.h"

#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QMenu>
#include <QMetaMethod>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleHints>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

constexpr double zoomFactor = 1.5;
constexpr double zoomFactorAnim = 1.7;
// Пределы масштаба, px/мм -- общие для колеса и кнопок. Ниже 1 не хватает
// запаса выделения DataPath (gi_datapath.cpp), выше 1e5 нет смысла.
constexpr double kMinScale = 1.0;
constexpr double kMaxScale = 100000.0;
// ПКМ: сдвиг больше порога -- панорама, меньше -- клик (меню). Как у Viewer3d.
constexpr int kPanThresholdPx = 4;
// Запас диапазона прокрутки вида вокруг данных, мм. При kMinScale это
// 5000 px с каждой стороны -- больше любого вьюпорта, а при kMaxScale
// (W + 10 м) * 1e5 px ещё помещается в int для любой реальной платы.
constexpr double kViewPadMm = 5000.0;

void setCursor(QWidget* w) {
    enum {
        Size = 21,
        Mid = 10
    };
    QPixmap cursor{Size, Size};
    cursor.fill(Qt::transparent);
    QPainter p{&cursor};
    p.setPen({QColor(App::settings().guiColor(GuiColors::Background).rgb() ^ 0xFFFFFF), 1.0});
    p.drawLine(0, Mid, Size, Mid);
    p.drawLine(Mid, 0, Mid, Size);
    w->setCursor(QCursor{cursor, Mid, Mid});
}

GraphicsView::GraphicsView(QWidget* parent)
    : QGraphicsView{
          parent
}
    , hRuler{new Ruler{Qt::Horizontal, this}},
    vRuler{new Ruler{Qt::Vertical, this}},
    gridLayout{new QGridLayout{this}} {

    // Режим обновления и кэш ставятся в setOpenGL(): setViewport() сбрасывает
    // атрибуты вьюпорта, поэтому задавать их до него бессмысленно.
    setOptimizationFlag(DontSavePainterState);
    // Рамка выделения своя (band_): штатная умеет только заменить выделение
    // или добавить к нему, а снять рамкой -- нет.
    setDragMode(NoDrag);

    // setContextMenuPolicy(Qt::DefaultContextMenu);

    setAcceptDrops(true);

    ////////////////////////////////////
    struct Scene final : public QGraphicsScene {
        QPointF last;
        using QGraphicsScene::QGraphicsScene;
        QPointF mappedPos() { return qobject_cast<GraphicsView*>(parent())->scenePos; }
        // Shift -- «добавить», а Qt о Shift ничего не знает: нажатие мимо
        // item'ов (сцена) и по item'у (QGraphicsItem::mousePressEvent) без
        // Ctrl снимает прочее выделение, и рамке с Shift добавлять было бы не
        // к чему. Ctrl подставляем в событие СЦЕНЫ, а не в QMouseEvent: копию
        // того вид хранит и повторяет захватчику при прокрутке и зуме
        // (replayLastMouseEvent), а Pin::mouseMoveEvent читает Ctrl сам.
        static bool shiftAdds(const QGraphicsSceneMouseEvent* event) {
            return event->button() == Qt::LeftButton
                && (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) == Qt::ShiftModifier;
        }
        void pressLike(QGraphicsSceneMouseEvent* event, bool doubleClick) {
            event->setScenePos(last = mappedPos());
            const auto mods = event->modifiers();
            if(shiftAdds(event)) event->setModifiers(mods | Qt::ControlModifier);
            // Квалифицированно: через указатель на член вызов ушёл бы
            // виртуально -- обратно сюда.
            if(doubleClick)
                QGraphicsScene::mouseDoubleClickEvent(event);
            else
                QGraphicsScene::mousePressEvent(event);
            event->setModifiers(mods);
        }
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override { pressLike(event, false); }
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
            if(last == mappedPos()) return;
            event->setScenePos(last = mappedPos());
            QGraphicsScene::mouseMoveEvent(event);
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
            event->setScenePos(last = mappedPos());
            QGraphicsScene::mouseReleaseEvent(event);
        }
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override { pressLike(event, true); }
    };
    setScene(new Scene{this});
    App::grView.set(this);

    updateSceneRectToContents(); // пустой проект -- запасной прямоугольник

    // add two rulers on top and left.
    setViewportMargins(Ruler::Breadth, 0, 0, Ruler::Breadth);

    // create rulers

    hRuler->setMouseTrack(true);
    vRuler->setMouseTrack(true);
    ::setCursor(hRuler);
    ::setCursor(vRuler);

    // add items to grid layout
    QPushButton* corner = new QPushButton{App::settings().isBanana() ? u"I"_s : u"M"_s, this};
    connect(corner, &QPushButton::clicked, this, [corner, this](bool fl) {
        corner->setText(fl ? u"I"_s : u"M"_s);
        corner->setToolTip(fl ? u"Banana"_s : u"Metric"_s);
        App::settings().setBanana(fl);
        viewport()->update(); // сменился шаг сетки -- она в drawForeground
        hRuler->update();
        vRuler->update();
    });
    corner->setCheckable(true);
    corner->setFixedSize(Ruler::Breadth, Ruler::Breadth);

    // add grid layout
    gridLayout->setSpacing({});
    gridLayout->setContentsMargins({});
    gridLayout->addWidget(corner, 1, 0);
    gridLayout->addWidget(hRuler, 1, 1);
    gridLayout->addWidget(vRuler, 0, 0);
    gridLayout->addWidget(viewport(), 0, 1);
    gridLayout->addWidget(horizontalScrollBar(), 2, 1);
    gridLayout->addWidget(verticalScrollBar(), 0, 2);

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
        this, &GraphicsView::updateRuler);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
        this, &GraphicsView::updateRuler);

    {
        QSettings settings;
        settings.beginGroup(u"Viewer"_s);
        setOpenGL(settings.value(u"chbxOpenGl"_s).toBool());
        const bool aa = settings.value(u"chbxAntialiasing"_s, false).toBool();
        setRenderHint(QPainter::Antialiasing, aa);
        // Флаг убирает запас в 2 device-px, который Qt добавляет к области
        // обновления каждого item'а под сглаживание. При включённом AA это
        // оставляет следы на диагональных трассах, при выключенном -- бесплатно
        // и корректно.
        setOptimizationFlag(DontAdjustForAntialiasing, !aa);
        viewport()->setObjectName(u"viewport"_s);
        settings.endGroup();
    }

    // Фон -- штатной кистью, а не таблицей стилей: drawBackground заливал
    // жёсткий Qt::black, из-за чего настройка GuiColors::Background до сцены
    // не доходила, а QStyleSheetStyle рисовал лишний примитив на каждый paint.
    setBackgroundBrush(App::settings().guiColor(GuiColors::Background));

    scale(1.0, -1.0); // flip vertical
    // Диапазон прокрутки вида теперь шире вьюпорта, и Qt больше не центрирует
    // маленькую сцену сам -- начало координат в центр, как было.
    centerOn(QPointF{});
}

GraphicsView::~GraphicsView() { App::grView.set(nullptr); }

void GraphicsView::zoom100() {
    double x = 1.0, y = 1.0;

    const double m11 = QGraphicsView::transform().m11(), m22 = QGraphicsView::transform().m22();
    if(/* DISABLES CODE */ (0)) {
        x = std::abs(1.0 / m11 / (25.4 / physicalDpiX()));
        y = std::abs(1.0 / m22 / (25.4 / physicalDpiY()));
    } else {
        const QSizeF size = screen()->physicalSize();                          // size in mm
        const QRect scrGeometry(QGuiApplication::primaryScreen()->geometry()); // size in pix
        x = std::abs(1.0 / m11 / (size.height() / scrGeometry.height()));
        y = std::abs(1.0 / m22 / (size.width() / scrGeometry.width()));
    }

    (0 && App::settings().guiSmoothScSh())
        ? animate(this, "scale"_ba, getScale(), x * zoomFactorAnim)
        : scale(x, y);
}

void GraphicsView::zoomFit() {
    auto rect{App::layoutFrames().boundingRect()};
    auto size{rect.size() * 0.1};
    rect += QMarginsF(size.width(), size.height(), size.width(), size.height());
    fitInView(rect /*scene()->itemsBoundingRect()*/, false);
}

void GraphicsView::zoomToSelected() {
    QRectF rect;
    for(const QGraphicsItem* item: scene()->selectedItems()) {
        const QRectF tmpRect(item->pos().isNull()
                ? item->boundingRect()
                : item->boundingRect().translated(item->pos()));
        rect = /*rect.isEmpty() ? tmpRect :*/ rect.united(tmpRect);
    }
    if(rect.isEmpty()) return;
    fitInView(rect);
}

void GraphicsView::zoomIn() {
    // Те же пределы, что у колеса (zoomAt), -- последний шаг укорачивается.
    const double s = getScale();
    if(s >= kMaxScale) return;
    if(App::settings().guiSmoothScSh()) {
        animate(this, "scale"_ba, s, std::min(s * zoomFactorAnim, kMaxScale));
    } else {
        const double k = std::min(zoomFactor, kMaxScale / s);
        scale(k, k);
    }
}

void GraphicsView::zoomOut() {
    const double s = getScale();
    if(s <= kMinScale) return;
    if(App::settings().guiSmoothScSh()) {
        animate(this, "scale"_ba, s, std::max(s / zoomFactorAnim, kMinScale));
    } else {
        const double k = std::max(1.0 / zoomFactor, kMinScale / s);
        scale(k, k);
    }
}

void GraphicsView::fitInView(QRectF dstRect, bool withBorders) {
    if(dstRect.isNull()) return;
    if(withBorders) {
        const auto padding = dstRect.size() / 5; // 5 mm
        dstRect += QMarginsF{
            padding.width(),
            padding.height(),
            padding.width(),
            padding.height(),
        };
    }
    // Не крупнее kMaxScale: точка или отрезок без толщины в выделении давали
    // бы сколь угодно большой масштаб (а нулевую сторону Qt не вписывает
    // вовсе). Нижний предел не применяем -- большая раскладка должна
    // вписываться целиком.
    if(const QSizeF minSize = QSizeF{viewport()->size()} / kMaxScale;
        dstRect.width() < minSize.width() || dstRect.height() < minSize.height()) {
        const QPointF center = dstRect.center();
        dstRect.setSize(dstRect.size().expandedTo(minSize));
        dstRect.moveCenter(center);
    }
    // const auto r1(getViewRect().toRect());
    // const auto r2(dstRect.toRect());
    // if (r1 == r2)
    // return;
    if(App::settings().guiSmoothScSh()) {
        animate(this, "viewRect", getViewRect(), dstRect);
    } else {
        QGraphicsView::fitInView(dstRect, Qt::KeepAspectRatio);
        onTransformChanged();
    }
}

void GraphicsView::setRuler(bool ruller) {
    rulerCtr = 0;
    ruler_ = ruller;
    viewport()->update(); // мерная линейка -- оверлей вьюпорта, не слой сцены
}

QPointF GraphicsView::toScenePos(QMouseEvent* event) {
    scenePos = App::settings().getSnappedPos(
        mapToScene(event->position()), event->modifiers());
    return scenePos;
}

QPointF GraphicsView::mapFromScene(const QPointF& point) const {
    static QPainterPath pp;
    pp.clear();
    pp.moveTo(point);
    return QGraphicsView::mapFromScene(pp).elementAt(0);
}

QPointF GraphicsView::mapToScene(const QPointF& point) const {
    static QPainterPath pp;
    pp.clear();
    pp.moveTo(point);
    return QGraphicsView::mapToScene(pp).elementAt(0);
}

void GraphicsView::setScale(double s) noexcept {
    const auto trf(transform());
    setTransform({+s /*11*/, trf.m12(), trf.m13(),
        /*      */ trf.m21(), -s /*22*/, trf.m23(),
        /*      */ trf.m31(), trf.m32(), trf.m33()});
    App::viewScaleFactor() = 1.0 / std::abs(s);
}

void GraphicsView::scale(double sx, double sy) {
    QGraphicsView::scale(sx, sy);
    onTransformChanged();
}

void GraphicsView::scrollViewBy(QPointF delta) {
    if(const int dx = qRound(delta.x())) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    if(const int dy = qRound(delta.y())) verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void GraphicsView::zoomAt(QPointF viewPos, double factor) {
    const double from = getScale();
    // Если масштаб уже вне пределов (fitInView большой раскладки), дальше от
    // них не уходим, но и не прыгаем к ним.
    const double to = std::clamp(from * factor, std::min(kMinScale, from), std::max(kMaxScale, from));
    if(to == from) return;
    // Якорь считаем сами: AnchorUnderMouse берёт lastMouseMoveScenePoint,
    // округлённый до пикселя и не обновляемый колесом. Чужой якорь на время
    // снимаем -- он нужен кнопкам зума (mainwindow.cpp).
    const QPointF anchor = mapToScene(viewPos);
    const auto keepAnchor = transformationAnchor();
    setTransformationAnchor(NoAnchor);
    QGraphicsView::scale(to / from, to / from); // m22 остаётся отрицательным
    setTransformationAnchor(keepAnchor);
    scrollViewBy(mapFromScene(anchor) - viewPos);
    onTransformChanged();
}

void GraphicsView::setOpenGL(bool useOpenGL) {
    // Настройка могла остаться включённой с машины, где GL работал, поэтому
    // последнее слово за проверкой драйвера: без неё QOpenGLWidget уронил бы
    // приложение прямо здесь, ещё до появления окна.
    if(useOpenGL && !OpenGlCheck::available()) {
        qWarning().noquote() << "GraphicsView: OpenGL отключён --" << OpenGlCheck::report();
        useOpenGL = false;
    }
    // do {
    if(useOpenGL) {
        // if(dynamic_cast<QOpenGLWidget*>(viewport())) break;
        auto oglWidget = new QOpenGLWidget{this};
        QSurfaceFormat format;
        // 4x, а не 8x: MSAA умножает память FBO и полосу resolve'а на число
        // сэмплов при полном разрешении вьюпорта, а на тонких линиях разница
        // между 4x и 8x почти незаметна -- пути Qt сглаживает своим движком.
        format.setSamples(4);
        oglWidget->setFormat(format);
        setViewport(oglWidget);
        // QOpenGLWidget всё равно перерисовывает весь FBO; при частичных
        // обновлениях в невосстановленных областях остаётся мусор.
        setViewportUpdateMode(FullViewportUpdate);
        qInfo() << "GL: сэмплов запрошено" << format.samples()
                << "выдано" << oglWidget->format().samples();
    } else {
        // if(dynamic_cast<QWidget*>(viewport())) break;
        setViewport(new QWidget{this});
        // Минимальный режим верен именно для платы: элементы разбросаны, и
        // BoundingRectViewportUpdate склеил бы два выделенных проводника в
        // противоположных углах в весь вьюпорт. От вырождения Qt страхуется
        // сам -- после порога по числу прямоугольников переходит на полное
        // обновление.
        setViewportUpdateMode(MinimalViewportUpdate);
    }
    // } while(false);
    // Сетка остаётся в drawForeground, кэшировать нечего.
    setCacheMode(CacheNone);
    ::setCursor(viewport());
    gridLayout->addWidget(viewport(), 0, 1);
}

void GraphicsView::updateSceneRectToContents() {
    // Было -1000..+1000 мм -- 4 м² корня дерева BSP под плату в 80 см².
    // Глубину дерева Qt берёт от числа item'ов, но корневой прямоугольник --
    // именно sceneRect, и при таком перекосе почти все элементы падают в одну
    // горсть листьев: поиск вырождается в линейный перебор.
    QRectF r = scene()->itemsBoundingRect();
    if(r.isEmpty()) r = {-100, -100, 200, 200};             // пустой проект, мм
    const auto pad = std::max(r.width(), r.height()) * 0.5; // запас на панорамирование
    scene()->setSceneRect(r.adjusted(-pad, -pad, pad, pad));
    // У вида свой, широкий, sceneRect: QGraphicsView::setSceneRect задаёт
    // только диапазон прокрутки и BSP сцены не трогает. С узким диапазоном
    // сцены панорама и зум под курсором упирались в края, а при малом
    // масштабе Qt центрировал сцену и не давал её двигать вовсе.
    QGraphicsView::setSceneRect(r.adjusted(-kViewPadMm, -kViewPadMm, kViewPadMm, kViewPadMm));
}

void GraphicsView::scheduleSceneRectUpdate() {
    if(std::exchange(sceneRectUpdatePending_, true)) return;
    QTimer::singleShot(0, this, [this] {
        sceneRectUpdatePending_ = false;
        updateSceneRectToContents();
    });
}

void GraphicsView::addAnimated(QGraphicsItem* item) {
    animated_.insert(item);
    updateAnimationTimer();
}

void GraphicsView::removeAnimated(QGraphicsItem* item) {
    if(animated_.remove(item)) updateAnimationTimer();
}

void GraphicsView::updateAnimationTimer() {
    const bool need = !animated_.isEmpty() && App::settings().animSelection();
    if(need && !animTimerId_)
        animTimerId_ = startTimer(40, Qt::CoarseTimer); // 25 Гц
    else if(!need && animTimerId_)
        killTimer(std::exchange(animTimerId_, 0));
}

void GraphicsView::setViewRect(const QRectF& r) {
    QGraphicsView::fitInView(r, Qt::KeepAspectRatio);
    App::viewScaleFactor() = 1.0 / std::abs(transform().m11());
}

QRectF GraphicsView::getViewRect() {
    return mapToScene(rect()).boundingRect();
    // QPointF topLeft(horizontalScrollBar()->value(), verticalScrollBar()->value());
    // QPointF bottomRight(topLeft + viewport()->rect().bottomRight());
    // QRectF visible_scene_rect(transform().inverted().mapRect({topLeft, bottomRight}));
    // return visible_scene_rect;
}

QRectF GraphicsView::getSelectedBoundingRect() {
    auto selectedItems{scene()->selectedItems()};
    if(selectedItems.isEmpty()) return {};
    // Точный габарит по залитому контуру нужен только здесь. Раньше он
    // включался флагом прямо в Item::boundingRect() -- самой горячей
    // виртуальной функции сцены; теперь это отдельный метод.
    auto rectOf = [](QGraphicsItem* gi) {
        auto* item = dynamic_cast<Gi::Item*>(gi);
        return item ? item->fillBoundingRect() : gi->boundingRect();
    };
    QRectF rect = rectOf(selectedItems.front());
    for(auto* gi: selectedItems) rect = rect.united(rectOf(gi));
    if(!rect.isEmpty()) App::project().setWorckRect(rect);
    return rect;
}

void GraphicsView::onTransformChanged() {
    App::viewScaleFactor() = 1.0 / std::abs(transform().m11());
    // Экранное положение перекрестья задано точкой СЦЕНЫ -- после смены
    // трансформации его надо пересчитать, иначе крест останется на старом
    // месте вьюпорта.
    cursorViewPos_ = QGraphicsView::mapFromScene(scenePos);
    updateRuler();
    bandViewChanged();
}

void GraphicsView::updateRuler() {
    // layout()->setContentsMargins(0, 0, 0, horizontalScrollBar()->isVisible() ? horizontalScrollBar()->height() : 0);
    updateSceneRect(QRectF{}); // actualize mapFromScene
    QPoint p = QGraphicsView::mapFromScene(QPointF{});
    vRuler->setOrigin(p.y());
    hRuler->setOrigin(p.x());
    vRuler->setRulerZoom(std::abs(transform().m22() * 0.1));
    hRuler->setRulerZoom(std::abs(transform().m11() * 0.1));
}

template <class T>
void GraphicsView::animate(QObject* target, const QByteArray& propertyName, T begin, T end) {
    auto* animation = new QPropertyAnimation{target, propertyName};
    connect(animation, &QPropertyAnimation::finished, [propertyName, end, this] {
        setProperty(propertyName.data(), end);
        onTransformChanged();
        // Финальный setProperty уже сменил трансформацию -- сцена
        // инвалидирована, звать scene()->update() поверх незачем.
        scenePos = mapToScene(viewport()->mapFromGlobal(QCursor::pos()));
    });
    if constexpr(std::is_same_v<T, QRectF>) {
        animation->setEasingCurve(QEasingCurve(QEasingCurve::InOutSine));
        animation->setDuration(200);
    } else if constexpr(std::is_same_v<decltype(target), QScrollBar>) {
        animation->setEasingCurve(QEasingCurve(QEasingCurve::Linear));
        animation->setDuration(50);
    } else {
        animation->setEasingCurve(QEasingCurve(QEasingCurve::InOutSine));
        animation->setDuration(100);
    }
    animation->setStartValue(begin);
    animation->setEndValue(end);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void GraphicsView::drawRuller(QPainter* painter) const {
    if(rulPt2 == rulPt1) return;

    // Геометрия и подписи считаются по координатам СЦЕНЫ (миллиметры), а
    // рисуется всё в координатах вьюпорта -- поэтому длины перьев, крестов и
    // текста больше не надо делить на масштаб.
    QLineF line{rulPt2, rulPt1};

    const QRectF rect{rulPt1, rulPt2};
    const double angle = line.angle();
    const auto sz = QPointF{rect.width(), rect.height()} /= App::settings().lenUnit();
    QString text = QString::fromStdString(
        std::format(
            " ∆X: {0:.3f} {5}\n"
            " ∆Y: {1:.3f} {5}\n"
            " ∆/: {2:.3f} {5}\n"
            " Area: {3:.3f} {5}²\n"
            " Angle: {4:.3f}°",
            sz.x(),                                  // 0
            sz.y(),                                  // 1
            line.length(),                           // 2
            std::abs(sz.x() * sz.y()),               // 3
            normalizeAngleDegrees(angle),            // 4
            App::settings().isBanana() ? "in" : "mm" // 5
            ));

    // Дальше -- только пиксели вьюпорта.
    const QPointF vp1 = QGraphicsView::mapFromScene(rulPt1);
    const QPointF vp2 = QGraphicsView::mapFromScene(rulPt2);
    QLineF vpLine{vp2, vp1};
    const double vpAngle = vpLine.angle();

    constexpr double crossLength = 20.0; // px
    painter->setPen({Qt::green, 1.0});
    auto drawCross = [painter](QPointF pt) {
        painter->drawLine(QLineF{
            {pt.x(), pt.y() - crossLength},
            {pt.x(), pt.y() + crossLength}
        });
        painter->drawLine(QLineF{
            {pt.x() - crossLength, pt.y()},
            {pt.x() + crossLength, pt.y()}
        });
    };
    drawCross(vp1);
    drawCross(vp2);

    // draw arrow
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen({Qt::white, 1.0});
    painter->drawLine(vpLine);
    vpLine.setLength(crossLength);
    vpLine.setAngle(vpAngle + 10);
    painter->drawLine(vpLine);
    vpLine.setAngle(vpAngle - 10);
    painter->drawLine(vpLine);

    // draw text
    const auto size{QFontMetrics{font()}.size(Qt::AlignLeft | Qt::AlignJustify | Qt::TextDontClip, text)};
    const QRect vpRect = viewport()->rect();
    QPoint pt = QRectF{vp1, vp2}.center().toPoint();
    pt.rx() = std::clamp(pt.x() - size.width() / 2, vpRect.left(), vpRect.right() - size.width());
    pt.ry() = std::clamp(pt.y() - size.height() / 2, vpRect.top(), vpRect.bottom() - size.height());
    painter->setFont(font());

    painter->setRenderHint(QPainter::TextAntialiasing);
    const QRect textRect{pt, size};
    painter->fillRect(textRect, QColor{0, 0, 0, 127});
    painter->setPen(Qt::white);
    painter->drawText(textRect, Qt::AlignLeft, text);
}

namespace Gi {

class Guide : public QGraphicsItem {

    Qt::Orientation orientation;
    double scaleFactor() const {
        if(scene() && scene()->views().size())
            return 1.0 / scene()->views().first()->transform().m11();
        return 1.0;
    };

public:
    Guide(QPointF pos, Qt::Orientation orientation)
        : orientation{orientation} {
        setFlags(ItemIsSelectable | ItemIsMovable);
        orientation == Qt::Horizontal ? setPos(0, pos.y()) : setPos(pos.x(), 0);
    }
    ~Guide() override = default;

public:
    // QGraphicsItem interface
    QRectF boundingRect() const override {
        qDebug(__FUNCTION__);
        auto sceneRect{scene()->sceneRect()};
        auto k = 2 * scaleFactor();
        if(orientation == Qt::Horizontal)
            return {sceneRect.left(), -k, sceneRect.width(), k * 2};
        else
            return {-k, sceneRect.top(), k * 2, sceneRect.height()};
    }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/, QWidget* /*widget*/) override {
        painter->fillRect(boundingRect(), Qt::magenta);
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsItem::mouseReleaseEvent(event);
        orientation == Qt::Horizontal ? setPos(0, y()) : setPos(x(), 0);
    }
};

} // namespace Gi

void GraphicsView::GiToShapeEvent(QMouseEvent* event, QGraphicsItem* item) {
    QPointF center, radius, rect;
    double maxX = -1000000, maxY = -1000000;

    if(item->type() == Gi::Type::Drill) {
        Gi::Drill* gitem = dynamic_cast<Gi::Drill*>(item);
        center = item->boundingRect().center();
        radius = QPointF{gitem->diameter() / 2, 0};
        rect = QPointF{gitem->diameter() / 2, gitem->diameter() / 2};
    }

    if(item->type() == Gi::Type::DataSolid) {
        Gi::DataFill* ditem = dynamic_cast<Gi::DataFill*>(item);
        double distance{};
        center = item->boundingRect().center();
        qFatal("edit");
        // for(const auto& paths: ditem->getPaths()) {
        //     for(const auto& path: paths) {
        //         QPointF point{path.x * dScale, path.y * dScale};
        //         distance = std::max(distance,
        //             std::sqrt(std::pow(point.x() - center.x(), 2)
        //                 + std::pow(point.y() - center.y(), 2)));
        //         maxX = std::max(maxX, point.x());
        //         maxY = std::max(maxY, point.y());
        //     }
        // }

        radius = QPointF{distance, 0};
        rect = QPointF{maxX, maxY} - center;
    }

    QMenu menu;
    QAction makeCircle(tr("Circle from Item"), &menu);
    QAction makeRect(tr("Rectangle from Item"), &menu);
    menu.addAction(&makeCircle);
    menu.addAction(&makeRect);

    connect(&makeCircle, &QAction::triggered, [&center, &radius]() {
        App::project().makeShapeCircle(center, center + radius);
    });

    connect(&makeRect, &QAction::triggered, [&center, &rect]() {
        App::project().makeShapeRectangle(center - rect, center + rect);
    });
    menu.exec(event->globalPosition().toPoint());
    viewport()->update();
}

void GraphicsView::dragEnterEvent(QDragEnterEvent* event) {
    qDebug(__FUNCTION__);
    auto mimeData{event->mimeData()};

    if(mimeData->hasFormat(Ruler::MimeType))
        event->acceptProposedAction();
    else if(mimeData->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void GraphicsView::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void GraphicsView::dropEvent(QDropEvent* event) {
    qDebug(__FUNCTION__);
    auto mimeData{event->mimeData()};
    for(QUrl& var: mimeData->urls())
        emit fileDroped(var.path()
#ifdef Q_OS_WINDOWS
                .remove(0, 1)
#endif
        );

    if(mimeData->hasFormat(Ruler::MimeType) && event->source() != this)
        scene()->addItem(new Gi::Guide{
            mapToScene(event->position().toPoint()),
            Qt::Orientation(*mimeData->data(Ruler::MimeType).data()),
        });

    event->accept();
}

void GraphicsView::setViewRectDeferred(QRectF rect) {
    if(rect.isNull()) return;
    pendingViewRect_ = rect;
    // Следующим тактом цикла событий, а не сразу: проект грузится из
    // loadSettings() -- до show(), -- и на этот момент viewport ещё имеет
    // начальный размер. Раскладка досчитывается уже после показа, а очередь
    // событий заведомо разбирается после неё.
    QTimer::singleShot(0, this, &GraphicsView::applyPendingViewRect);
}

void GraphicsView::applyPendingViewRect() {
    if(pendingViewRect_.isNull()) return;
    // Без анимации: восстановление -- не переход из осмысленного вида, а
    // установка нужного, и анимировать её не от чего.
    QGraphicsView::fitInView(std::exchange(pendingViewRect_, {}), Qt::KeepAspectRatio);
    onTransformChanged();
}

void GraphicsView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    onTransformChanged();
}

void GraphicsView::wheelEvent(QWheelEvent* event) {
    QPoint delta = event->angleDelta();
    if(delta.isNull()) delta = event->pixelDelta(); // некоторые тачпады шлют только пиксели
    // Alt -- переключатель привязки к сетке (getSnappedPos), не навигация. Но
    // Qt на Windows и X11 сам переводит Alt+колесо в горизонтальное --
    // возвращаем ось на место.
    if((event->modifiers() & Qt::AltModifier) && !delta.y()) delta = delta.transposed();
    auto mods = event->modifiers();
    mods.setFlag(Qt::AltModifier, false);
    // Как в KiCad: колесо -- зум под курсором, Shift -- вверх/вниз, Ctrl --
    // влево/вправо. Сразу, без анимации: анимация на каждый щелчок дралась
    // со следующей. macOS сам переводит Shift-прокрутку в x -- берём ту ось,
    // что пришла.
    const int along = delta.y() ? delta.y() : delta.x();
    if(mods == Qt::ShiftModifier) {
        scrollViewBy({0.0, -double(along)});
    } else if(mods == Qt::ControlModifier) {
        scrollViewBy({-double(along), 0.0});
    } else if(mods == Qt::NoModifier) {
        // Множитель непрерывный: 1.5 за щелчок (120), доли щелчка -- колёсам
        // высокого разрешения и тачпадам (раньше зум ждал ровно 120).
        if(delta.y()) zoomAt(event->position(), std::pow(zoomFactor, delta.y() / 120.0));
        if(delta.x()) scrollViewBy({-double(delta.x()), 0.0}); // наклон колеса
    } else {
        return; // прочие сочетания -- не навигация
    }
    if(panState_ != PanState::Idle) {
        // Вид сдвинулся под курсором: панорама продолжится от новой точки, а
        // ПКМ+колесо -- уже жест, меню на отпускании не нужно.
        panAnchor_ = mapToScene(event->position());
        if(panState_ == PanState::Armed) panState_ = PanState::Consumed;
    }
    // После зума и прокрутки под курсором ДРУГАЯ точка сцены, поэтому scenePos
    // надо пересчитать -- как это делает mouseMoveEvent. Раньше здесь только
    // испускался сигнал, а scenePos оставался прежним, и вторая точка мерной
    // линейки при зуме стояла на месте вместо того, чтобы ехать за курсором.
    scenePos = App::settings().getSnappedPos(mapToScene(event->position()), event->modifiers());
    if(ruler_ && rulerCtr & 0x1) rulPt2 = scenePos;
    emit mouseMove(scenePos);

    event->accept();
    // Без update(): при зуме смена трансформации инвалидирует вид сама, а при
    // прокрутке принудительная перерисовка глушила блиттинг Qt. Перекрестье
    // обновляется через оверлей.
    updateOverlay();
}

void GraphicsView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    // Оверлей рисуется в ЭКРАННЫХ координатах, но внутри прохода сцены,
    // поэтому при прокрутке уезжает вместе с блиттингом содержимого: на старом
    // месте оставался след перекрестья. Стираем и то место, куда его сдвинуло,
    // и то, где оно должно быть.
    const QRegion region = overlayRegion();
    viewport()->update(region | region.translated(dx, dy));
    bandViewChanged();
}

void GraphicsView::mousePressEvent(QMouseEvent* event) {
    toScenePos(event);
    // Нажатие посреди панорамы глотаем вместе с его отпусканием: ЛКМ иначе
    // ставила бы точку фигуры (mouseClickL), а ПКМ при СКМ-панораме -- меню.
    // Аккорд при нажатой ПКМ -- уже жест, меню на её отпускании не нужно.
    swallowed_.setFlag(event->button(), false); // прежнее отпускание потерялось
    if(panState_ != PanState::Idle && event->button() != panButton_) {
        swallowed_.setFlag(event->button());
        if(panState_ == PanState::Armed) panState_ = PanState::Consumed;
        return event->accept();
    }
    if(event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        // В базу НЕ отдаём: сцена на любую кнопку, не принятую item'ами,
        // снимает выделение, а подвижные item'ы захватили бы мышь правой
        // кнопкой. Прежняя СКМ-панорама через ScrollHandDrag,
        // setInteractive(false) и поддельную ЛКМ больше не нужна.
        event->accept();
        // Прежнее отпускание могло потеряться (модальное окно, Alt+Tab).
        endPan();
        if(event->buttons() != event->button()) return; // аккорд с ЛКМ -- без панорамы
        panButton_ = event->button();
        panPressPos_ = event->position().toPoint();
        panAnchor_ = mapToScene(event->position());
        if(panButton_ == Qt::MiddleButton)
            startPan(); // СКМ -- сразу, кликом она ничего не делает
        else
            panState_ = PanState::Armed; // ПКМ -- клик (меню) или панорама, решит порог
    } else {
        if(event->button() == Qt::LeftButton)
            pressLeft(event, false);
        else
            QGraphicsView::mousePressEvent(event);
        // Нажатие по виду родителю не уходит -- как и при RubberBandDrag,
        // которая принимала всё, что не взяла сцена.
        event->accept();
        if(ruler_ && !(rulerCtr++ & 0x1)) rulPt1 = scenePos;

        if(auto item{scene()->itemAt(scenePos, transform())};
            0 && item
            && item->type() == QGraphicsPixmapItem::Type) { // NOTE  возможно DD для направляющих не сделаю.
            QMimeData* mimeData = new QMimeData;
            mimeData->setText(Ruler::MimeType);
            mimeData->setData(Ruler::MimeType,
                QByteArray::fromRawData(reinterpret_cast<const char*>(item),
                    sizeof(void*)));

            QPixmap pixmapIcon{Ruler::Breadth, Ruler::Breadth};
            pixmapIcon.fill(Qt::magenta);

            QDrag* drag = new QDrag{this};
            drag->setMimeData(mimeData);
            drag->setPixmap(pixmapIcon);
            drag->setHotSpot(pixmapIcon.rect().center());
            Qt::DropAction dropAction = drag->exec(); // Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
            if(dropAction == Qt::MoveAction) { }
        }
    }
}

void GraphicsView::mouseReleaseEvent(QMouseEvent* event) {
    if(swallowed_ & event->button()) { // нажатие проглотила панорама
        swallowed_.setFlag(event->button(), false);
        return event->accept();
    }
    if(event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        event->accept();
        if(event->button() != panButton_) return; // нажатие было не наше: аккорд, закрытие меню
        const auto state = panState_;
        // Сброс до меню: QMenu::exec крутит вложенный цикл событий.
        endPan();
        if(state == PanState::Armed) { // чистый клик ПКМ
            // Раньше испускался на нажатии -- теперь панорама посреди рисования
            // фигуры её не завершает. До меню -- чтобы при его вызове ничего
            // постороннего (недорисованной фигуры) не было.
            emit mouseClickR(toScenePos(event));
            showSceneContextMenu(event);
        }
    } else {
        if(event->button() == Qt::LeftButton) endBand();
        QGraphicsView::mouseReleaseEvent(event);
        emit mouseClickL(toScenePos(event));
    }
}

void GraphicsView::mouseMoveEvent(QMouseEvent* event) {
    swallowed_ &= event->buttons(); // отпущенные мимо нас -- забыть
    if(panState_ != PanState::Idle) {
        if(!(event->buttons() & panButton_)) { // отпускание потерялось
            endPan();
        } else {
            if(panState_ != PanState::Panning
                && (event->position().toPoint() - panPressPos_).manhattanLength() > kPanThresholdPx)
                startPan();
            // Точка сцены под курсором едет за ним. Сдвиг -- от якоря, а не от
            // прошлого события: дробные позиции (HiDPI) не копят ошибку.
            if(panState_ == PanState::Panning) scrollViewBy(mapFromScene(panAnchor_) - event->position());
        }
    }
    QGraphicsView::mouseMoveEvent(event);

    if(band_.state != Band::Idle) {
        if(!(event->buttons() & Qt::LeftButton))
            endBand(); // отпускание потерялось (модальное окно, Alt+Tab)
        else
            updateBand(event->position().toPoint());
    }

    QPoint pt = event->position().toPoint();
    vRuler->setCursorPos(pt);
    hRuler->setCursorPos(pt);

    emit mouseMove(toScenePos(event));

    if(ruler_ && rulerCtr & 0x1) rulPt2 = scenePos;

    updateOverlay();
}

void GraphicsView::mouseDoubleClickEvent(QMouseEvent* event) {
    // Второе быстрое нажатие приходит как DblClick, а не Press. Без этого
    // вторая панорама подряд не начиналась бы, а двойной клик ПКМ/СКМ доходил
    // бы до item'ов: Marker/Pin -- resetPos(), фигура -- редактор.
    if(event->button() == Qt::RightButton || event->button() == Qt::MiddleButton
        || panState_ != PanState::Idle)
        return mousePressEvent(event);
    if(event->button() != Qt::LeftButton) return QGraphicsView::mouseDoubleClickEvent(event);
    // ЛКМ: двойной клик item'ам -- как раньше, но и рамка от него тоже.
    // Иначе «щёлкнул и сразу потянул» молча ничего не делал: второе нажатие
    // пришло двойным кликом, а рамку заводит только обычное.
    toScenePos(event);
    pressLeft(event, true);
}

void GraphicsView::contextMenuEvent(QContextMenuEvent* event) {
    // Мышиное меню Qt синтезирует сам -- после НАЖАТИЯ ПКМ (X11, Wayland,
    // macOS) или после отпускания (Windows), принято нажатие или нет. Нажатие
    // ПКМ теперь ещё и начало панорамы, поэтому такое меню глотаем, а по
    // чистому клику его открывает mouseReleaseEvent. С клавиатуры (Menu,
    // Shift+F10) -- как было.
    if(event->reason() == QContextMenuEvent::Mouse) return event->accept();
    QGraphicsView::contextMenuEvent(event);
}

bool GraphicsView::event(QEvent* event) {
    // Esc посреди рамки -- отмена жеста, а не QShortcut главного окна
    // («снять всё», да и тот молчит, пока открыта форма). Приняв
    // ShortcutOverride, получаем клавишу сами -- в keyPressEvent.
    if(event->type() == QEvent::ShortcutOverride && band_.state == Band::Active
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        event->accept();
        return true;
    }
    return QGraphicsView::event(event);
}

void GraphicsView::keyPressEvent(QKeyEvent* event) {
    if(event->key() == Qt::Key_Escape && band_.state == Band::Active) {
        cancelBand();
        return event->accept();
    }
    QGraphicsView::keyPressEvent(event);
}

void GraphicsView::showSceneContextMenu(const QMouseEvent* event) {
    QContextMenuEvent menuEvent{QContextMenuEvent::Mouse, event->position().toPoint(),
        event->globalPosition().toPoint(), event->modifiers()};
    menuEvent.setTimestamp(event->timestamp());
    // Квалифицированно -- мимо своего фильтра, сразу к сцене и item'ам.
    QGraphicsView::contextMenuEvent(&menuEvent);
}

void GraphicsView::startPan() {
    panState_ = PanState::Panning;
    // Не ::setCursor(): тот рисует перекрестье. Сохраняем то, что стоит, и
    // вернём его в endPan().
    panSavedCursor_ = viewport()->cursor();
    viewport()->setCursor(Qt::ClosedHandCursor);
}

void GraphicsView::endPan() {
    if(std::exchange(panState_, PanState::Idle) == PanState::Panning)
        viewport()->setCursor(panSavedCursor_);
    panButton_ = Qt::NoButton;
}

// Рамка выделения ////////////////////////////////////////////////////////////

// Запас выборки кандидатов вокруг рамки, px. Выделение правится только у
// тех, кого вернула выборка по габаритам, а попадание решается по форме:
// где форма вылезает за габарит, известный индексу (ручки выделенной фигуры
// -- HandleSize 20 px, abstract_shape.cpp; округление на кромке рамки), item
// выпал бы из выборки и остался в прежнем состоянии.
constexpr int kBandSlackPx = 20;

GraphicsView::BandOp GraphicsView::bandOp(Qt::KeyboardModifiers mods) {
    if(mods & Qt::ControlModifier) return BandOp::Remove; // и Ctrl+Shift
    if(mods & Qt::ShiftModifier) return BandOp::Add;
    return BandOp::Replace;
}

void GraphicsView::pressLeft(QMouseEvent* event, bool doubleClick) {
    endBand(); // прежнее отпускание могло потеряться
    // Рамки нет в режиме измерения (ЛКМ ставит точки линейки) и пока рисуется
    // фигура: там каждое отпускание ЛКМ -- её точка (mouseClickL).
    const bool bandable = !ruler_ && !isSignalConnected(QMetaMethod::fromSignal(&GraphicsView::mouseClickL));
    const BandOp op = bandOp(event->modifiers());
    // Снимок -- до нажатия: оно само меняет выделение (без модификаторов --
    // снимает прочее). Нужен и замене: по нему Esc откатывает рамку.
    QSet<QGraphicsItem*> base;
    if(bandable) {
        const auto selected = scene()->selectedItems();
        base = QSet<QGraphicsItem*>(selected.cbegin(), selected.cend());
    }
    if(doubleClick)
        QGraphicsView::mouseDoubleClickEvent(event);
    else
        QGraphicsView::mousePressEvent(event); // NoDrag: только доставка сцене
    if(bandable) {
        if(!event->isAccepted()) { // мимо item'ов -- как штатная рамка
            armBand(event->position(), op, false, std::move(base));
        } else if(const QGraphicsItem* grabber = scene()->mouseGrabberItem();
                  grabber && !(grabber->flags() & QGraphicsItem::ItemIsMovable)) {
            // По неподвижному item'у тащить нечего, а площадки и полигоны
            // покрывают плату так, что пустого места под начало рамки нет.
            // Подвижные (редактируемые фигуры, булавки, маркеры, мосты,
            // направляющие) тянутся, как раньше.
            armBand(event->position(), op, true, std::move(base));
        }
    }
    event->accept();
}

void GraphicsView::armBand(QPointF pressPos, BandOp op, bool fromItem, QSet<QGraphicsItem*> base) {
    band_ = Band{};
    band_.state = Band::Armed;
    band_.op = op;
    band_.fromItem = fromItem;
    band_.originScene = mapToScene(pressPos);
    band_.pressPx = band_.cursorPx = pressPos.toPoint();
    band_.base = std::move(base);
}

void GraphicsView::updateBand(QPoint cursorPx) {
    band_.cursorPx = cursorPx;
    if(band_.state == Band::Armed) {
        // С item'а порог вдвое: дрогнувшая рука при щелчке по трассе не
        // должна превращать его в крошечную рамку, которая эту же трассу снимет.
        const int threshold = QGuiApplication::styleHints()->startDragDistance() * (band_.fromItem ? 2 : 1);
        if((cursorPx - band_.pressPx).manhattanLength() < threshold) return;
        band_.state = Band::Active;
        // Отпускание item'у больше не нужно: оно переключило бы его или
        // оставило выделенным его одного.
        if(QGraphicsItem* grabber = scene()->mouseGrabberItem(); band_.fromItem && grabber)
            grabber->ungrabMouse();
        // Нажатие по item'у уже выделило его одного -- теперь решает рамка.
        if(band_.op == BandOp::Replace) scene()->clearSelection();
    }
    placeBand();
    applyBand();
}

void GraphicsView::placeBand() {
    const QRect old = band_.rect;
    const bool oldCrossing = band_.crossing;
    const QPoint o = QGraphicsView::mapFromScene(band_.originScene), c = band_.cursorPx;
    // Не QRect::normalized(): в Qt 6 она при обмене углов сдвигает их на
    // пиксель внутрь, и рамка теряла бы точку нажатия и курсор.
    band_.rect = QRect{std::min(o.x(), c.x()), std::min(o.y(), c.y()),
        std::abs(o.x() - c.x()) + 1, std::abs(o.y() - c.y()) + 1};
    band_.crossing = c.x() < o.x();
    if(band_.rect != old || band_.crossing != oldCrossing)
        viewport()->update(bandFrameRegion(old) | bandFrameRegion(band_.rect));
}

void GraphicsView::applyBand() {
    if(band_.state != Band::Active) return;
    const QRectF now = QGraphicsView::mapToScene(band_.rect).boundingRect();
    if(now == band_.applied && band_.crossing == band_.appliedCrossing) return;
    // Кандидаты -- всё, что габаритом задевает прежнюю рамку или новую, и
    // каждому состояние назначается заново: по base и попаданию. Прежнюю
    // выборку не храним -- файл могут перечитать посреди жеста, а
    // пересчитывать её по прежней рамке нельзя: габарит и форма фигуры
    // меняются вместе с её выделением.
    const double slack = kBandSlackPx * scaleFactor();
    const QRectF query = now.united(band_.applied).adjusted(-slack, -slack, slack, slack);
    const auto candidates = scene()->items(query, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder, viewportTransform());
    for(QGraphicsItem* item: candidates) {
        if(!(item->flags() & QGraphicsItem::ItemIsSelectable)) continue;
        const bool in = bandHit(item, now, band_.crossing);
        bool want = in;
        if(band_.op == BandOp::Add)
            want = in || band_.base.contains(item);
        else if(band_.op == BandOp::Remove)
            want = !in && band_.base.contains(item);
        // Только где состояние меняется: у Gi::Item это перекраска и реестр
        // анимации, у фигуры -- ещё и редактор, у термопревью -- дерево формы.
        if(item->isSelected() != want) item->setSelected(want);
    }
    band_.applied = now;
    band_.appliedCrossing = band_.crossing;
}

bool GraphicsView::bandHit(const QGraphicsItem* item, const QRectF& band, bool crossing) {
    // Свои сравнения вместо QRectF::contains/intersects: те считают
    // прямоугольник нулевой ширины пустым, а у горизонтального или
    // вертикального отрезка габарит именно такой.
    auto inside = [&band](const QRectF& r) {
        return r.left() >= band.left() && r.right() <= band.right()
            && r.top() >= band.top() && r.bottom() <= band.bottom();
    };
    const QRectF bounds = item->sceneBoundingRect();
    if(bounds.left() > band.right() || bounds.right() < band.left()
        || bounds.top() > band.bottom() || bounds.bottom() < band.top())
        return false; // габарит мимо -- форма тем более
    // Фигуре -- контур без ручек: выделенная редактируемая фигура отдаёт
    // shape() вместе с ними, и рамка у её края выделяла бы её и снимала
    // попеременно, с каждым движением мыши.
    const int type = item->type();
    const QPainterPath shape = type >= Gi::Type::ShapeBegin && type < Gi::Type::ShapeEnd
        ? static_cast<const Gi::Item*>(item)->Gi::Item::shape()
        : item->shape();
    if(shape.isEmpty()) return false;
    if(crossing && inside(bounds)) return true; // габарит внутри -- форма задета
    // По форме, а не по габариту, как ContainsItemShape у Qt: у DataPath
    // габарит с запасом 3 мм под прицел (gi_datapath.cpp), и «целиком внутри»
    // требовало бы обводить трассу с таким же запасом. Вид не вращается --
    // рамка в сцене остаётся прямоугольником.
    const QTransform t = item->sceneTransform();
    if(crossing)
        return t.isRotating() ? t.map(shape).intersects(band) : shape.intersects(t.inverted().mapRect(band));
    return inside(t.isRotating() ? t.map(shape).boundingRect() : t.mapRect(shape.boundingRect()));
}

void GraphicsView::bandViewChanged() {
    if(band_.state != Band::Active) return;
    placeBand(); // угол держится за точку сцены
    // Выделение -- одним проходом в конце такта: шаг колеса проходит здесь
    // по нескольку раз (полосы прокрутки, затем onTransformChanged).
    if(std::exchange(band_.applyQueued, true)) return;
    QTimer::singleShot(0, this, [this] {
        band_.applyQueued = false;
        applyBand();
    });
}

void GraphicsView::endBand() {
    if(band_.state == Band::Active) viewport()->update(bandFrameRegion(band_.rect));
    band_ = Band{};
}

void GraphicsView::cancelBand() {
    const QSet<QGraphicsItem*> base = std::move(band_.base);
    endBand(); // кнопка ещё нажата -- дальше движения рамку не продолжат
    // Обход всех item'ов сцены, а не по base: указатели в нём могли повиснуть.
    // Замена снимала выделение где угодно (нажатие мимо item'ов), поэтому и
    // возвращать его приходится по всей сцене; Esc жмут редко -- O(N) терпимо.
    for(QGraphicsItem* item: scene()->items()) {
        if(!(item->flags() & QGraphicsItem::ItemIsSelectable)) continue;
        if(const bool want = base.contains(item); item->isSelected() != want) item->setSelected(want);
    }
}

QRegion GraphicsView::bandFrameRegion(const QRect& r) {
    // Только контур: рамка не залита, и перерисовывать её внутренность при
    // каждом движении незачем (в MinimalViewportUpdate это вся площадь рамки).
    if(r.isEmpty()) return {};
    QRegion region{r.adjusted(-1, -1, 1, 1)};
    if(r.width() > 4 && r.height() > 4) region -= QRegion{r.adjusted(2, 2, -2, -2)};
    return region;
}

void GraphicsView::drawBand(QPainter* painter) const {
    QColor color;
    switch(band_.op) {
    case BandOp::Replace: color = QColor{App::settings().guiColor(GuiColors::Background).rgb() ^ 0xFFFFFF}; break;
    case BandOp::Add    : color = Qt::green; break;
    case BandOp::Remove : color = Qt::red; break;
    }
    painter->setPen(QPen{color, 1.0, band_.crossing ? Qt::DashLine : Qt::SolidLine});
    painter->setBrush(Qt::NoBrush);
    // QRect в drawRect рисуется на пиксель шире и выше -- как у штатной рамки.
    painter->drawRect(band_.rect.adjusted(0, 0, -1, -1));
}

void GraphicsView::drawForeground(QPainter* painter, const QRectF& rect) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHints(QPainter::TextAntialiasing); // | QPainter::HighQualityAntialiasing);

    // Single pass over the finest grid: every 5th tick also belongs to the
    // medium grid, every 10th also to the coarse grid, so classifying by
    // index replaces the old float-hashed dedup set entirely.
    std::vector<QLineF> lines[3]; // 0 = Grid01, 1 = Grid05, 2 = Grid10
    const auto gridStep = App::settings().gridStep(getScale());

    const auto tickCountX = static_cast<size_t>(std::floor((rect.right() - rect.left()) / gridStep)) + 2;
    const auto tickCountY = static_cast<size_t>(std::floor((rect.bottom() - rect.top()) / gridStep)) + 2;
    for(auto& l: lines) l.reserve(tickCountX + tickCountY);

    forEachGridTick(0.0, rect.left(), rect.right(), gridStep, [&](int64_t index, double x) {
        lines[gridTickLevel(index)].emplace_back(x, rect.top(), x, rect.bottom());
    });
    forEachGridTick(0.0, rect.top(), rect.bottom(), gridStep, [&](int64_t index, double y) {
        lines[gridTickLevel(index)].emplace_back(rect.left(), y, rect.right(), y);
    });

    const QColor color[3]{
        App::settings().guiColor(GuiColors::Grid01),
        App::settings().guiColor(GuiColors::Grid05),
        App::settings().guiColor(GuiColors::Grid10),
    };
    const double penWidth = 1.0 / getScale();
    // draw grid, coarsest on top
    for(int i{}; i < 3; ++i) {
        painter->setPen({color[i], penWidth});
        painter->drawLines(lines[i].data(), lines[i].size());
    }

    // draw the origin
    // if(rect.contains(QPointF{}))
    {
        auto color = App::settings().guiColor(GuiColors::Grid10);
        color.setRed(255);
        painter->setPen({color, penWidth});
        QLineF lines[2]{
            {0,           rect.top(), 0,            rect.bottom()},
            {rect.left(), 0,          rect.right(), 0            }
        };
        painter->drawLines(lines, 2);
    }

    // Перекрестье и мерная линейка -- в ЭКРАННЫХ координатах: плечи у них
    // заданы в пикселях, и делить их на масштаб больше не надо.
    //
    // Рисуются здесь же, а не отдельным QPainter поверх вьюпорта в paintEvent:
    // при включённом OpenGL вьюпорт -- это QOpenGLWidget, второй QPainter по
    // нему обнуляет кадр (сцены и сетки не видно вовсе). Выигрыш всё равно
    // остаётся: инвалидируются только полосы перекрестья (updateOverlay), а не
    // весь вид.
    painter->resetTransform();
    drawOverlay(painter);

    painter->restore();
}

// Экранные украшения поверх вьюпорта, в его собственных координатах.
constexpr int kCrossArmPx = 100;

QRegion GraphicsView::overlayRegion() const {
    const auto& p = cursorViewPos_;
    // QRect{x, y, w, h} покрывает [x, x+w-1], а плечо креста рисуется
    // ВКЛЮЧИТЕЛЬНО до p+kCrossArmPx. Без +1 крайний пиксель нижнего и правого
    // плеча не попадал в область стирания и оставался на экране при движении
    // вверх и вправо. Ещё +1 с каждой стороны -- запас на округление.
    constexpr int arm = kCrossArmPx + 1;
    QRegion region{
        QRect{p.x() - arm, p.y() - 1, arm * 2 + 1, 3}
    };
    region += QRect{p.x() - 1, p.y() - arm, 3, arm * 2 + 1};
    // В режиме измерения оверлей -- это ещё стрелка и подпись, размер которой
    // зависит от шрифта и текста, а положение вдобавок прижимается к краям
    // вида. Предсказывать её прямоугольник здесь значило бы дублировать всю
    // раскладку из drawRuller и снова разъезжаться с ней; режим кратковременный
    // и интерактивный, поэтому проще обновить вьюпорт целиком.
    if(ruler_) region += viewport()->rect();
    // Рамка -- тоже оверлей: при прокрутке её уносит блиттингом вместе со сценой.
    if(band_.state == Band::Active) region += bandFrameRegion(band_.rect);
    return region;
}

void GraphicsView::updateOverlay() {
    const QRegion old = overlayRegion();
    // Перекрестье стоит в СНАПНУТОЙ точке, а не под сырым курсором: scenePos
    // прогнан через getSnappedPos, и при включённом прилипании крест должен
    // держаться сетки. (Раньше он рисовался прямо по позиции курсора.)
    cursorViewPos_ = QGraphicsView::mapFromScene(scenePos);
    viewport()->update(old | overlayRegion());
}

void GraphicsView::drawOverlay(QPainter* painter) {
    if(band_.state == Band::Active) drawBand(painter);
    if(viewport()->underMouse()) { // перекрестье под курсором
        const auto& p = cursorViewPos_;
        painter->setPen({Qt::red, 1.0});
        QLine lines[2]{
            {p.x() - kCrossArmPx, p.y(),               p.x() + kCrossArmPx, p.y()              },
            {p.x(),               p.y() - kCrossArmPx, p.x(),               p.y() + kCrossArmPx}
        };
        painter->drawLines(lines, 2);
    }
    if(ruler_) drawRuller(painter);
}

void GraphicsView::timerEvent(QTimerEvent* event) {
    if(event->timerId() != animTimerId_) return QGraphicsView::timerEvent(event);
    // Период узора -- 4pi-1. Держим смещение внутри периода: прежний счётчик
    // int за долгую сессию уходил в переполнение.
    App::dashOffset() = std::fmod(App::dashOffset() + 1.0, 4 * std::numbers::pi - 1);
    // std::as_const: неконстантный begin() у контейнеров Qt отсоединяет данные.
    if(animated_.size() > 256) { // много выделено -- один регион дешевле
        QRectF r;
        for(auto* i: std::as_const(animated_)) r |= i->sceneBoundingRect();
        scene()->invalidate(r, QGraphicsScene::ItemLayer);
    } else
        for(auto* i: std::as_const(animated_)) i->update();
}

#include "moc_graphicsview.cpp"
