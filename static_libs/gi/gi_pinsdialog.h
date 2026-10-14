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
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QKeyEvent;
class QLabel;
class QPushButton;

namespace Gi {

// Координаты булавок (реперов), режим их симметрии и привязка к отверстиям
// сверловки.
//
// Расстановка в окне всегда симметрична по выбранному режиму (ось или точка,
// см. Pin): правка одной булавки двигает и остальные, правка оси/точки --
// всю расстановку. Правка идёт вживую: булавки на сцене двигаются вместе с
// полями, метка симметрии видна, пока окно открыто. В проект -- а с ним и в
// УП нижней стороны -- расстановка и режим уходят только по OK; Cancel
// возвращает булавки на место, проект при этом и не трогался.
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
    struct Hole {
        QPointF pos;     // центр, координаты сцены
        double diameter; // на сцене, с масштабом файла
    };
    // Откуда брать отверстия: выделенные на сцене (так пользователь сам
    // выбирает, к каким) либо все отверстия одного файла сверловки.
    struct HoleSource {
        QString name;
        std::vector<Hole> holes;
    };
    static std::vector<HoleSource> holeSources();

    // Ноль отсчёта полей -- всегда точка Zero: так же считают подсказка
    // булавки, "Zeroed" в строке состояния и сама УП.
    QPointF origin() const;
    void setPointMode(bool point);
    void pinEdited(int idx, bool isX);
    void centerEdited(bool isX);
    void snapToHoles();
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
    const std::vector<HoleSource> sources_;

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
