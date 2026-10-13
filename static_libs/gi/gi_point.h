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
class Pin : public QGraphicsObject {
    Q_OBJECT
public:
    using Positions = std::array<QPointF, 4>;
    using UsedFlags = std::array<bool, 4>;

    // Итог проверки расстановки на симметрию. Для каждой ИСПОЛЬЗУЕМОЙ
    // булавки: на какую используемую булавку ложится её зеркальное
    // отражение (на оси -- на саму себя) и на сколько оно промахивается.
    struct Symmetry {
        double axis{};
        std::array<int, 4> target{-1, -1, -1, -1}; // -1 -- булавка не используется
        std::array<double, 4> deviation{};         // мм
        double maxDeviation{};
    };

    Pin();
    ~Pin() override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;
    int type() const override;

    static void setPinsPos(QPointF pos[4]);

    static double minX() { return qMin(App::pin0().x(), App::pin1().x()); }
    static double maxX() { return std::max(App::pin0().x(), App::pin1().x()); }
    static double minY() { return qMin(App::pin0().y(), App::pin2().y()); }
    static double maxY() { return std::max(App::pin0().y(), App::pin2().y()); }

    // Зеркальный партнёр булавки: 0-1, 2-3.
    static constexpr int partner(int idx) { return idx ^ 1; }

    static Positions positions(); // текущие, в координатах сцены
    static UsedFlags usedFlags(); // флаги "Используется" проекта

    // X оси переворота -- средняя из середин пар. Берутся пары, где
    // используются обе булавки; нет таких -- где хотя бы одна; нет и таких --
    // все. У старых проектов булавки всегда стояли прямоугольником, середины
    // пар совпадают -- ось та же, что прежняя (minX + maxX) / 2.
    static double flipAxisX();
    static double flipAxisX(std::span<const QPointF, 4> pos, std::span<const bool, 4> used);
    // Ось, относительно которой зеркалится правка пары булавки idx: середина
    // ДРУГОЙ пары, если та весит в flipAxisX не меньше (тот же или старший
    // разряд "Используется"), иначе середина своей пары. Тогда flipAxisX после
    // правки не сдвигается, а пара встаёт симметрично остальной расстановке --
    // среднее двух несовпадающих середин уезжало бы с каждой правкой.
    static double mirrorAxisX(std::span<const QPointF, 4> pos, std::span<const bool, 4> used, int idx);
    static Symmetry symmetry(std::span<const QPointF, 4> pos, std::span<const bool, 4> used);

    // Пунктир оси переворота. Виден, пока булавку тащат или открыт диалог
    // булавок.
    static void showFlipAxis(bool show);
    static void updateFlipAxis(double axisX, std::span<const QPointF, 4> pos);

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
    static inline double pressAxis_{}; // ось переворота в момент нажатия
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
