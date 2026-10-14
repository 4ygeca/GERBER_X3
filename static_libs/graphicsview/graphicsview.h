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

#include "ggcore_export.h"
#include <QCursor>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QScreen>
#include <QSet>
#include <QSettings>

#include <ranges>

namespace v = std ::views;
namespace r = std ::ranges;

class Ruler;
class QGridLayout;
// class Scene;

template <size_t N>
struct EnumHelper2 : std::integral_constant<bool, N != 0> {
    template <typename... Enums>
        requires std::conjunction_v<std::is_enum<Enums>...> || (sizeof...(Enums) == 0)
    constexpr EnumHelper2(Enums... e)
        : array{e...} { }
    std::array<int, N> array;
    constexpr bool operator()(auto* item) const {
        return r::contains(array, item->type());
    }
};

template <typename... Es>
EnumHelper2(Es...) -> EnumHelper2<sizeof...(Es)>;

class GGCORE_EXPORT GraphicsView : public QGraphicsView {
    Q_OBJECT

    Q_PROPERTY(double scale READ getScale WRITE setScale)
    Q_PROPERTY(QRectF viewRect READ getViewRect WRITE setViewRect)

public:
    explicit GraphicsView(QWidget* parent = nullptr);
    ~GraphicsView() override;
    // void setScene(QGraphicsScene* Scene);
    void zoom100();
    void zoomFit();
    void zoomToSelected();
    void zoomIn();
    void zoomOut();
    void fitInView(QRectF destRect, bool withBorders = true);

    // Показать этот участок сцены, когда виджет получит НАСТОЯЩИЙ размер.
    // Проект грузится из loadSettings() -- до show(), -- и обычный fitInView
    // считает трансформацию по начальной геометрии viewport'а, а следующий
    // resize её уже не пересчитывает: масштаб и положение остаются чужими.
    // Применяется следующим тактом цикла событий -- к тому моменту раскладка
    // досчитана и viewport имеет настоящий размер.
    void setViewRectDeferred(QRectF rect);

    void setRuler(bool ruller);

    double scaleFactor() const noexcept { return 1.0 / getScale(); }
    QPointF toScenePos(QMouseEvent* event);
    QPointF mapFromScene(const QPointF& point) const;
    QPointF mapToScene(const QPointF& point) const;
    using QGraphicsView::mapToScene;

    void setScale(double s) noexcept;
    double getScale() const noexcept { return transform().m11(); }
    void scale(double sx, double sy);

    void setOpenGL(bool useOpenGL);

    void setViewRect(const QRectF& r);
    QRectF getViewRect();
    QRectF getSelectedBoundingRect();

    // Реестр анимируемых item'ов. Раньше вид безусловно дёргал
    // scene()->update() каждые 20 мс -- полная перерисовка сцены 50 раз в
    // секунду даже на простое. Анимация нужна только «бегущим муравьям» на
    // выделенных DataPath и пульсации Gi::Error, поэтому item регистрируется
    // сам, а таймер крутится, только пока реестр непуст.
    void addAnimated(QGraphicsItem* item);
    void removeAnimated(QGraphicsItem* item);

    // Пересчитать sceneRect по фактическим данным. Звать при добавлении и
    // удалении файла, но НЕ покадрово: setSceneRect перестраивает индекс BSP
    // целиком. Заодно задаёт виду собственный, широкий, диапазон прокрутки.
    void updateSceneRectToContents();
    // То же, но с откладыванием на конец такта событий: при загрузке проекта
    // файлы добавляются по одному, а itemsBoundingRect() обходит всю сцену.
    void scheduleSceneRectUpdate();

    /////////////////////////////////
    template <typename T = QGraphicsItem, typename... Ts>
    auto items(Ts... ts) const {
        return getItemImpl<qOverload<Qt::SortOrder>(&QGraphicsScene::items), T>(EnumHelper2{ts...}, Qt::DescendingOrder);
    }

    template <typename T = QGraphicsItem, typename... Ts>
    auto selectedItems(Ts... ts) const {
        return getItemImpl<&QGraphicsScene::selectedItems, T>(EnumHelper2{ts...});
    }

    template <typename T>
    auto addItem(T* item) const { return scene()->addItem(item), item; }

    template <typename T, typename... Args>
    auto addItem(Args... args) const {
        auto item = new T{std::forward<Args>(args)...};
        return addItem(item);
    }

signals:
    void fileDroped(const QString&);
    void mouseClickL(const QPointF&);
    void mouseClickR(const QPointF&);
    void mouseMove(const QPointF&);

private:
    template <auto Ptr, typename T, typename FilterInt, typename... Args>
    std::vector<T*> getItemImpl(FilterInt&& et, Args&&... args) const {
        const auto items = (scene()->*Ptr)(std::forward<Args>(args)...); // get all items
        constexpr bool isQGraphicsItem = std::is_same_v<T, QGraphicsItem>;
        if constexpr(isQGraphicsItem && !FilterInt::value) { // вернуть все QGraphicsItem*
            return {items.begin(), items.end()};
        } else {
            // WARNING FilterInt faster than dynamic_cast
            // to improve speed dont use FilterDyn
            using FilterDyn = decltype([](auto* item) { return bool(dynamic_cast<T*>(item)); });
            using Transform = decltype([](auto* item) { return static_cast<T*>(item); });
            if constexpr(!isQGraphicsItem && !FilterInt::value) { // вернуть все T*
                auto rview = items | v::filter(FilterDyn{}) | v::transform(Transform{});
                return {rview.begin(), rview.end()};
            } else if constexpr(!isQGraphicsItem && FilterInt::value) { // вернуть все T* отсортированные по type()
                auto rview = items | v::filter(et) | v::filter(FilterDyn{}) | v::transform(Transform{});
                return {rview.begin(), rview.end()};
            } else if constexpr(isQGraphicsItem && FilterInt::value) { // вернуть все QGraphicsItem* отсортированные по type()
                auto rview = items | v::filter(et);
                return {rview.begin(), rview.end()};
            }
        }
    }

    Ruler* const hRuler;
    Ruler* const vRuler;
    QGridLayout* const gridLayout;
    // Scene* scene_;
    bool ruler_{};
    int rulerCtr{};
    void updateRuler();
    // Единая точка на смену трансформации: публикует масштаб в App и
    // подтягивает линейки.
    void onTransformChanged();
    template <class T>
    void animate(QObject* target, const QByteArray& propertyName, T begin, T end);
    QPointF scenePos, rulPt1, rulPt2;

    // Экранные украшения (перекрестье под курсором и мерная линейка) рисуются
    // поверх вьюпорта в его собственных координатах, а не в слое сцены: плечи
    // у них заданы в пикселях, и сцене незачем перерисовываться ради них.
    void drawOverlay(QPainter* painter);
    void drawRuller(QPainter* painter) const;
    QRegion overlayRegion() const;
    // Перерисовать только полосы перекрестья (старые и новые), не весь вид.
    void updateOverlay();
    QPoint cursorViewPos_;

    // Множество, а не вектор: рамка и «выделить всё» меняют выделение у
    // тысяч item'ов разом, и поиск по вектору делал это O(N²).
    QSet<QGraphicsItem*> animated_;
    int animTimerId_{};
    void updateAnimationTimer();

    bool sceneRectUpdatePending_{};

    // Навигация как в KiCad: ПКМ/СКМ-перетаскивание -- панорама, колесо -- зум
    // под курсором. Клик ПКМ без сдвига -- mouseClickR и контекстное меню.
    enum class PanState {
        Idle,     // кнопка панорамы не нажата
        Armed,    // ПКМ нажата, порог не пройден -- это ещё может быть клик
        Consumed, // то же, но уже был жест (колесо, аккорд) -- меню не нужно
        Panning,  // точка сцены под курсором едет за ним
    };
    PanState panState_{PanState::Idle};
    Qt::MouseButton panButton_{Qt::NoButton};
    Qt::MouseButtons swallowed_{}; // нажатия посреди панорамы -- глотаются вместе с отпусканием
    QPoint panPressPos_;           // вьюпорт, px -- для порога
    QPointF panAnchor_;            // точка СЦЕНЫ, которая едет за курсором
    QCursor panSavedCursor_;
    void startPan();
    void endPan();
    // Сдвинуть вид на delta пикселей вьюпорта (полосы прокрутки целые).
    void scrollViewBy(QPointF delta);
    // Масштаб в factor раз в пределах kMinScale..kMaxScale; точка сцены под
    // viewPos остаётся на месте.
    void zoomAt(QPointF viewPos, double factor);
    void showSceneContextMenu(const QMouseEvent* event);

    // Рамка выделения -- своя, а не RubberBandDrag: у Qt::ItemSelectionOperation
    // нет «вычесть», а AddToSelection не снимает задетое, когда рамку тянут
    // обратно. Операция задаётся модификатором в момент нажатия, режим --
    // направлением по горизонтали, как в KiCad: слева направо -- только
    // целиком внутри (сплошная), справа налево -- всё задетое (пунктир).
    enum class BandOp : uint8_t {
        Replace, // без модификаторов
        Add,     // Shift
        Remove,  // Ctrl (и Ctrl+Shift)
    };
    static BandOp bandOp(Qt::KeyboardModifiers mods);
    struct Band {
        enum State : uint8_t {
            Idle,   // ЛКМ не нажата или рамка здесь не положена
            Armed,  // нажата, порог не пройден -- это ещё может быть щелчок
            Active, // рамка тянется и правит выделение
        } state{Idle};
        BandOp op{BandOp::Replace};
        bool fromItem{};        // нажали по неподвижному item'у -- на пороге отнять у него мышь
        bool crossing{};        // тянут справа налево
        bool appliedCrossing{}; // режим, которым выставлено выделение по applied
        bool applyQueued{};     // смену вида досчитать одним проходом в конце такта
        QPointF originScene;    // угол держится за точку СЦЕНЫ -- зум и прокрутка его не сдвигают
        QPoint pressPx;         // вьюпорт: точка нажатия -- для порога
        QPoint cursorPx;        // вьюпорт: второй угол
        QRect rect;             // нарисованная рамка, вьюпорт
        QRectF applied;         // она же в сцене -- по ней выделение уже выставлено
        // Выделение до нажатия -- ТОЛЬКО contains(): файл могут перечитать
        // посреди жеста, и указатели в нём повиснут.
        QSet<QGraphicsItem*> base;
    };
    Band band_;
    void pressLeft(QMouseEvent* event, bool doubleClick);
    void armBand(QPointF pressPos, BandOp op, bool fromItem, QSet<QGraphicsItem*> base);
    void updateBand(QPoint cursorPx);
    void placeBand();
    void applyBand();
    void bandViewChanged();
    void endBand();
    // Esc посреди рамки: вернуть выделение, каким оно было до нажатия.
    void cancelBand();
    void drawBand(QPainter* painter) const;
    static QRegion bandFrameRegion(const QRect& r);
    static bool bandHit(const QGraphicsItem* item, const QRectF& band, bool crossing);

    void GiToShapeEvent(QMouseEvent* event, QGraphicsItem* item);
    // QWidget interface
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // Фон рисует сам QGraphicsView кистью из setBackgroundBrush.
    void drawForeground(QPainter* painter, const QRectF& rect) override;

    // QObject interface
protected:
    void timerEvent(QTimerEvent* event) override;

private:
    // Отложенный вид: пуст, пока восстанавливать нечего.
    QRectF pendingViewRect_;
    void applyPendingViewRect();
};

#include "app.h"
