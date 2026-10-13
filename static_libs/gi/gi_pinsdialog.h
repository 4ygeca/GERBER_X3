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
class QLabel;
class QPushButton;

namespace Gi {

// Координаты булавок (реперов) и привязка их к отверстиям сверловки с
// проверкой расстановки на симметрию для переворота заготовки.
//
// Правка идёт вживую: булавки на сцене двигаются вместе с полями, пунктир оси
// переворота виден, пока окно открыто. В проект -- а с ним и в УП нижней
// стороны -- расстановка уходит только по OK; Cancel возвращает булавки на
// место, проект при этом и не трогался.
class PinsDialog : public QDialog {
    Q_OBJECT

public:
    enum Mode {
        Coordinates, // только координаты
        SnapToHoles, // сразу привязать используемые булавки к ближайшим отверстиям
    };

    explicit PinsDialog(QWidget* parent, Mode mode = Coordinates);
    ~PinsDialog() override;

    // Есть ли к чему привязывать: круглые отверстия в файлах проекта или
    // среди выделенного на сцене.
    static bool hasHoles();

    void accept() override;
    void reject() override;

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

    QPointF origin() const; // ноль отсчёта полей: точка Zero либо начало сцены
    void pinEdited(int idx, bool isX);
    void axisEdited(double value);
    void snapToHoles();
    void updateFields();  // положения -> поля
    void updatePreview(); // булавки на сцене, ось, итог проверки
    void saveSettings() const;

    const Pin::Positions orig_;
    const Pin::UsedFlags origUsed_;
    Pin::Positions pos_;
    Pin::UsedFlags used_;
    const std::vector<HoleSource> sources_;

    QDoubleSpinBox* dsbxX_[4]{};
    QDoubleSpinBox* dsbxY_[4]{};
    QCheckBox* chbxUsed_[4]{};
    QDoubleSpinBox* dsbxAxis_{};
    QCheckBox* chbxKeepSymmetric_{};
    QCheckBox* chbxRelative_{};
    QComboBox* cbxSource_{};
    QPushButton* pbSnap_{};
    QDoubleSpinBox* dsbxTolerance_{};
    QLabel* lblSnap_{};
    QLabel* lblStatus_{};
    bool updating_{}; // поля заполняются программно -- правкой это не считать
};

} // namespace Gi
