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
#pragma once

#include "graphicsview.h"

#include <array>
#include <span>

bool updateRect();

namespace Gi {

class Marker : public QGraphicsObject {
    Q_OBJECT
public:
    enum Type {
        Zero,
        Home
    };

    Marker(Type type);
    ~Marker() override;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;
    int type() const override;

    void resetPos(bool flUpdateRect = true);
    void setPosX(double x);
    void setPosY(double y);
    inline void setPos(const QPointF& pos) { QGraphicsItem::setPos(pos); }

    // Пересобрать геометрию под текущую настройку scaleHZMarkers. Звать после
    // её смены.
    void applyScaleMode();

private:
    QRectF rect_;
    QPainterPath path_;
    QPainterPath shape_;
    // Исходная геометрия в миллиметрах. При «постоянном экранном размере»
    // path_/shape_ пересобираются из неё сразу в ПИКСЕЛЯХ.
    QPainterPath basePath_;
    QPainterPath baseShape_;
    double dotRadius_{2};
    const Type type_;
    void updateGCPForm();
    bool moved{};

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
};

// Булавки (реперы) -- четыре штифта для переворота заготовки вокруг
// вертикальной оси. Это две пары: нижняя 0-1 и верхняя 3-2 (индексы идут
// против часовой от левой нижней -- так их ставит resetPos). Заготовку
// переворачивают на штифтах, и отверстие каждой булавки ложится на место её
// партнёра: пара обязана быть зеркальной относительно оси переворота. Ось же
// ни к центру платы, ни к нулю УП не привязана -- она там, где стоят булавки
// (flipAxisX), и по ней же зеркалится нижняя сторона в УП.
//
// Симметрия держится всегда, режим выбирается в проекте:
//  * ось -- пары зеркальны относительно одной вертикали X = a; булавка ходит
//    вместе со своим партнёром, ось стоит;
//  * точка -- прямоугольник с центром в точке C; булавка тащит все четыре
//    (как было исходно). Переворот и тут вокруг вертикали X = C.x.
// Ctrl в обоих режимах сдвигает всю расстановку вместе с осью/точкой.
class Pin : public QGraphicsObject {
    Q_OBJECT
public:
    using Positions = std::array<QPointF, 4>;
    using UsedFlags = std::array<bool, 4>;

    Pin();
    ~Pin() override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;
    int type() const override;

    // Зеркальный партнёр булавки: 0-1, 2-3. По диагонали -- idx ^ 2, с той
    // же X -- idx ^ 3.
    static constexpr int partner(int idx) { return idx ^ 1; }

    static Positions positions(); // текущие, в координатах сцены
    static UsedFlags usedFlags(); // флаги "Используется" проекта

    // Режим симметрии проекта: false -- ось, true -- точка.
    static bool pointSymmetry();
    // Сменить режим из меню булавки: проект, выравнивание расстановки под
    // новый режим (conform), подсказки, ненадолго -- метка симметрии.
    static void setPointSymmetry(bool point);

    // X оси переворота -- средняя из середин пар. Берутся пары, где
    // используются обе булавки; нет таких -- где хотя бы одна; нет и таких --
    // все. У симметричной расстановки середины пар совпадают, и ось от флагов
    // не зависит.
    static double flipAxisX();
    static double flipAxisX(std::span<const QPointF, 4> pos, std::span<const bool, 4> used);
    // Центр симметрии: X -- ось переворота, Y -- посередине между парами.
    static QPointF center(std::span<const QPointF, 4> pos, std::span<const bool, 4> used);
    // Булавку idx -- в pt, остальные -- по симметрии режима относительно c:
    // партнёр зеркально через вертикаль c.x, в режиме точки ещё и вторая
    // пара (по диагонали -- через c, с той же X -- через горизонталь c.y).
    static Positions place(Positions pos, int idx, QPointF pt, QPointF c, bool pointMode);
    static bool isSymmetric(std::span<const QPointF, 4> pos, std::span<const bool, 4> used, bool pointMode, double eps = 1e-9);
    // Привести расстановку к симметрии режима. Уже симметричную (в пределах
    // 1e-9) возвращает как есть, без единого сдвига: булавка могла стоять на
    // отверстии точно. Ось переворота сохраняет.
    static Positions conform(Positions pos, std::span<const bool, 4> used, bool pointMode);

    // УП зависят от булавок только осью переворота: по ней зеркалится нижняя
    // сторона. Сдвинулась относительно before -- пересчитать УП; нет --
    // ничего не делать.
    static void flipAxisMoved(double before);

    // Метка симметрии: пунктир оси, а в режиме точки -- перекрестье через
    // точку. Видна, пока булавку тащат или открыт диалог булавок.
    static void showSymmetryMark(bool show);
    static void updateSymmetryMark(bool pointMode, QPointF c, std::span<const QPointF, 4> pos);

    int index() const { return static_cast<int>(index_); }

    static void resetPos(bool fl = true);
    static void setPos(const QPointF pos[4]);
    void updateToolTip();
    void setPos(const QPointF& pos);

    // См. Marker::applyScaleMode.
    void applyScaleMode();

private:
    QPainterPath path_;
    QPainterPath shape_;
    QPainterPath basePath_;
    QPainterPath baseShape_;
    double dotRadius_{2};
    QRectF rect_;
    QPointF lastPos_; // положение в момент нажатия -- от него считается перетаскивание
    const uint index_;
    static inline int ctr_;
    static inline QPointF pressCenter_{}; // ось/точка симметрии в момент нажатия
    static inline double pressFlipX_{};   // ось переворота, по которой посчитаны УП
    bool moved{};

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
};

// Применить настройки «постоянного экранного размера» ко всем маркерам.
// Звать И при чтении настроек, и при сохранении: маркеры создаются в
// конструкторе MainWindow, а QSettings читаются позже (main.cpp), поэтому на
// момент их создания настройка ещё в значении по умолчанию.
void applyMarkersScaleMode();

} // namespace Gi

class LayoutFrames : public QGraphicsObject {
    Q_OBJECT
    QPainterPath path_;
    QRectF rect_;

public:
    LayoutFrames();
    ~LayoutFrames() override;
    int type() const override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void updateRect(bool fl = false);
};
