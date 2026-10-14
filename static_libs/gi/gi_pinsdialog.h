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
#pragma once

#include "gi_point.h"

#include <QDialog>
#include <QStringList>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QKeyEvent;
class QLabel;
class QPushButton;

namespace Gi {

// Координаты булавок (реперов), режим их симметрии и расстановка на
// отверстия сверловки.
//
// Расстановка в окне всегда симметрична по выбранному режиму (ось или точка,
// см. Pin): правка одной булавки двигает и остальные, правка оси/точки --
// всю расстановку. Правка идёт вживую: булавки на сцене двигаются вместе с
// полями, метка симметрии видна, пока окно открыто. В проект -- а с ним и в
// УП нижней стороны -- расстановка и режим уходят только по OK; Cancel
// возвращает булавки на место, проект при этом и не трогался.
//
// На отверстия булавки встают только целым набором: 3-4 круглых отверстия
// Excellon одного диаметра, симметричных по режиму ТОЧНО -- с точностью до
// записи координат в файле, без допуска. Несимметричный набор -- отказ с
// объяснением, булавки не двигаются: на такие штифты перевёрнутая плата не
// сядет, а "ближайшие" отверстия молча уводили расстановку в сторону.
class PinsDialog : public QDialog {
    Q_OBJECT

public:
    explicit PinsDialog(QWidget* parent);
    ~PinsDialog() override;

    void accept() override;
    void reject() override;

protected:
    // Enter в поле координат -- лишь ввод значения (правка живая), а не OK.
    void keyPressEvent(QKeyEvent* event) override;

private:
    // Набор отверстий под булавки: 3-4 круглых отверстия одного диаметра.
    // Только значения: файл могут перезагрузить, пока окно открыто, и
    // указатели на Gi::Drill повисли бы.
    struct HoleSet {
        QString name;                 // файл и инструменты; пусто -- выделенные на сцене
        std::vector<QPointF> centres; // координаты сцены, без повторов
        double diameter{};            // мм, как в файле; 0 -- у выделенных разные
        double eps{};                 // разрешение координат на сцене, мм
        QString error;                // набор не годится независимо от режима
    };
    // Как булавки встают на набор в режиме окна.
    struct Fit {
        Pin::Positions pos{};
        QString error;      // пусто -- встают; иначе почему нет (булавки не двигать)
        QStringList notes;  // к успеху: например, пара на одном отверстии на оси
        double maxOffset{}; // наибольший уход булавки от центра своего отверстия
        bool symmetric{};   // при ошибке: симметричны, но четырём булавкам не годятся
    };
    // Кандидаты -- из каждого файла Excellon, по инструменту и по диаметру;
    // выделенные на сцене -- первыми. selected -- сколько круглых отверстий
    // Excellon выделено (подсказать, если их не 3-4).
    static std::vector<HoleSet> holeSets(qsizetype& selected);
    // org -- точка Zero: координаты в сообщениях -- от неё, как в полях.
    static Fit fitAxis(const HoleSet& set, QPointF org);
    static Fit fitPoint(const HoleSet& set, QPointF org);
    Fit fit(const HoleSet& set) const; // по режиму окна
    void updateSetLabels();            // подписи наборов: симметричен ли в режиме окна

    // Ноль отсчёта полей -- всегда точка Zero: так же считают подсказка
    // булавки, "Zeroed" в строке состояния и сама УП.
    QPointF origin() const;
    void setPointMode(bool point);
    void pinEdited(int idx, bool isX);
    void centerEdited(bool isX);
    void placeOnHoles(); // на набор, выбранный в списке
    // Используемые булавки, чей зеркальный партнёр не используется, а сами
    // они не на оси: после переворота платы под такой булавкой нет отверстия.
    std::vector<int> unpairedPins() const;
    void updateFields();  // положения -> поля
    void updatePreview(); // булавки на сцене, метка симметрии, итог проверки

    const Pin::Positions orig_;
    const Pin::UsedFlags origUsed_;
    const bool origPointMode_;
    bool pointMode_; // режим окна: false -- ось, true -- точка (в проект -- по OK)
    Pin::Positions pos_;
    Pin::UsedFlags used_;
    std::vector<HoleSet> sets_;
    QString holesInfo_; // что сказать о наборах, пока булавки на них не ставили

    QCheckBox* chbxPoint_{};
    QDoubleSpinBox* dsbxX_[4]{};
    QDoubleSpinBox* dsbxY_[4]{};
    QCheckBox* chbxUsed_[4]{};
    QLabel* lblCenter_{};
    QDoubleSpinBox* dsbxCenterX_{}; // ось переворота X либо X точки
    QDoubleSpinBox* dsbxCenterY_{}; // Y точки, только в режиме точки
    QComboBox* cbxSource_{};
    QPushButton* pbSnap_{};
    QLabel* lblSnap_{};
    QLabel* lblStatus_{};
    bool updating_{}; // поля заполняются программно -- правкой это не считать
};

} // namespace Gi
